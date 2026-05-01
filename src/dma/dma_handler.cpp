#include "dma_handler.h"
#include <iostream>
#include <cstring>

// ─────────────────────────────────────────────────────────────
//  x64 paging constants
// ─────────────────────────────────────────────────────────────
static constexpr uintptr_t PAGE_SIZE       = 0x1000;
static constexpr uintptr_t PML4_MASK       = 0xFF8000000000ULL;
static constexpr uintptr_t PDPT_MASK       = 0x7FC0000000ULL;
static constexpr uintptr_t PD_MASK         = 0x3FE00000ULL;
static constexpr uintptr_t PT_MASK         = 0x1FF000ULL;
static constexpr uintptr_t PHYS_ADDR_MASK  = 0x000FFFFFFFFFF000ULL;
static constexpr uintptr_t PRESENT_BIT     = 1ULL;
static constexpr uintptr_t LARGE_PAGE_BIT  = (1ULL << 7);

// Physical RAM to scan. 16 GB is safe upper bound;
// increase if the machine has more.
static constexpr uintptr_t SCAN_RAM_MAX    = 0x400000000ULL; // 16 GB
// Step between PML4 candidates — they're page-aligned
static constexpr uintptr_t PML4_SCAN_STEP  = PAGE_SIZE;

// ─────────────────────────────────────────────────────────────
//  Init / Shutdown
// ─────────────────────────────────────────────────────────────

bool DMAHandler::Init(const std::string& targetProcess) {
    // FPGA device string — "FPGA" for real hardware
    // "TOTALMELTDOWN" or "file://mem.dmp" for offline testing
    LPCSTR args[] = { "", "-device", "FPGA", "-v", "" };

    m_hVMM = VMMDLL_Initialize(4, args);
    if (!m_hVMM) {
        std::cerr << "[DMA] MemProcFS init failed. Check FPGA connection.\n";
        return false;
    }

    if (!VMMDLL_PidGetFromName(m_hVMM,
            const_cast<LPSTR>(targetProcess.c_str()), &m_pid)) {
        std::cerr << "[DMA] Process not found: " << targetProcess << "\n";
        Shutdown();
        return false;
    }

    std::cout << "[DMA] Attached to " << targetProcess
              << "  PID: " << m_pid << "\n";
    std::cout << "[DMA] Read-only mode — no write paths exposed.\n";
    m_ready = true;
    return true;
}

void DMAHandler::Shutdown() {
    if (m_hVMM) {
        VMMDLL_Close(m_hVMM);
        m_hVMM    = nullptr;
        m_ready   = false;
        m_cr3Fixed = false;
    }
}

// ─────────────────────────────────────────────────────────────
//  Read helpers
// ─────────────────────────────────────────────────────────────

bool DMAHandler::ReadRaw(uintptr_t address, void* buf, size_t size) {
    return VMMDLL_MemReadEx(m_hVMM, m_pid,
        address, static_cast<PBYTE>(buf),
        static_cast<DWORD>(size), nullptr,
        VMMDLL_FLAG_NOCACHE) != 0;
}

std::string DMAHandler::ReadString(uintptr_t address, size_t maxLen) {
    std::vector<char> buf(maxLen + 1, 0);
    ReadRaw(address, buf.data(), maxLen);
    return std::string(buf.data());
}

uintptr_t DMAHandler::GetModuleBase(const std::string& moduleName) {
    PVMMDLL_MAP_MODULEENTRY pEntry = nullptr;
    std::wstring wName(moduleName.begin(), moduleName.end());
    uintptr_t base = 0;
    if (VMMDLL_Map_GetModuleFromNameW(m_hVMM, m_pid,
            const_cast<LPWSTR>(wName.c_str()), &pEntry, 0) && pEntry) {
        base = pEntry->vaBase;
        VMMDLL_MemFree(pEntry);
    }
    return base;
}

size_t DMAHandler::GetModuleSize(const std::string& moduleName) {
    PVMMDLL_MAP_MODULEENTRY pEntry = nullptr;
    std::wstring wName(moduleName.begin(), moduleName.end());
    size_t sz = 0;
    if (VMMDLL_Map_GetModuleFromNameW(m_hVMM, m_pid,
            const_cast<LPWSTR>(wName.c_str()), &pEntry, 0) && pEntry) {
        sz = pEntry->cbImageSize;
        VMMDLL_MemFree(pEntry);
    }
    return sz;
}

// ─────────────────────────────────────────────────────────────
//  Pattern scanner (IDA-style sigs over DMA)
//
//  Tokenizes "48 8B 05 ?? ?? ?? ?? 48" into a byte/mask vector, then
//  reads target memory in 64KB chunks (with overlap = pattern_len-1
//  to handle matches straddling chunk boundaries) and runs a linear
//  search per chunk. Returns first match VA or 0.
//
//  Performance note: DMA reads are ~5-50µs per round trip; 200 MB module
//  at 64 KB chunks = ~3200 reads = 16-160 ms one-time scan cost. Fine
//  at startup; cache results in m_sigCache to avoid repeating.
// ─────────────────────────────────────────────────────────────
namespace {
    bool ParseIdaPattern(const std::string& s,
                          std::vector<uint8_t>& bytes,
                          std::vector<bool>& mask) {
        bytes.clear();
        mask.clear();
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
            if (i >= s.size()) break;
            if (s[i] == '?') {
                bytes.push_back(0);
                mask.push_back(false);
                i++;
                if (i < s.size() && s[i] == '?') i++;
            } else {
                if (i + 1 >= s.size()) return false;
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int hi = hex(s[i]), lo = hex(s[i+1]);
                if (hi < 0 || lo < 0) return false;
                bytes.push_back((uint8_t)((hi << 4) | lo));
                mask.push_back(true);
                i += 2;
            }
        }
        return !bytes.empty();
    }
}

uintptr_t DMAHandler::FindPattern(uintptr_t base, size_t size,
                                   const std::string& idaPattern) {
    if (!m_ready || !base || size == 0) return 0;

    std::vector<uint8_t> patBytes;
    std::vector<bool>    patMask;
    if (!ParseIdaPattern(idaPattern, patBytes, patMask)) return 0;
    const size_t patLen = patBytes.size();
    if (patLen == 0 || patLen > size) return 0;

    constexpr size_t CHUNK = 64 * 1024;
    std::vector<uint8_t> buf(CHUNK + patLen - 1, 0);

    for (size_t off = 0; off < size; off += CHUNK) {
        size_t toRead = std::min(CHUNK + patLen - 1, size - off);
        if (!ReadRaw(base + off, buf.data(), toRead)) continue;

        const size_t scanEnd = (toRead >= patLen) ? (toRead - patLen + 1) : 0;
        for (size_t i = 0; i < scanEnd; i++) {
            bool ok = true;
            for (size_t j = 0; j < patLen; j++) {
                if (patMask[j] && buf[i + j] != patBytes[j]) { ok = false; break; }
            }
            if (ok) return base + off + i;
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
//  DMAHandler::Scatter — batched reads via VMMDLL_Scatter_*
//
//  Each Scatter instance wraps one VMMDLL_SCATTER_HANDLE.
//  Reads queue up via Prepare/PrepareEx and execute in a single
//  round trip when Execute() is called. Destination buffers MUST
//  outlive the Execute() call.
// ─────────────────────────────────────────────────────────────

DMAHandler::Scatter::Scatter(VMM_HANDLE hVMM, DWORD pid) {
    m_handle = VMMDLL_Scatter_Initialize(hVMM, pid, VMMDLL_FLAG_NOCACHE);
}

DMAHandler::Scatter::~Scatter() {
    if (m_handle) {
        VMMDLL_Scatter_CloseHandle(m_handle);
        m_handle = nullptr;
    }
}

void DMAHandler::Scatter::ReadRaw(uintptr_t va, void* dest, size_t cb) {
    if (!m_handle || !dest || cb == 0) return;
    VMMDLL_Scatter_PrepareEx(m_handle, va, static_cast<DWORD>(cb),
        static_cast<PBYTE>(dest), nullptr);
}

bool DMAHandler::Scatter::Execute() {
    if (!m_handle) return false;
    return VMMDLL_Scatter_Execute(m_handle) != 0;
}

uintptr_t DMAHandler::FindPatternInModule(const std::string& moduleName,
                                           const std::string& idaPattern) {
    std::string key = moduleName + "|" + idaPattern;
    auto it = m_sigCache.find(key);
    if (it != m_sigCache.end()) return it->second;

    uintptr_t base = GetModuleBase(moduleName);
    size_t    size = GetModuleSize(moduleName);
    if (!base || !size) return 0;

    uintptr_t result = FindPattern(base, size, idaPattern);
    m_sigCache[key] = result;
    return result;
}

// ─────────────────────────────────────────────────────────────
//  Physical memory read (bypasses virtual address translation)
//  Uses VMMDLL_MemReadEx with PID = -1 (physical)
// ─────────────────────────────────────────────────────────────

bool DMAHandler::ReadPhysical(uintptr_t physAddr, void* buf, size_t size) {
    // PID 0xFFFFFFFF (-1 cast) = physical memory in MemProcFS
    return VMMDLL_MemReadEx(m_hVMM,
        static_cast<DWORD>(-1),
        physAddr,
        static_cast<PBYTE>(buf),
        static_cast<DWORD>(size),
        nullptr,
        VMMDLL_FLAG_NOCACHE) != 0;
}

// ─────────────────────────────────────────────────────────────
//  Manual x64 page walk using a candidate CR3 (physical addr)
//
//  x64 virtual address layout:
//    [63:48] sign ext  [47:39] PML4 idx  [38:30] PDPT idx
//    [29:21] PD idx    [20:12] PT idx    [11:0]  page offset
//
//  Each table entry: 64-bit, bits[51:12] = next physical base
//  Bit 0 = Present, Bit 7 in PD/PDPT = Large Page (2MB/1GB)
// ─────────────────────────────────────────────────────────────

bool DMAHandler::VirtToPhysViaCustomCR3(uintptr_t cr3,
                                         uintptr_t va,
                                         uintptr_t& outPhys) {
    // ── PML4 ──────────────────────────────────────────────────
    uint64_t pml4Idx   = (va >> 39) & 0x1FF;
    uint64_t pml4Entry = 0;
    if (!ReadPhysical(cr3 + pml4Idx * 8, &pml4Entry, 8)) return false;
    if (!(pml4Entry & PRESENT_BIT)) return false;

    // ── PDPT ──────────────────────────────────────────────────
    uintptr_t pdptBase = pml4Entry & PHYS_ADDR_MASK;
    uint64_t  pdptIdx  = (va >> 30) & 0x1FF;
    uint64_t  pdptEntry= 0;
    if (!ReadPhysical(pdptBase + pdptIdx * 8, &pdptEntry, 8)) return false;
    if (!(pdptEntry & PRESENT_BIT)) return false;

    // 1 GB large page?
    if (pdptEntry & LARGE_PAGE_BIT) {
        outPhys = (pdptEntry & 0x000FFFFFC0000000ULL) | (va & 0x3FFFFFFF);
        return true;
    }

    // ── PD ────────────────────────────────────────────────────
    uintptr_t pdBase  = pdptEntry & PHYS_ADDR_MASK;
    uint64_t  pdIdx   = (va >> 21) & 0x1FF;
    uint64_t  pdEntry = 0;
    if (!ReadPhysical(pdBase + pdIdx * 8, &pdEntry, 8)) return false;
    if (!(pdEntry & PRESENT_BIT)) return false;

    // 2 MB large page?
    if (pdEntry & LARGE_PAGE_BIT) {
        outPhys = (pdEntry & 0x000FFFFFFFE00000ULL) | (va & 0x1FFFFF);
        return true;
    }

    // ── PT ────────────────────────────────────────────────────
    uintptr_t ptBase  = pdEntry & PHYS_ADDR_MASK;
    uint64_t  ptIdx   = (va >> 12) & 0x1FF;
    uint64_t  ptEntry = 0;
    if (!ReadPhysical(ptBase + ptIdx * 8, &ptEntry, 8)) return false;
    if (!(ptEntry & PRESENT_BIT)) return false;

    outPhys = (ptEntry & PHYS_ADDR_MASK) | (va & 0xFFF);
    return true;
}

// ─────────────────────────────────────────────────────────────
//  CR3 scan
//
//  Strategy:
//    - Iterate every page-aligned physical address as a PML4 candidate
//    - For each candidate CR3, walk the page tables for moduleVA
//    - Read the first 2 bytes of the resolved physical page
//    - If they == "MZ" → this CR3 correctly maps the module
//    - Additional sanity: verify "PE\0\0" at MZ.e_lfanew offset
//
//  This is intentionally slower (physical scan) but only runs once
//  at startup. Typical scan time on FPGA: 2–8 seconds.
//
//  Optimisation: scan only kernel-range PML4 entries (index 256+)
//  since the game process is at user-mode VA but the cloned CR3
//  is a kernel-level construct. We check all entries but skip
//  PML4 candidates whose self-reference entry doesn't pass a
//  quick validity check first.
// ─────────────────────────────────────────────────────────────

uintptr_t DMAHandler::ScanForCorrectCR3(uintptr_t moduleVA,
                                         uintptr_t physRamSize) {
    std::cout << "[CR3] Scanning physical memory for correct CR3...\n";
    std::cout << "[CR3] Target VA: 0x" << std::hex << moduleVA
              << "  RAM: 0x" << physRamSize << std::dec << "\n";

    // A valid PML4 table has its own physical address self-mapped
    // in entry 0x1ED (Windows kernel uses this index for recursive mapping).
    // Use as a quick pre-filter: read entry 0x1ED of candidate,
    // check if bits[51:12] == candidate itself.
    // This drastically reduces false positives.
    constexpr uint64_t SELFREF_IDX = 0x1ED;

    uint32_t candidates = 0;
    uint32_t checked    = 0;

    for (uintptr_t phys = 0; phys < physRamSize; phys += PML4_SCAN_STEP) {

        // ── Pre-filter: self-reference check ──────────────────
        uint64_t selfRef = 0;
        if (!ReadPhysical(phys + SELFREF_IDX * 8, &selfRef, 8)) continue;

        // Must be present and point back to phys (within noise)
        if (!(selfRef & PRESENT_BIT))                   continue;
        if ((selfRef & PHYS_ADDR_MASK) != phys)         continue;

        candidates++;

        // ── Walk page tables for moduleVA ─────────────────────
        uintptr_t resolvedPhys = 0;
        if (!VirtToPhysViaCustomCR3(phys, moduleVA, resolvedPhys)) continue;

        // ── Check MZ signature at resolved physical page ───────
        uint8_t magic[4] = {};
        if (!ReadPhysical(resolvedPhys, magic, 4)) continue;
        if (magic[0] != 'M' || magic[1] != 'Z')   continue;

        // ── Verify PE header ───────────────────────────────────
        // Read e_lfanew (4 bytes at offset 0x3C)
        uint32_t e_lfanew = 0;
        if (!ReadPhysical(resolvedPhys + 0x3C, &e_lfanew, 4)) continue;
        if (e_lfanew == 0 || e_lfanew > 0x400)    continue; // sanity

        uint8_t peSig[4] = {};
        if (!ReadPhysical(resolvedPhys + e_lfanew, peSig, 4)) continue;
        if (peSig[0] != 'P' || peSig[1] != 'E' ||
            peSig[2] != 0   || peSig[3] != 0)      continue;

        checked++;
        std::cout << "[CR3] Found valid CR3: 0x" << std::hex << phys
                  << "  (candidates checked: " << std::dec << candidates << ")\n";
        return phys;
    }

    std::cout << "[CR3] Scan complete. Candidates: " << candidates
              << "  PE-verified: " << checked << "\n";
    return 0;
}

// ─────────────────────────────────────────────────────────────
//  ApplyCR3Fix — public entry point
//
//  1. Get module base VA (MemProcFS still works for enumeration
//     even with wrong CR3 in many cases, or use a static VA)
//  2. Scan physical RAM for the correct CR3
//  3. Apply via VMMDLL_ConfigSet(VMMDLL_OPT_PROCESS_DTB)
//     This tells MemProcFS to use our CR3 for all subsequent
//     virtual reads on this PID.
// ─────────────────────────────────────────────────────────────

bool DMAHandler::ApplyCR3Fix(uintptr_t moduleVA, const std::string& moduleName) {
    if (!m_ready) return false;

    std::cout << "[CR3] Applying CR3 fix for " << moduleName << "\n";

    // Use provided VA, or try to get it from MemProcFS (may work even
    // with wrong CR3 since module enum uses PEB in some MemProcFS paths)
    uintptr_t targetVA = moduleVA;
    if (targetVA == 0) {
        targetVA = GetModuleBase(moduleName);
        if (targetVA == 0) {
            std::cerr << "[CR3] Cannot determine module VA. "
                      << "Pass it explicitly.\n";
            return false;
        }
    }

    // Get physical RAM upper bound via the physical memory map
    // (VMMDLL_OPT_CORE_MEMORYMODEL is the model enum, NOT a size — common bug)
    uintptr_t ramSize = 0;
    PVMMDLL_MAP_PHYSMEM pPhysMem = nullptr;
    if (VMMDLL_Map_GetPhysMem(m_hVMM, &pPhysMem) && pPhysMem) {
        for (DWORD i = 0; i < pPhysMem->cMap; i++) {
            uintptr_t endAddr = pPhysMem->pMap[i].pa + pPhysMem->pMap[i].cb;
            if (endAddr > ramSize) ramSize = endAddr;
        }
        VMMDLL_MemFree(pPhysMem);
    }
    if (ramSize == 0 || ramSize > 0x800000000ULL) {
        ramSize = SCAN_RAM_MAX;     // 16 GB safe upper bound
    }
    std::cout << "[CR3] Physical RAM upper bound: 0x" << std::hex
              << ramSize << std::dec << "\n";

    uintptr_t correctCR3 = ScanForCorrectCR3(targetVA, ramSize);
    if (correctCR3 == 0) {
        std::cerr << "[CR3] Fix failed — no valid CR3 found. "
                  << "Reads may return garbage.\n";
        return false;
    }

    // Tell MemProcFS to use this DTB for the process
    bool ok = VMMDLL_ConfigSet(m_hVMM,
                   VMMDLL_OPT_PROCESS_DTB | ((ULONG64)m_pid << 32),
                   correctCR3) != 0;

    if (ok) {
        m_cr3      = correctCR3;
        m_cr3Fixed = true;
        std::cout << "[CR3] Fix applied. CR3 = 0x"
                  << std::hex << correctCR3 << std::dec << "\n";
    } else {
        std::cerr << "[CR3] VMMDLL_ConfigSet failed.\n";
    }
    return ok;
}
