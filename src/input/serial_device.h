#pragma once
#include <string>
#include <Windows.h>

// ─────────────────────────────────────────────────────────────
//  SerialDevice — covers ALL serial-based HID input devices:
//    Arduino (Leonardo, Pro Micro)
//    Teensy (2.0, 2.0++, 4.x with USB HID)
//    MAKCU
//    Ferrum
//
//  All of them speak the same protocol over a COM port:
//    "M <dx> <dy>\n"
//  Flash the matching sketch to whatever device you have.
//  No code changes needed here — just set the COM port.
// ─────────────────────────────────────────────────────────────

enum class SerialDeviceType {
    Arduino,
    Teensy,
    MAKCU,
    Ferrum,
    Unknown
};

class SerialDevice {
public:
    // deviceType is cosmetic only — protocol is identical for all
    bool Init(const std::string& comPort,
              DWORD baud = 115200,
              SerialDeviceType type = SerialDeviceType::Unknown);
    void Shutdown();
    bool IsConnected() const { return m_hCom != INVALID_HANDLE_VALUE; }

    SerialDeviceType GetType() const { return m_type; }

    // Relative mouse move — signed deltas, sent as "M dx dy\n"
    bool MouseMove(int dx, int dy);

    // Smoothed move — breaks into sub-steps to look human
    bool MouseMoveSmooth(int dx, int dy, int steps = 8);

private:
    bool WriteSerial(const std::string& data);

    HANDLE           m_hCom = INVALID_HANDLE_VALUE;
    SerialDeviceType m_type = SerialDeviceType::Unknown;
};
