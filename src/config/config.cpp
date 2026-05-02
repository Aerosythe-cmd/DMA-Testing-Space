#include "config.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

void Config::LoadFromJson(const json& j) {
    std::lock_guard<std::mutex> lock(mtx);

    // Per-field load that swallows type-mismatch errors silently. A
    // hand-edited config that has e.g. "aimFov": "8" (string instead of
    // number) will keep the existing default for that one field rather
    // than aborting the whole load with a half-populated state.
    auto get = [&j](const char* key, auto& dest) {
        try { if (j.contains(key)) dest = j[key]; } catch (...) {}
    };

    // Aimbot
    get("aimbotEnabled",    aimbotEnabled);
    get("aimbotHotkey",     aimbotHotkey);
    get("aimbotHotkeyMode", aimbotHotkeyMode);
    get("aimFov",           aimFov);
    get("gameFov",          gameFov);
    get("aimSmoothness",    aimSmoothness);
    get("aimSmoothSteps",   aimSmoothSteps);
    get("aimBone",          aimBone);
    get("aimPrediction",    aimPrediction);
    get("aimOnlyVisible",   aimOnlyVisible);
    get("mouseSensitivity", mouseSensitivity);

    // ESP
    get("espEnabled",     espEnabled);
    get("espHotkey",      espHotkey);
    get("espHotkeyMode",  espHotkeyMode);
    get("espBox",         espBox);
    get("espSkeleton",    espSkeleton);
    get("espName",        espName);
    get("espWeapon",      espWeapon);
    get("espHealth",      espHealth);
    get("espSnaplines",   espSnaplines);
    get("espTeammates",   espTeammates);
    get("espMaxDistance", espMaxDistance);

    // Display
    get("screenW",   screenW);
    get("screenH",   screenH);
    get("fuserMode", fuserMode);

    // Input
    get("inputDevice", inputDevice);
    get("kmboxIP",     kmboxIP);
    get("kmboxPort",   kmboxPort);
    get("arduinoPort", arduinoPort);

    // Anti-recoil
    get("antiRecoilEnabled",    antiRecoilEnabled);
    get("antiRecoilHotkey",     antiRecoilHotkey);
    get("antiRecoilHotkeyMode", antiRecoilHotkeyMode);
    get("antiRecoilStrength",   antiRecoilStrength);
    get("antiRecoilResetMs",    antiRecoilResetMs);

    get("offsetAutoDiscover", offsetAutoDiscover);
    get("configName",         configName);
}

json Config::ToJson() const {
    std::lock_guard<std::mutex> lock(mtx);
    return {
        {"aimbotEnabled",    aimbotEnabled},
        {"aimbotHotkey",     aimbotHotkey},
        {"aimbotHotkeyMode", aimbotHotkeyMode},
        {"aimFov",          aimFov},
        {"gameFov",         gameFov},
        {"aimSmoothness",   aimSmoothness},
        {"aimSmoothSteps",  aimSmoothSteps},
        {"aimBone",         aimBone},
        {"aimPrediction",   aimPrediction},
        {"aimOnlyVisible",  aimOnlyVisible},
        {"mouseSensitivity", mouseSensitivity},
        {"espEnabled",     espEnabled},
        {"espHotkey",      espHotkey},
        {"espHotkeyMode",  espHotkeyMode},
        {"espBox",         espBox},
        {"espSkeleton",    espSkeleton},
        {"espName",        espName},
        {"espWeapon",      espWeapon},
        {"espHealth",      espHealth},
        {"espSnaplines",   espSnaplines},
        {"espTeammates",   espTeammates},
        {"espMaxDistance", espMaxDistance},
        {"screenW",        screenW},
        {"screenH",        screenH},
        {"fuserMode",      fuserMode},
        {"inputDevice",    inputDevice},
        {"kmboxIP",        kmboxIP},
        {"kmboxPort",      kmboxPort},
        {"arduinoPort",    arduinoPort},
        {"antiRecoilEnabled",    antiRecoilEnabled},
        {"antiRecoilHotkey",     antiRecoilHotkey},
        {"antiRecoilHotkeyMode", antiRecoilHotkeyMode},
        {"antiRecoilStrength",   antiRecoilStrength},
        {"antiRecoilResetMs",    antiRecoilResetMs},
        {"offsetAutoDiscover", offsetAutoDiscover},
        {"configName",         configName},
    };
}

bool Config::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        LoadFromJson(j);
        return true;
    } catch (...) {
        std::cerr << "[Config] Failed to parse " << path << "\n";
        return false;
    }
}

bool Config::Save(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << ToJson().dump(4);
    return true;
}
