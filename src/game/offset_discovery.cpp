#include "offset_discovery.h"
#include "offset_resolver.h"
#include "offsets.h"
#include "signatures.h"
#include "../dma/dma_handler.h"

#include <iostream>
#include <iomanip>

namespace {

void Hdr(const char* title) {
    std::cout << "\n────────────────────────────────────────────\n";
    std::cout << " " << title << "\n";
    std::cout << "────────────────────────────────────────────\n";
}

void Row(const char* label, uint32_t expected, uint32_t found, bool ok) {
    std::cout << "  " << std::left << std::setw(34) << label
              << " expected=0x" << std::hex << std::setw(6) << expected
              << " found=0x"    << std::setw(6) << found
              << std::dec       << "  " << (ok ? "OK" : "MISMATCH") << "\n";
}

bool LooksLikeClient(uintptr_t va) {
    return OffsetResolver::LooksLikeClassInstance(va);
}

} // anon

namespace OffsetDiscovery {

bool ValidateChain(uintptr_t gameCtx) {
    auto& dma = DMAHandler::Get();
    bool allGood = true;

    Hdr("Chain validation");

    if (!gameCtx) {
        std::cout << "  ClientGameContext is null — cannot validate.\n";
        return false;
    }

    // Step 1: gameCtx + LocalPlayer → ClientPlayer
    uintptr_t localPlayer = dma.Read<uintptr_t>(gameCtx + Offsets::LocalPlayer);
    bool lpOk = LooksLikeClient(localPlayer);
    Row("gameCtx -> LocalPlayer", Offsets::LocalPlayer, Offsets::LocalPlayer, lpOk);
    allGood &= lpOk;

    // Step 2: gameCtx + PlayerManager → ClientPlayerManager
    uintptr_t pm = dma.Read<uintptr_t>(gameCtx + Offsets::PlayerManager);
    bool pmOk = LooksLikeClient(pm);
    Row("gameCtx -> PlayerManager", Offsets::PlayerManager, Offsets::PlayerManager, pmOk);
    allGood &= pmOk;

    if (pm) {
        // Step 3: pm + ClientPlayersArray → array
        uintptr_t arr = dma.Read<uintptr_t>(pm + Offsets::ClientPlayersArray);
        // Array is just a ptr to an array of ptrs; first slot should be a ClientPlayer or null
        bool arrOk = (arr >= 0x10000ULL);
        Row("pm -> ClientPlayersArray", Offsets::ClientPlayersArray, Offsets::ClientPlayersArray, arrOk);
        allGood &= arrOk;

        if (arr) {
            // Probe slot 0
            uintptr_t firstPlayer = dma.Read<uintptr_t>(arr);
            if (firstPlayer) {
                // Step 4: firstPlayer + SoldierEntity → ClientSoldierEntity
                uintptr_t soldier = dma.Read<uintptr_t>(firstPlayer + Offsets::ClientPlayer::SoldierEntity);
                bool sOk = LooksLikeClient(soldier);
                Row("ClientPlayer -> SoldierEntity",
                    Offsets::ClientPlayer::SoldierEntity,
                    Offsets::ClientPlayer::SoldierEntity, sOk);
                allGood &= sOk;

                if (soldier) {
                    // Step 5: components
                    auto check = [&](const char* lbl, uint32_t off) {
                        uintptr_t c = dma.Read<uintptr_t>(soldier + off);
                        bool ok = LooksLikeClient(c);
                        Row(lbl, off, off, ok);
                        return ok;
                    };
                    allGood &= check("Soldier::HealthComponent",  Offsets::Soldier::HealthComponent);
                    allGood &= check("Soldier::WeaponsComponent", Offsets::Soldier::WeaponsComponent);
                    allGood &= check("Soldier::AnimaComponent",   Offsets::Soldier::AnimaComponent);
                    allGood &= check("Soldier::CameraComponent",  Offsets::Soldier::CameraComponent);
                }
            } else {
                std::cout << "  (no player in slot 0 — chain probe partial)\n";
            }
        }
    }

    std::cout << "\n  result: " << (allGood ? "all links healthy" : "DRIFT DETECTED") << "\n";
    return allGood;
}

void Run(uintptr_t gameCtx) {
    auto& dma = DMAHandler::Get();

    // ── Static VAs via signatures ────────────────────────────
    Hdr("Static VAs (sig vs offsets.h)");
    uintptr_t gameCtxStatic = OffsetResolver::ResolveStatic(
        "ClientGameContext", "bf6.exe",
        Signatures::ClientGameContext,
        Offsets::ClientGameContext,
        OffsetResolver::LooksLikePointer);
    OffsetResolver::ResolveStatic(
        "GameRenderer", "bf6.exe",
        Signatures::GameRenderer,
        Offsets::GameRenderer,
        OffsetResolver::LooksLikePointer);
    OffsetResolver::ResolveStatic(
        "LocalPlayerStatic", "bf6.exe",
        Signatures::LocalPlayerStatic,
        Offsets::LocalPlayerStatic,
        OffsetResolver::LooksLikePointer);

    // Resolve gameCtx if caller didn't pass one (use the same VA the
    // resolver settled on, not a direct fallback read).
    if (!gameCtx && gameCtxStatic) {
        gameCtx = dma.Read<uintptr_t>(gameCtxStatic);
    }
    if (!gameCtx) {
        std::cout << "[discover] gameCtx still null; aborting struct walk.\n";
        return;
    }

    // ── Validate current chain ───────────────────────────────
    bool chainGood = ValidateChain(gameCtx);
    if (chainGood) {
        std::cout << "\n[discover] chain is healthy — no struct-field discovery needed.\n";
        return;
    }

    // ── Heuristic struct-field discovery ─────────────────────
    Hdr("Heuristic discovery");

    // gameCtx -> LocalPlayer (try [0x500, 0x800])
    {
        uint32_t found = OffsetResolver::DiscoverPtrField(
            gameCtx, 0x400, 0x800, OffsetResolver::LooksLikeClassInstance);
        std::cout << "  gameCtx::LocalPlayer  candidate offset = 0x"
                  << std::hex << found << std::dec
                  << "  (offsets.h has 0x" << std::hex
                  << Offsets::LocalPlayer << std::dec << ")\n";
    }

    // gameCtx -> PlayerManager (try [0x40, 0x200])
    {
        uint32_t found = OffsetResolver::DiscoverPtrField(
            gameCtx, 0x40, 0x200, OffsetResolver::LooksLikeClassInstance);
        std::cout << "  gameCtx::PlayerManager candidate offset = 0x"
                  << std::hex << found << std::dec
                  << "  (offsets.h has 0x" << std::hex
                  << Offsets::PlayerManager << std::dec << ")\n";
    }

    // PlayerManager -> ClientPlayersArray (try [0x600, 0x900])
    {
        uintptr_t pm = dma.Read<uintptr_t>(gameCtx + Offsets::PlayerManager);
        if (pm) {
            // For the array-ptr we want a non-null heap pointer (not a class instance)
            uint32_t found = OffsetResolver::DiscoverPtrField(
                pm, 0x400, 0x1000, OffsetResolver::LooksLikePointer);
            std::cout << "  pm::ClientPlayersArray candidate offset = 0x"
                      << std::hex << found << std::dec
                      << "  (offsets.h has 0x" << std::hex
                      << Offsets::ClientPlayersArray << std::dec << ")\n";
        }
    }

    // ClientPlayer::SoldierEntity (need a known ClientPlayer first)
    {
        uintptr_t pm = dma.Read<uintptr_t>(gameCtx + Offsets::PlayerManager);
        uintptr_t arr = pm ? dma.Read<uintptr_t>(pm + Offsets::ClientPlayersArray) : 0;
        uintptr_t firstPlayer = arr ? dma.Read<uintptr_t>(arr) : 0;
        if (firstPlayer) {
            uint32_t found = OffsetResolver::DiscoverPtrField(
                firstPlayer, 0x1000, 0x1800, OffsetResolver::LooksLikeClassInstance);
            std::cout << "  ClientPlayer::SoldierEntity candidate offset = 0x"
                      << std::hex << found << std::dec
                      << "  (offsets.h has 0x" << std::hex
                      << Offsets::ClientPlayer::SoldierEntity << std::dec << ")\n";
        } else {
            std::cout << "  (no live player slot — can't discover ClientPlayer::*)\n";
        }
    }

    std::cout << "\n[discover] done. Update offsets.h with non-zero candidates above.\n";
}

} // namespace OffsetDiscovery
