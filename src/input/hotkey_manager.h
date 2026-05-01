#pragma once
#include <cstdint>
#include <array>

// ─────────────────────────────────────────────────────────────
//  HotkeyManager — wraps each gateable feature behind a hotkey
//
//  Each feature has:
//    enabled  (bool, from Config)  — master "consider this at all"
//    hotkey   (int VK code)        — which key controls it
//    mode     (HotkeyMode)         — Off / Hold / Toggle
//
//  Effective gate per mode:
//    Off    → enabled
//    Hold   → enabled && key_currently_down
//    Toggle → enabled && internal_toggle_state (flips on key edge,
//             defaults to ON when entering Toggle mode)
//
//  Tick() must be called once per frame from the main loop after
//  reading config + before features run.
// ─────────────────────────────────────────────────────────────

enum class HotkeyMode : int {
    Off    = 0,
    Hold   = 1,
    Toggle = 2,
};

enum class Feature : int {
    Aimbot     = 0,
    ESP        = 1,
    AntiRecoil = 2,
    Count      = 3,
};

class HotkeyManager {
public:
    static HotkeyManager& Get() {
        static HotkeyManager inst;
        return inst;
    }

    // Read config + input monitor, refresh per-feature active state.
    void Tick();

    // Effective "should this feature run this frame?"
    bool IsActive(Feature f) const;

    // Read-only access for the web/diag layer to display state
    bool ToggleState(Feature f) const;

private:
    HotkeyManager() {
        // Toggle features default to ON so the user isn't surprised
        // by everything being inactive after switching modes.
        for (auto& v : m_toggled) v = true;
    }

    std::array<bool, (int)Feature::Count> m_toggled{};
    std::array<bool, (int)Feature::Count> m_prevDown{};
    std::array<bool, (int)Feature::Count> m_active{};
};
