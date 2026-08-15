#define NOMINMAX
#include "misc.h"
#include "../render.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace features {

    void BunnyHop(const GameData& gd) {
        if (!g_cfg.bunny_hop || !gd.localPlayerPawn || !gd.client) return;
        if (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) return;
        int fl = mem::ReadMem<int>(gd.localPlayerPawn + m_fFlags);
        mem::WriteToMem<int>(gd.client + dwForceJump, (fl & 1) ? 65537 : 4);
    }

    void NoFlash(const GameData& gd) {
        if (!g_cfg.no_flash || !gd.localPlayerPawn) return;
        if (mem::ReadMem<float>(gd.localPlayerPawn + m_flFlashDuration) > 0.f)
            mem::WriteToMem<float>(gd.localPlayerPawn + m_flFlashDuration, 0.f);
    }

    void CustomCrosshair() {
        if (!g_cfg.custom_crosshair) return;
        auto dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;

        float cx = (float)screenWidth * 0.5f;
        float cy = (float)screenHeight * 0.5f;
        float sz = (float)g_cfg.crosshair_size;
        float gap = (float)g_cfg.crosshair_gap;
        ImU32 col = IM_COL32(
            (int)(g_cfg.crosshair_color[0] * 255), (int)(g_cfg.crosshair_color[1] * 255),
            (int)(g_cfg.crosshair_color[2] * 255), (int)(g_cfg.crosshair_color[3] * 255));
        ImU32 shadow = IM_COL32(0, 0, 0, 160);
        float t = 1.8f;

        // Shadow pass
        dl->AddLine({ cx - gap - sz + 1,cy + 1 }, { cx - gap + 1,cy + 1 }, shadow, t + 1.f);
        dl->AddLine({ cx + gap + 1,cy + 1 }, { cx + gap + sz + 1,cy + 1 }, shadow, t + 1.f);
        dl->AddLine({ cx + 1,cy - gap - sz + 1 }, { cx + 1,cy - gap + 1 }, shadow, t + 1.f);
        dl->AddLine({ cx + 1,cy + gap + 1 }, { cx + 1,cy + gap + sz + 1 }, shadow, t + 1.f);
        // Color pass
        dl->AddLine({ cx - gap - sz,cy }, { cx - gap,cy }, col, t);
        dl->AddLine({ cx + gap,cy }, { cx + gap + sz,cy }, col, t);
        dl->AddLine({ cx,cy - gap - sz }, { cx,cy - gap }, col, t);
        dl->AddLine({ cx,cy + gap }, { cx,cy + gap + sz }, col, t);
        if (g_cfg.crosshair_dot)
            dl->AddCircleFilled({ cx, cy }, 2.f, col, 8);
    }

}