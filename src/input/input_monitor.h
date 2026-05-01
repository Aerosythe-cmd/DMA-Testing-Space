#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <array>
#include <cstdint>

// ─────────────────────────────────────────────────────────────
//  InputMonitor — game-PC keyboard/mouse state for hotkeys
//
//  The cheat process can run on either:
//    a) the GAME PC itself (rare for true DMA, but happens) → Win32
//       GetAsyncKeyState sees the game keyboard directly.
//    b) a separate ATTACK PC (true air-gap DMA) → Win32 only sees
//       the attack PC's keyboard, which has nothing to do with the
//       game. In that case we read input via the KMBox's monitor
//       mode: the keyboard physically plugs into the kmbox, which
//       passes it through to the game PC AND streams its state
//       back to the attack PC over UDP.
//
//  This class supports both. Win32 is always live (cheap). KMBox
//  monitor runs on a background thread and merges its state with
//  Win32's. IsKeyDown() returns true if either source says so.
//
//  All keys are normalised to Win32 virtual-key codes (VK_*).
//  Mouse buttons: VK_LBUTTON=0x01, VK_RBUTTON=0x02, VK_MBUTTON=0x04,
//                 VK_XBUTTON1=0x05, VK_XBUTTON2=0x06.
// ─────────────────────────────────────────────────────────────

class InputMonitor {
public:
    static InputMonitor& Get() {
        static InputMonitor inst;
        return inst;
    }

    // Start the kmbox monitor reader. Safe to call even if kmbox is
    // unreachable — Win32 polling still works for keys local to the
    // attack PC. Returns false only if the socket itself fails.
    bool Start(const std::string& kmboxIp, uint16_t kmboxPort);
    void Stop();

    // Thread-safe queries. vk = Win32 virtual-key code.
    // Edge: returns true on the frame the key transitions up→down.
    // Down: returns true while the key is held.
    bool IsKeyDown(int vk) const;
    bool WasKeyPressed(int vk);   // consumes edge — call once per frame max

    // ── Bind capture ─────────────────────────────────────────
    // Returns the most recently pressed key since BeginCapture().
    // 0 = nothing pressed yet. Used by the web-menu hotkey binder.
    void BeginCapture();
    int  CapturedKey();
    void CancelCapture();

    // ── Status / diagnostics ─────────────────────────────────
    bool      KMBoxConnected()  const { return m_kmConnected.load(); }
    uint64_t  KMBoxPackets()    const { return m_kmPackets.load();   }

private:
    InputMonitor() = default;
    ~InputMonitor() { Stop(); }

    // KMBox monitor reader thread loop
    void RunKMBoxLoop();

    // Wire-format helpers — see input_monitor.cpp for the protocol notes.
    // ParseMonitorPacket fills m_kmKeys[] from a raw kmbox UDP datagram.
    void ParseMonitorPacket(const uint8_t* data, int len);

    // Map USB HID usage code → Win32 VK (0 = unmapped)
    static int HidToVk(uint8_t hid);

    // ── State ────────────────────────────────────────────────
    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_kmConnected{false};
    std::atomic<uint64_t> m_kmPackets{0};

    // Mirror of game-PC keyboard/mouse state via kmbox monitor.
    // 256-bit bitmap indexed by VK code.
    mutable std::mutex m_kmMtx;
    std::array<uint8_t, 256> m_kmKeys{};   // 1 = down

    // Edge-detection state for WasKeyPressed (per-VK previous frame).
    mutable std::mutex m_edgeMtx;
    std::array<uint8_t, 256> m_edgePrev{};

    // Bind-capture: vk of first keypress since BeginCapture()
    mutable std::mutex m_capMtx;
    std::atomic<bool>  m_capturing{false};
    int                m_capVk = 0;

    // Socket lives here so Stop() can shut it down cleanly
    // (declared as void* to keep winsock out of the header)
    void* m_sock = nullptr;
    void* m_addr = nullptr;
};
