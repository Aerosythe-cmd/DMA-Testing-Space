#pragma once
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

// ─────────────────────────────────────────────────────
//  Config — hot-reloadable, JSON-backed
//  Web server writes JSON → Config::Load() picks it up
// ─────────────────────────────────────────────────────

struct Config {
    // ── Aimbot ───────────────────────────────────────
    bool  aimbotEnabled    = true;
    int   aimbotHotkey     = 0x02;     // VK_RBUTTON
    int   aimbotHotkeyMode = 1;        // 0=Off (always-on) 1=Hold 2=Toggle
    float aimFov          = 8.f;       // degrees radius (target selection cone)
    float gameFov         = 90.f;      // actual game camera horizontal FOV (degrees)
    float aimSmoothness   = 4.f;       // 1=instant, higher=smoother
    int   aimSmoothSteps  = 8;         // mouse move sub-steps
    int   aimBone         = 0;         // 0=head, 1=chest
    bool  aimPrediction   = true;
    bool  aimOnlyVisible  = true;
    float mouseSensitivity= 0.022f;    // mouse-to-degree scale (game-specific, tune per setup)

    // ── ESP ──────────────────────────────────────────
    bool  espEnabled      = true;
    int   espHotkey       = 0;         // 0 = no hotkey
    int   espHotkeyMode   = 0;         // 0=Off 1=Hold 2=Toggle
    bool  espBox          = true;
    bool  espSkeleton     = true;
    bool  espName         = true;
    bool  espWeapon       = true;
    bool  espHealth       = true;
    bool  espSnaplines    = false;
    bool  espTeammates    = false;
    float espMaxDistance  = 200.f;     // meters

    // ── Display ──────────────────────────────────────
    float screenW         = 1920.f;
    float screenH         = 1080.f;
    bool  fuserMode       = false;     // overlay on primary monitor

    // ── Input device ─────────────────────────────────
    int         inputDevice   = 0;     // 0=KMBox, 1=Arduino
    std::string kmboxIP       = "192.168.2.188";
    uint16_t    kmboxPort     = 4096;
    std::string arduinoPort   = "COM3";

    // Anti-Recoil
    bool  antiRecoilEnabled    = true;
    int   antiRecoilHotkey     = 0;    // 0 = no hotkey
    int   antiRecoilHotkeyMode = 0;    // 0=Off 1=Hold 2=Toggle
    float antiRecoilStrength   = 1.0f; // 0.0=off 1.0=full >1.0=over-comp
    int   antiRecoilResetMs    = 350;  // ms no shots before pattern resets

    // ── Offset auto-discovery ────────────────────────
    // When true and a static-VA sig miss + fallback both fail validation,
    // run heuristic struct-field discovery via DMA reads. Adds ~50–200ms
    // to startup. Off by default — flip on after a game patch breaks
    // the static VAs and you don't have a fresh sig yet.
    bool  offsetAutoDiscover  = false;

    // ── Config meta ──────────────────────────────────
    std::string configName    = "default";

    // ── Singleton ────────────────────────────────────
    static Config& Get() {
        static Config inst;
        return inst;
    }

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    void LoadFromJson(const nlohmann::json& j);
    nlohmann::json ToJson() const;

    // Thread-safe lock for web server writes
    mutable std::mutex mtx;

private:
    Config() = default;
};
