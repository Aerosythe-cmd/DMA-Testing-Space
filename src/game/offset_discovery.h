#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────
//  OffsetDiscovery — runs as a separate program mode (--discover)
//  or after a startup validation failure.
//
//  Walks the live BF6 process via DMA, validates every confirmed
//  field offset in offsets.h against the running game, and for
//  any offset that no longer points at the expected type, runs
//  heuristic auto-discovery to find the new slot.
//
//  Output is a copy-pasteable diff for offsets.h.
//
//  AC-safety: pure DMA reads from the attack PC. Same surface
//  area as normal cheat operation; nothing new is written or
//  injected on the gaming PC.
// ─────────────────────────────────────────────────────────────

namespace OffsetDiscovery {

    // Run the full discovery pass.
    //   gameCtx — already-resolved ClientGameContext VA. Pass 0 to make
    //             Run() resolve it itself via OffsetResolver.
    // Prints findings to stdout. Always non-fatal.
    void Run(uintptr_t gameCtx);

    // Lightweight validation — walks the chain once and reports any
    // link that doesn't dereference to a class-shaped instance.
    // Returns true if every link looked sane.
    bool ValidateChain(uintptr_t gameCtx);
}
