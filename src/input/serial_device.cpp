#include "serial_device.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <random>
#include <cmath>

static const char* DeviceTypeName(SerialDeviceType t) {
    switch (t) {
        case SerialDeviceType::Arduino: return "Arduino";
        case SerialDeviceType::Teensy:  return "Teensy";
        case SerialDeviceType::MAKCU:   return "MAKCU";
        case SerialDeviceType::Ferrum:  return "Ferrum";
        default:                        return "Serial HID Device";
    }
}

bool SerialDevice::Init(const std::string& comPort, DWORD baud, SerialDeviceType type) {
    m_type = type;

    std::string fullPort = "\\\\.\\" + comPort;
    m_hCom = CreateFileA(fullPort.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);

    if (m_hCom == INVALID_HANDLE_VALUE) {
        std::cerr << "[" << DeviceTypeName(type) << "] Failed to open " << comPort << "\n";
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_hCom, &dcb)) { Shutdown(); return false; }
    dcb.BaudRate  = baud;
    dcb.ByteSize  = 8;
    dcb.StopBits  = ONESTOPBIT;
    dcb.Parity    = NOPARITY;
    if (!SetCommState(m_hCom, &dcb)) { Shutdown(); return false; }

    COMMTIMEOUTS timeouts{};
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(m_hCom, &timeouts);

    std::cout << "[" << DeviceTypeName(type) << "] Connected on "
              << comPort << " @ " << baud << "\n";
    return true;
}

void SerialDevice::Shutdown() {
    if (m_hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
    }
}

bool SerialDevice::WriteSerial(const std::string& data) {
    DWORD written = 0;
    return WriteFile(m_hCom,
        data.c_str(), static_cast<DWORD>(data.size()),
        &written, nullptr) && written == data.size();
}

bool SerialDevice::MouseMove(int dx, int dy) {
    if (!IsConnected()) return false;
    std::ostringstream ss;
    ss << "M " << dx << " " << dy << "\n";
    return WriteSerial(ss.str());
}

// ─────────────────────────────────────────────────────────────
//  Humanized smoothing — cubic bezier ease-in/out + jitter
//  (Same approach as KMBox::MouseMoveSmooth; see that file for rationale.)
// ─────────────────────────────────────────────────────────────
bool SerialDevice::MouseMoveSmooth(int dx, int dy, int steps) {
    if (!IsConnected() || steps <= 0) return false;
    if (steps == 1)                   return MouseMove(dx, dy);

    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> ctrlVar (-0.06f, 0.06f);
    std::uniform_real_distribution<float> jitterPx(-0.4f,  0.4f);
    std::uniform_int_distribution<int>    delayUs (600,    1200);

    const float p1 = 0.05f + ctrlVar(rng);
    const float p2 = 0.95f + ctrlVar(rng);

    auto bezier01 = [](float t, float p1, float p2) {
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
