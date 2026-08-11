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
        constexpr float RAD_TO_DEG = 180.0f / static_cast<float>(M_PI);
        constexpr float DEG_TO_RAD = static_cast<float>(M_PI) / 180.0f;
        constexpr float DEFAULT_FOV = 90.0f;
        constexpr float MIN_FOV = 1.0f;
        constexpr float MAX_FOV = 179.0f;
        constexpr float ANGLE_DEADZONE = 0.02f;
        constexpr float TARGET_SWITCH_RATIO = 0.75f;
        constexpr float MAX_DELTA_TIME = 0.1f;

        struct AimCandidate {
            float score = FLT_MAX;
            float requiredYaw = 0.0f;
            float requiredPitch = 0.0f;
            float pixelDist = FLT_MAX;
            float worldDist = FLT_MAX;
            uintptr_t pawnAddr = 0;
        };

        static uintptr_t lockedTarget = 0;
        static auto lastUpdate = std::chrono::steady_clock::now();

        float NormalizeYaw(float yaw) {
            while (yaw > 180.0f) yaw -= 360.0f;
            while (yaw < -180.0f) yaw += 360.0f;
            return yaw;
        }

        float GetDeltaTime() {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - lastUpdate).count();
            lastUpdate = now;

            if (!std::isfinite(dt) || dt <= 0.0f)
                return 1.0f / 60.0f;

            return std::min(dt, MAX_DELTA_TIME);
        }

        float GetGameFOV(const GameData& gd) {
            float fov = DEFAULT_FOV;

            // get FOV
            uintptr_t cameraServices = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pCameraServices);
            if (cameraServices) {
                uint32_t storedFov = mem::ReadMem<uint32_t>(cameraServices + m_iFOV);
                if (storedFov >= MIN_FOV && storedFov <= MAX_FOV)
                    fov = static_cast<float>(storedFov);
            }

            if (g_cfg.fov_changer && g_cfg.custom_fov >= MIN_FOV && g_cfg.custom_fov <= MAX_FOV)
                fov = g_cfg.custom_fov;

            return fov;
        }

        float CalculateFocalLength(float screenWidth, float fov) {
            if (screenWidth <= 1.0f) return 1.0f;

            fov = std::clamp(fov, MIN_FOV, MAX_FOV);
            float halfFov = std::tanf(DEG_TO_RAD * fov * 0.5f);

            if (!std::isfinite(halfFov) || halfFov <= 0.00001f)
                return screenWidth * 0.5f;

            return (screenWidth * 0.5f) / halfFov;
        }

        float CalcFovRadius(float screenWidth, float actualFov, float aimFov) {
            if (aimFov <= 0.0f) return 0.0f;

            float focalLength = CalculateFocalLength(screenWidth, actualFov);
            float angle = std::clamp(aimFov, 0.0f, 179.0f);

            return focalLength * std::tanf(DEG_TO_RAD * angle);
        }

        bool ValidAngles(const ViewAngles& angles) {
            if (!std::isfinite(angles.pitch) || !std::isfinite(angles.yaw))
                return false;
            if (angles.pitch < -90.0f || angles.pitch > 90.0f)
                return false;
            if (angles.yaw < -360.0f || angles.yaw > 360.0f)
                return false;
            return true;
        }

        float Distance3D(const Vector3& a, const Vector3& b) {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        AimCandidate* FindCandidate(std::vector<AimCandidate>& candidates, uintptr_t address) {
            for (auto& c : candidates) {
                if (c.pawnAddr == address)
                    return &c;
            }
            return nullptr;
        }

        //target switching
        AimCandidate* SelectBest(std::vector<AimCandidate>& candidates, uintptr_t currentLock) {
            if (candidates.empty())
                return nullptr;

            AimCandidate* best = &candidates[0];
            for (auto& c : candidates) {
                if (c.score < best->score)
                    best = &c;
            }

            AimCandidate* locked = FindCandidate(candidates, currentLock);
            if (!locked)
                return best;

            if (locked == best)
                return locked;

            if (best->score < locked->score * TARGET_SWITCH_RATIO)
                return best;

            return locked;
        }

        void DrawFovCircle(float radius) {
            if (!g_cfg.aimbot_fov_draw || !g_cfg.aimbot_fov_enabled || radius <= 0.0f)
                return;

            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            if (!draw) return;

            float* color = g_cfg.fov_circle_color;
            if (!color) return;

            ImU32 col = IM_COL32(
                (int)(std::clamp(color[0], 0.0f, 1.0f) * 255),
                (int)(std::clamp(color[1], 0.0f, 1.0f) * 255),
                (int)(std::clamp(color[2], 0.0f, 1.0f) * 255),
                (int)(std::clamp(color[3], 0.0f, 1.0f) * 255)
            );

            draw->AddCircle(
                ImVec2(screenWidth * 0.5f, screenHeight * 0.5f),
                radius, col, 64, 1.4f
            );
        }

        void ResetState() {
            lockedTarget = 0;
        }
    }

    void Aimbot(const std::vector<EntityData>& entities, const GameData& gd) {
        float deltaTime = GetDeltaTime();

        if (!g_cfg.aimbot || !gd.localPlayerPawn || !gd.client) {
            ResetState();
            return;
        }

        bool keyDown = (GetAsyncKeyState(g_cfg.aimbot_key) & 0x8000) != 0;
        if (!g_cfg.aimbot_always_on && !keyDown) {
            ResetState();
            return;
        }

        ViewAngles currentAngles = mem::ReadMem<ViewAngles>(gd.client + dwViewAngles);
        if (!ValidAngles(currentAngles)) {
            ResetState();
            return;
        }

        currentAngles.yaw = NormalizeYaw(currentAngles.yaw);

        // FOV calc
        float gameFov = GetGameFOV(gd);
        float aimFov = std::clamp(g_cfg.aimbot_fov, 0.0f, 179.0f);
        float focalLength = CalculateFocalLength(screenWidth, gameFov);
        float fovRadius = CalcFovRadius(screenWidth, gameFov, aimFov);
        float fovRadiusSq = fovRadius * fovRadius;

        DrawFovCircle(fovRadius);

        // Local origin
        Vector3 localOrigin = {};
        uintptr_t sceneNode = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pGameSceneNode);
        if (sceneNode)
            localOrigin = mem::ReadMem<Vector3>(sceneNode + m_vOldOrigin);

        std::vector<AimCandidate> candidates;
        candidates.reserve(entities.size());

        float screenCX = screenWidth * 0.5f;
        float screenCY = screenHeight * 0.5f;

        for (const auto& ent : entities) {
            if (!ent.valid || !ent.isEnemy || ent.health <= 0 || !ent.pawnAddress)
                continue;

            // bone
            Vector3 targetPos = ent.head;
            if (ent.hasBoneData && g_cfg.aimbot_target_bone >= 0 && g_cfg.aimbot_target_bone < 25) {
                Vector3 bone = ent.bones[g_cfg.aimbot_target_bone];
                if (bone.x != 0 || bone.y != 0 || bone.z != 0)
                    targetPos = bone;
            }

            // World to screen
            Vector3 screenPos = targetPos.w2s(gd.view_matrix);
            if (!std::isfinite(screenPos.x) || !std::isfinite(screenPos.y) || !std::isfinite(screenPos.z))
                continue;
            if (screenPos.z < 0.01f)
                continue;

            // pixel dist
            float dx = screenPos.x - screenCX;
            float dy = screenPos.y - screenCY;
            float pixelDistSq = dx * dx + dy * dy;

            // fov check
            if (g_cfg.aimbot_fov_enabled && pixelDistSq > fovRadiusSq)
                continue;

            float pixelDist = std::sqrt(pixelDistSq);

            float worldDist = 0.0f;
            if (g_cfg.aimbot_sort_mode == 1 || g_cfg.aimbot_sort_mode == 2) {
                worldDist = Distance3D(targetPos, localOrigin);
                if (!std::isfinite(worldDist)) continue;
                if (worldDist < 1.0f) worldDist = 1.0f;
            }

            float yawDelta = -std::atan2(dx, focalLength) * RAD_TO_DEG;
            float pitchDelta = std::atan2(dy, focalLength) * RAD_TO_DEG;

            float requiredYaw = NormalizeYaw(currentAngles.yaw + yawDelta);
            float requiredPitch = std::clamp(currentAngles.pitch + pitchDelta, -89.0f, 89.0f);

            float score = pixelDist;
            if (g_cfg.aimbot_sort_mode == 1) {
                score = worldDist;
            }
            else if (g_cfg.aimbot_sort_mode == 2) {
                score = (pixelDist * 0.5f) + ((worldDist / 100.0f) * 0.5f);
            }

            if (!std::isfinite(score)) continue;

            candidates.push_back({
                score,
                requiredYaw,
                requiredPitch,
                pixelDist,
                worldDist,
                ent.pawnAddress
                });
        }

        if (candidates.empty()) {
            ResetState();
            return;
        }

        AimCandidate* chosen = SelectBest(candidates, lockedTarget);
        if (!chosen) {
            ResetState();
            return;
        }

        lockedTarget = chosen->pawnAddr;

        float deltaYaw = NormalizeYaw(chosen->requiredYaw - currentAngles.yaw);
        float deltaPitch = chosen->requiredPitch - currentAngles.pitch;

        if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch))
            return;

        if (g_cfg.aimbot_auto_shoot) {
            if (std::fabs(deltaYaw) < 0.5f && std::fabs(deltaPitch) < 0.5f)
                TriggerbotForceShoot(gd);
        }

        if (std::fabs(deltaYaw) < ANGLE_DEADZONE && std::fabs(deltaPitch) < ANGLE_DEADZONE)
            return;

        float smooth = std::max(1.0f, g_cfg.aimbot_smooth);
        float speed = 50.0f / smooth;
        float factor = std::clamp(1.0f - std::exp(-speed * deltaTime), 0.0f, 1.0f);

        ViewAngles newAngles;
        newAngles.pitch = std::clamp(currentAngles.pitch + deltaPitch * factor, -89.0f, 89.0f);
        newAngles.yaw = NormalizeYaw(currentAngles.yaw + deltaYaw * factor);
        newAngles.roll = 0.0f;

        if (!std::isfinite(newAngles.pitch) || !std::isfinite(newAngles.yaw))
            return;

        mem::WriteToMem<ViewAngles>(gd.client + dwViewAngles, newAngles);
    }
}