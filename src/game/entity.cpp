#include "entity.h"
#include <algorithm>
#include <array>

// ─────────────────────────────────────────────────────────────
//  Health read via HealthComponent pointer chain:
//    soldier + Soldier::HealthComponent (0x100) → component ptr
//    component + HealthComponent::Health (0x20) → float
// ─────────────────────────────────────────────────────────────

float Entity::GetHealth() const {
    auto& dma = DMAHandler::Get();
    uintptr_t healthComp = dma.Read<uintptr_t>(m_base + Offsets::Soldier::HealthComponent);
    if (!healthComp) return 0.f;
    return dma.Read<float>(healthComp + Offsets::HealthComponent::Health);
}

// ─────────────────────────────────────────────────────────────
//  Bone read — Frostbite RenderSkeleton chain:
//    entity → AnimaComponent (0x1300)
//           → AnimaComponentTable (0x40)
//           → RenderSkeleton (0x138 within table entry)
//           → SqtsArray (0xD0) → float[boneId * 0x30]  (Vec3 + pad)
// ─────────────────────────────────────────────────────────────

Vec3 Entity::GetBonePos(int boneId) const {
    auto& dma = DMAHandler::Get();

    uintptr_t animaComp  = dma.Read<uintptr_t>(m_base + Offsets::Soldier::AnimaComponent);
    if (!animaComp) return {};

    uintptr_t animaTable = dma.Read<uintptr_t>(animaComp + Offsets::AnimaComponent::Table);
    if (!animaTable) return {};

    // Table entry 0 holds the RenderSkeleton at +0x138
    uintptr_t renderSkel = dma.Read<uintptr_t>(animaTable + Offsets::RenderSkeleton::Self);
    if (!renderSkel) return {};

    uintptr_t sqtsArray  = dma.Read<uintptr_t>(renderSkel + Offsets::RenderSkeleton::SqtsArray);
    if (!sqtsArray) return {};

    // Each bone entry: 0x30 bytes (Vec3 position + Vec3 rotation + pad)
    return dma.Read<Vec3>(sqtsArray + boneId * 0x30);
}

std::string Entity::GetName() const {
    if (!m_clientPlayer) return "Unknown";
    auto& dma = DMAHandler::Get();

    // Try pointer-based name first if offset is set (TBD for BF6)
    if (Offsets::ClientPlayer::NamePtr != 0) {
        uintptr_t namePtr = dma.Read<uintptr_t>(
            m_clientPlayer + Offsets::ClientPlayer::NamePtr);
        if (namePtr) {
            std::string s = dma.ReadString(namePtr, 32);
            if (!s.empty()) return s;
        }
    }

    // Fall back to inline char[32] at NameInline
    char buf[33] = {};
    if (dma.ReadRaw(m_clientPlayer + Offsets::ClientPlayer::NameInline,
                    buf, 32)) {
        // Sanity: first char printable ASCII, NULL within 32 bytes
        if (buf[0] >= 0x20 && buf[0] < 0x7F) {
            buf[32] = 0;
            return std::string(buf);
        }
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────
//  IsVisible — line-of-sight check against the engine's own data.
//
//  Tier order (first non-zero offset wins):
//    1. SoldierVisibility::LocalFlag   — uint8, "visible to local player"
//    2. SoldierVisibility::VisibleFlags — uint32 bitmask, bit per team
//    3. Awareness::IsVisibleByte       — uint8 from awareness component
//    4. fallback → true (assume visible; aimbot doesn't gate anyone out)
//
//  All offsets are 0x0 placeholders right now — flip them in offsets.h
//  as you reverse the components and this lights up automatically.
// ─────────────────────────────────────────────────────────────
bool Entity::IsVisible(int localTeam) const {
    auto& dma = DMAHandler::Get();

    if (Offsets::SoldierVisibility::LocalFlag != 0) {
        uintptr_t comp = dma.Read<uintptr_t>(m_base + Offsets::Soldier::SoldierVisibility);
        if (comp) {
            uint8_t v = dma.Read<uint8_t>(comp + Offsets::SoldierVisibility::LocalFlag);
            return v != 0;
        }
    }

    if (Offsets::SoldierVisibility::VisibleFlags != 0) {
        uintptr_t comp = dma.Read<uintptr_t>(m_base + Offsets::Soldier::SoldierVisibility);
        if (comp) {
            uint32_t mask = dma.Read<uint32_t>(comp + Offsets::SoldierVisibility::VisibleFlags);
            if (localTeam >= 0 && localTeam < 32)
                return (mask & (1u << localTeam)) != 0;
        }
    }

    if (Offsets::Awareness::IsVisibleByte != 0) {
        uintptr_t comp = dma.Read<uintptr_t>(m_base + Offsets::Soldier::Awareness);
        if (comp) {
            uint8_t v = dma.Read<uint8_t>(comp + Offsets::Awareness::IsVisibleByte);
            return v != 0;
        }
    }

    return true;
}

std::string Entity::GetWeaponName() const {
    auto& dma = DMAHandler::Get();

    uintptr_t weaponComp = dma.Read<uintptr_t>(m_base + Offsets::Soldier::WeaponsComponent);
    if (!weaponComp) return "";

    uintptr_t slot       = dma.Read<uintptr_t>(weaponComp + Offsets::WeaponComponent::ActiveWeaponSlot);
    if (!slot) return "";

    uintptr_t soldierWpn = dma.Read<uintptr_t>(slot + Offsets::WeaponSlot::WeaponPtr);
    if (!soldierWpn) return "";

    uintptr_t namePtr    = dma.Read<uintptr_t>(soldierWpn + Offsets::SoldierWeapon::WeaponName);
    if (!namePtr) return "";

    return dma.ReadString(namePtr, 48);
}

// ─────────────────────────────────────────────────────────────
//  EntityCache::Update
//  Reads entity list via ClientGameContext → PlayerManager → array
// ─────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────
//  EntityCache::Update — scatter-batched
//
//  Old per-DWORD-read implementation took ~400 DMA round trips per
//  frame for 64 entities × ~5 chain reads × 21 bones each.
//
//  This version uses VMMDLL_Scatter to collapse each stage into a
//  single round trip:
//
//    Stage 1 — read 64 ClientPlayer ptrs from the players array
//    Stage 2 — for each candidate, read SoldierPtr + TeamId
//    Stage 3 — for each surviving candidate, read component ptrs
//              (Health, Weapons, Anima)
//    Stage 4 — read health value + AnimaTable ptr
//    Stage 5 — read RenderSkeleton ptr from AnimaTable
//    Stage 6 — read SqtsArray ptr from RenderSkeleton
//    Stage 7 — read all 21 bone Vec3s for every entity
//
//  Total: 7 + 2 setup reads = ~9 round trips/frame regardless of
//  player count, vs ~400+ with the old loop. ~50× reduction in
//  DMA latency dominated frame time.
//
//  Name + weapon-name reads stay per-entity (chain depth + variable
//  string length make them awkward to batch; cost is marginal vs
//  bones).
// ─────────────────────────────────────────────────────────────

void EntityCache::Update(uintptr_t gameCtx, int localTeam) {
    auto& dma = DMAHandler::Get();
    m_entities.clear();

    // ── Setup: PlayerManager → playersArray (sequential, can't batch) ──
    uintptr_t playerMgr = dma.Read<uintptr_t>(gameCtx + Offsets::PlayerManager);
    if (!playerMgr) return;
    uintptr_t playersArray = dma.Read<uintptr_t>(playerMgr + Offsets::ClientPlayersArray);
    if (!playersArray) return;

    // ── Stage 1: 64 ClientPlayer ptrs in one round trip ────────
    constexpr int MAX_PLAYERS = 64;
    std::array<uintptr_t, MAX_PLAYERS> playerPtrs{};
    {
        auto sc = dma.BeginScatter();
        for (int i = 0; i < MAX_PLAYERS; i++)
            sc.Read(playersArray + i * sizeof(uintptr_t), &playerPtrs[i]);
        sc.Execute();
    }

    // Per-candidate working state. cands.reserve(64) keeps addresses
    // stable across push_backs — required since we hand &cand.* into
    // scatter operations whose buffers must outlive Execute().
    struct Cand {
        uintptr_t player;
        uintptr_t soldier      = 0;
        int       team         = 0;
        uintptr_t healthComp   = 0;
        uintptr_t weaponsComp  = 0;
        uintptr_t animaComp    = 0;
        uintptr_t animaTable   = 0;
        uintptr_t renderSkel   = 0;
        uintptr_t sqtsArray    = 0;
        float     health       = 0.f;
        std::array<Vec3, 21> bones{};
    };
    std::vector<Cand> cands;
    cands.reserve(MAX_PLAYERS);
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (playerPtrs[i]) cands.push_back({ playerPtrs[i] });

    // ── Stage 2: SoldierPtr + TeamId per candidate ─────────────
    {
        auto sc = dma.BeginScatter();
        for (auto& c : cands) {
            sc.Read(c.player + Offsets::ClientPlayer::SoldierEntity, &c.soldier);
            sc.Read(c.player + Offsets::ClientPlayer::TeamId,        &c.team);
        }
        sc.Execute();
    }
    // Filter: must have a soldier and not be on local team
    cands.erase(std::remove_if(cands.begin(), cands.end(),
        [&](const Cand& c) { return !c.soldier || c.team == localTeam; }),
        cands.end());
    if (cands.empty()) { m_prev.clear(); return; }

    // ── Stage 3: component pointers (Health, Weapons, Anima) ───
    {
        auto sc = dma.BeginScatter();
        for (auto& c : cands) {
            sc.Read(c.soldier + Offsets::Soldier::HealthComponent,  &c.healthComp);
            sc.Read(c.soldier + Offsets::Soldier::WeaponsComponent, &c.weaponsComp);
            sc.Read(c.soldier + Offsets::Soldier::AnimaComponent,   &c.animaComp);
        }
        sc.Execute();
    }

    // ── Stage 4: health value + AnimaTable ptr ─────────────────
    {
        auto sc = dma.BeginScatter();
        for (auto& c : cands) {
            if (c.healthComp)
                sc.Read(c.healthComp + Offsets::HealthComponent::Health, &c.health);
            if (c.animaComp)
                sc.Read(c.animaComp + Offsets::AnimaComponent::Table,    &c.animaTable);
        }
        sc.Execute();
    }
    // Filter dead
    cands.erase(std::remove_if(cands.begin(), cands.end(),
        [](const Cand& c) { return c.health <= 0.f; }), cands.end());
    if (cands.empty()) { m_prev.clear(); return; }

    // ── Stage 5: AnimaTable → RenderSkeleton ─────────────────
    {
        auto sc = dma.BeginScatter();
        for (auto& c : cands)
            if (c.animaTable)
                sc.Read(c.animaTable + Offsets::RenderSkeleton::Self, &c.renderSkel);
        sc.Execute();
    }

    // ── Stage 6: RenderSkeleton → SqtsArray ──────────────────
    {
        auto sc = dma.BeginScatter();
        for (auto& c : cands)
            if (c.renderSkel)
                sc.Read(c.renderSkel + Offsets::RenderSkeleton::SqtsArray, &c.sqtsArray);
        sc.Execute();
    }

    // ── Stage 7: ALL bones for ALL entities in one trip ───────
    // 21 bones × N entities, each 0x30 bytes from sqtsArray base.
    {
        auto sc = dma.BeginScatter();
        for (auto& c : cands) {
            if (!c.sqtsArray) continue;
            for (int b = 0; b < 21; b++)
                sc.Read(c.sqtsArray + b * 0x30, &c.bones[b]);
        }
        sc.Execute();
    }

    // ── Build Entity objects + velocity tracking ─────────────
    auto now = std::chrono::steady_clock::now();
    std::unordered_map<uintptr_t, PrevState> nextPrev;
    nextPrev.reserve(cands.size());

    for (auto& c : cands) {
        Entity e(c.soldier, c.player);
        e.cachedTeam     = c.team;
        e.cachedHealth   = c.health;
        e.cachedAlive    = true;
        e.cachedBones    = c.bones;
        e.cachedHeadPos  = e.cachedBones[Offsets::Bones::Head];
        e.cachedChestPos = e.cachedBones[Offsets::Bones::Chest];

        // Name + weapon: per-entity chain reads (not batched)
        e.cachedName   = e.GetName();
        e.cachedWeapon = e.GetWeaponName();

        // Visibility: while the visibility offsets are 0x0 placeholders this
        // is effectively free (early-return → true). Once offsets are RE'd,
        // this becomes 1–2 reads per entity; lift into a scatter stage then.
        e.cachedVisible = e.IsVisible(localTeam);

        // Velocity (frame-to-frame head delta, keyed by ClientPlayer ptr)
        auto it = m_prev.find(c.player);
        if (it != m_prev.end()) {
            float dt = std::chrono::duration<float>(now - it->second.ts).count();
            if (dt > 0.001f && dt < 0.5f) {
                e.cachedVelocity.x = (e.cachedHeadPos.x - it->second.pos.x) / dt;
                e.cachedVelocity.y = (e.cachedHeadPos.y - it->second.pos.y) / dt;
                e.cachedVelocity.z = (e.cachedHeadPos.z - it->second.pos.z) / dt;
            }
        }
        nextPrev[c.player] = { e.cachedHeadPos, now };

        m_entities.push_back(std::move(e));
    }

    m_prev = std::move(nextPrev);
}
