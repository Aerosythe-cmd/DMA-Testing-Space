#include "hotkey_manager.h"
#include "input_monitor.h"
#include "../config/config.h"

namespace {
struct FeatureBinding {
    bool       enabled;
    int        vk;
    HotkeyMode mode;
};

FeatureBinding ResolveBinding(Feature f) {
    auto& cfg = Config::Get();
    switch (f) {
        case Feature::Aimbot:
            return { cfg.aimbotEnabled, cfg.aimbotHotkey,
                     static_cast<HotkeyMode>(cfg.aimbotHotkeyMode) };
        case Feature::ESP:
            return { cfg.espEnabled, cfg.espHotkey,
                     static_cast<HotkeyMode>(cfg.espHotkeyMode) };
        case Feature::AntiRecoil:
            return { cfg.antiRecoilEnabled, cfg.antiRecoilHotkey,
                     static_cast<HotkeyMode>(cfg.antiRecoilHotkeyMode) };
        default:
            return { false, 0, HotkeyMode::Off };
    }
}
} // namespace

void HotkeyManager::Tick() {
    auto& im = InputMonitor::Get();

    for (int i = 0; i < (int)Feature::Count; i++) {
        auto bind = ResolveBinding(static_cast<Feature>(i));

        if (!bind.enabled) {
            m_active[i]   = false;
            m_prevDown[i] = false;
            continue;
        }

        switch (bind.mode) {
            case HotkeyMode::Off: {
                m_active[i] = true;
                break;
            }
            case HotkeyMode::Hold: {
                bool down = (bind.vk != 0) && im.IsKeyDown(bind.vk);
                // vk=0 in Hold mode is meaningless; treat as always-on
                m_active[i] = (bind.vk == 0) ? true : down;
                break;
            }
            case HotkeyMode::Toggle: {
                bool down = (bind.vk != 0) && im.IsKeyDown(bind.vk);
                if (down && !m_prevDown[i]) m_toggled[i] = !m_toggled[i];
                m_prevDown[i] = down;
                m_active[i]   = m_toggled[i];
                break;
            }
            default: {
                // Out-of-range mode value (e.g. corrupt config) — treat
                // as Off so a feature with a garbage hotkey config still
                // behaves sanely as long as its master toggle is on.
                m_active[i] = true;
                break;
            }
        }
    }
}

bool HotkeyManager::IsActive(Feature f) const {
    int i = static_cast<int>(f);
    if (i < 0 || i >= (int)Feature::Count) return false;
    return m_active[i];
}

bool HotkeyManager::ToggleState(Feature f) const {
    int i = static_cast<int>(f);
    if (i < 0 || i >= (int)Feature::Count) return false;
    return m_toggled[i];
}
