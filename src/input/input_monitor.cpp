#include "input_monitor.h"

#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <cstring>
#include <iostream>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

// ─────────────────────────────────────────────────────────────
//  KMBox Net B Pro — monitor-mode wire format (best-effort)
//
//  Protocol observed across common B Pro / B+ firmware revisions:
//    1. Attack PC sends a packet with cmd=CMD_MONITOR (0x0007)
//       to the kmbox. Same outer envelope as command packets:
//         u32 head = 0xAAAAAA
//         u32 indexpts (sequence)
//         u32 rand
//         u32 cmd      = 0x0007
//         ... (rest zeroed)
//         u32 crc32
//    2. The kmbox starts sending UDP datagrams back to the source
//       address/port. Each datagram carries the current HID state
//       at roughly 250–1000 Hz depending on firmware.
//    3. To stop, send cmd=CMD_MONITOR_OFF (0x0008) — or just close
//       the socket and the firmware will time it out.
//
//  Reply layout (the part we care about — payload offsets within
//  the 100-byte `point[]` field of the standard envelope):
//    point[0]      = USB HID keyboard modifier byte
//                      bit0 LCtrl  bit1 LShift  bit2 LAlt  bit3 LWin
//                      bit4 RCtrl  bit5 RShift  bit6 RAlt  bit7 RWin
//    point[1]      = reserved (0)
//    point[2..7]   = up to 6 simultaneously-held HID usage codes
//    point[8]      = mouse buttons
//                      bit0 L  bit1 R  bit2 M  bit3 X1  bit4 X2
//    point[9..12]  = mouse wheel (int32, LE) — unused here
//
//  If your firmware lays this out differently (some MAKCU-flashed
//  units do), the only place you need to touch is ParseMonitorPacket
//  below — everything above this point (queries, threading, web
//  binder) is wire-format agnostic.
// ─────────────────────────────────────────────────────────────

namespace {
constexpr uint32_t KMBOX_HEAD          = 0xAAAAAAu;
constexpr uint32_t CMD_MONITOR_ON      = 0x0007u;
constexpr uint32_t CMD_MONITOR_OFF     = 0x0008u;

#pragma pack(push, 1)
struct KMBoxEnvelope {
    uint32_t head;
    uint32_t indexpts;
    uint32_t rand;
    uint32_t cmd;
    int32_t  x;
    int32_t  y;
    int32_t  button;
    int32_t  wheel;
    uint8_t  point[100];
    uint32_t crc32;
};
#pragma pack(pop)
static_assert(sizeof(KMBoxEnvelope) == 136, "envelope size mismatch");

uint32_t CRC32(const void* data, size_t len) {
    static uint32_t table[256];
    static bool inited = false;
    if (!inited) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++)
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        inited = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    auto p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}
} // namespace

// ─────────────────────────────────────────────────────────────
//  HID usage code → Win32 VK mapping
//  Reference: USB HID Usage Tables, section 10 (Keyboard/Keypad)
// ─────────────────────────────────────────────────────────────
int InputMonitor::HidToVk(uint8_t hid) {
    // Letters a-z → 'A'..'Z'
    if (hid >= 0x04 && hid <= 0x1D) return 'A' + (hid - 0x04);
    // Digits 1-9, 0 (HID puts 1 first then 0 last)
    if (hid >= 0x1E && hid <= 0x26) return '1' + (hid - 0x1E);
    if (hid == 0x27)                return '0';
    // F1..F12
    if (hid >= 0x3A && hid <= 0x45) return VK_F1 + (hid - 0x3A);
    // F13..F24 (rare, but harmless to cover)
    if (hid >= 0x68 && hid <= 0x73) return VK_F13 + (hid - 0x68);

    switch (hid) {
        case 0x28: return VK_RETURN;
        case 0x29: return VK_ESCAPE;
        case 0x2A: return VK_BACK;
        case 0x2B: return VK_TAB;
        case 0x2C: return VK_SPACE;
        case 0x2D: return VK_OEM_MINUS;
        case 0x2E: return VK_OEM_PLUS;
        case 0x2F: return VK_OEM_4;       // [
        case 0x30: return VK_OEM_6;       // ]
        case 0x31: return VK_OEM_5;       // backslash
        case 0x33: return VK_OEM_1;       // ;
        case 0x34: return VK_OEM_7;       // '
        case 0x35: return VK_OEM_3;       // `
        case 0x36: return VK_OEM_COMMA;
        case 0x37: return VK_OEM_PERIOD;
        case 0x38: return VK_OEM_2;       // /
        case 0x39: return VK_CAPITAL;
        case 0x46: return VK_SNAPSHOT;
        case 0x47: return VK_SCROLL;
        case 0x48: return VK_PAUSE;
        case 0x49: return VK_INSERT;
        case 0x4A: return VK_HOME;
        case 0x4B: return VK_PRIOR;       // PgUp
        case 0x4C: return VK_DELETE;
        case 0x4D: return VK_END;
        case 0x4E: return VK_NEXT;        // PgDn
        case 0x4F: return VK_RIGHT;
        case 0x50: return VK_LEFT;
        case 0x51: return VK_DOWN;
        case 0x52: return VK_UP;
        case 0x53: return VK_NUMLOCK;
        default:   return 0;
    }
}

// ─────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────

bool InputMonitor::Start(const std::string& kmboxIp, uint16_t kmboxPort) {
    if (m_running.load()) return true;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[InputMon] WSAStartup failed.\n";
        return false;
    }

    auto* sock = new SOCKET(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (*sock == INVALID_SOCKET) {
        std::cerr << "[InputMon] socket() failed.\n";
        delete sock;
        return false;
    }
    m_sock = sock;

    // 50ms recv timeout — keeps shutdown responsive
    DWORD to = 50;
    setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&to), sizeof(to));

    auto* addr = new sockaddr_in{};
    addr->sin_family = AF_INET;
    addr->sin_port   = htons(kmboxPort);
    inet_pton(AF_INET, kmboxIp.c_str(), &addr->sin_addr);
    m_addr = addr;

    m_running = true;
    m_thread  = std::thread(&InputMonitor::RunKMBoxLoop, this);
    return true;
}

void InputMonitor::Stop() {
    if (!m_running.exchange(false)) return;

    if (m_sock) {
        auto* s = static_cast<SOCKET*>(m_sock);
        // Best-effort "monitor off" so the kmbox stops spamming us
        if (m_addr) {
            KMBoxEnvelope env{};
            env.head = KMBOX_HEAD;
            env.cmd  = CMD_MONITOR_OFF;
            env.crc32 = CRC32(&env, sizeof(env) - sizeof(uint32_t));
            sendto(*s, reinterpret_cast<char*>(&env), sizeof(env), 0,
                   static_cast<sockaddr*>(m_addr), sizeof(sockaddr_in));
        }
        closesocket(*s);
        delete s;
        m_sock = nullptr;
    }
    if (m_addr) {
        delete static_cast<sockaddr_in*>(m_addr);
        m_addr = nullptr;
    }

    if (m_thread.joinable()) m_thread.join();
    WSACleanup();
    m_kmConnected = false;
}

// ─────────────────────────────────────────────────────────────
//  Monitor reader thread
// ─────────────────────────────────────────────────────────────

void InputMonitor::RunKMBoxLoop() {
    auto* s    = static_cast<SOCKET*>(m_sock);
    auto* addr = static_cast<sockaddr_in*>(m_addr);

    // Send CMD_MONITOR_ON. If this fails we still spin — Win32 fallback
    // path keeps working for keys local to the attack PC.
    {
        KMBoxEnvelope env{};
        env.head     = KMBOX_HEAD;
        env.indexpts = 1;
        env.cmd      = CMD_MONITOR_ON;
        env.crc32    = CRC32(&env, sizeof(env) - sizeof(uint32_t));
        sendto(*s, reinterpret_cast<char*>(&env), sizeof(env), 0,
               reinterpret_cast<sockaddr*>(addr), sizeof(*addr));
    }

    auto lastResend = std::chrono::steady_clock::now();
    uint8_t buf[512];

    while (m_running.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(*s, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);

        if (n > 0) {
            m_kmConnected = true;
            m_kmPackets.fetch_add(1, std::memory_order_relaxed);
            ParseMonitorPacket(buf, n);
        }

        // Periodically re-send CMD_MONITOR_ON in case the kmbox lost it
        // (firmware quirk: some revisions stop streaming after idle).
        auto now = std::chrono::steady_clock::now();
        if (now - lastResend > std::chrono::seconds(2)) {
            KMBoxEnvelope env{};
            env.head     = KMBOX_HEAD;
            env.indexpts = static_cast<uint32_t>(m_kmPackets.load() + 2);
            env.cmd      = CMD_MONITOR_ON;
            env.crc32    = CRC32(&env, sizeof(env) - sizeof(uint32_t));
            sendto(*s, reinterpret_cast<char*>(&env), sizeof(env), 0,
                   reinterpret_cast<sockaddr*>(addr), sizeof(*addr));
            lastResend = now;
            // If we haven't seen a packet in 2s, mark disconnected
            // (state itself stays valid until overwritten).
            static uint64_t lastSeen = 0;
            uint64_t cur = m_kmPackets.load();
            if (cur == lastSeen) m_kmConnected = false;
            lastSeen = cur;
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  Parse a monitor reply → update m_kmKeys[] bitmap
// ─────────────────────────────────────────────────────────────

void InputMonitor::ParseMonitorPacket(const uint8_t* data, int len) {
    if (len < static_cast<int>(sizeof(KMBoxEnvelope))) return;

    auto* env = reinterpret_cast<const KMBoxEnvelope*>(data);
    if (env->head != KMBOX_HEAD) return;
    // Don't bother CRC-checking inbound — kmbox firmware sometimes
    // skips it on monitor packets to save cycles. Header magic is enough.

    const uint8_t* p = env->point;
    uint8_t modifier  = p[0];
    uint8_t mouseBtns = p[8];

    std::array<uint8_t, 256> next{};

    // Modifiers
    if (modifier & 0x01) next[VK_LCONTROL] = 1, next[VK_CONTROL] = 1;
    if (modifier & 0x02) next[VK_LSHIFT]   = 1, next[VK_SHIFT]   = 1;
    if (modifier & 0x04) next[VK_LMENU]    = 1, next[VK_MENU]    = 1;
    if (modifier & 0x08) next[VK_LWIN]     = 1;
    if (modifier & 0x10) next[VK_RCONTROL] = 1, next[VK_CONTROL] = 1;
    if (modifier & 0x20) next[VK_RSHIFT]   = 1, next[VK_SHIFT]   = 1;
    if (modifier & 0x40) next[VK_RMENU]    = 1, next[VK_MENU]    = 1;
    if (modifier & 0x80) next[VK_RWIN]     = 1;

    // Up to 6 simultaneously-held keys
    for (int i = 0; i < 6; i++) {
        uint8_t hid = p[2 + i];
        if (hid == 0) continue;
        int vk = HidToVk(hid);
        if (vk > 0 && vk < 256) next[vk] = 1;
    }

    // Mouse buttons → VK
    if (mouseBtns & 0x01) next[VK_LBUTTON]  = 1;
    if (mouseBtns & 0x02) next[VK_RBUTTON]  = 1;
    if (mouseBtns & 0x04) next[VK_MBUTTON]  = 1;
    if (mouseBtns & 0x08) next[VK_XBUTTON1] = 1;
    if (mouseBtns & 0x10) next[VK_XBUTTON2] = 1;

    // Edge detect for bind-capture & WasKeyPressed before we swap state
    if (m_capturing.load()) {
        std::lock_guard<std::mutex> capLock(m_capMtx);
        std::lock_guard<std::mutex> kbLock(m_kmMtx);
        if (m_capVk == 0) {
            for (int vk = 1; vk < 256; vk++) {
                if (next[vk] && !m_kmKeys[vk]) { m_capVk = vk; break; }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_kmMtx);
        m_kmKeys = next;
    }
}

// ─────────────────────────────────────────────────────────────
//  Queries — Win32 OR kmbox (whichever says "down")
// ─────────────────────────────────────────────────────────────

bool InputMonitor::IsKeyDown(int vk) const {
    if (vk <= 0 || vk >= 256) return false;

    // Win32 path — works when cheat process and game share the PC.
    if (GetAsyncKeyState(vk) & 0x8000) return true;

    // KMBox path — works for true 2-PC DMA setups.
    std::lock_guard<std::mutex> lock(m_kmMtx);
    return m_kmKeys[vk] != 0;
}

bool InputMonitor::WasKeyPressed(int vk) {
    if (vk <= 0 || vk >= 256) return false;
    bool now = IsKeyDown(vk);
    std::lock_guard<std::mutex> lock(m_edgeMtx);
    bool was = m_edgePrev[vk] != 0;
    m_edgePrev[vk] = now ? 1 : 0;
    return now && !was;
}

// ─────────────────────────────────────────────────────────────
//  Bind capture (web menu "press a key" UX)
// ─────────────────────────────────────────────────────────────

void InputMonitor::BeginCapture() {
    std::lock_guard<std::mutex> lock(m_capMtx);
    m_capVk = 0;
    m_capturing = true;
}

int InputMonitor::CapturedKey() {
    std::lock_guard<std::mutex> lock(m_capMtx);
    if (!m_capturing.load()) return 0;
    if (m_capVk != 0)        return m_capVk;   // already captured via kmbox

    // Win32 fallback so the binder works even when no kmbox is attached.
    // Skip mouse buttons here — those are noisy on a menu device (any
    // click captures). The kmbox monitor path catches game-PC mouse
    // buttons via ParseMonitorPacket without this restriction.
    for (int vk = 1; vk < 256; vk++) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
            vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            m_capVk = vk;
            break;
        }
    }
    return m_capVk;
}

void InputMonitor::CancelCapture() {
    std::lock_guard<std::mutex> lock(m_capMtx);
    m_capturing = false;
    m_capVk = 0;
}
