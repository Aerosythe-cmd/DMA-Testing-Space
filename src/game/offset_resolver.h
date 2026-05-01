#pragma once
#include <cstdint>
#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────
//  OffsetResolver — three-tier offset resolution.
//
//   Tier 1: pattern scan (sig from signatures.h) → RIP-relative deref
//   Tier 2: static fallback VA from offsets.h (last known good)
//   Tier 3: heuristic struct-field auto-discovery (DMA reads only)
//
//  Tiers 1 & 2 cover static VAs (e.g. ClientGameContext) — the result
//  is a VA inside bf6.exe.  Tier 3 covers struct field offsets (e.g.
//  ClientPlayer::SoldierEntity = 0x14D0) — given a known parent ptr
//  + a validator that recognises the target type, scans for the slot.
//
//  AC-safety note: every read here goes through DMAHandler, i.e. the
//  attack PC's PCIe interface only.  No code, hooks, or syscalls run
//  on the gaming PC.  Auto-discovery is just additional bounded reads.
// ─────────────────────────────────────────────────────────────

namespace OffsetResolver {

    // ── Pattern → VA helpers ──────────────────────────────────
    //
    // Find an IDA pattern in a module, then dereference it as a
    // RIP-relative reference. Most static-ptr loads in x64 look like
    //   mov rax, [rip+disp32]   ; 7 bytes total: 48 8B 05 dd dd dd dd
    // and the static's VA = matchAddr + instrLen + sint32(disp).
    //
    // sigOffset = byte offset of the disp32 within the matched bytes.
    // instrLen  = total length of the instruction containing the disp.
    //
    // Returns 0 on miss / read failure.
    uintptr_t ResolveRipRel(const std::string& moduleName,
                             const std::string& idaPattern,
                             int sigOffset = 3,
                             int instrLen  = 7);

    // High-level: resolve a static VA with sig + fallback + validation.
    //  1. Try ResolveRipRel(moduleName, idaPattern, sigOffset, instrLen)
    //  2. If hit and validator(sigResult) is true → return sigResult
    //  3. Else if validator(staticFallback) is true → return staticFallback
    //  4. Else → return 0 (caller must handle)
    //  5. Logs the path taken to stdout (sig / fallback(no-sig) /
    //     fallback(sig-rejected) / miss) so failures are visible.
    //
    // `validator` may be null → any non-zero result accepted.
    uintptr_t ResolveStatic(const std::string& name,
                             const std::string& moduleName,
                             const std::string& idaPattern,
                             uintptr_t staticFallback,
                             std::function<bool(uintptr_t)> validator = nullptr,
                             int sigOffset = 3,
                             int instrLen  = 7);

    // ── Struct-field auto-discovery ──────────────────────────
    //
    // Walk parentPtr in 8-byte aligned slots from minOffset to maxOffset.
    // For each slot, read it as a uintptr_t and call validator(ptrValue).
    // Returns the offset of the first slot that passes, or 0 on miss.
    //
    // Use case: "find ClientPlayer::SoldierEntity offset" when offsets.h
    // has 0x0 — given a known ClientPlayer*, scan its struct for any
    // pointer that, when followed, looks like a ClientSoldierEntity.
    //
    // Cost: one ReadRaw of (maxOffset-minOffset) bytes per parent —
    // the validator is then called locally on each pointer-aligned
    // slot of the buffer.  Span is hard-capped at 0x4000 bytes.
    uint32_t DiscoverPtrField(uintptr_t parentPtr,
                               uint32_t minOffset,
                               uint32_t maxOffset,
                               std::function<bool(uintptr_t candidate)> validator);

    // ── Built-in validators ──────────────────────────────────
    //
    // True if reading 8 bytes at va yields a value that looks like
    // a heap/module pointer (non-zero, canonical user-mode VA range).
    bool LooksLikePointer(uintptr_t va);

    // True if `candidate` reads back a vtable pointer that lives
    // inside bf6.exe's .rdata range. Caches the module range on
    // first call. Use this to confirm a candidate is a "class instance"
    // of some type defined in bf6.exe.
    bool LooksLikeClassInstance(uintptr_t candidate);

    // True if the first qword read at `candidate` equals expectedVTable.
    // Use with the TypeInfo VAs in offsets.h to confirm a struct is
    // exactly the type we expect.
    bool HasVTable(uintptr_t candidate, uintptr_t expectedVTable);

    // ── Diagnostics ──────────────────────────────────────────
    //
    // Prints a side-by-side report of each resolved offset:
    //   [RESOLVE] ClientGameContext: sig=0x149D14478  fallback=0x149D14478  used=sig
    // Useful for verifying sigs match what offsets.h expects.
    void PrintResolveTrace(const std::string& name,
                            uintptr_t fromSig,
                            uintptr_t fromFallback,
                            uintptr_t used,
                            const char* path);
}
