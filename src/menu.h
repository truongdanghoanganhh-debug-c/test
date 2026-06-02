#pragma once
/*
 * menu.h — ImGui Menu & ESP Drawing
 *
 * Toggle: INSERT key
 * Features: Box/Corner ESP, Skeleton (multi-layout), Health bar,
 * Head circle, 3D hat, Snaplines, Distance, Rainbow, Aimbot FOV circle
 */

#include <Windows.h>
#include <imgui.h>
#include <cmath>
#include <algorithm>

#include "game.h"
#include "math.h"

namespace Menu {

    // ─── Settings ────────────────────────────────────────────────────
    inline bool  bMenuOpen    = false;

    // ESP
    inline bool  bESP         = true;
    inline bool  bBox         = true;
    inline bool  bBoxCorner   = false;
    inline bool  bSkeleton    = true;
    inline bool  bHealth      = true;
    inline bool  bDistance    = true;
    inline bool  bHeadCircle  = true;
    inline bool  bHat         = false;
    inline bool  bSnapline    = false;
    inline bool  bRainbow     = false;
    inline float fThickness   = 1.5f;

    // Aimbot
    inline bool  bAimbot      = false;
    inline float fAimFOV      = 150.0f;
    inline float fSmooth      = 5.0f;
    inline int   iAimBone     = 0;       // 0=head, 1=chest/neck, 2=pelvis
    inline int   iAimKey      = VK_RBUTTON;
    inline bool  bDrawFOV     = true;

    // ─── Bone Layouts ────────────────────────────────────────────────
    // Layout for 101, 103, 104 bone characters
    struct BoneLayout {
        int hip, neck, lArm, lHand, lHand1, rArm, rHand, rHand1;
        int lThigh, lCalf, lFoot, rThigh, rCalf, rFoot, head;
    };

    constexpr BoneLayout Layout101 = { 3, 21, 23, 24, 25, 49, 50, 51, 75, 76, 78, 82, 83, 85, 20 };
    constexpr BoneLayout Layout103 = { 3, 9,  33, 30, 32, 58, 55, 57, 63, 65, 69, 77, 79, 83, 8  };
    constexpr BoneLayout Layout104 = { 3, 21, 23, 24, 25, 49, 50, 51, 77, 78, 80, 84, 85, 87, 20 };

    // 13 skeleton connections (bone pairs)
    struct BonePair { int from, to; };
    inline BonePair GetConnections(const BoneLayout& l, BonePair* out) {
        // Returns array of connections. We use a fixed 13-pair set.
        // Caller should use GetConnectionList() instead.
        (void)l; (void)out;
        return {};
    }

    // ─── Auto-detect bone layout ─────────────────────────────────────
    inline const BoneLayout& DetectLayout(uintptr_t meshComp) {
        // Get hip (bone 3) — same for all layouts
        FVector hip = Game::GetBonePosition(meshComp, 3);
        if (!hip.IsValid() || hip.IsZero()) return Layout101;

        // Try bone 9 (layout 103 neck)
        FVector neck9 = Game::GetBonePosition(meshComp, 9);
        if (neck9.IsValid() && !neck9.IsZero()) {
            double dz = neck9.Z - hip.Z;
            if (dz > 20.0 && dz < 120.0) {
                return Layout103;
            }
        }

        // Try bone 21 (layout 101/104 neck)
        FVector neck21 = Game::GetBonePosition(meshComp, 21);
        if (neck21.IsValid() && !neck21.IsZero()) {
            double dz = neck21.Z - hip.Z;
            if (dz > 20.0 && dz < 120.0) {
                // Distinguish 101 vs 104: check bone 87
                FVector bone87 = Game::GetBonePosition(meshComp, 87);
                if (bone87.IsValid() && !bone87.IsZero()) {
                    double dzFoot = bone87.Z - hip.Z;
                    if (fabs(dzFoot) < 120.0)  // bone 87 is rFoot for 104
                        return Layout104;
                }
                return Layout101;
            }
        }

        return Layout101; // fallback
    }

    // ─── HSV to ImU32 ────────────────────────────────────────────────
    inline ImU32 HSVtoColor(float h, float s, float v, float a = 1.0f) {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
        return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
    }

    // ─── Draw the Menu ───────────────────────────────────────────────
    inline void Draw() {
        if (!bMenuOpen) return;

        ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
        ImGui::Begin("NullWare", &bMenuOpen, ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginTabBar("MainTabs")) {
            // ── Aimbot Tab ──
            if (ImGui::BeginTabItem("Aimbot")) {
                ImGui::Checkbox("Enable Aimbot", &bAimbot);
                ImGui::SliderFloat("FOV##aim", &fAimFOV, 10.0f, 500.0f, "%.0f");
                ImGui::SliderFloat("Smooth", &fSmooth, 1.0f, 30.0f, "%.1f");
                ImGui::Combo("Bone", &iAimBone, "Head\0Neck/Chest\0Pelvis\0");
                ImGui::Checkbox("Draw FOV Circle", &bDrawFOV);
                ImGui::EndTabItem();
            }

            // ── Visuals Tab ──
            if (ImGui::BeginTabItem("Visuals")) {
                ImGui::Checkbox("Enable ESP", &bESP);
                ImGui::Separator();
                ImGui::Checkbox("Box", &bBox);
                ImGui::SameLine();
                ImGui::Checkbox("Corner Style", &bBoxCorner);
                ImGui::Checkbox("Skeleton", &bSkeleton);
                ImGui::Checkbox("Health Bar", &bHealth);
                ImGui::Checkbox("Distance", &bDistance);
                ImGui::Checkbox("Head Circle", &bHeadCircle);
                ImGui::Checkbox("Hat", &bHat);
                ImGui::Checkbox("Snaplines", &bSnapline);
                ImGui::Separator();
                ImGui::Checkbox("Rainbow Mode", &bRainbow);
                ImGui::SliderFloat("Thickness", &fThickness, 0.5f, 5.0f, "%.1f");
                ImGui::EndTabItem();
            }

            // ── Misc Tab ──
            if (ImGui::BeginTabItem("Misc")) {
                ImGui::Text("NullWare Internal");
                ImGui::Separator();
                ImGui::Text("END key — Unload DLL");
                ImGui::Text("INSERT — Toggle menu");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    // ─── Draw Corner Box ─────────────────────────────────────────────
    inline void DrawCornerBox(ImDrawList* dl, float x, float y, float w, float h, ImU32 col, float t) {
        float len = fminf(w, h) * 0.25f;
        // Top-left
        dl->AddLine(ImVec2(x, y), ImVec2(x + len, y), col, t);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + len), col, t);
        // Top-right
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w - len, y), col, t);
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + len), col, t);
        // Bottom-left
        dl->AddLine(ImVec2(x, y + h), ImVec2(x + len, y + h), col, t);
        dl->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - len), col, t);
        // Bottom-right
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - len, y + h), col, t);
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - len), col, t);
    }

    // ─── Draw 3D Conical Hat ─────────────────────────────────────────
    inline void DrawHat(ImDrawList* dl, const FVector2D& headScreen, float boxH, ImU32 col) {
        float radius = boxH * 0.06f;
        float hatHeight = boxH * 0.12f;
        ImVec2 apex = ImVec2(headScreen.X, headScreen.Y - hatHeight - radius * 0.5f);
        int segments = 16;
        float ySquash = 0.35f;

        // Draw brim ellipse
        for (int i = 0; i < segments; i++) {
            float a0 = (float)i / segments * 2.0f * (float)NW_PI;
            float a1 = (float)(i + 1) / segments * 2.0f * (float)NW_PI;
            ImVec2 p0 = ImVec2(headScreen.X + cosf(a0) * radius, headScreen.Y - radius * 0.5f + sinf(a0) * radius * ySquash);
            ImVec2 p1 = ImVec2(headScreen.X + cosf(a1) * radius, headScreen.Y - radius * 0.5f + sinf(a1) * radius * ySquash);
            dl->AddLine(p0, p1, col, fThickness);

            // Filled triangles from apex to brim
            ImU32 fillCol = (col & 0x00FFFFFF) | 0x30000000;  // low alpha fill
            dl->AddTriangleFilled(apex, p0, p1, fillCol);
        }

        // 4 radial ribs
        for (int i = 0; i < 4; i++) {
            float a = (float)i / 4.0f * 2.0f * (float)NW_PI;
            ImVec2 brim = ImVec2(headScreen.X + cosf(a) * radius, headScreen.Y - radius * 0.5f + sinf(a) * radius * ySquash);
            dl->AddLine(apex, brim, col, fThickness * 0.8f);
        }
    }

    // ─── Draw ESP (called every frame) ───────────────────────────────
    inline void DrawESP() {
        static DWORD espDbgLast = 0;
        bool espDbg = (GetTickCount() - espDbgLast >= 2000);

        if (!bESP) {
            if (espDbg) { espDbgLast = GetTickCount(); printf("[DBG-ESP] ESP disabled\n"); }
            return;
        }
        if (!Game::IsValidPtr(Game::GWorld)) {
            if (espDbg) { espDbgLast = GetTickCount(); printf("[DBG-ESP] GWorld invalid (0x%llX)\n", Game::GWorld); }
            return;
        }
        if (!Game::IsValidPtr(Game::AcknowledgedPawn)) {
            if (espDbg) { espDbgLast = GetTickCount(); printf("[DBG-ESP] AcknowledgedPawn invalid (0x%llX) — not in match?\n", Game::AcknowledgedPawn); }
            return;
        }

        auto* dl = ImGui::GetBackgroundDrawList();
        float time = (float)GetTickCount64() / 1000.0f;

        // Watermark
        {
            ImU32 wCol1 = HSVtoColor(fmodf(time * 0.3f, 1.0f), 0.8f, 1.0f);
            dl->AddText(ImVec2(12, 10), wCol1, "NullWare");
        }

        // Aimbot FOV circle
        if (bAimbot && bDrawFOV) {
            dl->AddCircle(
                ImVec2(Game::ScreenW * 0.5f, Game::ScreenH * 0.5f),
                fAimFOV,
                IM_COL32(255, 255, 255, 80),
                64, 1.0f
            );
        }

        // Get all actors from all loaded levels
        auto levels = Game::GetAllLevels();
        int entityIdx = 0;

        // Debug counters
        int dbgTotalActors = 0;
        int dbgValidChars = 0;
        int dbgEnemies = 0;
        int dbgHasMesh = 0;
        int dbgBoneOk = 0;
        int dbgW2SOk = 0;
        int dbgDrawn = 0;

        // Aimbot target tracking
        float closestDist = fAimFOV;
        FVector aimTarget = {};
        bool hasAimTarget = false;

        for (auto& level : levels) {
            auto actors = Game::GetActorsFromLevel(level);
            dbgTotalActors += (int)actors.size();

            for (auto& actor : actors) {
                if (!Game::IsValidCharacter(actor)) continue;
                dbgValidChars++;

                // No team check — reference doesn't use one, self is already excluded
                dbgEnemies++;

                uintptr_t meshComp = Game::GetMeshComponent(actor);
                if (!Game::IsValidPtr(meshComp) || (meshComp & 0x7) != 0) continue;
                dbgHasMesh++;

                // Detect bone layout for this actor
                const BoneLayout& lay = DetectLayout(meshComp);

                // Get head + base positions
                FVector headPos = Game::GetBonePosition(meshComp, lay.head);
                FVector basePos = Game::GetBonePosition(meshComp, lay.hip);

                if (!headPos.IsValid() || headPos.IsZero()) {
                    if (espDbg && dbgBoneOk == 0)
                        printf("[DBG-ESP] Bone FAIL: head(bone %d)=(%.1f,%.1f,%.1f) valid=%d zero=%d\n",
                               lay.head, headPos.X, headPos.Y, headPos.Z, headPos.IsValid(), headPos.IsZero());
                    continue;
                }
                if (!basePos.IsValid() || basePos.IsZero()) {
                    if (espDbg && dbgBoneOk == 0)
                        printf("[DBG-ESP] Bone FAIL: hip(bone %d)=(%.1f,%.1f,%.1f) valid=%d zero=%d\n",
                               lay.hip, basePos.X, basePos.Y, basePos.Z, basePos.IsValid(), basePos.IsZero());
                    continue;
                }
                dbgBoneOk++;

                // Adjust head up slightly for box
                FVector topPos = headPos;
                topPos.Z += 15.0;

                // Project to screen
                FVector2D headScreen, baseScreen, topScreen;
                if (!Game::W2S(headPos, headScreen)) continue;
                if (!Game::W2S(basePos, baseScreen)) continue;
                if (!Game::W2S(topPos, topScreen)) continue;
                dbgW2SOk++;

                // Box dimensions
                float boxH = fabs(baseScreen.Y - topScreen.Y);
                float boxW = boxH * 0.55f;
                float boxX = topScreen.X - boxW * 0.5f;
                float boxY = topScreen.Y;

                if (boxH < 5.0f || boxH > 2000.0f) continue;
                dbgDrawn++;

                // Color
                float health = Game::GetHealth(actor);
                ImU32 espCol;
                if (bRainbow) {
                    float hue = fmodf(time * 0.5f + entityIdx * 0.15f, 1.0f);
                    espCol = HSVtoColor(hue, 0.9f, 1.0f);
                } else {
                    float hFrac = std::clamp(health / 100.0f, 0.0f, 1.0f);
                    espCol = IM_COL32(
                        (int)((1.0f - hFrac) * 255),
                        (int)(hFrac * 255),
                        30, 255
                    );
                }

                // ── Box ESP ──
                if (bBox) {
                    if (bBoxCorner) {
                        DrawCornerBox(dl, boxX, boxY, boxW, boxH, espCol, fThickness);
                    } else {
                        dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH),
                                    espCol, 0.0f, 0, fThickness);
                    }
                }

                // ── Head Circle ──
                if (bHeadCircle) {
                    float headR = boxH * 0.08f;
                    dl->AddCircle(ImVec2(headScreen.X, headScreen.Y), headR, espCol, 16, fThickness);
                }

                // ── 3D Hat ──
                if (bHat) {
                    DrawHat(dl, headScreen, boxH, espCol);
                }

                // ── Skeleton ──
                if (bSkeleton) {
                    int connections[][2] = {
                        { lay.hip, lay.neck },
                        { lay.neck, lay.lArm },     { lay.lArm, lay.lHand },    { lay.lHand, lay.lHand1 },
                        { lay.neck, lay.rArm },      { lay.rArm, lay.rHand },    { lay.rHand, lay.rHand1 },
                        { lay.hip, lay.lThigh },     { lay.lThigh, lay.lCalf },  { lay.lCalf, lay.lFoot },
                        { lay.hip, lay.rThigh },     { lay.rThigh, lay.rCalf },  { lay.rCalf, lay.rFoot },
                    };

                    for (int b = 0; b < 13; b++) {
                        FVector p1 = Game::GetBonePosition(meshComp, connections[b][0]);
                        FVector p2 = Game::GetBonePosition(meshComp, connections[b][1]);
                        if (!p1.IsValid() || p1.IsZero()) continue;
                        if (!p2.IsValid() || p2.IsZero()) continue;

                        FVector2D s1, s2;
                        if (!Game::W2S(p1, s1) || !Game::W2S(p2, s2)) continue;

                        ImU32 boneCol = espCol;
                        if (bRainbow) {
                            float hue = fmodf(time * 0.5f + entityIdx * 0.15f + b * 0.05f, 1.0f);
                            boneCol = HSVtoColor(hue, 0.9f, 1.0f);
                        }

                        dl->AddLine(ImVec2(s1.X, s1.Y), ImVec2(s2.X, s2.Y), boneCol, fThickness);
                    }
                }

                // ── Health Bar ──
                if (bHealth && health > 0.0f) {
                    float maxHP = 100.0f;
                    float hFrac = std::clamp(health / maxHP, 0.0f, 1.0f);
                    float barW = 3.0f;
                    float barX = boxX - barW - 3.0f;
                    float barH = boxH * hFrac;

                    dl->AddRectFilled(ImVec2(barX, boxY), ImVec2(barX + barW, boxY + boxH),
                                      IM_COL32(0, 0, 0, 180));
                    ImU32 hpCol = IM_COL32(
                        (int)((1.0f - hFrac) * 255),
                        (int)(hFrac * 255),
                        0, 255
                    );
                    dl->AddRectFilled(ImVec2(barX, boxY + boxH - barH), ImVec2(barX + barW, boxY + boxH),
                                      hpCol);
                }

                // ── Distance ──
                if (bDistance) {
                    double dist = Game::CameraPos.DistTo(basePos) / 100.0;
                    char distBuf[32];
                    snprintf(distBuf, sizeof(distBuf), "%.0fm", dist);
                    dl->AddText(ImVec2(boxX + boxW * 0.5f - 10.0f, boxY + boxH + 2.0f),
                                IM_COL32(255, 255, 255, 220), distBuf);
                }

                // ── Snaplines ──
                if (bSnapline) {
                    dl->AddLine(
                        ImVec2(Game::ScreenW * 0.5f, Game::ScreenH),
                        ImVec2(baseScreen.X, baseScreen.Y),
                        espCol, fThickness * 0.7f
                    );
                }

                // ── Aimbot target selection ──
                if (bAimbot && (GetAsyncKeyState(iAimKey) & 0x8000)) {
                    int targetBoneIdx = lay.head;
                    if (iAimBone == 1) targetBoneIdx = lay.neck;
                    else if (iAimBone == 2) targetBoneIdx = lay.hip;

                    FVector targetBone = Game::GetBonePosition(meshComp, targetBoneIdx);
                    if (targetBone.IsValid() && !targetBone.IsZero()) {
                        FVector2D targetScreen;
                        if (Game::W2S(targetBone, targetScreen)) {
                            float dx = targetScreen.X - Game::ScreenW * 0.5f;
                            float dy = targetScreen.Y - Game::ScreenH * 0.5f;
                            float dist = sqrtf(dx * dx + dy * dy);
                            if (dist < closestDist) {
                                closestDist = dist;
                                aimTarget = targetBone;
                                hasAimTarget = true;
                            }
                        }
                    }
                }

                entityIdx++;
            }
        }

        // ── Debug summary ──
        if (espDbg) {
            espDbgLast = GetTickCount();
            printf("[DBG-ESP] Levels=%d, Actors=%d, ValidChars=%d, Enemies=%d, HasMesh=%d, BoneOK=%d, W2S=%d, Drawn=%d\n",
                   (int)levels.size(), dbgTotalActors, dbgValidChars, dbgEnemies, dbgHasMesh, dbgBoneOk, dbgW2SOk, dbgDrawn);
        }

        // ── Aimbot (direct rotation write — same as reference) ──
        // Discover ControlRotation offset (one-time scan, called every frame until found)
        if (!Game::ControlRotFound)
            Game::DiscoverControlRotation();

        static int aimDbg = 0;
        bool keyDown = (GetAsyncKeyState(iAimKey) & 0x8000) != 0;

        if (aimDbg++ % 300 == 0) {
            printf("[AIM-DBG] enabled=%d hasTarget=%d keyDown=%d ctrlRotFound=%d ctrlRotOff=0x%llX closestDist=%.1f\n",
                bAimbot, hasAimTarget, keyDown, Game::ControlRotFound, Game::ControlRotOffset, closestDist);
        }

        if (bAimbot && hasAimTarget && keyDown) {
            Game::AimAt(aimTarget, fSmooth);

            if (aimDbg % 60 == 0)
                printf("[AIM] Aiming at world (%.0f, %.0f, %.0f)\n", aimTarget.X, aimTarget.Y, aimTarget.Z);
        }
    }

    // ─── Apply ImGui style theme ─────────────────────────────────────
    inline void ApplyTheme() {
        auto& style = ImGui::GetStyle();
        style.WindowRounding    = 8.0f;
        style.FrameRounding     = 4.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.WindowPadding     = ImVec2(12, 12);
        style.FramePadding      = ImVec2(8, 4);
        style.ItemSpacing       = ImVec2(8, 6);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.06f, 0.15f, 0.96f);
        colors[ImGuiCol_Border]             = ImVec4(0.40f, 0.20f, 0.80f, 0.50f);
        colors[ImGuiCol_FrameBg]            = ImVec4(0.15f, 0.10f, 0.25f, 0.80f);
        colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.25f, 0.15f, 0.45f, 0.80f);
        colors[ImGuiCol_FrameBgActive]      = ImVec4(0.35f, 0.20f, 0.55f, 0.80f);
        colors[ImGuiCol_TitleBg]            = ImVec4(0.10f, 0.05f, 0.20f, 1.00f);
        colors[ImGuiCol_TitleBgActive]      = ImVec4(0.20f, 0.10f, 0.40f, 1.00f);
        colors[ImGuiCol_Tab]                = ImVec4(0.15f, 0.08f, 0.30f, 1.00f);
        colors[ImGuiCol_TabHovered]         = ImVec4(0.35f, 0.18f, 0.65f, 1.00f);
        colors[ImGuiCol_TabSelected]        = ImVec4(0.30f, 0.15f, 0.55f, 1.00f);
        colors[ImGuiCol_CheckMark]          = ImVec4(0.70f, 0.40f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]         = ImVec4(0.55f, 0.30f, 0.90f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.70f, 0.40f, 1.00f, 1.00f);
        colors[ImGuiCol_Button]             = ImVec4(0.20f, 0.10f, 0.40f, 1.00f);
        colors[ImGuiCol_ButtonHovered]      = ImVec4(0.30f, 0.15f, 0.55f, 1.00f);
        colors[ImGuiCol_ButtonActive]       = ImVec4(0.40f, 0.20f, 0.70f, 1.00f);
        colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.10f, 0.40f, 1.00f);
        colors[ImGuiCol_HeaderHovered]      = ImVec4(0.30f, 0.15f, 0.55f, 1.00f);
        colors[ImGuiCol_HeaderActive]       = ImVec4(0.40f, 0.20f, 0.70f, 1.00f);
        colors[ImGuiCol_Text]               = ImVec4(0.95f, 0.93f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled]       = ImVec4(0.50f, 0.45f, 0.60f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.05f, 0.03f, 0.10f, 0.80f);
        colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.30f, 0.15f, 0.50f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.20f, 0.65f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.25f, 0.80f, 1.00f);
    }
}
