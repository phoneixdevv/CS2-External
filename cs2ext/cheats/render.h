#pragma once

struct Color4;
typedef Color4 RGB;

#include <windows.h>
#ifdef RGB
#undef RGB
#endif

#include "../imgui/imgui.h"
#include "Esp.h"

extern int screenWidth;
extern int screenHeight;

struct ViewAngles { float pitch, yaw, roll; };

static inline float NormYaw(float y) {
    while (y >  180.f) y -= 360.f;
    while (y < -180.f) y += 360.f;
    return y;
}
static inline float YawDelta(float target, float current) {
    return NormYaw(target - current);
}

namespace render {

    static inline ImDrawList* DL() { return ImGui::GetBackgroundDrawList(); }

    inline void DrawRect(int x, int y, int w, int h, RGB c, float t) {
        if (auto dl = DL()) dl->AddRect(
            ImVec2((float)x, (float)y), ImVec2((float)(x+w), (float)(y+h)),
            IM_COL32(c.R, c.G, c.B, c.A), 0, 0, t);
    }
    inline void DrawFilledRect(int x, int y, int w, int h, RGB c) {
        if (auto dl = DL()) dl->AddRectFilled(
            ImVec2((float)x, (float)y), ImVec2((float)(x+w), (float)(y+h)),
            IM_COL32(c.R, c.G, c.B, c.A));
    }
    inline void DrawLine(float x1, float y1, float x2, float y2, RGB c, float t) {
        if (auto dl = DL()) dl->AddLine(
            ImVec2(x1,y1), ImVec2(x2,y2), IM_COL32(c.R, c.G, c.B, c.A), t);
    }
    inline void DrawText(int x, int y, RGB c, const char* txt) {
        if (auto dl = DL()) dl->AddText(
            ImVec2((float)x, (float)y), IM_COL32(c.R, c.G, c.B, c.A), txt);
    }
    inline void DrawCircle(int x, int y, int r, RGB c, float t, int s = 32) {
        if (auto dl = DL()) dl->AddCircle(
            ImVec2((float)x, (float)y), (float)r, IM_COL32(c.R, c.G, c.B, c.A), s, t);
    }
    inline void DrawFilledCircle(int x, int y, int r, RGB c, int s = 12) {
        if (auto dl = DL()) dl->AddCircleFilled(
            ImVec2((float)x, (float)y), (float)r, IM_COL32(c.R, c.G, c.B, c.A), s);
    }
    inline void DrawTextBackground(int x, int y, RGB c, RGB bg, const char* txt) {
        auto dl = DL(); if (!dl) return;
        ImVec2 ts = ImGui::CalcTextSize(txt);
        dl->AddRectFilled(
            ImVec2((float)(x-2), (float)(y-2)),
            ImVec2((float)(x+(int)ts.x+2), (float)(y+(int)ts.y+2)),
            IM_COL32(bg.R, bg.G, bg.B, 190));
        dl->AddText(ImVec2((float)x, (float)y), IM_COL32(c.R, c.G, c.B, c.A), txt);
    }
    inline void DrawDebugInfo(const GameData& gd, size_t ec, int fps) {
        char buf[256];
        snprintf(buf, sizeof(buf), "SiberdeyizCS2  |  ents:%zu  |  %dfps  |  INS:menu  DEL:exit", ec, fps);
        auto dl = DL(); if (!dl) return;
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddRectFilled(ImVec2(8.f, 6.f), ImVec2(14.f + ts.x, 6.f + ts.y + 4.f), IM_COL32(0,0,0,160), 3.f);
        dl->AddText(ImVec2(10.f, 8.f), IM_COL32(80, 170, 240, 200), buf);
    }

    void DrawESP(const std::vector<EntityData>& entities,
                 const BombData& bomb,
                 const std::vector<std::string>& spectators,
                 const GameData& gd);

}
