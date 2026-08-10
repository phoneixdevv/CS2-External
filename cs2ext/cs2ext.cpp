#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <chrono>
#include <thread>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>

#include "math/math.h"
#include "tools/offset_updater.h"
#include "mem/memory.h"
#include "offsets/offsets.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "cheats/Esp.h"
#include "cheats/config.h"
#include "cheats/render.h"
#include "cheats/features/aimbot.h"
#include "cheats/features/triggerbot.h"
#include "cheats/features/misc.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static int  g_fps = 0;
static int  g_frameCount = 0;
static auto g_lastFPS = std::chrono::high_resolution_clock::now();
static HWND g_hwnd = nullptr;

// Şık ve profesyonel hata kutusu yardımcı fonksiyonu
static void ShowErrorBox(const wchar_t* title, const wchar_t* message) {
    MessageBoxW(nullptr, message, title, MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(wnd, msg, wp, lp)) return 0L;
    switch (msg) {
    case WM_DESTROY:    PostQuitMessage(0); return 0L;
    case WM_SYSCOMMAND: if ((wp & 0xFFF0) == SC_KEYMENU) return 0L; break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HWND FindCS2Window() {
    HWND h = FindWindowW(L"Counter-Strike 2", nullptr);
    if (!h && mem::procID) {
        struct D { DWORD pid; HWND hwnd; } d = { mem::procID, nullptr };
        EnumWindows([](HWND w, LPARAM lp) -> BOOL {
            D* d2 = (D*)lp; DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
            if (pid == d2->pid && IsWindowVisible(w)) { d2->hwnd = w; return FALSE; }
            return TRUE;
            }, (LPARAM)&d);
        h = d.hwnd;
    }
    return h;
}

static void SyncOverlay(HWND overlay, HWND game) {
    if (!game) return;
    RECT r; if (!GetClientRect(game, &r)) return;
    POINT p = { 0,0 }; ClientToScreen(game, &p);
    int w = r.right - r.left, h = r.bottom - r.top;
    MoveWindow(overlay, p.x, p.y, w, h, TRUE);
    screenWidth = w;
    screenHeight = h;
}

static void Cleanup(IDXGISwapChain*& sc, ID3D11Device*& dev,
    ID3D11DeviceContext*& ctx, ID3D11RenderTargetView*& rtv,
    ID3D11BlendState*& bs, HWND wnd, WNDCLASSEXW& wc)
{
    timeEndPeriod(1);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (bs) { bs->Release(); bs = nullptr; }
    if (sc) { sc->Release(); sc = nullptr; }
    if (ctx) { ctx->Release(); ctx = nullptr; }
    if (dev) { dev->Release(); dev = nullptr; }
    if (rtv) { rtv->Release(); rtv = nullptr; }
    if (wnd) DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

static void DrawDebugInfoRight(const GameData& gd, size_t ec, int fps) {
    auto dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    char buf[256];
    snprintf(buf, sizeof(buf),
        "SiberdeyizCS2  |  ents:%zu  |  %dfps  |  [INS] menu  [DEL] exit",
        ec, fps);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    float rx = (float)screenWidth - ts.x - 14.f;
    float ry = 8.f;
    dl->AddRectFilled(ImVec2(rx - 4, ry - 2), ImVec2(rx + ts.x + 4, ry + ts.y + 4),
        IM_COL32(8, 8, 12, 220), 4.f);
    dl->AddRect(ImVec2(rx - 4, ry - 2), ImVec2(rx + ts.x + 4, ry + ts.y + 4),
        IM_COL32(200, 20, 40, 100), 4.f, 0, 1.f);
    dl->AddText(ImVec2(rx, ry), IM_COL32(220, 220, 230, 220), buf);
}

namespace menu {
    namespace {

        static bool  s_waitingForKey = false;
        static int* s_keyTarget = nullptr;
        static char  s_statusMsg[64] = {};
        static float s_statusTimer = 0.f;

        static std::string VkToName(int vk) {
            if (vk == 0)        return "NONE";
            if (vk == 0x01)     return "LMB";
            if (vk == 0x02)     return "RMB";
            if (vk == 0x04)     return "MMB";
            if (vk == 0x05)     return "X1";
            if (vk == 0x06)     return "X2";
            if (vk == 0x10)     return "SHIFT";
            if (vk == 0x11)     return "CTRL";
            if (vk == 0x12)     return "ALT";
            if (vk == VK_SPACE) return "SPACE";
            char buf[8];
            if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { buf[0] = (char)vk; buf[1] = 0; return buf; }
            snprintf(buf, sizeof(buf), "0x%02X", vk); return buf;
        }

        static void KeybindPicker(const char* label, int& key) {
            bool amTarget = (s_waitingForKey && s_keyTarget == &key);
            std::string btnLabel = amTarget
                ? "[ PRESS KEY... ]##" + std::string(label)
                : "[ " + VkToName(key) + " ]##" + std::string(label);

            ImGui::PushStyleColor(ImGuiCol_Button, amTarget ? ImVec4(0.65f, 0.08f, 0.12f, 1.f) : ImVec4(0.12f, 0.09f, 0.14f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.10f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.12f, 0.18f, 1.f));

            if (ImGui::Button(btnLabel.c_str(), ImVec2(130, 28))) {
                if (amTarget) { s_waitingForKey = false; s_keyTarget = nullptr; }
                else { s_waitingForKey = true; s_keyTarget = &key; }
            }
            ImGui::PopStyleColor(3);

            if (amTarget) {
                for (int vk = 0; vk < 256; ++vk) {
                    if (vk == VK_LBUTTON && !(GetAsyncKeyState(vk) & 0x8000)) continue;
                    if (GetAsyncKeyState(vk) & 0x8001) {
                        key = (vk == VK_ESCAPE) ? 0 : vk;
                        s_waitingForKey = false; s_keyTarget = nullptr; break;
                    }
                }
            }
            ImGui::SameLine(); ImGui::TextDisabled("%s", label);
        }

        static void SectionHeader(const char* txt) {
            ImGui::Spacing();
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  w = ImGui::GetContentRegionAvail().x;
            auto* dl = ImGui::GetWindowDrawList();

            dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + 24.f),
                IM_COL32(18, 12, 18, 240), IM_COL32(10, 8, 12, 0), IM_COL32(10, 8, 12, 0), IM_COL32(18, 12, 18, 240));
            dl->AddLine(ImVec2(p.x, p.y + 24.f), ImVec2(p.x + w, p.y + 24.f), IM_COL32(220, 20, 40, 60), 1.f);
            dl->AddRectFilled(ImVec2(p.x, p.y + 6.f), ImVec2(p.x + 3.f, p.y + 18.f), IM_COL32(230, 30, 50, 255), 1.5f);

            ImGui::SetCursorScreenPos(ImVec2(p.x + 10.f, p.y + 3.f));
            ImGui::TextColored(ImVec4(0.95f, 0.90f, 0.95f, 1.f), "%s", txt);
            ImGui::Dummy(ImVec2(0, 8.f));
        }

        static void GlowSeparator() {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), IM_COL32(220, 20, 40, 30), 1.f);
            ImGui::Dummy(ImVec2(0, 6.f));
        }

        static void TabESP() {
            ImGui::BeginChild("##esp_scroll", ImVec2(0, 0), false);
            SectionHeader("Player ESP");
            ImGui::Checkbox("Enable ESP", &g_cfg.esp_enabled); ImGui::SameLine(180);
            ImGui::Checkbox("Show Teammates", &g_cfg.show_teammates);
            GlowSeparator();
            ImGui::Columns(2, "espcols", false);
            ImGui::SetColumnWidth(0, 220.f);
            ImGui::Checkbox("Box ESP", &g_cfg.box_esp);
            if (g_cfg.box_esp) {
                static const char* kBoxStyles[] = { "Full Rect","Corner Box","3D Box" };
                ImGui::PushItemWidth(150.f);
                ImGui::Combo("Style##box", &g_cfg.box_style, kBoxStyles, 3);
                ImGui::PopItemWidth();
            }
            ImGui::Checkbox("Name", &g_cfg.name_esp);
            ImGui::Checkbox("Health Bar", &g_cfg.health_esp);
            ImGui::Checkbox("Armor Bar", &g_cfg.armor_esp);
            ImGui::Checkbox("Distance", &g_cfg.distance_esp);
            ImGui::NextColumn();
            ImGui::Checkbox("Skeleton", &g_cfg.skeleton_esp);
            ImGui::Checkbox("Snap Lines", &g_cfg.snaplines_esp);
            ImGui::Checkbox("Weapon Tag", &g_cfg.weapon_esp);
            ImGui::Checkbox("Head Dot", &g_cfg.head_dot);
            ImGui::Checkbox("Bomb ESP", &g_cfg.bomb_esp);
            ImGui::Checkbox("Economy $", &g_cfg.economy_esp);
            ImGui::Columns(1);
            GlowSeparator();

            SectionHeader("Radar");
            ImGui::Checkbox("Radar Hack", &g_cfg.radar_hack);
            ImGui::SameLine(180);
            ImGui::TextDisabled("Forces all enemies visible on minimap");
            GlowSeparator();

            SectionHeader("Overlays & Enhancements");
            ImGui::Checkbox("Spectator List", &g_cfg.spectator_list); ImGui::SameLine(180);
            ImGui::Checkbox("Rank", &g_cfg.rank_esp);
            GlowSeparator();
            SectionHeader("FOV Changer");
            ImGui::Checkbox("Enable FOV Override", &g_cfg.fov_changer);
            if (g_cfg.fov_changer) {
                ImGui::PushItemWidth(280.f);
                ImGui::SliderFloat("FOV", &g_cfg.custom_fov, 60.f, 130.f, "%.0f");
                ImGui::PopItemWidth();
            }
            ImGui::EndChild();
        }

        static void TabAimbot() {
            static const char* kBoneDisplayNames[] = { "Head","Neck","Chest","Pelvis" };
            static const int   kBoneIds[] = { BONE_HEAD, BONE_NECK, BONE_CHEST, BONE_PELVIS };
            static constexpr int kBoneOptionCount = 4;

            auto BoneIdToIdx = [](int id) -> int {
                for (int i = 0; i < kBoneOptionCount; ++i)
                    if (kBoneIds[i] == id) return i;
                return 0;
                };

            static const char* kSortModes[] = { "Pixel Distance (Crosshair)","Player Distance (3D World)","Hybrid" };
            ImGui::BeginChild("##aimbot_scroll", ImVec2(0, 0), false);
            SectionHeader("Legit Aimbot");
            ImGui::Checkbox("Enable Aimbot", &g_cfg.aimbot);
            ImGui::Checkbox("Always On", &g_cfg.aimbot_always_on);
            ImGui::SameLine(180); KeybindPicker("Aim Key", g_cfg.aimbot_key);

            ImGui::Checkbox("Visible Only", &g_cfg.aimbot_visible_only);
            ImGui::SameLine(180);
            ImGui::TextDisabled("Only target visible enemies");

            GlowSeparator();
            SectionHeader("Auto Shoot");
            ImGui::Checkbox("Auto Shoot (On Target)", &g_cfg.aimbot_auto_shoot);
            if (g_cfg.aimbot_auto_shoot) {
                ImGui::PushItemWidth(280.f);
                ImGui::SliderInt("Shoot Delay", &g_cfg.aimbot_auto_shoot_delay, 0, 500, "%d ms");
                ImGui::PopItemWidth();
            }
            GlowSeparator();
            SectionHeader("Target Priority");
            ImGui::PushItemWidth(260.f);
            ImGui::Combo("Sort Mode##aimbot", &g_cfg.aimbot_sort_mode, kSortModes, 3);
            ImGui::PopItemWidth();
            GlowSeparator();
            SectionHeader("FOV Gate");
            ImGui::Checkbox("Enable FOV Gate", &g_cfg.aimbot_fov_enabled);
            if (g_cfg.aimbot_fov_enabled) {
                ImGui::PushItemWidth(280.f);
                ImGui::SliderFloat("FOV Angle", &g_cfg.aimbot_fov, 0.5f, 180.f, "%.1f deg");
                ImGui::PopItemWidth();
                ImGui::Checkbox("Draw FOV Circle", &g_cfg.aimbot_fov_draw);
            }
            GlowSeparator();
            SectionHeader("Smoothing");
            ImGui::PushItemWidth(280.f);
            ImGui::SliderFloat("Smooth", &g_cfg.aimbot_smooth, 2.f, 12.f, "%.1f");
            ImGui::PopItemWidth();
            g_cfg.aimbot_smooth = std::clamp(g_cfg.aimbot_smooth, 2.f, 12.f);
            GlowSeparator();

            SectionHeader("Target Bone");
            ImGui::PushItemWidth(200.f);
            int curIdx = BoneIdToIdx(g_cfg.aimbot_target_bone);
            if (ImGui::Combo("##bone", &curIdx, kBoneDisplayNames, kBoneOptionCount))
                g_cfg.aimbot_target_bone = kBoneIds[curIdx];
            ImGui::PopItemWidth();
            ImGui::EndChild();
        }

        static void TabTriggerbot() {
            ImGui::BeginChild("##tb_scroll", ImVec2(0, 0), false);
            SectionHeader("Triggerbot");
            ImGui::Checkbox("Enable Triggerbot", &g_cfg.triggerbot);
            KeybindPicker("Trigger Key", g_cfg.triggerbot_key);
            GlowSeparator();
            ImGui::PushItemWidth(280.f);
            ImGui::SliderInt("Pre-Fire Delay##tb", &g_cfg.triggerbot_delay, 0, 300, "%d ms");
            ImGui::PopItemWidth();
            ImGui::Checkbox("Auto-Stop (mouse freeze on fire)", &g_cfg.triggerbot_auto_stop);
            ImGui::EndChild();
        }

        static void TabMisc() {
            ImGui::BeginChild("##misc_scroll", ImVec2(0, 0), false);
            SectionHeader("Movement");
            ImGui::Checkbox("Bunny Hop", &g_cfg.bunny_hop);
            ImGui::Checkbox("No Flash", &g_cfg.no_flash);
            SectionHeader("Custom Crosshair");
            ImGui::Checkbox("Enable Crosshair", &g_cfg.custom_crosshair);
            if (g_cfg.custom_crosshair) {
                ImGui::Checkbox("Center Dot", &g_cfg.crosshair_dot);
                ImGui::PushItemWidth(200.f);
                ImGui::SliderInt("Size", &g_cfg.crosshair_size, 1, 20);
                ImGui::SliderInt("Gap", &g_cfg.crosshair_gap, 0, 20);
                ImGui::PopItemWidth();
            }
            ImGui::EndChild();
        }

        static void TabColors() {
            auto CE = [](const char* label, float* col) {
                ImGui::ColorEdit4(label, col,
                    ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueBar);
                };
            ImGui::BeginChild("##colors_scroll", ImVec2(0, 0), false);
            SectionHeader("ESP — Enemies");
            ImGui::Columns(2, "colA", false);
            CE("Enemy Box", g_cfg.box_color); CE("Team Box", g_cfg.team_box_color);
            CE("Skeleton", g_cfg.skeleton_color); CE("Snap Lines", g_cfg.snapline_color);
            ImGui::NextColumn();
            CE("Name", g_cfg.name_color); CE("Distance", g_cfg.distance_color);
            CE("Head Dot", g_cfg.head_dot_color);
            ImGui::Columns(1);
            SectionHeader("Bars");
            ImGui::Columns(2, "colB", false);
            CE("Health Bar", g_cfg.health_bar_color); ImGui::NextColumn();
            CE("Armor Bar", g_cfg.armor_bar_color);
            ImGui::Columns(1);
            SectionHeader("HUD & Aimbot");
            ImGui::Columns(2, "colE", false);
            CE("Crosshair", g_cfg.crosshair_color); ImGui::NextColumn();
            CE("FOV Circle", g_cfg.fov_circle_color);
            ImGui::Columns(1);
            ImGui::EndChild();
        }

        static void TabConfig() {
            ImGui::BeginChild("##cfg_scroll", ImVec2(0, 0), false);
            SectionHeader("Configuration");
            if (ImGui::Button("Save Config", ImVec2(160, 36))) {
                SaveConfig();
                snprintf(s_statusMsg, sizeof(s_statusMsg), "Configuration saved successfully.");
                s_statusTimer = 2.5f;
            }
            ImGui::SameLine(180.f);
            if (ImGui::Button("Load Config", ImVec2(160, 36))) {
                bool ok = LoadConfig();
                snprintf(s_statusMsg, sizeof(s_statusMsg), ok ? "Configuration loaded successfully." : "Load failed: File not found.");
                s_statusTimer = 2.5f;
            }
            if (s_statusTimer > 0.f) {
                s_statusTimer -= ImGui::GetIO().DeltaTime;
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, std::min(1.f, s_statusTimer)), "%s", s_statusMsg);
            }
            GlowSeparator();
            ImGui::TextDisabled("Config file path: cs2ext_config.bin (directory of executable)");
            ImGui::EndChild();
        }

    }

    void InitStyle() {
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding = 8.f;
        st.ChildRounding = 6.f;
        st.FrameRounding = 4.f;
        st.GrabRounding = 4.f;
        st.TabRounding = 4.f;
        st.ScrollbarRounding = 4.f;
        st.PopupRounding = 6.f;
        st.WindowBorderSize = 1.0f;
        st.FrameBorderSize = 0.f;
        st.ItemSpacing = ImVec2(10.f, 8.f);
        st.FramePadding = ImVec2(8.f, 6.f);
        st.WindowPadding = ImVec2(14.f, 14.f);
        st.ScrollbarSize = 6.f;
        st.GrabMinSize = 14.f;
        st.TabBarBorderSize = 0.f;

        auto* c = st.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.05f, 0.07f, 0.98f);
        c[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.06f, 0.09f, 0.50f);
        c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.06f, 0.10f, 0.98f);
        c[ImGuiCol_Border] = ImVec4(0.25f, 0.08f, 0.12f, 0.70f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.07f, 0.11f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.05f, 0.07f, 0.90f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.04f, 0.06f, 0.50f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.08f, 0.12f, 0.80f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.10f, 0.16f, 0.90f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.65f, 0.12f, 0.20f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.10f, 0.18f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.90f, 0.15f, 0.25f, 1.00f);
        c[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.08f, 0.13f, 0.90f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.10f, 0.18f, 0.95f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.12f, 0.22f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.20f, 0.30f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.14f, 0.09f, 0.16f, 0.90f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.11f, 0.18f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.14f, 0.22f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.20f, 0.08f, 0.14f, 0.70f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.10f, 0.18f, 0.85f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.12f, 0.22f, 1.00f);
        c[ImGuiCol_Tab] = ImVec4(0.10f, 0.07f, 0.12f, 0.80f);
        c[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.09f, 0.16f, 1.00f);
        c[ImGuiCol_TabActive] = ImVec4(0.35f, 0.10f, 0.18f, 1.00f);
        c[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.05f, 0.08f, 0.80f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.07f, 0.12f, 1.00f);
        c[ImGuiCol_Separator] = ImVec4(0.25f, 0.08f, 0.12f, 0.40f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.10f, 0.16f, 0.70f);
        c[ImGuiCol_SeparatorActive] = ImVec4(0.65f, 0.12f, 0.20f, 1.00f);
        c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.40f, 0.48f, 1.00f);
        c[ImGuiCol_NavHighlight] = ImVec4(0.80f, 0.15f, 0.25f, 1.00f);
        c[ImGuiCol_DragDropTarget] = ImVec4(0.90f, 0.20f, 0.30f, 0.90f);
    }

    void Draw() {
        if (!g_cfg.menu_opened) return;

        static constexpr float DEFAULT_W = 1000.f;
        static constexpr float DEFAULT_H = 720.f;

        ImGui::SetNextWindowSizeConstraints(ImVec2(700.f, 500.f), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowPos(
            ImVec2((float)(screenWidth - DEFAULT_W) * 0.5f, (float)(screenHeight - DEFAULT_H) * 0.5f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(DEFAULT_W, DEFAULT_H), ImGuiCond_FirstUseEver);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

        static ImVec2 s_prevPos = ImVec2(-1.f, -1.f);
        static ImVec2 s_prevSize = ImVec2(DEFAULT_W, DEFAULT_H);

        if (s_prevPos.x >= 0.f) {
            if (auto* bg = ImGui::GetBackgroundDrawList()) {
                ImVec2 br = ImVec2(s_prevPos.x + s_prevSize.x, s_prevPos.y + s_prevSize.y);
                bg->AddRect(ImVec2(s_prevPos.x - 4, s_prevPos.y - 4), ImVec2(br.x + 4, br.y + 4),
                    IM_COL32(220, 20, 40, 40), 10.f, 0, 3.f);
                bg->AddRect(ImVec2(s_prevPos.x - 1, s_prevPos.y - 1), ImVec2(br.x + 1, br.y + 1),
                    IM_COL32(180, 15, 30, 100), 9.f, 0, 1.f);
            }
        }

        bool open = true;
        ImGui::Begin("##cs2ext_menu", &open, flags);
        s_prevPos = ImGui::GetWindowPos();
        s_prevSize = ImGui::GetWindowSize();
        float tw = s_prevSize.x;
        float th = s_prevSize.y;

        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = s_prevPos;

            dl->AddRectFilled(p0, ImVec2(p0.x + tw, p0.y + th), IM_COL32(14, 11, 17, 255), 8.0f);
            dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + tw, p0.y + 60.f),
                IM_COL32(28, 12, 18, 240), IM_COL32(28, 12, 18, 240),
                IM_COL32(14, 11, 17, 0), IM_COL32(14, 11, 17, 0));


            float s = 0.065f;
            ImVec2 lp = ImVec2(p0.x + 16.f, p0.y + 8.f);

            ImVec2 r1[] = {
                {lp.x + 169 * s,lp.y + 244 * s},{lp.x + 169 * s,lp.y + 310 * s},{lp.x + 318 * s,lp.y + 387 * s},
                {lp.x + 328 * s,lp.y + 394 * s},{lp.x + 209 * s,lp.y + 454 * s},{lp.x + 192 * s,lp.y + 465 * s},
                {lp.x + 250 * s,lp.y + 499 * s},{lp.x + 261 * s,lp.y + 501 * s},{lp.x + 421 * s,lp.y + 419 * s},
                {lp.x + 430 * s,lp.y + 412 * s},{lp.x + 430 * s,lp.y + 376 * s},{lp.x + 426 * s,lp.y + 372 * s},
                {lp.x + 214 * s,lp.y + 264 * s}
            };
            dl->AddConvexPolyFilled(r1, 13, IM_COL32(140, 15, 25, 255));

            ImVec2 r2[] = {
                {lp.x + 170 * s,lp.y + 188 * s},{lp.x + 171 * s,lp.y + 229 * s},{lp.x + 419 * s,lp.y + 354 * s},
                {lp.x + 430 * s,lp.y + 357 * s},{lp.x + 430 * s,lp.y + 292 * s},{lp.x + 270 * s,lp.y + 208 * s},
                {lp.x + 275 * s,lp.y + 203 * s},{lp.x + 407 * s,lp.y + 138 * s},{lp.x + 395 * s,lp.y + 129 * s},
                {lp.x + 353 * s,lp.y + 105 * s},{lp.x + 340 * s,lp.y + 100 * s}
            };
            dl->AddConvexPolyFilled(r2, 11, IM_COL32(235, 35, 45, 255));
            dl->AddPolyline(r2, 11, IM_COL32(255, 100, 100, 120), 0, 1.0f);

            float logoRight = lp.x + 430 * s + 12.f;
            dl->AddText(ImGui::GetFont(), 20.f, ImVec2(logoRight, p0.y + 10.f),
                IM_COL32(245, 245, 250, 255), "SiberdeyizCS2");
            dl->AddText(ImGui::GetFont(), 11.f, ImVec2(logoRight, p0.y + 34.f),
                IM_COL32(180, 50, 70, 220), "made by phoneix");

            const char* hint = "[INS] Hide Menu   [DEL] Unload";
            ImVec2 hs = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(p0.x + tw - hs.x - 16.f, p0.y + 20.f), IM_COL32(120, 110, 125, 200), hint);

            if (ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + tw, p0.y + 45.f)) && ImGui::IsMouseDown(0)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                ImVec2 pos = ImGui::GetWindowPos();
                ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
            }
        }

        ImGui::SetCursorPosY(72.f);

        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8.f, 6.f));
        if (ImGui::BeginTabBar("##maintabs")) {
            if (ImGui::BeginTabItem("  ESP  ")) { TabESP();        ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("  AIMBOT  ")) { TabAimbot();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("  TRIGGER  ")) { TabTriggerbot(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("  MISC  ")) { TabMisc();       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("  COLORS  ")) { TabColors();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("  CONFIG  ")) { TabConfig();     ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar(2);

        if (!open) g_cfg.menu_opened = false;
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

INT APIENTRY WinMain(HINSTANCE instance, HINSTANCE, PSTR, INT cmd_show) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"SiberdeyizCS2";
    if (!RegisterClassExW(&wc)) {
        ShowErrorBox(L"SiberdeyizCS2 - Error", L"Window registration failed.");
        return 0;
    }

    HWND window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"SiberdeyizCS2 overlay", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) {
        ShowErrorBox(L"SiberdeyizCS2 - Error", L"Window creation failed.");
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 0;
    }
    g_hwnd = window;

    SetLayeredWindowAttributes(window, (COLORREF)0, BYTE(255), LWA_ALPHA);

    RECT client_area{}, window_area{};
    GetClientRect(window, &client_area);
    GetWindowRect(window, &window_area);
    POINT diff{}; ClientToScreen(window, &diff);
    const MARGINS margins{
        window_area.left + (diff.x - window_area.left),
        window_area.top + (diff.y - window_area.top),
        client_area.right, client_area.bottom
    };
    DwmExtendFrameIntoClientArea(window, &margins);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc.Count = 1U;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2U;
    sd.OutputWindow = window;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    constexpr D3D_FEATURE_LEVEL lvls[2]{ D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11BlendState* blend_state = nullptr;
    D3D_FEATURE_LEVEL feat_level;

    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        lvls, 2, D3D11_SDK_VERSION, &sd, &swap_chain, &device, &feat_level, &device_context)))
    {
        ShowErrorBox(L"SiberdeyizCS2 - DirectX Error", L"D3D11 device and swap chain creation failed.");
        Cleanup(swap_chain, device, device_context, rtv, blend_state, window, wc);
        return 0;
    }

    ID3D11Texture2D* bb = nullptr;
    swap_chain->GetBuffer(0, IID_PPV_ARGS(&bb));
    device->CreateRenderTargetView(bb, nullptr, &rtv);
    bb->Release();

    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&bd, &blend_state);
    }

    ShowWindow(window, cmd_show);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    io.Fonts->Clear();
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 14.f))
        io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device, device_context);
    menu::InitStyle();
    LoadConfig();

    DWORD pid = mem::GetProcID(L"cs2.exe");
    if (!pid) {
        ShowErrorBox(L"SiberdeyizCS2 - Warning", L"Counter-Strike 2 (cs2.exe) process not found. Please launch the game first.");
        Cleanup(swap_chain, device, device_context, rtv, blend_state, window, wc);
        return 1;
    }
    uintptr_t client = mem::GetModuleBase(pid, L"client.dll");
    if (!client) {
        ShowErrorBox(L"SiberdeyizCS2 - Warning", L"client.dll could not be located in cs2.exe.");
        Cleanup(swap_chain, device, device_context, rtv, blend_state, window, wc);
        return 1;
    }

    OffsetChecker_RunStartup();
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    GameData gd;
    gd.client = client; gd.pid = pid;

    EntityCache entity_cache;
    entity_cache.Start(client, &gd);

    HWND gameWin = FindCS2Window();
    if (gameWin) SyncOverlay(window, gameWin);

    if (g_cfg.aimbot_target_bone == 0)
        g_cfg.aimbot_target_bone = BONE_HEAD;

    bool running = true;
    bool ins_pressed = false;
    bool del_pressed = false;
    auto lastOverlaySync = std::chrono::steady_clock::now();

    constexpr float cc[4]{ 0.f, 0.f, 0.f, 0.f };
    constexpr float bf[4]{ 0.f, 0.f, 0.f, 0.f };

    uintptr_t lastLocalPawn = 0;

    while (running) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
            if (!ins_pressed) {
                ins_pressed = true;
                g_cfg.menu_opened = !g_cfg.menu_opened;
                LONG_PTR ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
                if (g_cfg.menu_opened) {
                    ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
                    SetWindowLongPtrW(window, GWL_EXSTYLE, ex);
                    SetForegroundWindow(window);
                }
                else {
                    ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
                    SetWindowLongPtrW(window, GWL_EXSTYLE, ex);
                }
            }
        }
        else { ins_pressed = false; }

        if (GetAsyncKeyState(VK_DELETE) & 0x8000) {
            if (!del_pressed) { del_pressed = true; running = false; break; }
        }
        else { del_pressed = false; }

        auto nowT = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            nowT - lastOverlaySync).count() > 500) {
            lastOverlaySync = nowT;
            HWND gw = FindCS2Window();
            if (gw) SyncOverlay(window, gw);
        }

        {
            std::lock_guard<std::mutex> lk(gd.mtx);
            gd.localPlayerPawn = mem::ReadMem<uintptr_t>(client + dwLocalPlayerPawn);
            gd.localPlayerController = mem::ReadMem<uintptr_t>(client + dwLocalPlayerController);
            gd.entList = mem::ReadMem<uintptr_t>(client + dwEntityList);
            gd.view_matrix = mem::ReadMem<view_matrix_t>(client + dwViewMatrix);
            gd.isInGame = (gd.localPlayerPawn != 0 && gd.entList != 0);
            if (gd.localPlayerPawn)
                gd.localTeam = mem::ReadMem<int>(gd.localPlayerPawn + m_iTeamNum);
        }

        if (gd.localPlayerPawn != lastLocalPawn) {
            lastLocalPawn = gd.localPlayerPawn;
        }

        static auto lastSlot = std::chrono::steady_clock::now();
        if (gd.localPlayerController && gd.entList) {
            if (gd.localPlayerIndex == 0 ||
                std::chrono::duration_cast<std::chrono::seconds>(nowT - lastSlot).count() >= 2)
            {
                lastSlot = nowT;
                for (int si = 1; si < 65; ++si) {
                    uintptr_t sle = mem::ReadMem<uintptr_t>(gd.entList + 0x8 * ((si & 0x7FFF) >> 9) + 0x10);
                    if (!sle) continue;
                    uintptr_t sc = mem::ReadMem<uintptr_t>(sle + 0x70 * (si & 0x1FF));
                    if (sc == gd.localPlayerController) { gd.localPlayerIndex = si; break; }
                }
            }
        }

        ++g_frameCount;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            frameStart - g_lastFPS).count() >= 1000) {
            g_fps = g_frameCount; g_frameCount = 0; g_lastFPS = frameStart;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        auto entities = entity_cache.GetEntities();
        auto bomb = entity_cache.GetBombData();
        auto spectators = entity_cache.GetSpectators();

        if (OffsetChecker_DrawStartupOverlay()) {
            ImGui::Render();
            device_context->OMSetRenderTargets(1, &rtv, nullptr);
            device_context->OMSetBlendState(blend_state, bf, 0xFFFFFFFF);
            device_context->ClearRenderTargetView(rtv, cc);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            swap_chain->Present(0, 0);
            continue;
        }

        DrawDebugInfoRight(gd, entities.size(), g_fps);
        render::DrawESP(entities, bomb, spectators, gd);

        if (gd.isInGame) {
            features::Aimbot(entities, gd);
            features::Triggerbot(entities, gd);
            features::BunnyHop(gd);
            features::NoFlash(gd);
        }

        features::CustomCrosshair();
        features::DrawBombTimerOverlay(bomb);
        features::DrawSpectatorList(spectators);

        menu::Draw();

        ImGui::Render();
        device_context->OMSetRenderTargets(1, &rtv, nullptr);
        device_context->OMSetBlendState(blend_state, bf, 0xFFFFFFFF);
        device_context->ClearRenderTargetView(rtv, cc); 
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_chain->Present(0, 0);
    }

    features::TriggerbotFlush();
    entity_cache.Stop();
    Cleanup(swap_chain, device, device_context, rtv, blend_state, window, wc);
    return 0;
}