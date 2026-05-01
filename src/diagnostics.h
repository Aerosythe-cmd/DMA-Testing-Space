#pragma once
#include <cstdint>
#include <string>
#include <vector>

class KMBox;
class SerialDevice;

// ─────────────────────────────────────────────────────────────
//  Diagnostics — connectivity + aiming-device self-tests.
//
//  All tests are run on demand from the web menu (Tests tab) or
//  by direct REST call. Reports are returned as plain structs and
//  serialized to JSON by the web layer.
//
//  Tests are read-only (DMA side) and bounded (input side: small,
//  recoverable mouse movements that any user can stop by moving
//  their physical mouse / hitting Esc on the gaming PC).
// ─────────────────────────────────────────────────────────────

namespace Diagnostics {

    // ── DMA chain + latency report ───────────────────────────
    struct DMAReport {
        bool        dmaReady       = false;     // VMMDLL initialized
        bool        cr3Fixed       = false;     // CR3 override applied
        uint32_t    pid            = 0;         // bf6.exe PID
        uintptr_t   moduleBase     = 0;         // bf6.exe base VA
        size_t      moduleSize     = 0;         // bf6.exe image size
        uintptr_t   gameCtxStatic  = 0;         // resolved address of CGC ptr
        uintptr_t   gameCtx        = 0;         // dereferenced value
        bool        chainHealthy   = false;     // ValidateChain pass
        int         enemyCount     = 0;         // entities currently cached
        double      readLatencyUs  = 0.0;       // avg single 8-byte read
        double      scatterLatencyUs = 0.0;     // avg 64-read scatter batch
        std::vector<std::string> notes;         // human-readable warnings
    };

    // ── Input-device test report ─────────────────────────────
    struct InputReport {
        bool        deviceReady = false;        // device handle is open
        std::string deviceName;                 // "KMBox", "Arduino", etc.
        int         movesSent   = 0;            // total deltas sent
        bool        ok          = false;        // every send succeeded
        std::string error;                      // populated when !ok
        double      durationMs  = 0.0;          // wall-clock time of test
    };

    // ── Singleton-style accessor ─────────────────────────────
    void SetKMBox(KMBox* k);
    void SetSerial(SerialDevice* s);
    void SetInputDeviceIndex(int idx);          // mirrors Config::inputDevice

    // ── Frame snapshot pushed by main loop each tick ─────────
    // Lets web-thread tests read game state without racing
    // the main thread's EntityCache writes.
    void PushFrameSnapshot(bool inMatch, int enemyCount, uintptr_t gameCtx);

    // ── Tests ────────────────────────────────────────────────

    // Read-only chain validation + latency probe.
    DMAReport RunDMASelfTest();

    // Sends a small back-and-forth wiggle (left amplitude px, then back).
    // amplitude: pixels (default 30). Recoverable if user is mid-game.
    InputReport RunInputWiggle(int amplitude = 30);

    // Traces a circle of given radius/steps — easy visual confirmation
    // that smoothing pipeline + device are both healthy.
    InputReport RunInputCircle(int radius = 25, int steps = 16);

    // Issues `shots` simulated recoil-compensation kicks (downward dy).
    // Helps verify the antirecoil send path independent of game state.
    InputReport RunRecoilDryFire(int shots = 5, int dyPerShot = 8);

    // ── Quick status snapshot (cheap, no test moves) ─────────
    struct Status {
        bool dmaReady      = false;
        bool cr3Fixed      = false;
        bool inMatch       = false;
        bool inputReady    = false;
        std::string inputDeviceName;
        int  enemyCount    = 0;
        uintptr_t gameCtx  = 0;
    };
    Status QuickStatus();
}
