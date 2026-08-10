
#define NOMINMAX
#include "misc.h"
#include "../render.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace features {

void BunnyHop(const GameData& gd) {
    if (!g_cfg.bunny_hop || !gd.localPlayerPawn || !gd.client) return;
    bool held = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (!held) return;
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
    float cx  = (float)screenWidth  * 0.5f;
    float cy  = (float)screenHeight * 0.5f;
    float sz  = (float)g_cfg.crosshair_size;
    float gap = (float)g_cfg.crosshair_gap;
    ImU32 col = IM_COL32(
        (int)(g_cfg.crosshair_color[0]*255), (int)(g_cfg.crosshair_color[1]*255),
        (int)(g_cfg.crosshair_color[2]*255), (int)(g_cfg.crosshair_color[3]*255));
    ImU32 shadow = IM_COL32(0, 0, 0, 160);
    float t = 1.8f;
    // Shadow pass
    dl->AddLine(ImVec2(cx-gap-sz+1, cy+1), ImVec2(cx-gap+1,    cy+1),    shadow, t+1.f);
    dl->AddLine(ImVec2(cx+gap+1,    cy+1), ImVec2(cx+gap+sz+1, cy+1),    shadow, t+1.f);
    dl->AddLine(ImVec2(cx+1, cy-gap-sz+1), ImVec2(cx+1,        cy-gap+1),shadow, t+1.f);
    dl->AddLine(ImVec2(cx+1, cy+gap+1),    ImVec2(cx+1,        cy+gap+sz+1),shadow,t+1.f);
    // Color pass
    dl->AddLine(ImVec2(cx-gap-sz, cy), ImVec2(cx-gap,    cy),    col, t);
    dl->AddLine(ImVec2(cx+gap,    cy), ImVec2(cx+gap+sz, cy),    col, t);
    dl->AddLine(ImVec2(cx, cy-gap-sz), ImVec2(cx,        cy-gap),col, t);
    dl->AddLine(ImVec2(cx, cy+gap),    ImVec2(cx,        cy+gap+sz), col, t);
    if (g_cfg.crosshair_dot)
        dl->AddCircleFilled(ImVec2(cx, cy), 2.f, col, 8);
}

void DrawBombTimerOverlay(const BombData& bomb) {
    if (!g_cfg.bomb_esp) return;
    auto dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    if (!bomb.isValid || !bomb.isPlanted) return;

    static constexpr float PW = 220.f;
    static constexpr float PH = 66.f;
    float defuseH = (bomb.defuseTimer > 0.f) ? 26.f : 0.f;
    float ph      = PH + defuseH;
    float px      = (float)screenWidth  - PW - 10.f;
    float py      = (float)screenHeight - ph - 10.f;

    dl->AddRectFilled(ImVec2(px-1, py-1), ImVec2(px+PW+1, py+ph+1), IM_COL32(0,0,0,0), 6.f);
    dl->AddRectFilled(ImVec2(px, py),     ImVec2(px+PW, py+ph),      IM_COL32(14,14,22,220), 6.f);

    float t_now   = bomb.timer;
    bool  critical = (t_now > 0.f && t_now < 5.f);
    float pulse   = critical ? (sinf((float)GetTickCount() / 150.f) + 1.f) * 0.5f : 1.f;
    ImU32 borderCol = critical
        ? IM_COL32(255, (int)(30*pulse), (int)(30*pulse), 255)
        : (bomb.isDefused ? IM_COL32(30, 220, 80, 255) : IM_COL32(220, 80, 30, 200));
    dl->AddRect(ImVec2(px, py), ImVec2(px+PW, py+ph), borderCol, 6.f, 0, 1.5f);

    const char* title = bomb.isDefused ? "BOMB DEFUSED" : "BOMB PLANTED";
    ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(px + PW*0.5f - ts.x*0.5f, py + 6.f),
        bomb.isDefused ? IM_COL32(50,220,80,255) : IM_COL32(220,80,30,255), title);

    float barX = px + 10.f, barY = py + 24.f, barW = PW - 20.f, barH = 10.f;
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX+barW, barY+barH), IM_COL32(30,30,40,255), 4.f);

    if (bomb.isTicking && t_now > 0.f) {
        static constexpr float kFuseSec = 40.f;
        float frac = std::clamp(t_now / kFuseSec, 0.f, 1.f);
        ImU32 fillCol = critical
            ? IM_COL32(255, (int)(30 + 80*frac), 0, 255)
            : IM_COL32((int)(220 - 180*frac), (int)(80 + 120*frac), 0, 255);
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW*frac, barY+barH), fillCol, 4.f);

        char tbuf[16]; snprintf(tbuf, sizeof(tbuf), "%.1fs", t_now);
        ImVec2 bts = ImGui::CalcTextSize(tbuf);
        dl->AddText(ImVec2(px + PW*0.5f - bts.x*0.5f, barY + 14.f),
            critical ? IM_COL32(255, 80, 80, 255) : IM_COL32(200, 200, 200, 255), tbuf);
    }

    if (bomb.defuseTimer > 0.f) {
        float dy2 = py + PH + 2.f;
        dl->AddRectFilled(ImVec2(barX, dy2), ImVec2(barX+barW, dy2+barH), IM_COL32(30,30,40,255), 4.f);
        float dfrac = std::clamp(bomb.defuseTimer / 10.f, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(barX, dy2), ImVec2(barX + barW*dfrac, dy2+barH),
            IM_COL32(30, 160, 255, 255), 4.f);
        char dbuf[24]; snprintf(dbuf, sizeof(dbuf), "DEFUSE %.1fs", bomb.defuseTimer);
        ImVec2 dts = ImGui::CalcTextSize(dbuf);
        dl->AddText(ImVec2(px + PW*0.5f - dts.x*0.5f, dy2 + 12.f),
            IM_COL32(80, 180, 255, 255), dbuf);
    }
}

void DrawSpectatorList(const std::vector<std::string>& specs) {
    if (!g_cfg.spectator_list || specs.empty()) return;
    auto dl = ImGui::GetBackgroundDrawList(); if (!dl) return;

    static constexpr float PILL_H = 18.f;
    static constexpr float PILL_W = 170.f;
    float sy = 54.f, sx = 10.f;

    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx+PILL_W, sy+PILL_H), IM_COL32(14,14,22,220), 4.f);
    dl->AddRect(      ImVec2(sx, sy), ImVec2(sx+PILL_W, sy+PILL_H), IM_COL32(200,60,60,180), 4.f, 0, 1.f);
    dl->AddText(ImVec2(sx+6.f, sy+2.f), IM_COL32(255, 90, 90, 255), "SPECTATORS");
    sy += PILL_H + 2.f;

    for (const auto& name : specs) {
        dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx+PILL_W, sy+PILL_H), IM_COL32(20,20,32,200), 3.f);
        dl->AddRect(      ImVec2(sx, sy), ImVec2(sx+PILL_W, sy+PILL_H), IM_COL32(80,80,120,120), 3.f, 0, 0.8f);
        std::string dn = name.size() > 20 ? name.substr(0, 18) + ".." : name;
        dl->AddText(ImVec2(sx+6.f, sy+2.f), IM_COL32(220, 200, 255, 255), dn.c_str());
        sy += PILL_H + 2.f;
    }
}

} 
