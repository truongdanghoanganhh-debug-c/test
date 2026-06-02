#pragma once
/*
 * game.h — Memory Reading & Game Logic
 *
 * Reads UE5 game structures from memory using pointer chains.
 * All world-space reads are double precision (UE5 LWC).
 * Bone positions are obtained via direct engine function call
 * since Valorant encrypts ComponentToWorld.
 *
 * NOTE: MSVC cannot use __try in functions that have C++ objects
 * requiring unwinding (std::vector, std::string). SEH is used
 * only in leaf functions that operate on POD types.
 */

#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <intrin.h>

#include "math.h"
#include "offsets.h"

namespace Game {

    // ─── Globals ──────────────────────────────────────────────────────
    inline uintptr_t  Base              = 0;
    inline uintptr_t  ModuleEnd         = 0;

    inline uintptr_t  GWorld            = 0;
    inline uintptr_t  PersistentLevel   = 0;
    inline uintptr_t  GameInstance      = 0;
    inline uintptr_t  LocalPlayer       = 0;
    inline uintptr_t  PlayerController  = 0;
    inline uintptr_t  CameraManager     = 0;
    inline uintptr_t  AcknowledgedPawn  = 0;

    inline FVector     CameraPos        = {};
    inline FRotator    CameraRot        = {};
    inline float       CameraFOV        = 103.0f;

    inline float       ScreenW          = 1920.0f;
    inline float       ScreenH          = 1080.0f;

    inline uintptr_t   LevelsOffset     = 0;
    inline uintptr_t   ControlRotOffset = 0;

    // ─── Debug Logging ────────────────────────────────────────────────
    inline DWORD       DbgLastTick      = 0;
    inline bool ShouldLog() {
        DWORD now = GetTickCount();
        if (now - DbgLastTick >= 2000) { // log every 2 seconds
            DbgLastTick = now;
            return true;
        }
        return false;
    }

    // ─── Safe Memory Read ─────────────────────────────────────────────
    template<typename T>
    inline T Read(uintptr_t addr) {
        if (addr == 0) return T{};
        __try {
            return *(volatile T*)addr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return T{};
        }
    }

    template<typename T>
    inline bool Write(uintptr_t addr, const T& val) {
        if (addr == 0) return false;
        __try {
            *(volatile T*)addr = val;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ─── Pointer Validation ──────────────────────────────────────────
    inline bool IsValidPtr(uintptr_t ptr) {
        return ptr > 0x10000 && ptr < 0x7FFFFFFFFFFF;
    }

    inline bool IsHeapPtr(uintptr_t ptr) {
        return ptr > 0x100000000ULL && ptr < 0x7FFFFFFFFFFF &&
               !(ptr >= Base && ptr < ModuleEnd);
    }

    inline uintptr_t CleanPtr(uintptr_t ptr) {
        return ptr & ~0x7ULL;
    }

    // ─── GetBoneMatrix function type ──────────────────────────────────
    using GetBoneMatrix_fn = FMatrix*(__fastcall*)(void* mesh, FMatrix* outResult, int boneIndex);

    // ─── Get bone world position via engine function call ─────────────
    inline FVector GetBonePosition(uintptr_t meshComp, int boneIndex) {
        if (!IsValidPtr(meshComp) || boneIndex < 0) return {};

        auto fn = reinterpret_cast<GetBoneMatrix_fn>(Base + Offsets::BoneMatrixFunc);
        FMatrix outMatrix = {};

        __try {
            fn((void*)meshComp, &outMatrix, boneIndex);
            FVector pos;
            pos.X = outMatrix.M[3][0];
            pos.Y = outMatrix.M[3][1];
            pos.Z = outMatrix.M[3][2];
            if (pos.IsValid() && !pos.IsZero())
                return pos;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return {};
    }

    // ─── Bone count from mesh component ───────────────────────────────
    inline int GetBoneCount(uintptr_t meshComp) {
        if (!IsValidPtr(meshComp)) return 0;
        return Read<int32_t>(meshComp + Offsets::Mesh_BoneCount);
    }

    // ─── Initialize ──────────────────────────────────────────────────
    inline bool Init() {
        HMODULE hMod = GetModuleHandleW(L"VALORANT-Win64-Shipping.exe");
        if (!hMod) return false;

        Base = (uintptr_t)hMod;

        MODULEINFO modInfo = {};
        if (GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(modInfo))) {
            ModuleEnd = Base + modInfo.SizeOfImage;
        } else {
            ModuleEnd = Base + 0x10000000;
        }

        return true;
    }

    // ─── UWorld::Levels Auto-Discovery ────────────────────────────────
    inline void DiscoverLevelsOffset() {
        if (LevelsOffset != 0 || !IsValidPtr(GWorld)) return;

        for (uintptr_t off = 0x100; off <= 0x300; off += 8) {
            uintptr_t dataPtr = Read<uintptr_t>(GWorld + off);
            int32_t   count   = Read<int32_t>(GWorld + off + 8);
            int32_t   max     = Read<int32_t>(GWorld + off + 0xC);

            if (!IsValidPtr(dataPtr)) continue;
            if (count < 1 || count > 200) continue;
            if (max < count || max > 500) continue;

            bool found = false;
            for (int i = 0; i < count && i < 50; i++) {
                uintptr_t lvl = Read<uintptr_t>(dataPtr + i * 8);
                if (lvl == PersistentLevel) {
                    found = true;
                    break;
                }
            }
            if (found) {
                LevelsOffset = off;
                return;
            }
        }
    }

    // ─── Helpers that read raw arrays (no SEH + C++ object conflict) ──

    // Read up to maxOut level pointers into outBuf. Returns count written.
    inline int ReadSubLevels(uintptr_t* outBuf, int maxOut) {
        int written = 0;

        if (IsValidPtr(PersistentLevel) && written < maxOut)
            outBuf[written++] = PersistentLevel;

        if (LevelsOffset == 0 || !IsValidPtr(GWorld))
            return written;

        uintptr_t dataPtr = Read<uintptr_t>(GWorld + LevelsOffset);
        int32_t   count   = Read<int32_t>(GWorld + LevelsOffset + 8);

        if (!IsValidPtr(dataPtr) || count < 1 || count > 200)
            return written;

        for (int i = 0; i < count && written < maxOut; i++) {
            uintptr_t lvl = Read<uintptr_t>(dataPtr + i * 8);
            if (IsValidPtr(lvl) && lvl != PersistentLevel)
                outBuf[written++] = lvl;
        }
        return written;
    }

    // Read up to maxOut actors from a level into outBuf. Returns count written.
    inline int ReadActorsFromLevel(uintptr_t level, uintptr_t* outBuf, int maxOut) {
        if (!IsValidPtr(level)) return 0;

        uintptr_t arrayPtr = Read<uintptr_t>(level + Offsets::Level_ActorArray);
        int32_t   count    = Read<int32_t>(level + Offsets::Level_ActorCount);

        if (!IsValidPtr(arrayPtr) || count < 1 || count > 100000)
            return 0;

        int written = 0;
        for (int i = 0; i < count && written < maxOut; i++) {
            uintptr_t actor = Read<uintptr_t>(arrayPtr + i * 8);
            actor = CleanPtr(actor);
            if (IsHeapPtr(actor) && (actor & 0x7) == 0)
                outBuf[written++] = actor;
        }
        return written;
    }

    // ─── Vector wrappers (build vectors outside SEH) ──────────────────
    inline std::vector<uintptr_t> GetAllLevels() {
        uintptr_t buf[256];
        int n = ReadSubLevels(buf, 256);
        return std::vector<uintptr_t>(buf, buf + n);
    }

    inline std::vector<uintptr_t> GetActorsFromLevel(uintptr_t level) {
        // Use a thread-local static buffer to avoid massive stack allocs
        static thread_local uintptr_t buf[100000];
        int n = ReadActorsFromLevel(level, buf, 100000);
        return std::vector<uintptr_t>(buf, buf + n);
    }

    // ─── Update pointer chain (called every frame) ───────────────────
    inline void UpdatePointers() {
        bool dbg = ShouldLog();

        uintptr_t gworldAddr = Base + Offsets::GWorld;
        uintptr_t p1 = Read<uintptr_t>(gworldAddr);
        if (!IsValidPtr(p1)) {
            if (dbg) printf("[DBG] GWorld FAIL: Base=0x%llX, addr=0x%llX, p1=0x%llX (invalid)\n", Base, gworldAddr, p1);
            GWorld = 0; return;
        }
        GWorld = Read<uintptr_t>(p1);
        if (!IsValidPtr(GWorld)) {
            if (dbg) printf("[DBG] GWorld deref2 FAIL: p1=0x%llX -> GWorld=0x%llX (invalid)\n", p1, GWorld);
            GWorld = 0; return;
        }

        PersistentLevel = Read<uintptr_t>(GWorld + Offsets::UWorld_PersistentLevel);
        if (dbg) printf("[DBG] GWorld=0x%llX, PersistentLevel=0x%llX\n", GWorld, PersistentLevel);

        GameInstance = Read<uintptr_t>(GWorld + Offsets::UWorld_OwningGameInstance);
        if (!IsValidPtr(GameInstance)) {
            if (dbg) printf("[DBG] GameInstance FAIL: GWorld+0x%llX = 0x%llX\n", (uint64_t)Offsets::UWorld_OwningGameInstance, GameInstance);
            return;
        }

        uintptr_t lpArray = Read<uintptr_t>(GameInstance + Offsets::GameInstance_LocalPlayers);
        if (!IsValidPtr(lpArray)) {
            if (dbg) printf("[DBG] LocalPlayers array FAIL: GI+0x%llX = 0x%llX\n", (uint64_t)Offsets::GameInstance_LocalPlayers, lpArray);
            return;
        }

        LocalPlayer = Read<uintptr_t>(lpArray);
        if (!IsValidPtr(LocalPlayer)) {
            if (dbg) printf("[DBG] LocalPlayer[0] FAIL: arr=0x%llX -> 0x%llX\n", lpArray, LocalPlayer);
            return;
        }

        PlayerController = Read<uintptr_t>(LocalPlayer + Offsets::LocalPlayer_PlayerController);
        if (!IsValidPtr(PlayerController)) {
            if (dbg) printf("[DBG] PlayerController FAIL: LP+0x%llX = 0x%llX\n", (uint64_t)Offsets::LocalPlayer_PlayerController, PlayerController);
            return;
        }

        CameraManager = Read<uintptr_t>(PlayerController + Offsets::PC_CameraManager_Alt);
        if (!IsValidPtr(CameraManager)) {
            CameraManager = Read<uintptr_t>(PlayerController + Offsets::PC_CameraManager);
        }

        AcknowledgedPawn = Read<uintptr_t>(PlayerController + Offsets::PC_AcknowledgedPawn);

        if (dbg) {
            printf("[DBG] Chain: GI=0x%llX LP=0x%llX PC=0x%llX CamMgr=0x%llX Pawn=0x%llX\n",
                   GameInstance, LocalPlayer, PlayerController, CameraManager, AcknowledgedPawn);
            printf("[DBG] CamMgr valid=%d, Pawn valid=%d, LevelsOff=0x%llX\n",
                   IsValidPtr(CameraManager), IsValidPtr(AcknowledgedPawn), LevelsOffset);
        }

        DiscoverLevelsOffset();
    }

    // ─── Camera Auto-Discovery ────────────────────────────────────────
    inline uintptr_t CamPosOffset = 0;    // auto-discovered
    inline bool      CamOffsetFound = false;

    inline void DiscoverCameraOffset() {
        if (CamOffsetFound || !IsValidPtr(CameraManager)) return;

        // Scan CameraManager for pattern: 3 doubles (world pos) + 3 doubles (rotation) + float (FOV)
        // World pos: at least one axis > 100 (in UE units)
        // Rotation: all axes between -360 and 360
        // FOV: between 30 and 170
        for (uintptr_t off = 0x200; off <= 0x2000; off += 8) {
            double x = Read<double>(CameraManager + off);
            double y = Read<double>(CameraManager + off + 8);
            double z = Read<double>(CameraManager + off + 16);

            // Position: at least one axis should be large (world coords in UE5)
            if (fabs(x) < 50.0 && fabs(y) < 50.0 && fabs(z) < 50.0) continue;
            // But not insanely large (garbage)
            if (fabs(x) > 1e10 || fabs(y) > 1e10 || fabs(z) > 1e10) continue;

            // Check rotation at +0x18 (next 3 doubles)
            double p = Read<double>(CameraManager + off + 0x18);
            double yw = Read<double>(CameraManager + off + 0x20);
            double r = Read<double>(CameraManager + off + 0x28);

            if (fabs(p) > 360.0 || fabs(yw) > 360.0 || fabs(r) > 360.0) continue;

            // Check FOV at +0x30 (float)
            float fov = Read<float>(CameraManager + off + 0x30);
            if (fov < 30.0f || fov > 170.0f) continue;

            // Found it!
            CamPosOffset = off;
            CamOffsetFound = true;
            printf("[CAM-DISCOVER] Found camera at CamMgr+0x%llX!\n", off);
            printf("[CAM-DISCOVER] Pos=(%.1f, %.1f, %.1f)\n", x, y, z);
            printf("[CAM-DISCOVER] Rot=(P=%.2f, Y=%.2f, R=%.2f)\n", p, yw, r);
            printf("[CAM-DISCOVER] FOV=%.1f\n", fov);
            return;
        }

        // One-time dump if not found
        static bool scanned = false;
        if (!scanned) {
            scanned = true;
            printf("[CAM-DISCOVER] FAILED! Dumping CamMgr at various offsets:\n");
            uintptr_t checkOffsets[] = { 0x384, 0x900, 0x17C0, 0x1220, 0x1280, 0x1300, 0x1380, 0x1400, 0x1480, 0x1500, 0x1580, 0x1600, 0x1680, 0x1700, 0x1780, 0x1800, 0x1880, 0x1900, 0x1980 };
            for (auto o : checkOffsets) {
                double v1 = Read<double>(CameraManager + o);
                double v2 = Read<double>(CameraManager + o + 8);
                double v3 = Read<double>(CameraManager + o + 16);
                float fv = Read<float>(CameraManager + o);
                printf("  +0x%04llX: d(%.2f, %.2f, %.2f) f=%.2f\n", o, v1, v2, v3, fv);
            }
        }
    }

    // ─── Update camera (called every frame) ───────────────────────────
    inline void UpdateCamera() {
        if (!IsValidPtr(CameraManager)) return;

        // Try to find camera offset if not found yet
        if (!CamOffsetFound) {
            DiscoverCameraOffset();
            if (!CamOffsetFound) return;
        }

        static DWORD camDbgLast = 0;
        bool camDbg = (GetTickCount() - camDbgLast >= 2000);

        // Read camera using auto-discovered offsets
        FVector camLoc;
        camLoc.X = Read<double>(CameraManager + CamPosOffset);
        camLoc.Y = Read<double>(CameraManager + CamPosOffset + 8);
        camLoc.Z = Read<double>(CameraManager + CamPosOffset + 16);

        FRotator camRot;
        camRot.Pitch = Read<double>(CameraManager + CamPosOffset + 0x18);
        camRot.Yaw   = Read<double>(CameraManager + CamPosOffset + 0x20);
        camRot.Roll  = Read<double>(CameraManager + CamPosOffset + 0x28);

        float camFOV = Read<float>(CameraManager + CamPosOffset + 0x30);

        // Validate and use camera data
        bool validPos = (fabs(camLoc.X) > 1.0 || fabs(camLoc.Y) > 1.0 || fabs(camLoc.Z) > 1.0);
        if (validPos) {
            CameraPos = camLoc;
        }

        if (fabs(camRot.Pitch) > 0.01 || fabs(camRot.Yaw) > 0.01) {
            CameraRot = camRot;
        }

        if (camFOV > 10.0f && camFOV < 180.0f) {
            CameraFOV = camFOV;
        } else {
            CameraFOV = 103.0f;
        }

        if (camDbg) {
            camDbgLast = GetTickCount();
            printf("[DBG] Camera @+0x%llX: Pos(%.1f, %.1f, %.1f) Rot(P=%.1f Y=%.1f R=%.1f) FOV=%.1f\n",
                   CamPosOffset,
                   CameraPos.X, CameraPos.Y, CameraPos.Z,
                   CameraRot.Pitch, CameraRot.Yaw, CameraRot.Roll,
                   CameraFOV);
        }
    }

    // ─── Actor helpers ────────────────────────────────────────────────
    inline FVector GetActorPosition(uintptr_t actor) {
        if (!IsValidPtr(actor)) return {};
        uintptr_t rootComp = Read<uintptr_t>(actor + Offsets::Actor_RootComponent);
        if (!IsValidPtr(rootComp)) return {};

        FVector pos;
        pos.X = Read<double>(rootComp + Offsets::RootComp_Position);
        pos.Y = Read<double>(rootComp + Offsets::RootComp_Position + 8);
        pos.Z = Read<double>(rootComp + Offsets::RootComp_Position + 16);
        return pos;
    }

    inline uintptr_t GetMeshComponent(uintptr_t actor) {
        return Read<uintptr_t>(actor + Offsets::Char_MeshComponent);
    }

    inline uintptr_t GetDamageHandler(uintptr_t actor) {
        return Read<uintptr_t>(actor + Offsets::Char_DamageHandler);
    }

    inline float GetHealth(uintptr_t actor) {
        uintptr_t dmg = GetDamageHandler(actor);
        if (!IsValidPtr(dmg)) return 0.0f;
        return Read<float>(dmg + Offsets::DmgHandler_Health);
    }

    inline int GetTeamID(uintptr_t actor) {
        uintptr_t teamComp = Read<uintptr_t>(actor + Offsets::TeamComponent);
        if (!IsValidPtr(teamComp)) return -1;
        return Read<int32_t>(teamComp + Offsets::TeamID);
    }

    inline bool IsEnemy(uintptr_t actor) {
        if (!IsValidPtr(AcknowledgedPawn)) return false;

        // One-time team debug dump
        static bool teamDumped = false;
        if (!teamDumped && IsValidPtr(actor)) {
            teamDumped = true;
            uintptr_t myTeamComp = Read<uintptr_t>(AcknowledgedPawn + Offsets::TeamComponent);
            uintptr_t theirTeamComp = Read<uintptr_t>(actor + Offsets::TeamComponent);
            int32_t myID = IsValidPtr(myTeamComp) ? Read<int32_t>(myTeamComp + Offsets::TeamID) : -999;
            int32_t theirID = IsValidPtr(theirTeamComp) ? Read<int32_t>(theirTeamComp + Offsets::TeamID) : -999;
            printf("[TEAM] LocalPawn=0x%llX, TeamComp@+0x%llX = 0x%llX (valid=%d), TeamID@+0x%llX = %d\n",
                   AcknowledgedPawn, (uint64_t)Offsets::TeamComponent, myTeamComp, IsValidPtr(myTeamComp), (uint64_t)Offsets::TeamID, myID);
            printf("[TEAM] Target=0x%llX, TeamComp = 0x%llX (valid=%d), TeamID = %d\n",
                   actor, theirTeamComp, IsValidPtr(theirTeamComp), theirID);

            // Scan around TeamComponent offset for any heap pointers
            printf("[TEAM] Scanning target actor around 0x690 for heap ptrs:\n");
            for (uintptr_t off = 0x660; off <= 0x6C0; off += 8) {
                uintptr_t val = Read<uintptr_t>(actor + off);
                if (IsValidPtr(val))
                    printf("  +0x%03llX = 0x%llX [VALID]\n", off, val);
            }
        }

        int myTeam = GetTeamID(AcknowledgedPawn);
        int theirTeam = GetTeamID(actor);
        if (myTeam < 0 || theirTeam < 0) return false;
        return myTeam != theirTeam;
    }

    inline bool IsValidCharacter(uintptr_t actor) {
        if (!IsValidPtr(actor) || (actor & 0x7) != 0) return false;
        if (actor == AcknowledgedPawn) return false;

        // MeshComponent must be valid aligned pointer (same check as reference)
        uintptr_t mesh = GetMeshComponent(actor);
        if (!IsValidPtr(mesh) || (mesh & 0x7) != 0) return false;

        // DamageHandler must be valid aligned pointer (characters only have this)
        uintptr_t dmg = GetDamageHandler(actor);
        if (!IsValidPtr(dmg) || (dmg & 0x7) != 0) return false;

        return true;
    }

    inline bool W2S(const FVector& world, FVector2D& screen) {
        return WorldToScreen(world, CameraPos, CameraRot, CameraFOV, ScreenW, ScreenH, screen);
    }

    // ─── ControlRotation auto-discovery with WRITE VERIFICATION ────────
    inline bool ControlRotFound = false;
    inline bool ControlRotIsFloat = false;  // true = float pair, false = double pair
    inline uintptr_t pendingCtrlRotOff = 0;
    inline double    pendingWriteYaw   = 0.0;
    inline int       verifyFrame      = 0;
    inline uintptr_t nextScanStart    = 0x100;
    inline bool      dumpedPC         = false;

    inline void DiscoverControlRotation() {
        if (ControlRotFound || !IsValidPtr(PlayerController)) return;
        if (fabs(CameraRot.Pitch) < 3.0 && fabs(CameraRot.Yaw) < 3.0) return;

        double pitch = CameraRot.Pitch;
        double yaw   = CameraRot.Yaw;

        // One-time dump of ALL rotation-like values in PC
        if (!dumpedPC) {
            dumpedPC = true;
            printf("[AIM-SCAN] Scanning PC=0x%llX for rotation-like values (cam P=%.2f Y=%.2f):\n",
                PlayerController, pitch, yaw);
            for (uintptr_t off = 0x100; off <= 0x1000; off += 8) {
                // Check as doubles
                double d1 = Read<double>(PlayerController + off);
                double d2 = Read<double>(PlayerController + off + 8);
                if (fabs(d1) < 360.0 && fabs(d2) < 360.0 &&
                    fabs(d1 - pitch) < 5.0 && fabs(d2 - yaw) < 5.0) {
                    printf("  [D] PC+0x%03llX: (%.2f, %.2f) delta=(%.2f, %.2f)\n",
                        off, d1, d2, d1 - pitch, d2 - yaw);
                }
                // Check as floats
                float f1 = Read<float>(PlayerController + off);
                float f2 = Read<float>(PlayerController + off + 4);
                if (fabs(f1) < 360.0f && fabs(f2) < 360.0f &&
                    fabs(f1 - (float)pitch) < 5.0f && fabs(f2 - (float)yaw) < 5.0f) {
                    printf("  [F] PC+0x%03llX: (%.2f, %.2f) delta=(%.2f, %.2f)\n",
                        off, f1, f2, f1 - (float)pitch, f2 - (float)yaw);
                }
            }
        }

        if (verifyFrame == 0) {
            // Phase 1: Scan for candidates (try both double and float)
            for (uintptr_t off = nextScanStart; off <= 0x1000; off += 4) {
                // Try as floats first (more common in newer UE5 builds for rotation)
                float f1 = Read<float>(PlayerController + off);
                float f2 = Read<float>(PlayerController + off + 4);
                if (fabs(f1) < 360.0f && fabs(f2) < 360.0f &&
                    fabs(f1 - (float)pitch) < 1.0f && fabs(f2 - (float)yaw) < 1.0f) {
                    printf("[AIM-VERIFY] Float candidate at PC+0x%llX (P=%.2f, Y=%.2f)\n", off, f1, f2);
                    pendingCtrlRotOff = off;
                    ControlRotIsFloat = true;
                    pendingWriteYaw = (double)f2 + 5.0;
                    __try {
                        *(float*)(PlayerController + off + 4) = (float)pendingWriteYaw;
                    } __except (1) {
                        nextScanStart = off + 4;
                        continue;
                    }
                    verifyFrame = 1;
                    nextScanStart = off + 4;
                    return;
                }

                // Try as doubles (stride 8)
                if ((off & 7) == 0) {
                    double d1 = Read<double>(PlayerController + off);
                    double d2 = Read<double>(PlayerController + off + 8);
                    if (fabs(d1) < 360.0 && fabs(d2) < 360.0 &&
                        fabs(d1 - pitch) < 1.0 && fabs(d2 - yaw) < 1.0) {
                        printf("[AIM-VERIFY] Double candidate at PC+0x%llX (P=%.2f, Y=%.2f)\n", off, d1, d2);
                        pendingCtrlRotOff = off;
                        ControlRotIsFloat = false;
                        pendingWriteYaw = d2 + 5.0;
                        __try {
                            *(double*)(PlayerController + off + 8) = pendingWriteYaw;
                        } __except (1) {
                            nextScanStart = off + 8;
                            continue;
                        }
                        verifyFrame = 1;
                        nextScanStart = off + 8;
                        return;
                    }
                }
            }
            nextScanStart = 0x100; // wrap around
        }
        else if (verifyFrame == 1) {
            verifyFrame = 2; // wait one frame
        }
        else if (verifyFrame == 2) {
            double camYawNow = CameraRot.Yaw;
            double diff = fabs(camYawNow - pendingWriteYaw);
            if (diff > 180.0) diff = 360.0 - diff;

            if (diff < 3.0) {
                ControlRotOffset = pendingCtrlRotOff;
                ControlRotFound = true;
                printf("[+] VERIFIED ControlRotation at PC+0x%llX (%s)! Camera Y=%.2f (expected %.2f)\n",
                    pendingCtrlRotOff, ControlRotIsFloat ? "FLOAT" : "DOUBLE", camYawNow, pendingWriteYaw);
            }
            else {
                printf("[AIM-VERIFY] REJECTED PC+0x%llX — wrote Y=%.2f but cam Y=%.2f (diff=%.1f)\n",
                    pendingCtrlRotOff, pendingWriteYaw, camYawNow, diff);
            }
            verifyFrame = 0;
        }
    }

    // ─── AimAt (direct rotation write — same as reference) ────────────
    inline void AimAt(const FVector& targetWorld, float smooth) {
        if (!ControlRotFound || !IsValidPtr(PlayerController)) return;

        double dx = targetWorld.X - CameraPos.X;
        double dy = targetWorld.Y - CameraPos.Y;
        double dz = targetWorld.Z - CameraPos.Z;
        double dist2D = sqrt(dx * dx + dy * dy);
        if (dist2D < 1.0) return;

        double targetYaw   = atan2(dy, dx) * (180.0 / NW_PI);
        double targetPitch = atan2(dz, dist2D) * (180.0 / NW_PI);

        double currentPitch, currentYaw;
        if (ControlRotIsFloat) {
            currentPitch = (double)Read<float>(PlayerController + ControlRotOffset);
            currentYaw   = (double)Read<float>(PlayerController + ControlRotOffset + 4);
        } else {
            currentPitch = Read<double>(PlayerController + ControlRotOffset);
            currentYaw   = Read<double>(PlayerController + ControlRotOffset + 8);
        }

        double deltaPitch = targetPitch - currentPitch;
        double deltaYaw   = targetYaw - currentYaw;

        while (deltaYaw   >  180.0) deltaYaw   -= 360.0;
        while (deltaYaw   < -180.0) deltaYaw   += 360.0;
        while (deltaPitch >  180.0) deltaPitch -= 360.0;
        while (deltaPitch < -180.0) deltaPitch += 360.0;

        deltaPitch /= smooth;
        deltaYaw   /= smooth;

        __try {
            if (ControlRotIsFloat) {
                *(float*)(PlayerController + ControlRotOffset)     = (float)(currentPitch + deltaPitch);
                *(float*)(PlayerController + ControlRotOffset + 4) = (float)(currentYaw + deltaYaw);
            } else {
                *(double*)(PlayerController + ControlRotOffset)     = currentPitch + deltaPitch;
                *(double*)(PlayerController + ControlRotOffset + 8) = currentYaw + deltaYaw;
            }
        }
        __except (1) {
            printf("[AIM] Write failed!\n");
        }
    }

    // ─── FName decryption ─────────────────────────────────────────────
    inline uint64_t decrypt_xor_keys(uint32_t key, const uint64_t* state) {
        const uint64_t hash = 0x2545F4914F6CDD1DULL * (key ^ ((key ^ (key >> 15)) >> 12) ^ (key << 25));
        const uint64_t idx = hash % 7;
        uint64_t val = state[idx];
        const uint32_t hi = (uint32_t)(hash >> 32);
        const uint32_t mod7 = (uint32_t)(idx) % 7;

        if (mod7 == 0) {
            val = val + (2ULL * hi - 1);
        }
        else if (mod7 == 1) {
            uint64_t step1 = ((val >> 1) ^ (((val >> 1) ^ (2 * val)) & 0xAAAAAAAAAAAAAAAAULL));
            uint64_t v16 = step1 >> 2;
            uint64_t v17 = v16 ^ ((v16 ^ (4 * step1)) & 0xCCCCCCCCCCCCCCCCULL);
            uint64_t v18 = (v17 >> 4) ^ (((v17 >> 4) ^ (16 * v17)) & 0xF0F0F0F0F0F0F0F0ULL);
            uint64_t rotr_val = (v18 >> 8) ^ (((v18 >> 8) ^ (v18 << 8)) & 0xFF00FF00FF00FF00ULL);
            val = ~(uint64_t)(uint32_t)(hi + (uint32_t)idx) ^ _rotr64(rotr_val, 32);
        }
        else if (mod7 == 2) {
            val = ~val - (uint32_t)(hi + (uint32_t)idx);
        }
        else if (mod7 == 3) {
            uint8_t l = (uint8_t)(((int)hi + 2 * (int)idx) % 0x3F) + 1;
            uint8_t r = 63 * (uint8_t)(((int)hi + 2 * (int)idx) / 0x3F) - ((uint8_t)hi + 2 * (uint8_t)idx) + 63;
            val = ~((val >> r) | (val << l));
        }
        else if (mod7 == 4) {
            val = ~(uint64_t)(uint32_t)(hi + (uint32_t)idx) ^ (val - (uint32_t)(hi + 2 * (uint32_t)idx));
        }
        else if (mod7 == 5) {
            uint64_t temp = (uint32_t)(hi + 2 * (uint32_t)idx) + val;
            val = (temp >> 1) ^ (((temp >> 1) ^ (2 * temp)) & 0xAAAAAAAAAAAAAAAAULL);
        }
        else if (mod7 == 6) {
            val = ~(val - (uint32_t)(hi + 2 * (uint32_t)idx));
        }

        return val ^ key;
    }

    // ─── FName internal reader (POD only, SEH-safe) ───────────────────
    // Reads FName into outBuf (max 256 chars). Returns length, or 0 on failure.
    inline int ReadFNameRaw(uintptr_t actor, char* outBuf, int outBufSize) {
        if (!IsValidPtr(actor) || outBufSize <= 0) return 0;

        __try {
            int32_t fnameId = Read<int32_t>(actor + Offsets::Actor_FNameID);
            if (fnameId <= 0) return 0;

            uintptr_t fNamePool = Base + Offsets::FNamePool;
            int32_t block  = fnameId >> 16;
            int32_t offset = fnameId & 0xFFFF;

            uintptr_t blockPtr = Read<uintptr_t>(fNamePool + 0x10 + block * 8);
            if (!IsValidPtr(blockPtr)) return 0;

            uintptr_t entry = blockPtr + (uintptr_t)offset * 2;
            if (!IsValidPtr(entry)) return 0;

            uint16_t header = Read<uint16_t>(entry);
            int32_t  nameLen = header >> 6;
            bool     encrypted = (header & 1) != 0;

            if (nameLen <= 0 || nameLen > 256 || nameLen >= outBufSize) return 0;

            for (int i = 0; i < nameLen; i++) {
                outBuf[i] = Read<char>(entry + 2 + i);
            }
            outBuf[nameLen] = '\0';

            if (encrypted) {
                uint64_t stateArr[7] = {};
                for (int i = 0; i < 7; i++)
                    stateArr[i] = Read<uint64_t>(Base + Offsets::FNameState + i * 8);

                uint64_t xorKey = decrypt_xor_keys((uint32_t)fnameId, stateArr);
                for (int i = 0; i < nameLen; i++) {
                    outBuf[i] ^= (char)(xorKey >> ((i % 8) * 8));
                }
            }

            return nameLen;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // ─── Get FName string (vector wrapper) ────────────────────────────
    inline std::string GetFName(uintptr_t actor) {
        char buf[257] = {};
        int len = ReadFNameRaw(actor, buf, 257);
        if (len > 0)
            return std::string(buf, len);
        return "";
    }
}
