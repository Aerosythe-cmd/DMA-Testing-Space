#include "offset_resolver.h"
#include "../dma/dma_handler.h"

#include <iostream>
#include <iomanip>
#include <cstring>
#include <vector>
#include <utility>

namespace {

// User-mode canonical VA range on Windows x64
constexpr uintptr_t USERMODE_MIN = 0x10000ULL;
constexpr uintptr_t USERMODE_MAX = 0x00007FFFFFFFFFFFULL;

// Cached bf6.exe module range for LooksLikeClassInstance.
// Populated on first call; both 0 if init failed.
struct ModuleRange {
    uintptr_t base = 0;
    uintptr_t end  = 0;
    bool      tried = false;
};
ModuleRange g_modBF6;

void EnsureBF6Range() {
    if (g_modBF6.tried) return;
    g_modBF6.tried = true;
    auto& dma = DMAHandler::Get();
    g_modBF6.base = dma.GetModuleBase("bf6.exe");
    if (g_modBF6.base) {
        g_modBF6.end = g_modBF6.base + dma.GetModuleSize("bf6.exe");
    }
}

} // anon

namespace OffsetResolver {

uintptr_t ResolveRipRel(const std::string& moduleName,
                         const std::string& idaPattern,
                         int sigOffset,
                         int instrLen) {
    auto& dma = DMAHandler::Get();
    uintptr_t match = dma.FindPatternInModule(moduleName, idaPattern);
    if (!match) return 0;

    // disp32 lives at match + sigOffset
    int32_t disp = 0;
    if (!dma.ReadRaw(match + sigOffset, &disp, sizeof(disp))) return 0;

    return match + instrLen + static_cast<int64_t>(disp);
}

uintptr_t ResolveStatic(const std::string& name,
                         const std::string& moduleName,
                         const std::string& idaPattern,
                         uintptr_t staticFallback,
                         std::function<bool(uintptr_t)> validator,
                         int sigOffset,
                         int instrLen) {
    uintptr_t fromSig = ResolveRipRel(moduleName, idaPattern, sigOffset, instrLen);

    bool sigGood = (fromSig != 0) && (!validator || validator(fromSig));
    bool fbGood  = (staticFallback != 0) && (!validator || validator(staticFallback));

    uintptr_t used = 0;
    const char* path = "miss";
    if (sigGood) {
        used = fromSig;
        path = "sig";
    } else if (fbGood) {
        used = staticFallback;
        path = (fromSig == 0) ? "fallback(no-sig)" : "fallback(sig-rejected)";
    }

    PrintResolveTrace(name, fromSig, staticFallback, used, path);
    return used;
}

uint32_t DiscoverPtrField(uintptr_t parentPtr,
                           uint32_t minOffset,
                           uint32_t maxOffset,
                           std::function<bool(uintptr_t)> validator) {
    if (!parentPtr || maxOffset <= minOffset || !validator) return 0;
    auto& dma = DMAHandler::Get();

    // Read parent struct in one shot. minOffset/maxOffset are aligned to 8.
    minOffset &= ~uint32_t(7);
    maxOffset &= ~uint32_t(7);

    const uint32_t span = maxOffset - minOffset;
    if (span == 0 || span > 0x4000) return 0;     // bound the scan

    std::vector<uint8_t> buf(span);
    if (!dma.ReadRaw(parentPtr + minOffset, buf.data(), span)) return 0;

    // Walk pointer-aligned slots
    for (uint32_t off = 0; off + sizeof(uintptr_t) <= span; off += sizeof(uintptr_t)) {
        uintptr_t cand = 0;
        std::memcpy(&cand, buf.data() + off, sizeof(uintptr_t));
        if (!cand) continue;
        if (validator(cand)) return minOffset + off;
    }
    return 0;
}

bool LooksLikePointer(uintptr_t va) {
    if (va < USERMODE_MIN || va > USERMODE_MAX) return false;
    // Pointers are 8-byte aligned in practice (x64 ABI for class instances)
    if (va & 0x7) return false;
    auto& dma = DMAHandler::Get();
    uintptr_t deref = dma.Read<uintptr_t>(va);
    // Reject obviously broken reads — all-zero or all-FF pages
    if (deref == 0) return false;
    if (deref == ~uintptr_t(0)) return false;
    return true;
}

bool LooksLikeClassInstance(uintptr_t candidate) {
    if (candidate < USERMODE_MIN || candidate > USERMODE_MAX) return false;
    if (candidate & 0x7) return false;
    EnsureBF6Range();
    if (!g_modBF6.base) return false;

    auto& dma = DMAHandler::Get();
    uintptr_t vtbl = dma.Read<uintptr_t>(candidate);
    if (!vtbl) return false;
    return (vtbl >= g_modBF6.base && vtbl < g_modBF6.end);
}

bool HasVTable(uintptr_t candidate, uintptr_t expectedVTable) {
    if (!candidate || !expectedVTable) return false;
    if (candidate & 0x7) return false;
    auto& dma = DMAHandler::Get();
    return dma.Read<uintptr_t>(candidate) == expectedVTable;
}

void PrintResolveTrace(const std::string& name,
                        uintptr_t fromSig,
                        uintptr_t fromFallback,
                        uintptr_t used,
                        const char* path) {
    std::cout << "[RESOLVE] " << std::left << std::setw(28) << name
              << "sig=0x"      << std::hex << std::setw(12) << fromSig
              << "fallback=0x" << std::setw(12) << fromFallback
              << "used=0x"     << std::setw(12) << used
              << std::dec      << "(" << path << ")\n";
}

} // namespace OffsetResolver
