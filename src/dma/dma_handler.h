#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <vmmdll.h>

// ─────────────────────────────────────────────────────────────
//  READ-ONLY INVARIANT (project-wide)
//
//  This class deliberately exposes ZERO write paths to the target
//  process's memory. The public API is:
//      Read<T>, ReadRaw, ReadString          — virtual reads
//      ReadPhysical                          — physical reads (CR3 scan)
//      Scatter::Read / ::ReadRaw             — batched virtual reads
//      Get*ModuleBase / GetModuleSize        — metadata
//      ApplyCR3Fix                           — writes MemProcFS's
//                                              local per-PID DTB; does
//                                              NOT touch game memory
//
//  No `Write*` method exists. No `VMMDLL_MemWrite*` symbol is
//  referenced anywhere in the codebase. Mouse output is delivered
//  via a separate USB HID device (KMBox / Arduino), not via DMA.
//
//  If you ever feel the need to add a write path here: STOP.
//   - Writes to game memory are detectable by EA AntiCheat (page
//     fault on R/O memory, integrity hashing, kernel page-table
//     watches, custom CR3 divergence checks).
//   - The whole reason this project works is that it never modifies
//     the gaming PC's memory or filesystem. Keep it that way.
//
//  Audit grep targets (must all return ZERO matches in src/):
//      VMMDLL_MemWrite       VMMDLL_MemWriteEx
//      WriteProcessMemory    NtWriteVirtualMemory
//      WriteRaw              WritePhysical
//      MemWrite              Write<
// ─────────────────────────────────────────────────────────────
//  CR3 / DTB Fix — why this is needed for BF6
//
//  BF6 runs under EA AntiCheat which clones the process CR3
//  (Directory Table Base / page directory pointer). The OS-reported
//  CR3 for bf6.exe is a decoy — reads through it return garbage.
//  The real CR3 is a clone maintained separately by the AC driver.
//
//  Fix strategy:
//    1. Use PCILeech FPGA to read PHYSICAL memory directly (no paging)
//    2. Scan physical pages for the game's PE signature at the
//       virtual address bf6.exe is supposed to be at
//    3. When found, extract the PML4 base (CR3) that correctly maps
//       that physical page at that virtual address
//    4. Override MemProcFS with the correct CR3 via VMMDLL_ConfigSet
//       (this writes MemProcFS's local per-PID state on the ATTACK PC;
//        no bytes are sent to the gaming PC).
//
//  After the fix: all DMA reads go through the real CR3 transparently.
//  No further special handling needed in the rest of the codebase.
//
//  Physical scan range: typically 0 → installed RAM
//  Alignment: PML4 tables are 0x1000-aligned
//  PE signature: MZ at offset 0, "PE\0\0" at e_lfanew
// ─────────────────────────────────────────────────────────────

class DMAHandler {
public:
    static DMAHandler& Get() {
        static DMAHandler inst;
        return inst;
    }

    bool Init(const std::string& targetProcess);
    void Shutdown();
    bool IsReady() const { return m_ready; }

    // ── Core read interface ───────────────────────────────────
    template<typename T>
    T Read(uintptr_t address) {
        T val{};
        VMMDLL_MemReadEx(m_hVMM, m_pid,
            address,
            reinterpret_cast<PBYTE>(&val),
            sizeof(T), nullptr,
            VMMDLL_FLAG_NOCACHE);
        return val;
    }

    bool        ReadRaw(uintptr_t address, void* buf, size_t size);
    std::string ReadString(uintptr_t address, size_t maxLen = 64);
    uintptr_t   GetModuleBase(const std::string& moduleName);
    size_t      GetModuleSize(const std::string& moduleName);

    DWORD GetPID() const { return m_pid; }

    // ── Pattern scanner ──────────────────────────────────────
    // IDA-style pattern: "48 8B 05 ?? ?? ?? ?? 48 8B C8" — spaces separate
    // hex byte tokens; "??" or "?" is a wildcard byte.
    // Reads target memory in 64KB chunks via DMA.
    // Returns absolute VA of first match, or 0 if not found.
    uintptr_t FindPattern(uintptr_t base, size_t size, const std::string& idaPattern);

    // Convenience: scan an entire module by name. Caches the resolved
    // address per pattern so subsequent calls are free.
    uintptr_t FindPatternInModule(const std::string& moduleName,
                                   const std::string& idaPattern);

    // ── Scatter (batched) reads ──────────────────────────────
    // Build via BeginScatter(), queue up reads, then Execute() once.
    // ALL queued reads land in a single DMA round trip.
    //
    // Usage:
    //   auto sc = dma.BeginScatter();
    //   sc.Read(va1, &dest1);
    //   sc.Read(va2, &dest2);
    //   sc.Execute();
    //   // dest1, dest2 now populated
    //
    // Constraint: destination buffers MUST remain valid until Execute()
    // returns and the caller is done reading. Use stack/reserved-vector
    // storage; do not std::vector::push_back into a queued buffer's vector.
    class Scatter {
    public:
        Scatter(VMM_HANDLE hVMM, DWORD pid);
        ~Scatter();
        Scatter(const Scatter&)            = delete;
        Scatter& operator=(const Scatter&) = delete;

        // Queue a read of sizeof(T) bytes into *dest.
        template<typename T>
        void Read(uintptr_t va, T* dest) {
            ReadRaw(va, reinterpret_cast<void*>(dest), sizeof(T));
        }
        void ReadRaw(uintptr_t va, void* dest, size_t cb);

        // Execute the batch. Buffers populated on success.
        bool Execute();

        // True if init succeeded and the handle is usable.
        bool IsValid() const { return m_handle != nullptr; }

    private:
        VMMDLL_SCATTER_HANDLE m_handle = nullptr;
    };

    Scatter BeginScatter() { return Scatter(m_hVMM, m_pid); }

    // ── CR3 fix ───────────────────────────────────────────────
    // Call after Init() succeeds.
    // Returns true if a valid CR3 was found and applied.
    bool ApplyCR3Fix(uintptr_t moduleVA, const std::string& moduleName);

private:
    DMAHandler() = default;
    ~DMAHandler() { Shutdown(); }

    // Physical memory helpers (bypass paging entirely)
    bool ReadPhysical(uintptr_t physAddr, void* buf, size_t size);

    // Walk candidate PML4 → PDPT → PD → PT to check if
    // virtualAddr maps to physAddr. Returns true on success.
    bool VirtToPhysViaCustomCR3(uintptr_t cr3Candidate,
                                 uintptr_t virtualAddr,
                                 uintptr_t& outPhysAddr);

    // Scan physical RAM for a valid PML4 that maps moduleVA
    // to a page beginning with the MZ/PE signature of the module.
    uintptr_t ScanForCorrectCR3(uintptr_t moduleVA,
                                 uintptr_t physRamSize);

    VMM_HANDLE m_hVMM     = nullptr;
    DWORD      m_pid      = 0;
    uintptr_t  m_cr3      = 0;   // confirmed correct CR3 after fix
    bool       m_ready    = false;
    bool       m_cr3Fixed = false;

    // Cache for FindPatternInModule to avoid re-scanning each call
    std::unordered_map<std::string, uintptr_t> m_sigCache;
};
