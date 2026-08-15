#pragma once
#include <Windows.h>
#include <stdio.h>
#include <string>
#include <algorithm>

struct Config {
    // Menu
    bool menu_opened = true;

    // ESP
    bool esp_enabled = true;
    bool box_esp = true;
    int  box_style = 0;      // 0=Full, 1=Corner, 2=3D Box
    bool name_esp = true;
    bool health_esp = true;
    bool armor_esp = true;
    bool skeleton_esp = true;
    bool snaplines_esp = false;
    bool distance_esp = true;
    bool weapon_esp = true;
    bool head_dot = true;
    bool show_teammates = false;

    // Bar orientation  0=Vertical (left side), 1=Horizontal (bottom)
    int  health_bar_style = 0;
    int  armor_bar_style = 0;

    // FOV Changer
    bool  fov_changer = false;
    float custom_fov = 90.0f;

    // Aimbot (legit only)
    bool  aimbot = false;
    bool  aimbot_always_on = false;
    int   aimbot_key = 0x02;   // RMB
    bool  aimbot_fov_enabled = true;
    float aimbot_fov = 8.0f;
    float aimbot_smooth = 3.0f;
    int   aimbot_target_bone = 7;      // BONE_HEAD
    bool  aimbot_fov_draw = true;
    int   aimbot_sort_mode = 0;      // 0=Pixel dist, 1=Player dist, 2=Hybrid
    bool  aimbot_auto_shoot = false;
    int   aimbot_auto_shoot_delay = 200;    // ms
    int   aimbot_shot_cooldown = 150;    // ms between auto-shots
    bool  aimbot_visible_only = false;

    // Triggerbot
    bool triggerbot = false;
    bool triggerbot_always_on = false;      // ← ADD THIS
    int  triggerbot_key = 0x10;    // SHIFT
    int  triggerbot_delay = 10;
    bool triggerbot_auto_stop = true;
    int  triggerbot_shot_cooldown = 100; // ms between trigger shots

    // Player Mods
    bool no_flash = false;
    bool bunny_hop = false;

    // Radar Hack
    bool radar_hack = false;

    // Custom Crosshair
    bool custom_crosshair = false;
    bool crosshair_dot = true;
    int  crosshair_size = 6;
    int  crosshair_gap = 4;

    // Info overlays
    bool spectator_list = false;
    bool economy_esp = false;
    bool rank_esp = false;

    // Colors (RGBA 0-1)
    float box_color[4] = { 0.85f, 0.10f, 0.10f, 1.0f };
    float team_box_color[4] = { 0.20f, 0.40f, 1.00f, 1.0f };
    float skeleton_color[4] = { 0.80f, 0.80f, 0.80f, 1.0f };
    float team_skeleton_color[4] = { 0.20f, 0.60f, 1.00f, 1.0f };
    float snapline_color[4] = { 0.90f, 0.80f, 0.20f, 0.7f };
    float team_snapline_color[4] = { 0.20f, 0.40f, 1.00f, 0.7f };
    float name_color[4] = { 1.00f, 1.00f, 1.00f, 1.0f };
    float team_name_color[4] = { 0.50f, 0.80f, 1.00f, 1.0f };
    float distance_color[4] = { 0.90f, 0.90f, 0.00f, 1.0f };
    float head_dot_color[4] = { 1.00f, 0.00f, 0.00f, 1.0f };
    float team_head_dot_color[4] = { 0.20f, 0.40f, 1.00f, 1.0f };
    float crosshair_color[4] = { 0.00f, 1.00f, 0.00f, 1.0f };
    float fov_circle_color[4] = { 0.85f, 0.15f, 0.15f, 0.40f };
    float health_bar_color[4] = { 0.10f, 0.85f, 0.10f, 1.00f };
    float armor_bar_color[4] = { 0.30f, 0.60f, 1.00f, 1.00f };
    float glow_vis_color[4] = { 0.00f, 1.00f, 0.30f, 0.80f };
    float glow_hid_color[4] = { 1.00f, 0.20f, 0.00f, 0.80f };

    //performance mode
	bool performance_mode = false;
};

inline Config g_cfg;

// Bump magic so old configs don't silently load with wrong layout
static constexpr uint32_t CFG_MAGIC = 0xC52E2031u;
static constexpr uint32_t CFG_VERSION = sizeof(Config);

inline std::string GetConfigPath() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path = exePath;
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) path = path.substr(0, pos + 1);
    return path + "cs2ext_config.bin";
}

inline void SaveConfig() {
    std::string path = GetConfigPath();
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "wb");
    if (!f) return;
    fwrite(&CFG_MAGIC, sizeof(CFG_MAGIC), 1, f);
    fwrite(&CFG_VERSION, sizeof(CFG_VERSION), 1, f);
    fwrite(&g_cfg, sizeof(Config), 1, f);
    fclose(f);
}

inline bool LoadConfig() {
    std::string path = GetConfigPath();
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "rb");
    if (!f) return false;
    uint32_t magic = 0, ver = 0;
    fread(&magic, sizeof(magic), 1, f);
    fread(&ver, sizeof(ver), 1, f);
    bool ok = false;
    if (magic == CFG_MAGIC && ver == CFG_VERSION) {
        fread(&g_cfg, sizeof(Config), 1, f);
        ok = true;
    }
    fclose(f);
    return ok;
}