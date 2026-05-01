#pragma once
#include "../game/entity.h"
#include "../input/kmbox.h"
#include "../input/serial_device.h"
#include "../config/config.h"
#include <cmath>

// ─────────────────────────────────────────────────────
//  Aimbot — read-only on the game side.
//  Reads world positions via DMA, computes angle delta,
//  sends movement via KMBox/Arduino (HID, no software).
// ─────────────────────────────────────────────────────

// AimAngles is defined in entity.h (included above)

class Aimbot {
public:
    static Aimbot& Get() {
        static Aimbot inst;
        return inst;
    }

    // Call once per frame from main loop.
    // localSoldier: passed for bullet-speed read (lead prediction); pass 0 to disable.
    void Tick(const std::vector<Entity>& entities,
               const Vec3& camPos,
               const AimAngles& camAngles,
               uintptr_t localSoldier = 0);

    void SetKMBox(KMBox* km)         { m_kmbox   = km; }
    void SetSerial(SerialDevice* s)  { m_serial   = s; }

private:
    Aimbot() = default;

    // Find closest enemy to crosshair within FOV
    const Entity* GetBestTarget(const std::vector<Entity>& entities,
                                 const Vec3& camPos,
                                 const AimAngles& camAngles) const;

    // World → angle delta
    AimAngles CalcAngleDelta(const Vec3& from,
                              const AimAngles& fromAngles,
                              const Vec3& target) const;

    // Smooth factor — lerp towards target angle
    AimAngles Smooth(const AimAngles& delta, float factor) const;

    void SendMove(int dx, int dy);

    // Read bullet speed (m/s) from active weapon entity-data chain.
    // Returns 0 on any failure; caller skips prediction when 0.
    float ReadLocalBulletSpeed(uintptr_t localSoldier) const;

    KMBox*        m_kmbox  = nullptr;
    SerialDevice* m_serial = nullptr;
};
