#include "antirecoil.h"
#include "../dma/dma_handler.h"
#include "../config/config.h"
#include "../features/aimbot.h"  // AimAngles struct
#include "../input/hotkey_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

static uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// ─────────────────────────────────────────────────────────────
//  Constructor — build fallback table
// ─────────────────────────────────────────────────────────────

AntiRecoil::AntiRecoil() {
    BuildFallbackTable();
}

// ─────────────────────────────────────────────────────────────
//  Weapon entity data pointer chain walk
//
//  Chain (all reads are DMA, read-only):
//    localPlayer
//      + Soldier::WeaponsComponent (0x1700)  → weaponCompPtr
//    weaponCompPtr
//      + WeaponComponent::ActiveWeaponSlot (0x3990) → slotPtr
//    slotPtr
//      + WeaponSlot::WeaponPtr (0x038)       → soldierWeaponPtr
//    soldierWeaponPtr
//      + SoldierWeapon::EntityDataChain (0x0F08) → chainPtr
//    chainPtr
//      + WeaponDataChain::Next (0x150)       → finalPtr
//    finalPtr
//      + WeaponDataFinal::EntityData (0x0020)→ weaponEntityDataPtr
//    weaponEntityDataPtr
//      + WeaponEntityData::SoldierWeaponData → soldierWpnDataPtr
//    soldierWpnDataPtr
//      + SoldierWeaponData::ShotConfigData   → shotConfigPtr
//
//  Returns shotConfigPtr, or 0 if any step fails.
// ─────────────────────────────────────────────────────────────

uintptr_t AntiRecoil::ResolveShotConfigData(uintptr_t localSoldier) {
    auto& dma = DMAHandler::Get();

    auto R = [&](uintptr_t base, uint32_t off) -> uintptr_t {
        if (!base) return 0;
        return dma.Read<uintptr_t>(base + off);
    };

    uintptr_t weaponComp  = R(localSoldier, Offsets::Soldier::WeaponsComponent);
    uintptr_t slot        = R(weaponComp,   Offsets::WeaponComponent::ActiveWeaponSlot);
    uintptr_t soldierWpn  = R(slot,         Offsets::WeaponSlot::WeaponPtr);
    uintptr_t chain       = R(soldierWpn,   Offsets::SoldierWeapon::EntityDataChain);
    uintptr_t finalNode   = R(chain,        Offsets::WeaponDataChain::Next);
    uintptr_t entityData  = R(finalNode,    Offsets::WeaponDataFinal::EntityData);
    uintptr_t wpnData     = R(entityData,   Offsets::WeaponEntityData::SoldierWeaponData);
    uintptr_t shotConfig  = R(wpnData,      Offsets::SoldierWeaponData::ShotConfigData);

    return shotConfig;
}

// ─────────────────────────────────────────────────────────────
//  Read ShotConfigData → build RecoilPattern
//
//  Frostbite stores recoil as:
//    A) Per-shot float arrays (RecoilTablePtr / RecoilYawTablePtr)
//       RecoilTableSize tells how many entries. These are the real
//       per-bullet kick angles in degrees applied to the camera.
//       We convert degrees → pixels using sensitivity + screen size.
//
//    B) Scalar fallback (BaseKickPitch / KickVariancePitch etc.)
//       Used for weapons with uniform recoil (no distinct pattern).
//       We generate a synthetic pattern from these.
//
//  Sanity check: FireRateRPM should be 400–1400 for any real weapon.
//  If it reads 0 or >5000 the offset is wrong — bail out.
// ─────────────────────────────────────────────────────────────

// Convert Frostbite camera kick angle (degrees) → mouse counts using
// the configured cfg.mouseSensitivity (per-user/per-game scale factor).
// 0.022 was the legacy hardcoded Source-engine ratio; now it's replaced
// with cfg.mouseSensitivity which the user tunes for their BF6 setup.
static float DegToPixels(float deg, float screenW, float fov, float sens) {
    float ppd = screenW / fov;       // pixels per degree at current FOV
    return deg * ppd * sens;
}

bool AntiRecoil::ReadShotConfigData(uintptr_t shotConfigPtr,
                                     const std::string& weaponName,
                                     RecoilPattern& out) {
    if (!shotConfigPtr) return false;
    if (Offsets::ShotConfigData::RecoilTablePtr == 0 &&
        Offsets::ShotConfigData::BaseKickPitch  == 0) {
        // Offsets not filled in yet — can't read
        return false;
    }

    auto& dma = DMAHandler::Get();
    auto& cfg = Config::Get();

    // ── Sanity check via fire rate ─────────────────────────────
    if (Offsets::ShotConfigData::FireRateRPM != 0) {
        float rpm = dma.Read<float>(
            shotConfigPtr + Offsets::ShotConfigData::FireRateRPM);
        if (rpm < 100.f || rpm > 6000.f) {
            std::cout << "[AntiRecoil] ShotConfigData sanity fail "
                      << "(RPM=" << rpm << "). Wrong offset?\n";
            return false;
        }
    }

    out.weaponKey  = weaponName;
    out.fromMemory = true;
    out.steps.clear();

    // ── Path A: per-shot table ────────────────────────────────
    if (Offsets::ShotConfigData::RecoilTablePtr  != 0 &&
        Offsets::ShotConfigData::RecoilTableSize != 0) {

        uintptr_t pitchTablePtr = dma.Read<uintptr_t>(
            shotConfigPtr + Offsets::ShotConfigData::RecoilTablePtr);
        uintptr_t yawTablePtr   = dma.Read<uintptr_t>(
            shotConfigPtr + Offsets::ShotConfigData::RecoilYawTablePtr);
        int tableSize           = dma.Read<int>(
            shotConfigPtr + Offsets::ShotConfigData::RecoilTableSize);

        if (pitchTablePtr && tableSize > 0 && tableSize <= 64) {
            // Read entire tables in one shot each (minimize round trips)
            std::vector<float> pitchTable(tableSize, 0.f);
            std::vector<float> yawTable(tableSize, 0.f);

            dma.ReadRaw(pitchTablePtr, pitchTable.data(),
                        tableSize * sizeof(float));
            if (yawTablePtr)
                dma.ReadRaw(yawTablePtr, yawTable.data(),
                            tableSize * sizeof(float));

            for (int i = 0; i < tableSize; i++) {
                float pitchDeg = pitchTable[i];
                float yawDeg   = yawTable.empty() ? 0.f : yawTable[i];

                // Sanity: kick should be 0–15 degrees per shot
                if (std::abs(pitchDeg) > 15.f || std::abs(yawDeg) > 15.f)
                    continue;

                RecoilStep step;
                // Pitch is positive-up in Frostbite;
                // we push mouse DOWN to counter (positive y = down on screen)
                step.y = DegToPixels(pitchDeg, cfg.screenW, cfg.gameFov * 2.f, cfg.mouseSensitivity);
                step.x = DegToPixels(yawDeg,   cfg.screenW, cfg.gameFov * 2.f, cfg.mouseSensitivity);
                out.steps.push_back(step);
            }

            if (!out.steps.empty()) {
                std::cout << "[AntiRecoil] Read " << out.steps.size()
                          << " steps from memory for [" << weaponName << "]\n";
                return true;
            }
        }
    }

    // ── Path B: scalar fallback → synthetic pattern ────────────
    // Frostbite scalar recoil: basePitch is the mean kick per shot,
    // varPitch is the +/- random spread around it.
    // We generate a synthetic pattern using only the base (mean) values
    // since variance is random per-shot and can't be predicted.
    // Ramp factor models the typical Frostbite recoil curve:
    //   first ~5 shots ramp up, then plateau at full base kick.
    if (Offsets::ShotConfigData::BaseKickPitch != 0) {
        float basePitch = dma.Read<float>(
            shotConfigPtr + Offsets::ShotConfigData::BaseKickPitch);
        float baseYaw   = dma.Read<float>(
            shotConfigPtr + Offsets::ShotConfigData::BaseKickYaw);

        if (basePitch > 0.f && basePitch < 15.f) {
            for (int i = 0; i < 20; i++) {
                float ramp  = std::min(1.f, (float)i / 5.f);
                float pitch = basePitch * ramp;
                float yaw   = baseYaw * (i % 2 == 0 ? 1.f : -1.f) * 0.3f;

                RecoilStep step;
                step.y = DegToPixels(pitch, cfg.screenW, cfg.gameFov * 2.f, cfg.mouseSensitivity);
                step.x = DegToPixels(yaw,   cfg.screenW, cfg.gameFov * 2.f, cfg.mouseSensitivity);
                out.steps.push_back(step);
            }

            std::cout << "[AntiRecoil] Generated synthetic pattern "
                      << "from scalar data for [" << weaponName << "]\n";
            return true;
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────
//  TryReadPatternFromMemory — public wrapper
// ─────────────────────────────────────────────────────────────

bool AntiRecoil::TryReadPatternFromMemory(uintptr_t localSoldier,
                                           const std::string& weaponName,
                                           RecoilPattern& out) {
    uintptr_t shotConfig = ResolveShotConfigData(localSoldier);
    if (!shotConfig) return false;
    return ReadShotConfigData(shotConfig, weaponName, out);
}

// ─────────────────────────────────────────────────────────────
//  Main tick
// ─────────────────────────────────────────────────────────────

void AntiRecoil::Tick(uintptr_t localSoldier, const AimAngles& camAngles) {
    auto& cfg = Config::Get();
    if (!HotkeyManager::Get().IsActive(Feature::AntiRecoil)) return;
    if (!localSoldier)          return;

    auto& dma = DMAHandler::Get();

    // ── Read weapon name + ammo ───────────────────────────────
    // Walk: localSoldier → WeaponsComponent → active slot → name + ammo
    uintptr_t weaponComp = dma.Read<uintptr_t>(
        localSoldier + Offsets::Soldier::WeaponsComponent);
    uintptr_t slot       = dma.Read<uintptr_t>(
        weaponComp + Offsets::WeaponComponent::ActiveWeaponSlot);
    uintptr_t soldierWpn = dma.Read<uintptr_t>(
        slot + Offsets::WeaponSlot::WeaponPtr);

    if (!soldierWpn) return;

    int currentAmmo = dma.Read<int>(
        soldierWpn + Offsets::SoldierWeapon::CurrentAmmo);

    // Read weapon name via entity data chain
    std::string currentWeapon = m_cachedWeaponName;
    if (soldierWpn != m_cachedWeaponPtr) {
        m_cachedWeaponPtr  = soldierWpn;
        // Read weapon name (TBD exact offset — falls back gracefully to "")
        uintptr_t namePtr = dma.Read<uintptr_t>(
            soldierWpn + Offsets::SoldierWeapon::WeaponName);
        currentWeapon = namePtr ? dma.ReadString(namePtr, 48) : "unknown";
        m_cachedWeaponName = currentWeapon;
    }

    // ── Weapon switch → reset state + re-resolve pattern ─────
    if (currentWeapon != m_lastWeapon) {
        m_lastWeapon  = currentWeapon;
        m_shotIndex   = 0;
        m_lastAmmo    = currentAmmo;

        // Evict cache for this weapon so we re-read from memory
        m_patternCache.erase(currentWeapon);
        return;
    }

    // ── Ensure we have a pattern cached for this weapon ───────
    if (m_patternCache.find(currentWeapon) == m_patternCache.end()) {
        RecoilPattern pat;
        if (TryReadPatternFromMemory(localSoldier, currentWeapon, pat)) {
            m_patternCache[currentWeapon] = std::move(pat);
        } else {
            // Use fallback — mark as such so we can retry memory next weapon switch
            const RecoilPattern* fb = FindFallback(currentWeapon);
            if (fb) {
                m_patternCache[currentWeapon] = *fb;
                std::cout << "[AntiRecoil] Using fallback pattern for ["
                          << currentWeapon << "]\n";
            } else {
                // Empty pattern — just mark as visited so we don't spam
                m_patternCache[currentWeapon] = RecoilPattern{currentWeapon, false, {}};
            }
        }
    }

    // ── Pattern reset on stop-shooting ────────────────────────
    uint64_t nowMs = NowMs();
    bool shotFired = (m_lastAmmo > 0 && currentAmmo == m_lastAmmo - 1);

    if (shotFired) {
        m_lastShotMs = nowMs;
    } else {
        if ((nowMs - m_lastShotMs) > (uint64_t)cfg.antiRecoilResetMs)
            m_shotIndex = 0;
    }

    m_lastAmmo = currentAmmo;
    if (!shotFired) return;

    // ── Apply current pattern step ────────────────────────────
    const auto& pat = m_patternCache[currentWeapon];
    if (pat.steps.empty()) return;

    int idx = m_shotIndex % (int)pat.steps.size();
    RecoilStep step = pat.steps[idx];

    // Apply config strength multiplier
    step.x *= cfg.antiRecoilStrength;
    step.y *= cfg.antiRecoilStrength;

    ApplyStep(step);
    m_shotIndex++;
}

void AntiRecoil::ApplyStep(const RecoilStep& step) {
    int dx = static_cast<int>(std::round(step.x));
    int dy = static_cast<int>(std::round(step.y));
    if (dx == 0 && dy == 0) return;
    SendMove(dx, dy);
}

void AntiRecoil::SendMove(int dx, int dy) {
    if (m_kmbox && m_kmbox->IsConnected())
        m_kmbox->MouseMove(dx, dy);
    else if (m_serial && m_serial->IsConnected())
        m_serial->MouseMove(dx, dy);
}

// ─────────────────────────────────────────────────────────────
//  Fallback hardcoded table (last resort)
// ─────────────────────────────────────────────────────────────

static bool WeaponContains(const std::string& name, const std::string& key) {
    if (key == "_generic") return false;
    return std::search(name.begin(), name.end(), key.begin(), key.end(),
        [](char a, char b){ return std::tolower(a) == std::tolower(b); })
        != name.end();
}

const RecoilPattern* AntiRecoil::FindFallback(const std::string& weaponName) const {
    for (const auto& p : m_fallback)
        if (WeaponContains(weaponName, p.weaponKey)) return &p;
    for (const auto& p : m_fallback)
        if (p.weaponKey == "_generic") return &p;
    return nullptr;
}

void AntiRecoil::BuildFallbackTable() {
    // Substring-matched against weapon name; first hit wins.
    // Values are approximate per-shot kick (degrees-equivalent;
    // converted to pixels via DegToPixels) tuned for typical BF6 weapon
    // classes. These are last-resort patterns when memory walk fails —
    // once ShotConfigData offsets are populated, the live values are
    // always preferred and replace these.
    //
    // Weapon class heuristics:
    //   AR    : 3-4° per shot, ramps for first 5 shots, then plateau ~3°
    //   Carbine : similar to AR but lower (2.5-3°)
    //   SMG   : 2-3° per shot, lower ramp
    //   LMG   : higher sustain (3.5°+) over long bursts
    //   BR    : burst-fire spike then settle (e.g. AN-94)
    m_fallback = {
        // ── Assault rifles (BF6 + carryover) ─────────────────
        { "M4A1",    false, {{0,3.2f},{0,3.5f},{0.4f,3.8f},{-0.4f,3.8f},{0,3.5f},{0,3.3f},{0.3f,3.0f},{-0.3f,3.0f},{0,2.8f},{0,2.8f},{0,2.5f},{0,2.5f}} },
        { "M16",     false, {{0,2.8f},{0,3.0f},{0.3f,3.2f},{-0.3f,3.2f},{0,3.0f},{0,3.0f},{0.3f,2.8f},{-0.3f,2.8f},{0,2.6f},{0,2.6f},{0,2.4f},{0,2.4f}} },
        { "XM7",     false, {{0,3.0f},{0,3.5f},{0.4f,3.8f},{-0.4f,3.8f},{0,3.5f},{0,3.3f},{0.3f,3.0f},{-0.3f,3.0f},{0,2.8f},{0,2.8f},{0,2.6f},{0,2.6f}} },
        { "AK-12",   false, {{0,3.8f},{0.5f,4.2f},{0.5f,4.4f},{-0.5f,4.2f},{-0.5f,4.0f},{0,3.8f},{0.5f,3.6f},{-0.5f,3.6f},{0,3.4f},{0,3.4f},{0,3.2f},{0,3.2f}} },
        { "AK",      false, {{0,3.8f},{0.5f,4.2f},{0.5f,4.4f},{-0.5f,4.2f},{-0.5f,4.0f},{0,3.8f},{0.5f,3.6f},{-0.5f,3.6f},{0,3.4f},{0,3.4f},{0,3.2f},{0,3.2f}} },
        { "MCX",     false, {{0,3.0f},{0,3.3f},{0.3f,3.6f},{-0.3f,3.6f},{0,3.3f},{0,3.0f},{0.3f,2.8f},{-0.3f,2.8f},{0,2.6f},{0,2.6f}} },
        { "HK416",   false, {{0,3.2f},{0,3.5f},{0.3f,3.7f},{-0.3f,3.7f},{0,3.5f},{0,3.3f},{0.3f,3.0f},{-0.3f,3.0f},{0,2.8f},{0,2.8f}} },
        { "AN-94",   false, {{0,2.0f},{0,4.5f},{0.3f,4.8f},{-0.3f,4.5f},{0,4.2f},{0,4.0f},{0.3f,3.8f},{-0.3f,3.8f},{0,3.5f},{0,3.5f},{0,3.2f},{0,3.0f}} },

        // ── SMGs ─────────────────────────────────────────────
        { "MP5",     false, {{0,2.2f},{0,2.5f},{0.3f,2.6f},{-0.3f,2.6f},{0,2.4f},{0,2.4f},{0,2.2f},{0,2.2f},{0,2.0f},{0,2.0f}} },
        { "MP7",     false, {{0,2.0f},{0,2.3f},{0.3f,2.4f},{-0.3f,2.4f},{0,2.2f},{0,2.0f},{0,2.0f},{0,1.8f}} },
        { "Vector",  false, {{0,1.8f},{0,2.0f},{0.3f,2.2f},{-0.3f,2.2f},{0,2.0f},{0,1.8f},{0,1.8f},{0,1.6f}} },
        { "UMP",     false, {{0,2.5f},{0,2.8f},{0.3f,2.8f},{-0.3f,2.8f},{0,2.5f},{0,2.5f},{0,2.3f},{0,2.3f},{0,2.0f},{0,2.0f}} },
        { "MTAR",    false, {{0,2.6f},{0,2.9f},{0.3f,3.0f},{-0.3f,3.0f},{0,2.7f},{0,2.6f},{0,2.4f},{0,2.4f}} },

        // ── LMGs ─────────────────────────────────────────────
        { "M249",    false, {{0,3.0f},{0,3.5f},{-0.3f,3.8f},{0.3f,3.8f},{0,3.5f},{0,3.5f},{-0.5f,3.3f},{0.5f,3.3f},{0,3.0f},{0,3.0f},{0,2.8f},{0,2.8f},{-0.3f,2.8f},{0.3f,2.8f},{0,2.5f},{0,2.5f}} },
        { "RPK",     false, {{0,3.2f},{0,3.6f},{-0.3f,3.9f},{0.3f,3.9f},{0,3.6f},{0,3.5f},{-0.5f,3.3f},{0.5f,3.3f},{0,3.0f},{0,3.0f},{0,2.8f}} },
        { "L86",     false, {{0,3.0f},{0,3.4f},{-0.3f,3.6f},{0.3f,3.6f},{0,3.3f},{0,3.2f},{0,3.0f},{0,2.8f}} },

        // ── Generic last-resort ──────────────────────────────
        { "_generic",false, {{0,2.5f},{0,3.0f},{0,3.0f},{0,2.8f},{0,2.8f},{0,2.5f},{0,2.5f},{0,2.5f},{0,2.3f},{0,2.3f},{0,2.0f},{0,2.0f}} },
    };
}
