#include "web_server.h"
#include "../diagnostics.h"
#include "../input/input_monitor.h"
#include "../input/hotkey_manager.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ── JSON serializers for Diagnostics reports ─────────────────
static json ToJson(const Diagnostics::DMAReport& r) {
    return {
        {"dmaReady",         r.dmaReady},
        {"cr3Fixed",         r.cr3Fixed},
        {"pid",              r.pid},
        {"moduleBase",       r.moduleBase},
        {"moduleSize",       r.moduleSize},
        {"gameCtxStatic",    r.gameCtxStatic},
        {"gameCtx",          r.gameCtx},
        {"chainHealthy",     r.chainHealthy},
        {"enemyCount",       r.enemyCount},
        {"readLatencyUs",    r.readLatencyUs},
        {"scatterLatencyUs", r.scatterLatencyUs},
        {"notes",            r.notes},
    };
}
static json ToJson(const Diagnostics::InputReport& r) {
    return {
        {"deviceReady", r.deviceReady},
        {"deviceName",  r.deviceName},
        {"movesSent",   r.movesSent},
        {"ok",          r.ok},
        {"error",       r.error},
        {"durationMs",  r.durationMs},
    };
}
static json ToJson(const Diagnostics::Status& s) {
    return {
        {"dmaReady",        s.dmaReady},
        {"cr3Fixed",        s.cr3Fixed},
        {"inMatch",         s.inMatch},
        {"inputReady",      s.inputReady},
        {"inputDeviceName", s.inputDeviceName},
        {"enemyCount",      s.enemyCount},
        {"gameCtx",         s.gameCtx},
    };
}

bool WebServer::Start(int port, const std::string& configDir) {
    m_configDir = configDir;
    fs::create_directories(configDir);

    SetupRoutes();

    m_thread = std::thread([this, port]() {
        m_running = true;
        std::cout << "[WebServer] Listening on http://0.0.0.0:" << port << "\n";
        m_server.listen("0.0.0.0", port);
        m_running = false;
    });
    m_thread.detach();
    return true;
}

void WebServer::Stop() {
    m_server.stop();
    m_running = false;
}

void WebServer::PushPlayerData(const std::string& jsonPayload) {
    std::lock_guard<std::mutex> lock(m_dataMtx);
    m_playerDataJson = jsonPayload;
}

void WebServer::SetupRoutes() {
    // ── Serve web UI ─────────────────────────────────────────
    m_server.set_mount_point("/", "./web_menu");

    // ── GET current config ────────────────────────────────────
    m_server.Get("/api/config", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(Config::Get().ToJson().dump(), "application/json");
    });

    // ── POST update config ────────────────────────────────────
    m_server.Post("/api/config", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            Config::Get().LoadFromJson(j);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"bad json"})", "application/json");
        }
    });

    // ── GET live player data (polling) ────────────────────────
    m_server.Get("/api/players", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(m_dataMtx);
        if (m_playerDataJson.empty())
            res.set_content("[]", "application/json");
        else
            res.set_content(m_playerDataJson, "application/json");
    });

    // ── Cloud configs: list ───────────────────────────────────
    m_server.Get("/api/configs", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (auto& entry : fs::directory_iterator(m_configDir)) {
            if (entry.path().extension() == ".json")
                arr.push_back(entry.path().stem().string());
        }
        res.set_content(arr.dump(), "application/json");
    });

    // ── Cloud configs: save ───────────────────────────────────
    m_server.Post("/api/configs/save", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            std::string name = j.value("name", "unnamed");
            // Sanitize name
            for (auto& c : name)
                if (!isalnum(c) && c != '_' && c != '-') c = '_';

            std::string path = m_configDir + name + ".json";
            Config::Get().Save(path);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false})", "application/json");
        }
    });

    // ── Cloud configs: load ───────────────────────────────────
    m_server.Post("/api/configs/load", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            std::string name = j.value("name", "");
            std::string path = m_configDir + name + ".json";
            if (Config::Get().Load(path))
                res.set_content(R"({"ok":true})", "application/json");
            else {
                res.status = 404;
                res.set_content(R"({"ok":false,"error":"not found"})", "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false})", "application/json");
        }
    });

    // ── Diagnostics: status snapshot (cheap, polled by header pills) ─
    m_server.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(ToJson(Diagnostics::QuickStatus()).dump(), "application/json");
    });

    // ── Diagnostics: DMA chain validation + latency probe ──────
    m_server.Post("/api/test/dma", [](const httplib::Request&, httplib::Response& res) {
        auto rep = Diagnostics::RunDMASelfTest();
        res.set_content(ToJson(rep).dump(), "application/json");
    });

    // Helpers for input tests — pull optional integer from JSON body
    auto readInt = [](const httplib::Request& req, const char* key, int dflt) {
        try {
            if (req.body.empty()) return dflt;
            auto j = json::parse(req.body);
            if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
        } catch (...) {}
        return dflt;
    };

    // ── Diagnostics: input wiggle test ─────────────────────────
    m_server.Post("/api/test/wiggle", [readInt](const httplib::Request& req, httplib::Response& res) {
        int amp = readInt(req, "amplitude", 30);
        auto rep = Diagnostics::RunInputWiggle(amp);
        res.set_content(ToJson(rep).dump(), "application/json");
    });

    // ── Diagnostics: input circle trace ────────────────────────
    m_server.Post("/api/test/circle", [readInt](const httplib::Request& req, httplib::Response& res) {
        int radius = readInt(req, "radius", 25);
        int steps  = readInt(req, "steps",  16);
        auto rep = Diagnostics::RunInputCircle(radius, steps);
        res.set_content(ToJson(rep).dump(), "application/json");
    });

    // ── Diagnostics: dry-fire recoil pulse ─────────────────────
    m_server.Post("/api/test/recoil", [readInt](const httplib::Request& req, httplib::Response& res) {
        int shots     = readInt(req, "shots",     5);
        int dyPerShot = readInt(req, "dyPerShot", 8);
        auto rep = Diagnostics::RunRecoilDryFire(shots, dyPerShot);
        res.set_content(ToJson(rep).dump(), "application/json");
    });

    // ── Input bind: start capture ─────────────────────────────
    // Web menu calls this when the user clicks "Bind"; the next key
    // pressed (on game PC via kmbox monitor, or on attack PC via
    // Win32) gets captured and reported via /api/input/bind/status.
    m_server.Post("/api/input/bind/start", [](const httplib::Request&, httplib::Response& res) {
        InputMonitor::Get().BeginCapture();
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Input bind: poll status ───────────────────────────────
    m_server.Get("/api/input/bind/status", [](const httplib::Request&, httplib::Response& res) {
        json j = {
            {"capturedVk",      InputMonitor::Get().CapturedKey()},
            {"kmboxConnected",  InputMonitor::Get().KMBoxConnected()},
            {"kmboxPackets",    InputMonitor::Get().KMBoxPackets()},
        };
        res.set_content(j.dump(), "application/json");
    });

    // ── Input bind: cancel capture ────────────────────────────
    m_server.Post("/api/input/bind/cancel", [](const httplib::Request&, httplib::Response& res) {
        InputMonitor::Get().CancelCapture();
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Hotkey live state (for UI badges) ─────────────────────
    m_server.Get("/api/hotkeys/state", [](const httplib::Request&, httplib::Response& res) {
        auto& hm = HotkeyManager::Get();
        json j = {
            {"aimbotActive",     hm.IsActive(Feature::Aimbot)},
            {"espActive",        hm.IsActive(Feature::ESP)},
            {"antiRecoilActive", hm.IsActive(Feature::AntiRecoil)},
            {"aimbotToggle",     hm.ToggleState(Feature::Aimbot)},
            {"espToggle",        hm.ToggleState(Feature::ESP)},
            {"antiRecoilToggle", hm.ToggleState(Feature::AntiRecoil)},
        };
        res.set_content(j.dump(), "application/json");
    });

    // ── Cloud configs: delete ─────────────────────────────────
    m_server.Delete("/api/configs/:name", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.path_params.at("name");
        for (auto& c : name)
            if (!isalnum(c) && c != '_' && c != '-') c = '_';
        std::string path = m_configDir + name + ".json";
        if (fs::exists(path)) {
            fs::remove(path);
            res.set_content(R"({"ok":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"ok":false})", "application/json");
        }
    });
}
