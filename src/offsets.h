#pragma once
/*
 * offsets.h — All hardcoded game offsets (updated per patch)
 *
 * Source: offests.txt (latest patch)
 * Naming: matches UE5 class hierarchy where possible
 */

#include <cstdint>

namespace Offsets {

    // ─── Engine Native Functions (code offsets from module base) ──────
    constexpr uintptr_t ProcessEvent       = 0x1B437A0;
    constexpr uintptr_t StaticFindObject   = 0x1B6D070;
    constexpr uintptr_t StaticLoadObject   = 0x1B70BA0;
    constexpr uintptr_t BoneMatrixFunc     = 0x40C1190;   // GetBoneMatrix(mesh*, outMatrix*, boneIdx)
    constexpr uintptr_t SetOutlineMode     = 0x4055840;
    constexpr uintptr_t FMemoryMalloc      = 0x1724140;
    constexpr uintptr_t MarkDirtyRenderState = 0x159F240;

    // ─── Spoof call gadget region ────────────────────────────────────
    constexpr uintptr_t MagicOffsets       = 0x52A3450;   // ret.asm based

    // ─── GWorld / Root Pointers (data offsets from module base) ──────
    //     GWorld is DOUBLE DEREFERENCED: *(*(base + GWorld)) = UWorld*
    constexpr uintptr_t GWorld             = 0xC19A0C0;
    constexpr uintptr_t FNamePool          = 0xC334000;
    constexpr uintptr_t FNameState         = 0xC51E180;   // XOR decryption state (7 x uint64)
    constexpr uintptr_t FNameKey           = FNameState + 0x38;

    // ─── UWorld Chain ────────────────────────────────────────────────
    constexpr uintptr_t UWorld_PersistentLevel    = 0x0038;
    constexpr uintptr_t UWorld_OwningGameInstance = 0x01D8;

    // ─── UGameInstance → LocalPlayers ────────────────────────────────
    //     TArray<ULocalPlayer*> at +0x40. Layout: Data*(+0), Count(+8), Max(+0xC)
    constexpr uintptr_t GameInstance_LocalPlayers  = 0x0040;

    // ─── ULocalPlayer → PlayerController ─────────────────────────────
    constexpr uintptr_t LocalPlayer_PlayerController = 0x0038;

    // ─── APlayerController ───────────────────────────────────────────
    constexpr uintptr_t PC_LocalPlayerPawn    = 0x0460;
    constexpr uintptr_t PC_AcknowledgedPawn   = 0x0510;
    constexpr uintptr_t PC_CameraManager      = 0x0540;   // try 0x520 first, fallback 0x540
    constexpr uintptr_t PC_CameraManager_Alt  = 0x0520;
    constexpr uintptr_t PC_MyHUD              = 0x0518;

    // ─── Camera (APlayerCameraManager) ───────────────────────────────
    //     From offsets.txt: camerapos=900, camerarot=918, camerafov=930 (HEX without 0x prefix)
    //     0x918-0x900 = 0x18 = 24 bytes = 3 doubles (FVector3d) ✓
    //     0x930-0x918 = 0x18 = 24 bytes = 3 doubles (FRotator3d) ✓
    constexpr uintptr_t Camera_ViewInfo      = 0x900;
    constexpr uintptr_t Camera_Location      = 0x900;      // FVector3d (3 doubles, 24 bytes)
    constexpr uintptr_t Camera_Rotation      = 0x918;      // FRotator3d (3 doubles, 24 bytes)
    constexpr uintptr_t Camera_FOV           = 0x930;      // float

    // ─── APawn / AActor ──────────────────────────────────────────────
    constexpr uintptr_t Actor_RootComponent   = 0x0288;
    constexpr uintptr_t RootComp_Position     = 0x0170;    // FVector3d inside RootComponent
    constexpr uintptr_t Actor_PlayerState     = 0x0480;
    constexpr uintptr_t Actor_FNameID         = 0x0018;    // FName index for class name

    // ─── AShooterCharacter (Valorant-specific) ───────────────────────
    constexpr uintptr_t Char_DamageHandler    = 0x0C68;
    constexpr uintptr_t DmgHandler_Health     = 0x0200;
    constexpr uintptr_t Char_MeshComponent    = 0x04E8;    // USkeletalMeshComponent*
    constexpr uintptr_t Char_MeshCosmetic3P   = 0x0F40;
    constexpr uintptr_t Char_Mesh1P           = 0x0F30;
    constexpr uintptr_t Char_Mesh1POverlay    = 0x0F38;
    constexpr uintptr_t Char_Mesh3PMIDs       = 0x0F80;

    // ─── Team ────────────────────────────────────────────────────────
    constexpr uintptr_t TeamComponent         = 0x0690;
    constexpr uintptr_t TeamID                = 0x00F8;    // inside TeamComponent

    // ─── PersistentLevel → Actors ────────────────────────────────────
    constexpr uintptr_t Level_ActorArray      = 0x00A0;    // TArray<AActor*>
    constexpr uintptr_t Level_ActorCount      = 0x00A8;

    // ─── Bone/Mesh ───────────────────────────────────────────────────
    constexpr uintptr_t Mesh_BoneCount        = 0x0740;
    constexpr uintptr_t Mesh_BoneArray        = 0x0738;

    // ─── Other ───────────────────────────────────────────────────────
    constexpr uintptr_t ViewportWorld          = 0x78;
    constexpr uintptr_t UWorldPointer          = 0x80;
    constexpr uintptr_t DrawTransition         = 0x68;

    // ─── Nospread ────────────────────────────────────────────────────
    constexpr uintptr_t GetSpreadValues        = 0x62EE2C0;
    constexpr uintptr_t GetSpreadAngles        = 0x6F28690;
    constexpr uintptr_t GetFiringLocDir        = 0x3BF2000;
    constexpr uintptr_t ToVecNormalize         = 0x18238B0;
    constexpr uintptr_t ToAngleNormalize       = 0x181E0B0;
    constexpr uintptr_t FiringStateComponent   = 0x1208;
    constexpr uintptr_t StabilityComponent     = 0x490;
}
