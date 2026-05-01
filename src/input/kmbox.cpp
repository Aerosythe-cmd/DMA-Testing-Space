#include "kmbox.h"
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <cmath>

static uint32_t crc32_table[256];
static bool crc32_init = false;

static void BuildCRC32Table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_init = true;
}

uint32_t KMBox::CRC32(const void* data, size_t len) {
    if (!crc32_init) BuildCRC32Table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

bool KMBox::Init(const std::string& ip, uint16_t port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sock == INVALID_SOCKET) return false;

    // 100ms timeout
    DWORD timeout = 100;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<char*>(&timeout), sizeof(timeout));

    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family      = AF_INET;
    m_addr.sin_port        = htons(port);
    m_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    m_connected = true;
    std::cout << "[KMBox] Connected to " << ip << ":" << port << "\n";
    return true;
}

void KMBox::Shutdown() {
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    WSACleanup();
    m_connected = false;
}

bool KMBox::SendCmd(KMBoxCmd& cmd) {
    cmd.head     = 0xAAAAAA;
    cmd.indexpts = ++m_index;
    cmd.rand     = rand();
    // CRC over everything except the crc field itself
    cmd.crc32    = CRC32(&cmd, sizeof(cmd) - sizeof(uint32_t));

    int sent = sendto(m_sock,
        reinterpret_cast<char*>(&cmd), sizeof(cmd),
        0,
        reinterpret_cast<sockaddr*>(&m_addr), sizeof(m_addr));
    return sent == sizeof(cmd);
}

bool KMBox::MouseMove(int dx, int dy) {
    if (!m_connected) return false;
    KMBoxCmd cmd{};
    cmd.cmd = static_cast<uint32_t>(KMBoxCmd_e::MouseMove);
    cmd.x   = dx;
    cmd.y   = dy;
    return SendCmd(cmd);
}

// ─────────────────────────────────────────────────────────────
//  Humanized mouse smoothing
//
//  Replaces naive linear divide-by-N (which is statistically obvious in
//  input timing/path analysis) with a cubic-bezier ease-in/out curve plus
//  small per-call randomization of:
//    - control points (varies path shape across calls)
//    - sub-pixel jitter on each step
//    - inter-step sleep duration
//
//  The result is a path that starts slow, accelerates, decelerates, with
//  a slightly different curve every time — closer to actual human motor
//  patterns than a uniform-velocity straight line.
// ─────────────────────────────────────────────────────────────
bool KMBox::MouseMoveSmooth(int dx, int dy, int steps) {
    if (!m_connected || steps <= 0) return false;
    if (steps == 1)                  return MouseMove(dx, dy);

    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> ctrlVar (-0.06f, 0.06f);
    std::uniform_real_distribution<float> jitterPx(-0.4f,  0.4f);
    std::uniform_int_distribution<int>    delayUs (600,    1200);

    // Cubic-bezier control points for ease-in-out (slow start, fast mid, slow end)
    const float p1 = 0.05f + ctrlVar(rng);
    const float p2 = 0.95f + ctrlVar(rng);

    auto bezier01 = [](float t, float p1, float p2) {
        // 1D cubic bezier with anchors at 0 and 1
        const float u = 1.f - t;
        return 3.f * u * u * t * p1 + 3.f * u * t * t * p2 + t * t * t;
    };

    float prevX = 0.f, prevY = 0.f;
    for (int i = 1; i <= steps; i++) {
        float t    = static_cast<float>(i) / static_cast<float>(steps);
        float frac = bezier01(t, p1, p2);

        float curX = dx * frac + jitterPx(rng);
        float curY = dy * frac + jitterPx(rng);
        int   sx   = static_cast<int>(std::round(curX - prevX));
        int   sy   = static_cast<int>(std::round(curY - prevY));
        if (sx != 0 || sy != 0) MouseMove(sx, sy);
        prevX = curX;
        prevY = curY;

        std::this_thread::sleep_for(std::chrono::microseconds(delayUs(rng)));
    }
    return true;
}
