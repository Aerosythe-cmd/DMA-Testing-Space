#include "diagnostics.h"
#include "dma/dma_handler.h"
#include "game/offsets.h"
#include "game/signatures.h"
#include "game/offset_resolver.h"
#include "game/offset_discovery.h"
#include "input/kmbox.h"
#include "input/serial_device.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <array>
#include <atomic>

namespace {

KMBox*        g_kmbox    = nullptr;
SerialDevice* g_serial   = nullptr;
int           g_inputIdx = 0;

// Frame snapshot atomics — written by main loop, read by web thread.
std::atomic<bool>      g_inMatch{false};
std::atomic<int>       g_enemyCount{0};
std::atomic<uintptr_t> g_gameCtx{0};

using Clock = std::chrono::steady_clock;

double SecondsToMicros(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

const char* DeviceName(int idx) {
    switch (idx) {
        case 0: return "KMBox";
        case 1: return "Arduino";
        case 2: return "Teensy";
        case 3: return "MAKCU";
        case 4: return "Ferrum";
        default: return "Unknown";
    }
}

// Send one mouse delta through whichever device is active.
bool SendOne(int dx, int dy, int steps = 1) {
    if (g_inputIdx == 0) {
        if (!g_kmbox || !g_kmbox->IsConnected()) return false;
        return g_kmbox->MouseMoveSmooth(dx, dy, steps);
    } else {
        if (!g_serial || !g_serial->IsConnected()) return false;
        return g_serial->MouseMoveSmooth(dx, dy, steps);
    }
}

bool DeviceConnected() {
    return (g_inputIdx == 0)
        ? (g_kmbox  && g_kmbox->IsConnected())
        : (g_serial && g_serial->IsConnected());
}

} // anon

namespace Diagnostics {

void SetKMBox(KMBox* k)              { g_kmbox = k; }
void SetSerial(SerialDevice* s)      { g_serial = s; }
void SetInputDeviceIndex(int idx)    { g_inputIdx = idx; }

void PushFrameSnapshot(bool inMatch, int enemyCount, uintptr_t gameCtx) {
    g_inMatch.store(inMatch,        std::memory_order_relaxed);
    g_enemyCount.store(enemyCount,  std::memory_order_relaxed);
    g_gameCtx.store(gameCtx,        std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────
//  DMA self-test — resolves the chain, validates each link,
//  measures single-read and scatter-read latency.
// ─────────────────────────────────────────────────────────────
DMAReport RunDMASelfTest() {
    DMAReport r;
    auto& dma = DMAHandler::Get();

    r.dmaReady = dma.IsReady();
    if (!r.dmaReady) {
        r.notes.push_back("DMA not initialized — FPGA disconnected or bf6.exe not running.");
        return r;
    }

    r.pid        = dma.GetPID();
    r.moduleBase = dma.GetModuleBase("bf6.exe");
    r.moduleSize = dma.GetModuleSize("bf6.exe");
    if (!r.moduleBase) r.notes.push_back("bf6.exe module not visible — CR3 fix likely failed.");

    // Resolve ClientGameContext via the same path main.cpp uses
    r.gameCtxStatic = OffsetResolver::ResolveStatic(
        "ClientGameContext", "bf6.exe",
        Signatures::ClientGameContext,
        Offsets::ClientGameContext,
        OffsetResolver::LooksLikePointer);

    if (r.gameCtxStatic) {
        r.gameCtx = dma.Read<uintptr_t>(r.gameCtxStatic);
    }
    r.cr3Fixed = (r.gameCtx != 0);  // chain wouldn't deref without correct CR3
    if (!r.gameCtx) r.notes.push_back("ClientGameContext deref returned 0 — CR3 wrong or offsets stale.");

    // Validate chain (prints rows; we additionally capture pass/fail)
    r.chainHealthy = OffsetDiscovery::ValidateChain(r.gameCtx);

    // Entity count from the main-thread snapshot (no race on EntityCache)
    r.enemyCount = g_enemyCount.load(std::memory_order_relaxed);

    // ── Latency: single 8-byte read × 100 ─────────────────────
    if (r.gameCtxStatic) {
        constexpr int N = 100;
        auto t0 = Clock::now();
        volatile uintptr_t sink = 0;
        for (int i = 0; i < N; i++) {
            sink = dma.Read<uintptr_t>(r.gameCtxStatic);
        }
        (void)sink;
        r.readLatencyUs = SecondsToMicros(Clock::now() - t0) / N;
    }

    // ── Latency: 64-read scatter batch × 8 ───────────────────
    if (r.gameCtxStatic) {
        constexpr int BATCHES = 8;
        constexpr int PER     = 64;
        std::array<uintptr_t, PER> buf{};
        auto t0 = Clock::now();
        for (int b = 0; b < BATCHES; b++) {
            auto sc = dma.BeginScatter();
            for (int i = 0; i < PER; i++)
                sc.Read(r.gameCtxStatic, &buf[i]);
            sc.Execute();
        }
        r.scatterLatencyUs = SecondsToMicros(Clock::now() - t0) / BATCHES;
    }

    return r;
}

// ─────────────────────────────────────────────────────────────
//  Input wiggle — moves +amp, then -amp, returns to start.
// ─────────────────────────────────────────────────────────────
InputReport RunInputWiggle(int amplitude) {
    InputReport r;
    r.deviceName  = DeviceName(g_inputIdx);
    r.deviceReady = DeviceConnected();
    if (!r.deviceReady) {
        r.error = "Input device not connected. Check KMBox/serial setup.";
        return r;
    }
    if (amplitude < 1)   amplitude = 1;
    if (amplitude > 200) amplitude = 200;   // cap so a misclick can't yeet the cursor

    auto t0 = Clock::now();
    bool ok = true;
    ok &= SendOne(+amplitude, 0, 6);  std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ok &= SendOne(-amplitude * 2, 0, 6);  std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ok &= SendOne(+amplitude, 0, 6);
    r.movesSent = 3;
    r.ok        = ok;
    if (!ok) r.error = "One or more send calls failed. Device may be flaky.";
    r.durationMs = SecondsToMicros(Clock::now() - t0) / 1000.0;
    return r;
}

// ─────────────────────────────────────────────────────────────
//  Input circle — traces a circle of radius `r`, in `steps` segments.
//  Each segment is the relative delta between consecutive points;
//  the last delta returns the cursor to the starting position.
// ─────────────────────────────────────────────────────────────
InputReport RunInputCircle(int radius, int steps) {
    InputReport r;
    r.deviceName  = DeviceName(g_inputIdx);
    r.deviceReady = DeviceConnected();
    if (!r.deviceReady) {
        r.error = "Input device not connected. Check KMBox/serial setup.";
        return r;
    }
    if (radius < 5)  radius = 5;
    if (radius > 80) radius = 80;
    if (steps  < 8)  steps  = 8;
    if (steps  > 64) steps  = 64;

    auto t0 = Clock::now();
    bool ok = true;
    int prevX = radius, prevY = 0;
    for (int i = 1; i <= steps; i++) {
        double a = (2.0 * 3.14159265 * i) / steps;
        int x = (int)std::round(std::cos(a) * radius);
        int y = (int)std::round(std::sin(a) * radius);
        ok &= SendOne(x - prevX, y - prevY, 4);
        prevX = x; prevY = y;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    r.movesSent  = steps;
    r.ok         = ok;
    if (!ok) r.error = "One or more circle steps failed.";
    r.durationMs = SecondsToMicros(Clock::now() - t0) / 1000.0;
    return r;
}

// ─────────────────────────────────────────────────────────────
//  Dry-fire recoil — N downward kicks spaced by ~80ms.
//  Verifies the antirecoil send path independent of game state.
// ─────────────────────────────────────────────────────────────
InputReport RunRecoilDryFire(int shots, int dyPerShot) {
    InputReport r;
    r.deviceName  = DeviceName(g_inputIdx);
    r.deviceReady = DeviceConnected();
    if (!r.deviceReady) {
        r.error = "Input device not connected. Check KMBox/serial setup.";
        return r;
    }
    if (shots     < 1)  shots     = 1;
    if (shots     > 30) shots     = 30;
    if (dyPerShot < 1)  dyPerShot = 1;
    if (dyPerShot > 40) dyPerShot = 40;

    auto t0 = Clock::now();
    bool ok = true;
    for (int i = 0; i < shots; i++) {
        ok &= SendOne(0, dyPerShot, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    r.movesSent  = shots;
    r.ok         = ok;
    if (!ok) r.error = "One or more recoil pulses failed.";
    r.durationMs = SecondsToMicros(Clock::now() - t0) / 1000.0;
    return r;
}

// ─────────────────────────────────────────────────────────────
//  Quick status — cheap snapshot for the header pills. No test
//  moves issued, no extra reads beyond what main loop already does.
// ─────────────────────────────────────────────────────────────
Status QuickStatus() {
    Status s;
    auto& dma = DMAHandler::Get();
    s.dmaReady        = dma.IsReady();
    s.gameCtx         = g_gameCtx.load(std::memory_order_relaxed);
    s.cr3Fixed        = (s.gameCtx != 0);
    s.inMatch         = g_inMatch.load(std::memory_order_relaxed);
    s.inputReady      = DeviceConnected();
    s.inputDeviceName = DeviceName(g_inputIdx);
    s.enemyCount      = g_enemyCount.load(std::memory_order_relaxed);
    return s;
}

} // namespace Diagnostics
