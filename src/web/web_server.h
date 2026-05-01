#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <httplib.h>   // cpp-httplib (header-only)
#include "../config/config.h"

// ─────────────────────────────────────────────────────
//  Web server — runs on second PC, serves menu on LAN
//  Access from phone/tablet/laptop browser: http://<ip>:8080
//  REST API + WebSocket for live updates
// ─────────────────────────────────────────────────────

class WebServer {
public:
    static WebServer& Get() {
        static WebServer inst;
        return inst;
    }

    bool Start(int port = 8080,
               const std::string& configDir = "./configs/");
    void Stop();
    bool IsRunning() const { return m_running; }

    // Called by main loop to push live player data to UI
    void PushPlayerData(const std::string& jsonPayload);

private:
    WebServer() = default;

    void SetupRoutes();

    httplib::Server   m_server;
    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::string       m_configDir;

    // Latest player data snapshot for polling
    std::string       m_playerDataJson;
    std::mutex        m_dataMtx;
};
