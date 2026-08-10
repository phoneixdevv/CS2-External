// CS2 External — aimbot.cpp (Fixed Screen-Space Math & Fully Integrated)
#define NOMINMAX
#include "aimbot.h"
#include "triggerbot.h"
#include "../render.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>
#include <random>
#include <chrono>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace features {

    static float NormYaw(float y) {
        while (y > 180.f) y -= 360.f;
        while (y < -180.f) y += 360.f;
        return y;
    }

    static bool IsVisible(uintptr_t targetPawn, const GameData& gd) {
        if (!targetPawn || !gd.localPlayerPawn) return false;
        int slot = gd.localPlayerIndex;
        if (slot <= 0 || slot > 64) return true;

        uint32_t maskIdx = (uint32_t)(slot - 1) / 32;
        uint32_t bitPos = (uint32_t)(slot - 1) % 32;

        uintptr_t maskAddr = targetPawn
            + m_entitySpottedState
            + m_bSpottedByMask
            + maskIdx * sizeof(uint32_t);

        uint32_t mask = mem::ReadMem<uint32_t>(maskAddr);
        return (mask >> bitPos) & 1u;
    }

    struct AimCandidate {
        float      score;
        float      reqYaw;
        float      reqPitch;
        uintptr_t  pawnAddr;
        bool       visible;
    };

    static uintptr_t    s_lockedTarget = 0;
    static std::mt19937 s_rng(std::random_device{}());

    static AimCandidate* SelectBestCandidate(std::vector<AimCandidate>& group, uintptr_t currentLockedAddr) {
        if (group.empty()) return nullptr;

        AimCandidate* best = nullptr;
        AimCandidate* currentlyLocked = nullptr;

        for (auto& c : group) {
            if (c.pawnAddr == currentLockedAddr) {
                currentlyLocked = &c;
            }

            if (!best) {
                best = &c;
            } else {
                if (!best->visible && c.visible) {
                    best = &c;
                }
                else if (best->visible == c.visible) {
                    if (c.score < best->score) {
                        best = &c;
                    }
                }
            }
        }

        if (currentlyLocked && !currentlyLocked->visible && best && best->visible) {
            return best;
        }

        if (currentlyLocked && currentlyLocked->visible) {
            return best;
        }

        return best ? best : (currentlyLocked ? currentlyLocked : &group[0]);
    }

    static AimCandidate* FindInGroup(std::vector<AimCandidate>& group, uintptr_t addr) {
        for (auto& c : group)
            if (c.pawnAddr == addr) return &c;
        return nullptr;
    }

    void Aimbot(const std::vector<EntityData>& entities, const GameData& gd) {
        if (!g_cfg.aimbot || !gd.localPlayerPawn || !gd.client) return;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        bool keyHeld = (GetAsyncKeyState(g_cfg.aimbot_key) & 0x8000) != 0;
        if (!g_cfg.aimbot_always_on && !keyHeld) {
            s_lockedTarget = 0;
            return;
        }

        ViewAngles cur = mem::ReadMem<ViewAngles>(gd.client + dwViewAngles);
        if (cur.pitch < -90.f || cur.pitch > 90.f)  return;
        if (cur.yaw < -360.f || cur.yaw  > 360.f) return;

        float actualFOV = 90.f;
        {
            uintptr_t cs = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pCameraServices);
            if (cs) {
                uint32_t sf = mem::ReadMem<uint32_t>(cs + m_iFOV);
                if (sf > 0 && sf <= 150)                             actualFOV = (float)sf;
                else if (g_cfg.fov_changer && g_cfg.custom_fov > 0.f) actualFOV = g_cfg.custom_fov;
            }
        }

        float halfFOV_rad = actualFOV * 0.5f * (float)M_PI / 180.f;
        float focalLen = (float)screenWidth * 0.5f / tanf(halfFOV_rad);
        float fovGatePx = focalLen * tanf(g_cfg.aimbot_fov * (float)M_PI / 180.f);
        float fovGatePxSq = fovGatePx * fovGatePx;

        if (g_cfg.aimbot_fov_draw && g_cfg.aimbot_fov_enabled) {
            if (auto dl = ImGui::GetBackgroundDrawList()) {
                float* fc = g_cfg.fov_circle_color;
                dl->AddCircle(
                    ImVec2((float)screenWidth * 0.5f, (float)screenHeight * 0.5f),
                    fovGatePx,
                    IM_COL32((int)(fc[0] * 255), (int)(fc[1] * 255), (int)(fc[2] * 255), (int)(fc[3] * 255)),
                    64, 1.4f);
            }
        }

        Vector3 localOrigin = {};
        {
            uintptr_t gs = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pGameSceneNode);
            if (gs) localOrigin = mem::ReadMem<Vector3>(gs + m_vOldOrigin);
        }

        const int   tb = g_cfg.aimbot_target_bone;
        const int   sortMode = g_cfg.aimbot_sort_mode;
        const bool  fovOn = g_cfg.aimbot_fov_enabled;
        const float screenCX = (float)screenWidth * 0.5f;
        const float screenCY = (float)screenHeight * 0.5f;

        std::vector<AimCandidate> visibles;
        visibles.reserve(8);

        for (const auto& e : entities) {
            if (!e.valid || !e.isEnemy || e.health <= 0) continue;

            bool vis = IsVisible(e.pawnAddress, gd);
			// if visibleonyl is enabled, skip non-visible targets
            if (g_cfg.aimbot_visible_only && !vis) continue;

            Vector3 tp = e.head;
            if (e.hasBoneData && tb >= 0 && tb < 25) {
                Vector3 b = e.bones[tb];
                if (b.x != 0.f || b.y != 0.f || b.z != 0.f) tp = b;
            }

            Vector3 sp = tp.w2s(gd.view_matrix);
            if (sp.z < 0.01f) continue;

            float dx = sp.x - screenCX;
            float dy = sp.y - screenCY;
            float pixDistSq = dx * dx + dy * dy;

            if (fovOn && pixDistSq > fovGatePxSq) continue;
            float pixDist = sqrtf(pixDistSq);

            float dX = tp.x - localOrigin.x;
            float dY = tp.y - localOrigin.y;
            float dZ = tp.z - localOrigin.z;
            float world3D = sqrtf(dX * dX + dY * dY + dZ * dZ);
            if (world3D < 1.f) world3D = 1.f;

            float reqYaw = NormYaw(cur.yaw + (-atan2f(dx, focalLen) * (180.f / (float)M_PI)));
            float reqPitch = std::clamp(cur.pitch + (atan2f(dy, focalLen) * (180.f / (float)M_PI)), -89.f, 89.f);

            float score = 0.f;
            if (sortMode == 0)      score = pixDist;                       // Pixel Distance
            else if (sortMode == 1) score = world3D;                       // Player Distance (3D)
            else                    score = pixDist * 0.5f + (world3D / 100.f) * 0.5f; // Hybrid

            AimCandidate c{ score, reqYaw, reqPitch, e.pawnAddress, vis };
            visibles.push_back(c);
        }

        if (visibles.empty()) {
            s_lockedTarget = 0;
            return;
        }

        AimCandidate* chosen = SelectBestCandidate(visibles, s_lockedTarget);
        if (chosen) {
            s_lockedTarget = chosen->pawnAddr;
        } else {
            s_lockedTarget = 0;
            return;
        }

        float deltaYaw = NormYaw(chosen->reqYaw - cur.yaw);
        float deltaPitch = chosen->reqPitch - cur.pitch;

        if (g_cfg.aimbot_auto_shoot) {
            bool onTarget = (fabsf(deltaYaw) < 0.5f && fabsf(deltaPitch) < 0.5f);
            if (onTarget) TriggerbotForceShoot(gd);
        }

        if (fabsf(deltaYaw) < 0.05f && fabsf(deltaPitch) < 0.05f) return;

        float sm = std::max(1.0f, g_cfg.aimbot_smooth);

        ViewAngles next;
        next.pitch = std::clamp(cur.pitch + (deltaPitch / sm), -89.f, 89.f);
        next.yaw = NormYaw(cur.yaw + (deltaYaw / sm));
        next.roll = 0.f;

        mem::WriteToMem<ViewAngles>(gd.client + dwViewAngles, next);
    }

}