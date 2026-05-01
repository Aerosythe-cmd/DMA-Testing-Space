#pragma once
#include <string>
#include <cstdint>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

// ─────────────────────────────────────────────────────────
//  KMBox Net driver — UDP protocol
//  Send mouse deltas to KMBox which physically moves mouse.
//  Game sees a real HID device. Zero software footprint.
// ─────────────────────────────────────────────────────────

struct KMBoxCmd {
    uint32_t head;    // magic 0xAAAAAA
    uint32_t indexpts;
    uint32_t rand;
    uint32_t cmd;
    int32_t  x;
    int32_t  y;
    int32_t  button; // 1=left, 2=right
    int32_t  wheel;
    uint8_t  point[100];
    uint32_t crc32;
};

enum class KMBoxCmd_e : uint32_t {
    MouseMove    = 0x001,
    MouseLeft    = 0x002,
    MouseRight   = 0x003,
    MouseMiddle  = 0x004,
    MouseWheel   = 0x005,
    MouseAll     = 0x006,
};

class KMBox {
public:
    bool Init(const std::string& ip, uint16_t port);
    void Shutdown();
    bool IsConnected() const { return m_connected; }

    // Relative mouse move — signed deltas
    bool MouseMove(int dx, int dy);

    // Smoothed move — breaks into steps to look human
    bool MouseMoveSmooth(int dx, int dy, int steps = 8);

private:
    bool SendCmd(KMBoxCmd& cmd);
    uint32_t CRC32(const void* data, size_t len);

    SOCKET   m_sock      = INVALID_SOCKET;
    sockaddr_in m_addr   {};
    bool     m_connected = false;
    uint32_t m_index     = 0;
};
