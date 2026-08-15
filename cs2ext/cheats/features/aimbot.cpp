#define NOMINMAX
#include "aimbot.h"
#include "triggerbot.h"
#include "../render.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>
#include <chrono>
#include <Windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace features {
    namespace {

        constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
        constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
        constexpr float kDefaultFov = 90.0f;
        constexpr float kMinFov = 1.0f;
        constexpr float kMaxFov = 179.0f;
        constexpr float kAngleDeadzone = 0.02f;
        constexpr float kTargetSwitchRatio = 0.50f;
        constexpr float kMaxDeltaTime = 0.08f;

        struct AimCandidate {
            float     score = std::numeric_limits<float>::max();
            float     reqYaw = 0.f;
            float     reqPitch = 0.f;
            float     pixelDist = std::numeric_limits<float>::max();
            float     worldDist = std::numeric_limits<float>::max();
            uintptr_t pawnAddr = 0;
        };

        static uintptr_t s_lockedTarget = 0;
        static auto      s_lastUpdate = std::chrono::steady_clock::now();

        float NormYaw(float y) {
            while (y > 180.f) y -= 360.f;
            while (y < -180.f) y += 360.f;
            return y;
        }

        float GetDeltaTime() {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - s_lastUpdate).count();
            s_lastUpdate = now;

            if (!std::isfinite(dt) || dt <= 0.f)
                return 1.f / 128.f;

            return dt;
        }
        // Read actual in-game FOV (respects fov_changer setting)
        float GetActualFov(const GameData& gd) {
            if (g_cfg.fov_changer && g_cfg.custom_fov >= kMinFov && g_cfg.custom_fov <= kMaxFov)
                return g_cfg.custom_fov;

            uintptr_t cs = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pCameraServices);
            if (cs) {
                uint32_t stored = mem::ReadMem<uint32_t>(cs + m_iFOV);
                if (stored >= (uint32_t)kMinFov && stored <= (uint32_t)kMaxFov)
                    return (float)stored;
            }
            return kDefaultFov;
        }

        float FocalLength(float sw, float hFov) {
            hFov = std::clamp(hFov, kMinFov, kMaxFov);
            float t = std::tanf(hFov * 0.5f * kDegToRad);
            return (t > 0.00001f) ? (sw * 0.5f) / t : sw * 0.5f;
        }

        float FovRadius(float sw, float actualFov, float aimFov) {
            if (aimFov <= 0.f) return 0.f;
            return FocalLength(sw, actualFov) * std::tanf(std::clamp(aimFov, 0.f, 179.f) * kDegToRad);
        }

        bool ValidAngles(const ViewAngles& a) {
            return std::isfinite(a.pitch) && std::isfinite(a.yaw)
                && a.pitch >= -90.f && a.pitch <= 90.f
                && a.yaw >= -360.f && a.yaw <= 360.f;
        }

        void DrawFovCircle(float radius) {
            if (!g_cfg.aimbot_fov_draw || !g_cfg.aimbot_fov_enabled) return;
            if (radius <= 0.f || !std::isfinite(radius)) return;
            auto dl = ImGui::GetBackgroundDrawList();
            if (!dl) return;
            float* c = g_cfg.fov_circle_color;
            dl->AddCircle(
                { (float)screenWidth * 0.5f, (float)screenHeight * 0.5f },
                radius,
                IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), (int)(c[3] * 255)),
                64, 1.4f);
        }

        void ResetState() { s_lockedTarget = 0; }

    } // anon namespace

    void Aimbot(const std::vector<EntityData>& entities, const GameData& gd) {
        const float dt = GetDeltaTime();

        if (!g_cfg.aimbot || !gd.localPlayerPawn || !gd.client) {
            ResetState(); return;
        }

        const bool keyHeld = (GetAsyncKeyState(g_cfg.aimbot_key) & 0x8000) != 0;
        if (!g_cfg.aimbot_always_on && !keyHeld) { ResetState(); return; }

        ViewAngles cur = mem::ReadMem<ViewAngles>(gd.client + dwViewAngles);
        if (!ValidAngles(cur)) { ResetState(); return; }
        cur.yaw = NormYaw(cur.yaw);

        const float actualFov = GetActualFov(gd);
        const float aimFov = std::clamp(g_cfg.aimbot_fov, 0.f, 179.f);
        const float sw = (float)screenWidth;
        const float sh = (float)screenHeight;
        const float focal = FocalLength(sw, actualFov);
        const float fovPx = FovRadius(sw, actualFov, aimFov);
        const float fovPxSq = fovPx * fovPx;

        DrawFovCircle(fovPx);

        // Local origin for world-distance sort — read once from already-cached entity list
        // (no extra RPM for entList/localPawn here; gd is filled each frame in main loop)
        const float scx = sw * 0.5f;
        const float scy = sh * 0.5f;
        const int   bone = g_cfg.aimbot_target_bone;
        const int   sort = g_cfg.aimbot_sort_mode;
        const bool  fovE = g_cfg.aimbot_fov_enabled;

        // Use local player origin from gd — no extra read
        // (localPlayerPawn origin isn't cached in GameData, but we avoid full entity re-scan.
        //  We keep one lightweight read of the local origin here.)
        Vector3 localOrg{};
        if (sort == 1 || sort == 2) {
            uintptr_t gsn = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pGameSceneNode);
            if (gsn) localOrg = mem::ReadMem<Vector3>(gsn + m_vOldOrigin);
        }

        std::vector<AimCandidate> candidates;
        candidates.reserve(entities.size());

        for (const auto& ent : entities) {
            if (!ent.valid || !ent.isEnemy || ent.health <= 0 || !ent.pawnAddress)
                continue;

            Vector3 tgt = ent.head;
            if (ent.hasBoneData && bone >= 0 && bone < 25) {
                const Vector3& b = ent.bones[bone];
                if (b.x || b.y || b.z) tgt = b;
            }

            Vector3 sp = tgt.w2s(gd.view_matrix);
            if (!std::isfinite(sp.x) || !std::isfinite(sp.y) || sp.z < 0.01f) continue;

            const float dx = sp.x - scx;
            const float dy = sp.y - scy;
            const float pxDistSq = dx * dx + dy * dy;
            if (fovE && pxDistSq > fovPxSq) continue;

            const float pxDist = std::sqrt(pxDistSq);

            float wDist = 0.f;
            if (sort == 1 || sort == 2) {
                float ex = tgt.x - localOrg.x, ey = tgt.y - localOrg.y, ez = tgt.z - localOrg.z;
                wDist = std::sqrt(ex * ex + ey * ey + ez * ez);
                if (!std::isfinite(wDist)) continue;
                if (wDist < 1.f) wDist = 1.f;
            }

            float score;
            if (sort == 0) score = pxDist;
            else if (sort == 1) score = wDist;
            else                score = pxDist * 0.5f + (wDist / 100.f) * 0.5f;
            if (!std::isfinite(score)) continue;

            const float yawDelta = -std::atan2(dx, focal) * kRadToDeg;
            const float pitchDelta = std::atan2(dy, focal) * kRadToDeg;

            AimCandidate c;
            c.score = score;
            c.reqYaw = NormYaw(cur.yaw + yawDelta);
            c.reqPitch = std::clamp(cur.pitch + pitchDelta, -89.f, 89.f);
            c.pixelDist = pxDist;
            c.worldDist = wDist;
            c.pawnAddr = ent.pawnAddress;
            candidates.push_back(c);
        }

        if (candidates.empty()) { ResetState(); return; }

        //  Target selection with hysteresis 
        AimCandidate* best = nullptr;
        AimCandidate* locked = nullptr;
        for (auto& c : candidates) {
            if (!best || c.score < best->score) best = &c;
            if (c.pawnAddr == s_lockedTarget) locked = &c;
        }

        AimCandidate* chosen = best;
        if (locked && locked != best) {
            // Only switch if new target is meaningfully better
            if (best->score >= locked->score * kTargetSwitchRatio)
                chosen = locked;
        }
        if (!chosen) { ResetState(); return; }
        s_lockedTarget = chosen->pawnAddr;

        const float dYaw = NormYaw(chosen->reqYaw - cur.yaw);
        const float dPitch = chosen->reqPitch - cur.pitch;
        if (!std::isfinite(dYaw) || !std::isfinite(dPitch)) return;

        //  Auto-shoot with cooldown (delegated to TriggerbotForceShoot) 
        if (g_cfg.aimbot_auto_shoot) {
            constexpr float kShootTol = 0.5f;
            if (std::fabs(dYaw) < kShootTol && std::fabs(dPitch) < kShootTol)
                TriggerbotForceShoot(gd); // has its own cooldown guard inside
        }

        if (std::fabs(dYaw) < kAngleDeadzone && std::fabs(dPitch) < kAngleDeadzone) return;

        //  Smoothed movement — scoped players get tighter response 
        int   velBase = isScoped ? 10 : 60;
        float smooth = std::max(1.f, g_cfg.aimbot_smooth);
        float speed = velBase / smooth;
        float interp = 1.f - std::exp(-speed * dt);
        float factor = std::clamp(interp, 0.f, 1.f);

        ViewAngles next;
        next.pitch = std::clamp(cur.pitch + dPitch * factor, -89.f, 89.f);
        next.yaw = NormYaw(cur.yaw + dYaw * factor);
        next.roll = 0.f;
        if (!std::isfinite(next.pitch) || !std::isfinite(next.yaw)) return;

        mem::WriteToMem<ViewAngles>(gd.client + dwViewAngles, next);
    }

}
