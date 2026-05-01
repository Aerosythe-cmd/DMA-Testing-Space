#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../input/kmbox.h"
#include "../input/serial_device.h"
#include "../game/offsets.h"

// ─────────────────────────────────────────────────────────────
//  AntiRecoil
//
//  Source priority for recoil patterns (highest → lowest):
//    1. Live memory read — walks WeaponEntityData chain each
//       time a new weapon is equipped, reads Frostbite's own
//       per-shot recoil angle tables directly from game memory.
//       Zero external data files needed; auto-updates with patches.
//    2. Cached in-memory pattern — weapon already read this session.
//    3. Hardcoded table — last resort if memory walk fails
//       (e.g. ShotConfigData offsets not yet filled in).
//
//  Shot detection: ammo decrement (DMA read of CurrentAmmo).
//  Compensation: mouse delta sent via KMBox or SerialDevice.
//  NO writes to game memory at any point.
// ─────────────────────────────────────────────────────────────

struct RecoilStep {
    float x;  // horizontal compensation pixels (signed)
    float y;  // vertical compensation pixels (positive = push down = fight upward kick)
};

struct RecoilPattern {
    std::string  weaponKey;
    bool         fromMemory = false;  // true if sourced from game memory
    std::vector<RecoilStep> steps;
};

class AntiRecoil {
public:
    static AntiRecoil& Get() {
        static AntiRecoil inst;
        return inst;
    }

    void SetKMBox(KMBox* km)        { m_kmbox  = km; }
    void SetSerial(SerialDevice* s) { m_serial = s;  }

    // Call once per frame.
    // localSoldier: ptr to local ClientSoldierEntity (0 if dead/not found).
    // camAngles: current camera pitch/yaw — used only if memory walk unavailable.
    void Tick(uintptr_t localSoldier, const struct AimAngles& camAngles);

    // Force-flush cached pattern for current weapon (e.g. after offset update)
    void FlushCache() { m_patternCache.clear(); }

private:
    AntiRecoil();

    // ── Memory walk ───────────────────────────────────────────
    // Walks the Frostbite weapon entity data chain from localSoldier.
    // Returns true and fills 'out' if a valid recoil table was found.
    bool TryReadPatternFromMemory(uintptr_t localSoldier,
                                  const std::string& weaponName,
                                  RecoilPattern& out);

    // Resolve the full weapon entity data pointer chain:
    // localSoldier → WeaponsComponent → active slot → SoldierWeapon
    //              → EntityDataChain → WeaponDataChain → WeaponDataFinal
    //              → WeaponEntityData → SoldierWeaponData → ShotConfigData
    uintptr_t ResolveShotConfigData(uintptr_t localSoldier);

    // Read and build a RecoilPattern from a ShotConfigData pointer.
    // Returns false if offsets are zero / read fails / sanity check fails.
    bool ReadShotConfigData(uintptr_t shotConfigPtr,
                            const std::string& weaponName,
                            RecoilPattern& out);

    // ── Hardcoded fallback table ──────────────────────────────
    void BuildFallbackTable();
    const RecoilPattern* FindFallback(const std::string& weaponName) const;

    // ── Application ──────────────────────────────────────────
    void ApplyStep(const RecoilStep& step);
    void SendMove(int dx, int dy);

    // ── State ─────────────────────────────────────────────────
    KMBox*        m_kmbox  = nullptr;
    SerialDevice* m_serial = nullptr;

    // Pattern cache: weapon name → resolved pattern (memory or fallback)
    std::unordered_map<std::string, RecoilPattern> m_patternCache;

    // Fallback patterns (hardcoded, used only if memory walk fails)
    std::vector<RecoilPattern> m_fallback;

    // Per-frame state
    int         m_lastAmmo      = -1;
    int         m_shotIndex     = 0;
    std::string m_lastWeapon    = "";
    uint64_t    m_lastShotMs    = 0;

    // Current weapon ptr (cached to avoid re-walking every frame)
    uintptr_t   m_cachedWeaponPtr = 0;
    std::string m_cachedWeaponName= "";
};
