#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────
//  BF6 Offsets — sourced from UC thread + community RE + SDK dump
//  Game: Battlefield 6 (Frostbite engine)
//  Last updated: 2026-04-27 (latest Steam build)
//
//  Static addresses are RVAs from bf6.exe module base.
//  At runtime: absolute VA = GetModuleBase("bf6.exe") + RVA
//  Default ASLR base: 0x140000000
//
//  Function RVAs (for reference, not used in code):
//    RetrievePlayers:            0x13D2080
//    SetAuthorativeAiming:       0x542D20
//    GetServerConnectionLatency: 0x357B5E0
// ─────────────────────────────────────────────────────────────

namespace Offsets {

    // ── Static addresses (absolute VA, assuming base 0x140000000) ─
    constexpr uintptr_t ClientGameContext   = 0x149D14478;  // RVA 0x9D14478
    constexpr uintptr_t GameRenderer        = 0x149DA8DF0;  // RVA 0x9DA8DF0

    // ── Alternative static pointers (absolute VA) ─────────────
    constexpr uintptr_t LocalPlayerStatic   = 0x177DB0D0;   // RVA 0x37DB0D0 — direct local player ptr
    constexpr uintptr_t AimingSimulation    = 0x149BB1748;  // RVA 0x9BB1748 — ClientSoldierAimingSimulation

    // ── TypeInfo addresses (for type verification) ────────────
    constexpr uintptr_t ClientSoldierEntity_TypeInfo = 0x149E5D580;  // RVA 0x9E5D580
    constexpr uintptr_t GlobalRegistryTypeInfo       = 0x14959FD08;  // RVA 0x959FD08
    constexpr uintptr_t UnknownIndex                 = 0x1496EDD38;  // RVA 0x96EDD38

    // ── ClientGameContext chain ───────────────────────────────
    constexpr uint32_t LocalPlayer          = 0x570;   // → ClientSoldierEntity ptr (from GameCtx)
    constexpr uint32_t PlayerManager        = 0x90;    // → ClientPlayerManager ptr
    constexpr uint32_t ClientPlayersArray   = 0x7A0;   // → ptr array of ClientPlayer

    // ── ClientPlayer → SoldierEntity ─────────────────────────
    namespace ClientPlayer {
        constexpr uint32_t SoldierEntity    = 0x14D0;  // ClientPlayer → ClientSoldierEntity ptr
        constexpr uint32_t TeamId           = 0x13CC;  // ClientPlayer → team ID (int)
        // Player name: typically a UTF-8/ASCII char buffer (32 bytes) embedded
        // in ClientPlayer for older Frostbite, or a pointer to a wide string.
        // Confirmed offset for BF6 TBD — common candidates: 0x40, 0x48, 0x60.
        // We try inline-string first; if invalid, fall through.
        constexpr uint32_t NameInline       = 0x40;    // char[32] inline (try first)
        constexpr uint32_t NamePtr          = 0x0;     // ptr to wide string (TBD; 0 = disabled)
    }

    // ── ClientSoldierEntity components (from SDK dump 2026-04-27) ─
    namespace Soldier {
        // Physics
        constexpr uint32_t PhysicsComponent     = 0xB8;    // FBPhysicsComponent
        constexpr uint32_t HealthComponent      = 0x100;   // BFClientSoldierHealthComponent
        constexpr uint32_t CameraComponent      = 0x1360;  // ClientSoldierCameraComponent
        constexpr uint32_t CapsulePhysics       = 0x1518;  // ClientBFCapsulePhysicsComponent
        constexpr uint32_t OcclusionPadded      = 0x1550;  // SoldierPaddedOcclusionComponent
        constexpr uint32_t OcclusionPhysics     = 0x1558;  // SoldierPhysicsOcclusionComponent

        // Skeleton / animation
        constexpr uint32_t AnimaComponent       = 0x1300;  // SoldierAnimaComponent (unconfirmed in latest dump)
        constexpr uint32_t WeaponsComponent     = 0x1700;  // ClientSoldierWeaponsComponent (was 0x1510)
        constexpr uint32_t BreathControl        = 0x1708;  // ClientSoldierBreathControlComponent
        constexpr uint32_t AttachComponent      = 0x1710;  // ClientSoldierAttachComponent
        constexpr uint32_t AmmoManager          = 0x1718;  // ClientSoldierAmmoManagerComponent
        constexpr uint32_t PlayerEntry          = 0x1728;  // ClientSoldierPlayerEntryComponent
        constexpr uint32_t EntryUpdateRoot      = 0x18C0;  // ClientEntryUpdateRootComponent
        constexpr uint32_t MeshDefinition       = 0x18E0;  // ClientMeshDefinitionComponent
        constexpr uint32_t MotionMachine        = 0x1920;  // ClientMotionMachineComponent
        constexpr uint32_t EntityInteraction     = 0x1940;  // ClientEntityInteractionComponent
        constexpr uint32_t AbilitySet           = 0x19A0;  // BFClientPlayerAbilitySetComponent
        constexpr uint32_t BrainComponent       = 0x19E0;  // ClientBFBrainComponent
        constexpr uint32_t GameplayExpression   = 0x1A20;  // ClientDiceGameplayExpressionFeatureComponent
        constexpr uint32_t DrivenRagdoll        = 0x1A40;  // ClientBFDrivenRagdollComponent
        constexpr uint32_t Suppression          = 0x1A80;  // ClientSoldierSuppressionComponent
        constexpr uint32_t HitReaction          = 0x1AA0;  // ClientHitReactionComponent
        constexpr uint32_t FootplantEffect      = 0x1AC0;  // ClientSoldierFootplantEffectComponent
        constexpr uint32_t Affector             = 0x1AE0;  // ClientAffectorComponent
        constexpr uint32_t BoneCollision        = 0x1B00;  // ClientBoneCollisionComponent
        constexpr uint32_t VectorField          = 0x1B20;  // ClientDiceVectorFieldComponent
        constexpr uint32_t DropAbilities        = 0x1B40;  // ClientDropAbilitiesComponent
        constexpr uint32_t DropLoot             = 0x1B60;  // ClientDropLootComponent
        constexpr uint32_t GameplayCollision    = 0x1BE0;  // GameplayCollisionComponent
        constexpr uint32_t InteractPoint        = 0x1C00;  // ClientInteractPointComponent
        constexpr uint32_t Awareness            = 0x1C20;  // ClientAwarenessComponent
        constexpr uint32_t SoldierVisibility    = 0x1C40;  // ObjectSoldierVisibilityComponent
        constexpr uint32_t RenderLayers         = 0x1C60;  // RenderLayersComponent
        constexpr uint32_t IKSource             = 0x1C80;  // ClientIKSourceComponent
        constexpr uint32_t IKComponent          = 0x1CA0;  // ClientIKComponent
        constexpr uint32_t StandaloneMesh       = 0x1CC0;  // ClientStandaloneMeshDefinitionComponent
        constexpr uint32_t VoiceOverAnimation   = 0x1D00;  // ClientVoiceOverAnimationComponent
        constexpr uint32_t SquadSpawn           = 0x1D20;  // ClientSquadSpawnComponent
        constexpr uint32_t Emote                = 0x1D40;  // ClientSoldierEmoteComponent
        constexpr uint32_t Decal                = 0x1D60;  // ClientSoldierDecalComponent
        constexpr uint32_t PlayerLogo           = 0x1D80;  // ClientPlayerLogoComponent
        constexpr uint32_t MeshWind             = 0x1DC0;  // ClientBFMeshWindComponent
        constexpr uint32_t EntityId             = 0x1DE0;  // ClientDiceEntityIdComponent
        constexpr uint32_t SpottingTarget       = 0x1E00;  // ClientSpottingTargetComponent
        constexpr uint32_t FireProjectile       = 0x1E20;  // ClientFireProjectileComponent
        constexpr uint32_t ShaderParamProvider  = 0x1E40;  // ClientShaderParameterProviderComponent
        constexpr uint32_t Significance         = 0x1E60;  // ClientBFSignificanceComponent
        constexpr uint32_t SpectCameraAsset     = 0x1E80;  // ClientSpectatableControllableCameraAssetComponent
        constexpr uint32_t AimAssistNode        = 0x1EA0;  // ClientCharacterAimAssistNodeComponent
        constexpr uint32_t LiveReverbMetrics    = 0x1EC0;  // ClientLiveReverbMetricsComponent
        constexpr uint32_t Customization        = 0x1EE0;  // ClientBFCustomizationComponent
        constexpr uint32_t LockingTarget        = 0x1F00;  // ClientBFLockingTargetComponent
        constexpr uint32_t InterceptNotify      = 0x1F20;  // ClientInterceptNotificationComponent

        // Direct member offsets within soldier (remaining TBD placeholders)
        // Health: use HealthComponent chain instead (soldier + 0x100 -> comp -> +0x20)
        // TeamId: use ClientPlayer::TeamId instead (NOT on soldier)
        constexpr uint32_t Transform        = 0x0;     // 4x4 world matrix (TBD)
        constexpr uint32_t NamePtr          = 0x0;     // string ptr (TBD)
    }

    // ── HealthComponent sub-offsets ──────────────────────────
    // BFClientSoldierHealthComponent internal layout
    namespace HealthComponent {
        constexpr uint32_t Health      = 0x20;  // float — current HP
        constexpr uint32_t MaxHealth   = 0x24;  // float — max HP
    }

    // ── AnimaComponent → skeleton bones ───────────────────────
    namespace AnimaComponent {
        constexpr uint32_t Table            = 0x40;    // AnimaComponentTable
    }

    // ── RenderSkeleton (from AnimaComponentTable entry) ───────
    namespace RenderSkeleton {
        constexpr uint32_t Self             = 0x138;   // RenderSkeleton offset in table entry
        constexpr uint32_t SqtsArray        = 0xD0;    // bone transform array ptr
        constexpr uint32_t HierarchyArray   = 0x48;    // bone parent index array
        constexpr uint32_t TrajectoryBoneIdx= 0x148;   // int, offset to head bone index
    }

    // ── Weapon component chain ────────────────────────────────
    // WeaponsComponent + 0x3990 → active weapon slot ptr
    // then: +0x038 → +0x0F08 → +0x150 → +0x0020 → WeaponEntityData
    namespace WeaponComponent {
        constexpr uint32_t ActiveWeaponSlot = 0x3990;  // ptr to active weapon
    }

    namespace WeaponSlot {
        constexpr uint32_t WeaponPtr        = 0x038;   // ptr → SoldierWeapon
    }

    namespace SoldierWeapon {
        constexpr uint32_t EntityDataChain  = 0x0F08;  // ptr → weapon data chain
        constexpr uint32_t WeaponName       = 0x0030;  // ptr → weapon display name string
        constexpr uint32_t CurrentAmmo      = 0x0;     // int (TBD exact)
        constexpr uint32_t ReserveAmmo      = 0x4;
    }

    namespace WeaponDataChain {
        constexpr uint32_t Next             = 0x150;   // ptr chain step
    }

    namespace WeaponDataFinal {
        constexpr uint32_t EntityData       = 0x0020;  // ptr → WeaponEntityData
    }

    // ── WeaponEntityData (Frostbite asset, read-only in engine) ─
    // ShotConfigData holds the actual per-shot recoil table
    namespace WeaponEntityData {
        constexpr uint32_t SoldierWeaponData = 0x0;   // ptr → SoldierWeaponData (TBD)
    }

    namespace SoldierWeaponData {
        constexpr uint32_t ShotConfigData   = 0x0;    // ptr → ShotConfigData[0] (TBD)
    }

    namespace ShotConfigData {
        // Per-shot recoil angle tables (float arrays, parallel)
        constexpr uint32_t RecoilTablePtr   = 0x0;    // ptr → float[] pitch per shot
        constexpr uint32_t RecoilYawTablePtr= 0x0;    // ptr → float[] yaw per shot
        constexpr uint32_t RecoilTableSize  = 0x0;    // int, entry count

        // Scalar fallback (used when table size == 0)
        constexpr uint32_t BaseKickPitch    = 0x0;    // float degrees
        constexpr uint32_t BaseKickYaw      = 0x0;    // float degrees
        constexpr uint32_t KickVariancePitch= 0x0;    // float ±
        constexpr uint32_t KickVarianceYaw  = 0x0;    // float ±
        constexpr uint32_t FireRateRPM      = 0x0;    // float (sanity check)
    }

    // ── ObjectSoldierVisibilityComponent layout ───────────────
    // Component pointer at Soldier::SoldierVisibility (0x1C40).
    // Internal field offsets are TBD — Frostbite typically stores a per-team
    // visibility mask or "visible to local player" boolean. Until RE'd,
    // Entity::IsVisible() defers to the Awareness fallback below, and if
    // both are unset the visibility check returns true (assume visible)
    // so the aimbot doesn't accidentally suppress every target.
    namespace SoldierVisibility {
        constexpr uint32_t VisibleFlags  = 0x0;   // TBD — uint32 bitmask, bit per team
        constexpr uint32_t LocalFlag     = 0x0;   // TBD — uint8 bool, "visible to local"
        constexpr uint32_t LastSeenTime  = 0x0;   // TBD — float seconds since visible
    }

    // ── ClientAwarenessComponent (alternative LOS source) ─────
    // Component pointer at Soldier::Awareness (0x1C20). Holds per-player
    // awareness state — last-seen, threat level, etc. We use it only as a
    // fallback when SoldierVisibility::* offsets aren't filled in.
    namespace Awareness {
        constexpr uint32_t IsVisibleByte = 0x0;   // TBD — byte 0/1
    }

    // ── Bullet speed chain (community-confirmed) ──────────────
    // LocalSoldier + WeaponsComponent(0x1700)
    //   + 0x3990 → +0x038 → +0x0F08 → +0x150 → +0x0020 → +0x2D8
    namespace BulletData {
        constexpr uint32_t Speed            = 0x2D8;  // float m/s
    }

    // ── Camera / view ─────────────────────────────────────────
    // CameraComponent is at Soldier + 0x1360; sub-offsets within it TBD
    namespace Camera {
        constexpr uint32_t ViewMatrix       = 0x10;   // float[16] — 4x4 view/world matrix
        constexpr uint32_t Pos              = 0x0;    // Vec3 (TBD — within CameraComponent)
        constexpr uint32_t Pitch            = 0x0;    // float degrees (TBD)
        constexpr uint32_t Yaw              = 0x0;    // float degrees (TBD)
    }

    // ── Bone indices (Frostbite standard, verify with TrajectoryBoneIdx) ─
    namespace Bones {
        constexpr int Head      = 7;
        constexpr int Neck      = 6;
        constexpr int Chest     = 5;
        constexpr int Pelvis    = 4;
        constexpr int LShoulder = 11;
        constexpr int LElbow    = 12;
        constexpr int LWrist    = 13;
        constexpr int RShoulder = 8;
        constexpr int RElbow    = 9;
        constexpr int RWrist    = 10;
        constexpr int LHip      = 18;
        constexpr int LKnee     = 19;
        constexpr int LAnkle    = 20;
        constexpr int RHip      = 15;
        constexpr int RKnee     = 16;
        constexpr int RAnkle    = 17;
    }
}

// Skeleton draw pairs
static const int SKELETON_PAIRS[][2] = {
    {Offsets::Bones::Head,      Offsets::Bones::Neck},
    {Offsets::Bones::Neck,      Offsets::Bones::Chest},
    {Offsets::Bones::Chest,     Offsets::Bones::Pelvis},
    {Offsets::Bones::Chest,     Offsets::Bones::LShoulder},
    {Offsets::Bones::LShoulder, Offsets::Bones::LElbow},
    {Offsets::Bones::LElbow,    Offsets::Bones::LWrist},
    {Offsets::Bones::Chest,     Offsets::Bones::RShoulder},
    {Offsets::Bones::RShoulder, Offsets::Bones::RElbow},
    {Offsets::Bones::RElbow,    Offsets::Bones::RWrist},
    {Offsets::Bones::Pelvis,    Offsets::Bones::LHip},
    {Offsets::Bones::LHip,      Offsets::Bones::LKnee},
    {Offsets::Bones::LKnee,     Offsets::Bones::LAnkle},
    {Offsets::Bones::Pelvis,    Offsets::Bones::RHip},
    {Offsets::Bones::RHip,      Offsets::Bones::RKnee},
    {Offsets::Bones::RKnee,     Offsets::Bones::RAnkle},
};
static const int SKELETON_PAIR_COUNT = 15;
