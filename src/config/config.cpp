#include "config.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

void Config::LoadFromJson(const json& j) {
    std::lock_guard<std::mutex> lock(mtx);

    // Aimbot
    if (j.contains("aimbotEnabled"))    aimbotEnabled    = j["aimbotEnabled"];
    if (j.contains("aimbotHotkey"))     aimbotHotkey     = j["aimbotHotkey"];
    if (j.contains("aimbotHotkeyMode")) aimbotHotkeyMode = j["aimbotHotkeyMode"];
    if (j.contains("aimFov"))          aimFov          = j["aimFov"];
    if (j.contains("gameFov"))         gameFov         = j["gameFov"];
    if (j.contains("aimSmoothness"))   aimSmoothness   = j["aimSmoothness"];
    if (j.contains("aimSmoothSteps"))  aimSmoothSteps  = j["aimSmoothSteps"];
    if (j.contains("aimBone"))         aimBone         = j["aimBone"];
    if (j.contains("aimPrediction"))   aimPrediction   = j["aimPrediction"];
    if (j.contains("aimOnlyVisible"))  aimOnlyVisible  = j["aimOnlyVisible"];
    if (j.contains("mouseSensitivity")) mouseSensitivity = j["mouseSensitivity"];

    // ESP
    if (j.contains("espEnabled"))     espEnabled     = j["espEnabled"];
    if (j.contains("espHotkey"))      espHotkey      = j["espHotkey"];
    if (j.contains("espHotkeyMode"))  espHotkeyMode  = j["espHotkeyMode"];
    if (j.contains("espBox"))         espBox         = j["espBox"];
    if (j.contains("espSkeleton"))    espSkeleton    = j["espSkeleton"];
    if (j.contains("espName"))        espName        = j["espName"];
    if (j.contains("espWeapon"))      espWeapon      = j["espWeapon"];
    if (j.contains("espHealth"))      espHealth      = j["espHealth"];
    if (j.contains("espSnaplines"))   espSnaplines   = j["espSnaplines"];
    if (j.contains("espTeammates"))   espTeammates   = j["espTeammates"];
    if (j.contains("espMaxDistance")) espMaxDistance = j["espMaxDistance"];

    // Display
    if (j.contains("screenW"))        screenW        = j["screenW"];
    if (j.contains("screenH"))        screenH        = j["screenH"];
    if (j.contains("fuserMode"))      fuserMode      = j["fuserMode"];

    // Input
    if (j.contains("inputDevice"))    inputDevice    = j["inputDevice"];
    if (j.contains("kmboxIP"))        kmboxIP        = j["kmboxIP"];
    if (j.contains("kmboxPort"))      kmboxPort      = j["kmboxPort"];
    if (j.contains("arduinoPort"))    arduinoPort    = j["arduinoPort"];

    if (j.contains("antiRecoilEnabled"))    antiRecoilEnabled    = j["antiRecoilEnabled"];
    if (j.contains("antiRecoilHotkey"))     antiRecoilHotkey     = j["antiRecoilHotkey"];
    if (j.contains("antiRecoilHotkeyMode")) antiRecoilHotkeyMode = j["antiRecoilHotkeyMode"];
    if (j.contains("antiRecoilStrength"))   antiRecoilStrength   = j["antiRecoilStrength"];
    if (j.contains("antiRecoilResetMs"))    antiRecoilResetMs    = j["antiRecoilResetMs"];

    if (j.contains("offsetAutoDiscover")) offsetAutoDiscover = j["offsetAutoDiscover"];

    if (j.contains("configName"))     configName     = j["configName"];
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
