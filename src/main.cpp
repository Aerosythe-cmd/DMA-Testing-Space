#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>

#include "dma/dma_handler.h"
#include "game/entity.h"
#include "game/offsets.h"
#include "game/signatures.h"
#include "game/offset_resolver.h"
#include "game/offset_discovery.h"
#include "features/aimbot.h"
#include "features/antirecoil.h"
#include "features/esp.h"
#include "input/kmbox.h"
#include "input/serial_device.h"
#include "input/input_monitor.h"
#include "input/hotkey_manager.h"
#include "config/config.h"
#include "web/web_server.h"
#include "diagnostics.h"
#include <cstring>

// Helper: read local-player health for "in match" detection
static float ReadLocalHealth(uintptr_t localSoldier) {
    if (!localSoldier) return 0.f;
    auto& dma = DMAHandler::Get();
    uintptr_t hc = dma.Read<uintptr_t>(localSoldier + Offsets::Soldier::HealthComponent);
    if (!hc) return 0.f;
    return dma.Read<float>(hc + Offsets::HealthComponent::Health);
}

using json = nlohmann::json;

static bool g_running = true;

#include <Windows.h>
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) g_running = false;
    return TRUE;
}

static std::string BuildPlayerJson(const std::vector<Entity>& entities) {
    json arr = json::array();
    for (const auto& e : entities) {
        arr.push_back({
            {"name",   e.cachedName},
            {"weapon", e.cachedWeapon},
            {"health", e.cachedHealth},
            {"team",   e.cachedTeam},
        });
    }
    return arr.dump();
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // ── CLI flag: --discover ─────────────────────────────────
    // Runs the offset-discovery tool and exits. Use after a game
    // patch breaks the static VAs to get a fresh map for offsets.h.
    bool discoverMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--discover") == 0 ||
            std::strcmp(argv[i], "-d")          == 0) {
            discoverMode = true;
        }
    }

    std::cout << R"(
  ██████╗ ███████╗ ██████╗     ██████╗ ███╗   ███╗ █████╗
  ██╔══██╗██╔════╝██╔════╝     ██╔══██╗████╗ ████║██╔══██╗
  ██████╔╝█████╗  ███████╗     ██║  ██║██╔████╔██║███████║
  ██╔══██╗██╔══╝       ██║     ██║  ██║██║╚██╔╝██║██╔══██║
  ██████╔╝██║      ██████╔╝    ██████╔╝██║ ╚═╝ ██║██║  ██║
  ╚═════╝ ╚═╝      ╚═════╝     ╚═════╝ ╚═╝     ╚═╝╚═╝  ╚═╝
)" << "\n";

    Config::Get().Load("./configs/default.json");
    auto& cfg = Config::Get();

    // ── DMA init ─────────────────────────────────────────────
    std::cout << "[*] Initializing DMA...\n";
    if (!DMAHandler::Get().Init("bf6.exe")) {
        std::cerr << "[!] DMA init failed.\n";
        return 1;
    }

    // ── CR3 fix ───────────────────────────────────────────────
    // BF6 uses a cloned CR3 under EA AntiCheat.
    // Must be applied before any meaningful memory reads.
    // Pass the known static VA of bf6.exe (ASLR base — get from
    // DMAHandler::GetModuleBase first attempt, or hardcode if known).
    std::cout << "[*] Applying CR3 fix...\n";
    {
        // Try MemProcFS module enum first (sometimes works even with
        // wrong CR3 since it uses different kernel paths)
        uintptr_t moduleVA = DMAHandler::Get().GetModuleBase("bf6.exe");

        if (!DMAHandler::Get().ApplyCR3Fix(moduleVA, "bf6.exe")) {
            // Not fatal — reads will be attempted anyway.
            // Some firmware versions auto-fix CR3; check your card.
            std::cerr << "[!] CR3 fix failed. Reads may be unreliable.\n";
            std::cerr << "    If using Unchained/custom firmware, this may be handled by card.\n";
        }
    }

    // ── --discover short-circuit ─────────────────────────────
    // Skip input/web/ESP — discover only needs DMA + CR3.
    // Run(0) handles its own ClientGameContext resolution.
    if (discoverMode) {
        OffsetDiscovery::Run(0);
        std::cout << "\n[*] --discover complete. Exiting.\n";
        DMAHandler::Get().Shutdown();
        return 0;
    }

    // ── Input device ─────────────────────────────────────────
    KMBox        kmbox;
    SerialDevice serialDev;

    Diagnostics::SetKMBox(&kmbox);
    Diagnostics::SetSerial(&serialDev);
    Diagnostics::SetInputDeviceIndex(cfg.inputDevice);

    if (cfg.inputDevice == 0) {
        if (!kmbox.Init(cfg.kmboxIP, cfg.kmboxPort))
            std::cerr << "[!] KMBox init failed.\n";
        else {
            Aimbot::Get().SetKMBox(&kmbox);
            AntiRecoil::Get().SetKMBox(&kmbox);
        }

        // Game-PC keyboard/mouse state via kmbox monitor mode.
        // Win32 GetAsyncKeyState still works as fallback for keys
        // local to whichever PC this exe runs on.
        if (!InputMonitor::Get().Start(cfg.kmboxIP, cfg.kmboxPort))
            std::cerr << "[!] Input monitor failed to start; hotkeys "
                         "will only see attack-PC keys.\n";
    } else {
        static const SerialDeviceType typeMap[] = {
            SerialDeviceType::Unknown,
            SerialDeviceType::Arduino,
            SerialDeviceType::Teensy,
            SerialDeviceType::MAKCU,
            SerialDeviceType::Ferrum,
        };
        SerialDeviceType devType = (cfg.inputDevice < 5)
            ? typeMap[cfg.inputDevice]
            : SerialDeviceType::Unknown;

        if (!serialDev.Init(cfg.arduinoPort, 115200, devType))
            std::cerr << "[!] Serial device init failed.\n";
        else {
            Aimbot::Get().SetSerial(&serialDev);
            AntiRecoil::Get().SetSerial(&serialDev);
        }
    }

    // ── Web server ────────────────────────────────────────────
    std::cout << "[*] Starting web server on :8080\n";
    WebServer::Get().Start(8080, "./configs/");

    // ── ESP overlay (transparent always-on-top window) ─────────
    if (cfg.espEnabled) {
        std::cout << "[*] Initializing ESP overlay...\n";
        if (!ESP::Get().Init((int)cfg.screenW, (int)cfg.screenH)) {
            std::cerr << "[!] ESP init failed; ESP disabled this session.\n";
        }
    }

    // ── Resolve ClientGameContext static VA ──────────────────
    // Tier 1: pattern scan via Signatures::ClientGameContext
    // Tier 2: fallback to constexpr Offsets::ClientGameContext
    // Tier 3: --discover mode prints heuristic candidates
    std::cout << "[*] Resolving game context...\n";
    auto& dma = DMAHandler::Get();

    uintptr_t gameCtxStatic = OffsetResolver::ResolveStatic(
        "ClientGameContext", "bf6.exe",
        Signatures::ClientGameContext,
        Offsets::ClientGameContext,
        OffsetResolver::LooksLikePointer);

    uintptr_t gameCtx = gameCtxStatic ? dma.Read<uintptr_t>(gameCtxStatic) : 0;

    if (!gameCtx) {
        std::cerr << "[!] ClientGameContext null. Wrong offsets or bad read.\n";
        std::cerr << "    Re-run with --discover to find current offsets.\n";
        return 1;
    }
    std::cout << "[*] ClientGameContext: 0x" << std::hex << gameCtx << std::dec << "\n";

    // ── Validate the live struct chain; auto-discovery on miss ─
    if (!OffsetDiscovery::ValidateChain(gameCtx)) {
        std::cerr << "[!] Offset chain validation failed.\n";
        if (cfg.offsetAutoDiscover) {
            OffsetDiscovery::Run(gameCtx);
            std::cerr << "[!] Auto-discovery printed candidates above. Update "
                         "offsets.h and rebuild for full functionality.\n";
        } else {
            std::cerr << "    Set offsetAutoDiscover=true in config or run "
                         "with --discover to identify the new offsets.\n";
        }
    }

    std::cout << "[*] Running. Ctrl+C to exit.\n\n";

    const auto frameTime = std::chrono::milliseconds(8); // ~120 Hz

    while (g_running) {
        auto frameStart = std::chrono::steady_clock::now();

        // ── Local player chain ────────────────────────────────
        uintptr_t localClientPlayer = dma.Read<uintptr_t>(gameCtx + Offsets::LocalPlayer);

        int       localTeam   = 0;
        uintptr_t localSoldier = 0;
        if (localClientPlayer) {
            localTeam    = dma.Read<int>(      localClientPlayer + Offsets::ClientPlayer::TeamId);
            localSoldier = dma.Read<uintptr_t>(localClientPlayer + Offsets::ClientPlayer::SoldierEntity);
        }
        float localHealth = ReadLocalHealth(localSoldier);

        // ── Game state ────────────────────────────────────────
        // Only run features when actually in a match and alive.
        // Avoids aiming during menus, spawn screens, kill cams.
        bool inMatch = (localSoldier != 0 && localHealth > 0.f);

        Vec3      camPos{};
        AimAngles camAngles{};
        Matrix4x4 viewMatrix{};

        if (localSoldier) {
            uintptr_t camComp = dma.Read<uintptr_t>(localSoldier + Offsets::Soldier::CameraComponent);
            if (camComp) {
                viewMatrix = dma.Read<Matrix4x4>(camComp + Offsets::Camera::ViewMatrix);
                camPos.x = viewMatrix.m[3][0];
                camPos.y = viewMatrix.m[3][1];
                camPos.z = viewMatrix.m[3][2];
                camAngles.pitch = std::asin(-viewMatrix.m[2][1]) * (180.f / 3.14159265f);
                camAngles.yaw   = std::atan2(viewMatrix.m[2][0], viewMatrix.m[2][2]) * (180.f / 3.14159265f);
            }
        }

        // ── Entity cache (only refresh if in match) ───────────
        if (inMatch) {
            EntityCache::Get().Update(gameCtx, localTeam);
        }
        const auto& entities = EntityCache::Get().GetEntities();

        // ── Hotkey resolution (once per frame, before features) ──
        HotkeyManager::Get().Tick();

        // ── Features (gated on game state) ────────────────────
        if (inMatch) {
            Aimbot::Get().Tick(entities, camPos, camAngles, localSoldier);
            AntiRecoil::Get().Tick(localSoldier, camAngles);
        }

        // ── ESP overlay ───────────────────────────────────────
        if (HotkeyManager::Get().IsActive(Feature::ESP) &&
            ESP::Get().IsReady() && inMatch) {
            ESP::Get().Render(entities, viewMatrix);
        }

        // ── Web push (always, so menu shows status even out of match) ──
        WebServer::Get().PushPlayerData(BuildPlayerJson(entities));

        // ── Diagnostics frame snapshot for /api/status + tests ────────
        Diagnostics::PushFrameSnapshot(inMatch, (int)entities.size(), gameCtx);

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameTime)
            std::this_thread::sleep_for(frameTime - elapsed);
    }

    std::cout << "\n[*] Shutting down...\n";
    ESP::Get().Shutdown();
    WebServer::Get().Stop();
    InputMonitor::Get().Stop();
    kmbox.Shutdown();
    serialDev.Shutdown();
    DMAHandler::Get().Shutdown();
    return 0;
}
