#pragma once

// ─────────────────────────────────────────────────────────────
//  IDA-style signatures for BF6 static-VA resolution.
//
//  Each sig is a byte pattern matching an instruction in bf6.exe
//  that references the target static via RIP-relative addressing.
//
//  After OffsetResolver::ResolveRipRel hits, it dereferences the
//  disp32 at `sigOffset` (default 3 — byte index of the disp inside
//  a typical `mov rax,[rip+disp32]` instruction) and computes the
//  target VA as `match + instrLen + sint32(disp)`.
//
//  When a sig misses (game patched, layout shifted), the resolver
//  falls back to the static VA recorded in offsets.h.  When a sig
//  hits but the resolved value fails validation, it also falls back.
//
//  These patterns are starting points keyed off the 2026-04-27 build
//  RVAs in offsets.h — patch them when the game updates by re-RE'ing
//  the function epilogue around the affected static.
// ─────────────────────────────────────────────────────────────

namespace Signatures {

    // ── ClientGameContext ────────────────────────────────────
    // Pattern: `mov rax, [rip+disp]; test rax, rax; jz; mov rax, [rax+...]; ret`
    // The first instruction's disp32 holds the RVA to ClientGameContext.
    // sigOffset = 3 (skip "48 8B 05"), instrLen = 7 (mov rax,[rip+disp]).
    constexpr const char* ClientGameContext =
        "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 80 ?? ?? ?? ?? C3";

    // ── GameRenderer ─────────────────────────────────────────
    // `mov rcx, [rip+disp]; test rcx, rcx; jz; mov eax, [rcx+...]`
    // sigOffset = 3, instrLen = 7.
    constexpr const char* GameRenderer =
        "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 8B 81";

    // ── LocalPlayerStatic (direct local-player ptr) ──────────
    // Alternative path: skips ClientGameContext->LocalPlayer chain.
    // sigOffset = 3, instrLen = 7.
    constexpr const char* LocalPlayerStatic =
        "48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? 48 85 C9";

    // ── AimingSimulation (ClientSoldierAimingSimulation) ─────
    // Used by some implementations for raw view-angle reads.
    // sigOffset = 3, instrLen = 7.
    constexpr const char* AimingSimulation =
        "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? F3 0F 10";

    // ── ClientSoldierEntity_TypeInfo (vtable validator anchor) ─
    // Used to validate that a candidate ClientSoldierEntity pointer
    // really is one — its first qword should equal this VA.
    // Resolved as direct LEA: `lea rax, [rip+disp]` (sigOffset = 3, instrLen = 7).
    constexpr const char* ClientSoldierEntity_TypeInfo =
        "48 8D 05 ?? ?? ?? ?? 48 89 01 48 8D 05";
}
