#define NOMINMAX
#include "triggerbot.h"
#include "../render.h"
#include <chrono>
#include <thread>
#include <atomic>

namespace features {

    static std::atomic<bool> g_triggerFiring{ false };

    // Last time a shot was fired — for cooldown enforcement
    static std::atomic<long long> g_lastShotTime{ 0 };

    static void FireWorker(int delay_ms, bool doAutoStop) {
        if (delay_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

        if (doAutoStop) {
            INPUT freeze{};
            freeze.type = INPUT_MOUSE;
            freeze.mi.dwFlags = MOUSEEVENTF_MOVE;
            freeze.mi.dx = freeze.mi.dy = 0;
            SendInput(1, &freeze, sizeof(INPUT));
        }

        // LMB press
        INPUT dn{};
        dn.type = INPUT_MOUSE;
        dn.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &dn, sizeof(INPUT));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        INPUT up{};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &up, sizeof(INPUT));

        // Record shot timestamp
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        g_lastShotTime.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count(),
            std::memory_order_release);

        g_triggerFiring.store(false, std::memory_order_release);
    }

    static bool ShotCooldownReady(int cooldown_ms) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        long long last = g_lastShotTime.load(std::memory_order_acquire);
        return (now - last) >= cooldown_ms;
    }

    static bool ResolveTarget(const GameData& gd, uintptr_t* pawnOut) {
        if (!gd.localPlayerPawn || !gd.entList) return false;

        int xid = mem::ReadMem<int>(gd.localPlayerPawn + m_iIDEntIndex);
        if (xid <= 0 || xid > 2048) return false;

        uintptr_t le = mem::ReadMem<uintptr_t>(
            gd.entList + 0x8 * ((xid & 0x7FFF) >> 9) + 0x10);
        if (!le) return false;

        uintptr_t tp = mem::ReadMem<uintptr_t>(le + 0x70 * (xid & 0x1FF));
        if (!tp || tp == gd.localPlayerPawn) return false;

        if (mem::ReadMem<int>(tp + m_iTeamNum) == gd.localTeam) return false;
        int th = mem::ReadMem<int>(tp + m_iHealth);
        if (th <= 0 || th > 100) return false;

        if (pawnOut) *pawnOut = tp;
        return true;
    }

    void Triggerbot(const std::vector<EntityData>&, const GameData& gd) {
        if (!g_cfg.triggerbot) return;


        const bool keyActive = g_cfg.triggerbot_always_on
            || (GetAsyncKeyState(g_cfg.triggerbot_key) & 0x8000);
        if (!keyActive) return;

        if (g_triggerFiring.load(std::memory_order_acquire)) return;
        if (!ShotCooldownReady(g_cfg.triggerbot_shot_cooldown)) return;

        uintptr_t tp = 0;
        if (!ResolveTarget(gd, &tp)) return;

        int  delay = g_cfg.triggerbot_delay;
        bool autoStop = g_cfg.triggerbot_auto_stop;

        g_triggerFiring.store(true, std::memory_order_release);
        std::thread([delay, autoStop]() {
            FireWorker(delay, autoStop);
            }).detach();
    }

    void TriggerbotForceShoot(const GameData& gd) {
        if (g_triggerFiring.load(std::memory_order_acquire)) return;
        if (!ShotCooldownReady(g_cfg.aimbot_shot_cooldown)) return;

        uintptr_t tp = 0;
        if (!ResolveTarget(gd, &tp)) return;

        int delay = g_cfg.aimbot_auto_shoot_delay;

        g_triggerFiring.store(true, std::memory_order_release);
        std::thread([delay]() {
            FireWorker(delay, false);
            }).detach();
    }

    void TriggerbotFlush() {
        auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(250);
        while (g_triggerFiring.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() > deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

} 