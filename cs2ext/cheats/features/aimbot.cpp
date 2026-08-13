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
#include "../../cheats/Esp.h"

namespace features {

    namespace {

        constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
        constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;

        constexpr float kDefaultFov = 90.0f;
        constexpr float kMinFov = 1.0f;
        constexpr float kMaxFov = 179.0f;

        constexpr float kAngleDeadzone = 0.02f;
        constexpr float kTargetSwitchRatio = 0.75f;
        constexpr float kMaxDeltaTime = 0.1f;

        struct AimCandidate {
            float     score = std::numeric_limits<float>::max();
            float     reqYaw = 0.0f;
            float     reqPitch = 0.0f;
            float     pixelDistance = std::numeric_limits<float>::max();
            float     worldDistance = std::numeric_limits<float>::max();
            uintptr_t pawnAddr = 0;
        };

        static uintptr_t s_lockedTarget = 0;
        static auto s_lastUpdate = std::chrono::steady_clock::now();

        float NormalizeYaw(float yaw) {
            while (yaw > 180.0f)
                yaw -= 360.0f;
            while (yaw < -180.0f)
                yaw += 360.0f;
            return yaw;
        }

        float GetDeltaTime() {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - s_lastUpdate).count();
            s_lastUpdate = now;

            if (!std::isfinite(dt) || dt <= 0.0f)
                return 1.0f / 60.0f;

            return std::min(dt, kMaxDeltaTime);
        }

        float DegreesToRadians(float degrees) {
            return degrees * kDegToRad;
        }

        float GetActualFov(const GameData& gd) {
            float actualFov = kDefaultFov;

            uintptr_t cameraServices = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pCameraServices);

            if (cameraServices) {
                const uint32_t storedFov = mem::ReadMem<uint32_t>(cameraServices + m_iFOV);

                if (storedFov >= kMinFov && storedFov <= kMaxFov) {
                    actualFov = static_cast<float>(storedFov);
                }
            }

            if (g_cfg.fov_changer && g_cfg.custom_fov >= kMinFov && g_cfg.custom_fov <= kMaxFov) {
                actualFov = g_cfg.custom_fov;
            }

            return actualFov;
        }

        float CalculateFocalLength(float screenWidth, float horizontalFov) {
            if (screenWidth <= 1.0f)
                return 1.0f;

            horizontalFov = std::clamp(horizontalFov, kMinFov, kMaxFov);

            const float halfFovRad = DegreesToRadians(horizontalFov * 0.5f);
            const float tanHalf = std::tanf(halfFovRad);

            if (!std::isfinite(tanHalf) || tanHalf <= 0.00001f) {
                return screenWidth * 0.5f;
            }

            return (screenWidth * 0.5f) / tanHalf;
        }

        float CalculateFovRadius(float screenWidth, float actualFov, float aimFov) {
            if (aimFov <= 0.0f)
                return 0.0f;

            const float focalLength = CalculateFocalLength(screenWidth, actualFov);
            const float angle = std::clamp(aimFov, 0.0f, 179.0f);

            return focalLength * std::tanf(DegreesToRadians(angle));
        }

        bool IsValidAngles(const ViewAngles& angles) {
            if (!std::isfinite(angles.pitch) || !std::isfinite(angles.yaw)) {
                return false;
            }

            if (angles.pitch < -90.0f || angles.pitch > 90.0f) {
                return false;
            }

            if (angles.yaw < -360.0f || angles.yaw > 360.0f) {
                return false;
            }

            return true;
        }

        float CalculateWorldDistance(const Vector3& a, const Vector3& b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;

            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        AimCandidate* FindCandidate(std::vector<AimCandidate>& candidates, uintptr_t pawnAddress) {
            for (auto& candidate : candidates) {
                if (candidate.pawnAddr == pawnAddress)
                    return &candidate;
            }
            return nullptr;
        }

        AimCandidate* SelectBestCandidate(std::vector<AimCandidate>& candidates, uintptr_t currentLockedAddress) {
            if (candidates.empty())
                return nullptr;

            AimCandidate* best = nullptr;

            for (auto& candidate : candidates) {
                if (!best || candidate.score < best->score) {
                    best = &candidate;
                }
            }

            if (!best)
                return nullptr;

            AimCandidate* locked = FindCandidate(candidates, currentLockedAddress);

            if (!locked)
                return best;

            if (locked == best)
                return locked;

            if (best->score < locked->score * kTargetSwitchRatio) {
                return best;
            }

            return locked;
        }

        void DrawFovCircle(float radius) {
            if (!g_cfg.aimbot_fov_draw || !g_cfg.aimbot_fov_enabled) {
                return;
            }

            if (radius <= 0.0f || !std::isfinite(radius)) {
                return;
            }

            ImDrawList* drawList = ImGui::GetBackgroundDrawList();

            if (!drawList)
                return;

            float* color = g_cfg.fov_circle_color;

            if (!color)
                return;

            const ImU32 drawColor = IM_COL32(
                static_cast<int>(std::clamp(color[0], 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color[1], 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color[2], 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color[3], 0.0f, 1.0f) * 255.0f)
            );

            drawList->AddCircle(
                ImVec2(
                    static_cast<float>(screenWidth) * 0.5f,
                    static_cast<float>(screenHeight) * 0.5f
                ),
                radius,
                drawColor,
                64,
                1.4f
            );
        }

        void ResetAimbotState() {
            s_lockedTarget = 0;
        }
    }

    void Aimbot(const std::vector<EntityData>& entities, const GameData& gd) {
        const float deltaTime = GetDeltaTime();

        if (!g_cfg.aimbot || !gd.localPlayerPawn || !gd.client) {
            ResetAimbotState();
            return;
        }

        const bool keyHeld = (GetAsyncKeyState(g_cfg.aimbot_key) & 0x8000) != 0;

        if (!g_cfg.aimbot_always_on && !keyHeld) {
            ResetAimbotState();
            return;
        }

        ViewAngles currentAngles = mem::ReadMem<ViewAngles>(gd.client + dwViewAngles);

        if (!IsValidAngles(currentAngles)) {
            ResetAimbotState();
            return;
        }

        currentAngles.yaw = NormalizeYaw(currentAngles.yaw);

        const float actualFov = GetActualFov(gd);
        const float aimFov = std::clamp(g_cfg.aimbot_fov, 0.0f, 179.0f);

        const float focalLength = CalculateFocalLength(static_cast<float>(screenWidth), actualFov);
        const float fovGatePx = CalculateFovRadius(static_cast<float>(screenWidth), actualFov, aimFov);
        const float fovGatePxSq = fovGatePx * fovGatePx;

        DrawFovCircle(fovGatePx);

        Vector3 localOrigin{};

        uintptr_t gameScene = mem::ReadMem<uintptr_t>(gd.localPlayerPawn + m_pGameSceneNode);

        if (gameScene) {
            localOrigin = mem::ReadMem<Vector3>(gameScene + m_vOldOrigin);
        }

        const int targetBone = g_cfg.aimbot_target_bone;
        const int sortMode = g_cfg.aimbot_sort_mode;
        const bool fovEnabled = g_cfg.aimbot_fov_enabled;

        const float screenCX = static_cast<float>(screenWidth) * 0.5f;
        const float screenCY = static_cast<float>(screenHeight) * 0.5f;

        std::vector<AimCandidate> candidates;
        candidates.reserve(entities.size());

        for (const auto& entity : entities) {
            if (!entity.valid)
                continue;

            if (!entity.isEnemy)
                continue;

            if (entity.health <= 0)
                continue;

            if (!entity.pawnAddress)
                continue;

            Vector3 targetPosition = entity.head;

            if (entity.hasBoneData && targetBone >= 0 && targetBone < 25) {
                const Vector3 bone = entity.bones[targetBone];

                if (bone.x != 0.0f || bone.y != 0.0f || bone.z != 0.0f) {
                    targetPosition = bone;
                }
            }

            const Vector3 screenPosition = targetPosition.w2s(gd.view_matrix);

            if (!std::isfinite(screenPosition.x) || !std::isfinite(screenPosition.y) || !std::isfinite(screenPosition.z)) {
                continue;
            }

            if (screenPosition.z < 0.01f)
                continue;

            const float dx = screenPosition.x - screenCX;
            const float dy = screenPosition.y - screenCY;
            const float pixelDistanceSq = dx * dx + dy * dy;

            if (fovEnabled && pixelDistanceSq > fovGatePxSq) {
                continue;
            }

            const float pixelDistance = std::sqrt(pixelDistanceSq);

            float worldDistance = 0.0f;

            if (sortMode == 1 || sortMode == 2) {
                worldDistance = CalculateWorldDistance(targetPosition, localOrigin);

                if (!std::isfinite(worldDistance))
                    continue;

                if (worldDistance < 1.0f)
                    worldDistance = 1.0f;
            }

            const float yawDelta = -std::atan2(dx, focalLength) * kRadToDeg;
            const float pitchDelta = std::atan2(dy, focalLength) * kRadToDeg;

            const float requiredYaw = NormalizeYaw(currentAngles.yaw + yawDelta);
            const float requiredPitch = std::clamp(currentAngles.pitch + pitchDelta, -89.0f, 89.0f);

            float score = pixelDistance;

            if (sortMode == 0) {
                score = pixelDistance;
            }
            else if (sortMode == 1) {
                score = worldDistance;
            }
            else {
                const float normalizedWorld = worldDistance / 100.0f;
                score = pixelDistance * 0.5f + normalizedWorld * 0.5f;
            }

            if (!std::isfinite(score))
                continue;

            AimCandidate candidate;
            candidate.score = score;
            candidate.reqYaw = requiredYaw;
            candidate.reqPitch = requiredPitch;
            candidate.pixelDistance = pixelDistance;
            candidate.worldDistance = worldDistance;
            candidate.pawnAddr = entity.pawnAddress;

            candidates.push_back(candidate);
        }

        if (candidates.empty()) {
            ResetAimbotState();
            return;
        }

        AimCandidate* chosen = SelectBestCandidate(candidates, s_lockedTarget);

        if (!chosen) {
            ResetAimbotState();
            return;
        }

        s_lockedTarget = chosen->pawnAddr;

        const float deltaYaw = NormalizeYaw(chosen->reqYaw - currentAngles.yaw);
        const float deltaPitch = chosen->reqPitch - currentAngles.pitch;

        if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch)) {
            return;
        }

        if (g_cfg.aimbot_auto_shoot) {
            constexpr float shootTolerance = 0.5f;
            const bool onTarget = std::fabs(deltaYaw) < shootTolerance && std::fabs(deltaPitch) < shootTolerance;

            if (onTarget) {
                TriggerbotForceShoot(gd);
            }
        }

        if (std::fabs(deltaYaw) < kAngleDeadzone && std::fabs(deltaPitch) < kAngleDeadzone) {
            return;
        }

        int vel = isScoped ? 10 : 40;

        const float smooth = std::max(1.0f, g_cfg.aimbot_smooth);
        const float speed = vel / smooth;
        const float interpolation = 1.0f - std::exp(-speed * deltaTime);
        const float factor = std::clamp(interpolation, 0.0f, 1.0f);

        ViewAngles nextAngles;
        nextAngles.pitch = currentAngles.pitch + deltaPitch * factor;
        nextAngles.yaw = NormalizeYaw(currentAngles.yaw + deltaYaw * factor);
        nextAngles.pitch = std::clamp(nextAngles.pitch, -89.0f, 89.0f);
        nextAngles.roll = 0.0f;

        if (!std::isfinite(nextAngles.pitch) || !std::isfinite(nextAngles.yaw)) {
            return;
        }

        mem::WriteToMem<ViewAngles>(gd.client + dwViewAngles, nextAngles);
    }
}