#include "aimbot.h"
#include "../config/config.h"
#include "../dma/dma_handler.h"
#include "../input/hotkey_manager.h"
#include <cmath>
#include <limits>

constexpr float DEG2RAD = 3.14159265f / 180.f;
constexpr float RAD2DEG = 180.f / 3.14159265f;

static float Distance2D(float dx, float dy) {
    return std::sqrt(dx * dx + dy * dy);
}

AimAngles Aimbot::CalcAngleDelta(const Vec3& from,
                                   const AimAngles& fromAngles,
                                   const Vec3& target) const {
    // ── Frostbite axis convention: y = up, z = forward, x = right ──
    // Matches main.cpp camera extraction:
    //   pitch = asin(-viewMatrix.m[2][1])  → asin(-forward.y)
    //   yaw   = atan2(viewMatrix.m[2][0], viewMatrix.m[2][2])
    //         → atan2(forward.x, forward.z)
    //
    // Therefore aim math is in the xz plane for horizontal,
    // dy is vertical, and pitch is negative when target is above.
    float dx = target.x - from.x;
    float dy = target.y - from.y;
    float dz = target.z - from.z;

    float horizDist = std::sqrt(dx * dx + dz * dz);   // xz plane

    // Pitch: negative when target is above (matches asin(-fwd.y) convention)
    float idealPitch = -std::atan2(dy, horizDist) * RAD2DEG;
    // Yaw: 0 = looking +z, +90 = looking +x (matches atan2(fwd.x, fwd.z))
    float idealYaw   =  std::atan2(dx, dz)         * RAD2DEG;

    float deltaPitch = idealPitch - fromAngles.pitch;
    float deltaYaw   = idealYaw   - fromAngles.yaw;

    // Normalize yaw to [-180, 180]
    while (deltaYaw >  180.f) deltaYaw -= 360.f;
    while (deltaYaw < -180.f) deltaYaw += 360.f;

    // Pitch shouldn't wrap but clamp defensively
    if (deltaPitch >  180.f) deltaPitch -= 360.f;
    if (deltaPitch < -180.f) deltaPitch += 360.f;

    return { deltaPitch, deltaYaw };
}

AimAngles Aimbot::Smooth(const AimAngles& delta, float factor) const {
    // factor 1.0 = instant snap, 0.0 = no movement
    // Config::Get().aimSmoothness is inverted (higher = smoother/slower)
    return { delta.pitch * factor, delta.yaw * factor };
}

const Entity* Aimbot::GetBestTarget(const std::vector<Entity>& entities,
                                     const Vec3& camPos,
                                     const AimAngles& camAngles) const {
    auto& cfg = Config::Get();
    float bestFov = cfg.aimFov; // degrees radius
    const Entity* best = nullptr;

    for (const auto& e : entities) {
        if (!e.cachedAlive) continue;
        if (cfg.aimOnlyVisible && !e.cachedVisible) continue;

        AimAngles delta = CalcAngleDelta(camPos, camAngles, e.cachedHeadPos);
        float fovDist   = Distance2D(delta.yaw, delta.pitch);

        if (fovDist < bestFov) {
            bestFov = fovDist;
            best    = &e;
        }
    }
    return best;
}

void Aimbot::SendMove(int dx, int dy) {
    if (m_kmbox && m_kmbox->IsConnected()) {
        m_kmbox->MouseMoveSmooth(dx, dy, Config::Get().aimSmoothSteps);
    } else if (m_serial && m_serial->IsConnected()) {
        m_serial->MouseMoveSmooth(dx, dy, Config::Get().aimSmoothSteps);
    }
}

// ─────────────────────────────────────────────────────────────
//  Local bullet speed — read from active weapon entity-data chain
//  Same chain antirecoil walks; the speed lives at WeaponEntityData + 0x2D8.
// ─────────────────────────────────────────────────────────────
float Aimbot::ReadLocalBulletSpeed(uintptr_t localSoldier) const {
    if (!localSoldier) return 0.f;
    auto& dma = DMAHandler::Get();
    auto R = [&](uintptr_t base, uint32_t off) -> uintptr_t {
        return base ? dma.Read<uintptr_t>(base + off) : 0;
    };

    uintptr_t weaponComp = R(localSoldier, Offsets::Soldier::WeaponsComponent);
    uintptr_t slot       = R(weaponComp,   Offsets::WeaponComponent::ActiveWeaponSlot);
    uintptr_t soldierWpn = R(slot,         Offsets::WeaponSlot::WeaponPtr);
    uintptr_t chain      = R(soldierWpn,   Offsets::SoldierWeapon::EntityDataChain);
    uintptr_t finalNode  = R(chain,        Offsets::WeaponDataChain::Next);
    uintptr_t entityData = R(finalNode,    Offsets::WeaponDataFinal::EntityData);
    if (!entityData) return 0.f;

    float speed = dma.Read<float>(entityData + Offsets::BulletData::Speed);
    // Sanity: typical FPS bullet speeds 200–1500 m/s; reject obvious garbage
    if (speed < 100.f || speed > 3000.f) return 0.f;
    return speed;
}

void Aimbot::Tick(const std::vector<Entity>& entities,
                   const Vec3& camPos,
                   const AimAngles& camAngles,
                   uintptr_t localSoldier) {
    auto& cfg = Config::Get();
    // HotkeyManager folds together: master enable + hotkey vk + mode
    // (Off / Hold / Toggle). Replaces direct GetAsyncKeyState so that
    // a 2-PC DMA setup (kmbox monitor on game PC) gates correctly.
    if (!HotkeyManager::Get().IsActive(Feature::Aimbot)) return;

    const Entity* target = GetBestTarget(entities, camPos, camAngles);
    if (!target) return;

    Vec3 aimPos = (cfg.aimBone == 0) ? target->cachedHeadPos : target->cachedChestPos;

    // ── Lead prediction ───────────────────────────────────────
    // Lead = target_velocity * (distance / bullet_speed)
    // Skipped unless prediction enabled, bullet speed valid, and we have
    // a non-trivial velocity (target actually moving).
    if (cfg.aimPrediction) {
        float bulletSpeed = ReadLocalBulletSpeed(localSoldier);
        if (bulletSpeed > 0.f) {
            float dx = aimPos.x - camPos.x;
            float dy = aimPos.y - camPos.y;
            float dz = aimPos.z - camPos.z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            float t = dist / bulletSpeed;   // seconds
            // Cap at 1 second of lead — beyond that the prediction is unreliable
            if (t > 1.f) t = 1.f;
            aimPos.x += target->cachedVelocity.x * t;
            aimPos.y += target->cachedVelocity.y * t;
            aimPos.z += target->cachedVelocity.z * t;
        }
    }

    AimAngles delta  = CalcAngleDelta(camPos, camAngles, aimPos);
    AimAngles smooth = Smooth(delta, 1.f / cfg.aimSmoothness);

    // Convert angle delta → mouse counts
    // pixelPerDeg is screen-space; mouseSensitivity is the per-game scale (tunable)
    float pixelPerDeg = cfg.screenW / (cfg.gameFov * 2.f);
    int dx = static_cast<int>(smooth.yaw   * pixelPerDeg * cfg.mouseSensitivity);
    int dy = static_cast<int>(smooth.pitch * pixelPerDeg * cfg.mouseSensitivity);

    if (dx == 0 && dy == 0) return;
    SendMove(dx, dy);
}
