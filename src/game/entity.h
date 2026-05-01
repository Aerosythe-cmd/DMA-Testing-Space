#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <unordered_map>
#include <chrono>
#include "../dma/dma_handler.h"
#include "offsets.h"

struct Vec2     { float x, y; };
struct Vec3     { float x, y, z; };
struct Matrix4x4{ float m[4][4]; };

struct AimAngles { float pitch, yaw; };

class Entity {
public:
    Entity(uintptr_t soldierBase, uintptr_t clientPlayerBase = 0)
        : m_base(soldierBase), m_clientPlayer(clientPlayerBase) {}

    bool IsValid()    const { return m_base != 0; }

    // Health: read via HealthComponent pointer chain (soldier + offset -> component -> +0x20)
    float GetHealth() const;

    // Team: must be read from ClientPlayer, NOT from soldier.
    // Use the cached value set by EntityCache::Update instead.
    // int GetTeamId() -- removed: team lives on ClientPlayer, not soldier

    Vec3  GetBonePos(int boneId) const;
    std::string GetName()       const;       // Reads from ClientPlayer (NOT soldier)
    std::string GetWeaponName() const;
    uintptr_t   GetBase()       const { return m_base; }
    uintptr_t   GetClientPlayer() const { return m_clientPlayer; }

    // Visibility: reads the SoldierVisibility component, falling through
    // to Awareness, defaulting to true if neither is RE'd. localTeam = the
    // team-id of the local player (used for the bitmask path).
    bool IsVisible(int localTeam) const;

    // Frame cache
    Vec3        cachedHeadPos{};
    Vec3        cachedChestPos{};
    Vec3        cachedVelocity{};   // m/s — derived from frame-to-frame head delta
    std::string cachedName;
    std::string cachedWeapon;
    float       cachedHealth = 0.f;
    int         cachedTeam   = -1;
    bool        cachedAlive  = false;
    bool        cachedVisible = true;       // assume visible if offsets not RE'd
    std::array<Vec3, 21> cachedBones{};

private:
    uintptr_t m_base;
    uintptr_t m_clientPlayer;
    template<typename T>
    T Read(uint32_t offset) const {
        return DMAHandler::Get().Read<T>(m_base + offset);
    }
};

class EntityCache {
public:
    static EntityCache& Get() {
        static EntityCache inst;
        return inst;
    }
    // Takes gameCtx directly — uses real offset chain
    void Update(uintptr_t gameCtx, int localTeam);
    const std::vector<Entity>& GetEntities() const { return m_entities; }

private:
    std::vector<Entity> m_entities;

    // Per-player prev position state for velocity derivation.
    // Keyed by ClientPlayer ptr (stable across frames for the same player slot).
    struct PrevState {
        Vec3                                  pos;
        std::chrono::steady_clock::time_point ts;
    };
    std::unordered_map<uintptr_t, PrevState> m_prev;
};
