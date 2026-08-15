#define NOMINMAX
#include "Esp.h"
#include "render.h"
#include "../imgui/imgui.h"
#include <cmath>
#include <algorithm>
#include <memory>

//  Health gradient LUT 
static struct { RGB c[101]; } hgrad;

static struct HGI {
    HGI() {
        for (int i = 0; i <= 100; i++) {
            float p = i / 100.f;

            hgrad.c[i] = {
                (int)(255 * (1.f - p)), // R: 255 -> 0
                (int)(255 * p),         // G: 0 -> 255
                0,
                255
            };
        }
    }
} hgInit;

bool isScoped = false;

// 
//  EntityCache
// 
EntityCache::EntityCache(UINT, UINT, int)
    : should_run(false), client_dll(0), g_data(nullptr) {
    update_interval_ms = 8;
}
EntityCache::~EntityCache() { Stop(); }

void EntityCache::Start(uintptr_t client, const GameData* gd) {
    if (should_run) return;
    client_dll = client; g_data = gd; should_run = true;
    worker_thread = std::thread(&EntityCache::WorkerLoop, this);
}

void EntityCache::Stop() {
    should_run = false;
    if (worker_thread.joinable()) worker_thread.join();
    std::lock_guard<std::mutex> l(mtx);
    cached_entities.reset();
    cached_spectators.reset();
}

//  Worker loop 
void EntityCache::WorkerLoop() {
    while (should_run) {
        if (client_dll && g_data && g_data->IsValid()) {
            auto ne = std::make_shared<std::vector<EntityData>>(ReadEntities());
            auto ns = g_cfg.spectator_list
                ? std::make_shared<std::vector<SpectatorEntry>>(ReadSpectators())
                : std::make_shared<std::vector<SpectatorEntry>>();

            {
                std::lock_guard<std::mutex> l(mtx);
                cached_entities = std::move(ne);
                cached_spectators = std::move(ns);
            }

            if (g_cfg.fov_changer) SetFOV(g_cfg.custom_fov);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(update_interval_ms.load(std::memory_order_relaxed)));
    }
}

//  Main thread accessors 
EntitySnapshot EntityCache::GetEntities() {
    std::lock_guard<std::mutex> l(mtx);
    return cached_entities;
}

SpectatorSnapshot EntityCache::GetSpectators() {
    std::lock_guard<std::mutex> l(mtx);
    return cached_spectators;
}

//Weapon name helper 
std::string EntityCache::GetWeaponName(uintptr_t pawn) {
    uintptr_t ws = mem::ReadMem<uintptr_t>(pawn + m_pWeaponServices); if (!ws) return "";
    uint32_t  awh = mem::ReadMem<uint32_t>(ws + m_hActiveWeapon);
    if (!awh || awh == 0xFFFFFFFF) return "";
    uintptr_t le = mem::ReadMem<uintptr_t>(g_data->entList + 0x8 * ((awh & 0x7FFF) >> 9) + 0x10); if (!le) return "";
    uintptr_t we = mem::ReadMem<uintptr_t>(le + 0x70 * (awh & 0x1FF));                              if (!we) return "";
    uintptr_t vd = mem::ReadMem<uintptr_t>(we + m_nSubclassID + 0x8);                               if (!vd) return "";
    uintptr_t np = mem::ReadMem<uintptr_t>(vd + m_szName);                                          if (!np) return "";
    std::string n = mem::ReadString(np, 64);
    if (n.find("weapon_") == 0) n = n.substr(7);
    size_t p = n.find_last_of('_');
    if (p != std::string::npos && p + 1 < n.size()) n = n.substr(p + 1);
    for (char c : n) if (c < 0x20 || c > 0x7E) return "";
    return n;
}

//  FOV override
void EntityCache::SetFOV(float fov) {
    if (!g_data || !g_data->localPlayerPawn) return;
    bool scoped = mem::ReadMem<bool>(g_data->localPlayerPawn + m_bIsScoped);
    isScoped = scoped;
    if (scoped) return;
    uintptr_t cs = mem::ReadMem<uintptr_t>(g_data->localPlayerPawn + m_pCameraServices);
    if (!cs) return;
    uint32_t tf = (uint32_t)std::clamp(fov, 1.f, 120.f);
    uint32_t cur = mem::ReadMem<uint32_t>(cs + m_iFOV);
    if (cur != tf) {
        mem::WriteToMem<uint32_t>(cs + m_iFOV, tf);
        mem::WriteToMem<float>(cs + m_flFOVRate, 0.f);
        mem::WriteToMem<float>(cs + m_flFOVTime, 0.f);
    }
}

// Spectator reader 
std::vector<SpectatorEntry> EntityCache::ReadSpectators()
{
    std::vector<SpectatorEntry> res;
    if (!g_data || !g_data->localPlayerPawn || !g_data->entList) return res;

    uintptr_t entList = g_data->entList;
    uintptr_t lpPawn = g_data->localPlayerPawn;
    uintptr_t lpCtrl = g_data->localPlayerController;   // skip self

    for (int i = 1; i <= 64; ++i)
    {
        uintptr_t le = mem::ReadMem<uintptr_t>(
            entList + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
        if (!le) continue;

        uintptr_t ctrl = mem::ReadMem<uintptr_t>(le + 0x70 * (i & 0x1FF));
        if (!ctrl || ctrl == lpCtrl) continue;            // skip self / invalid

        uint32_t ph = mem::ReadMem<uint32_t>(ctrl + m_hPawn);
        if (!ph || ph == 0xFFFFFFFF) continue;
        uintptr_t pe = mem::ReadMem<uintptr_t>(
            entList + 0x8 * ((ph & 0x7FFF) >> 9) + 0x10);
        if (!pe) continue;
        uintptr_t pawn = mem::ReadMem<uintptr_t>(pe + 0x70 * (ph & 0x1FF));
        if (!pawn || pawn == lpPawn) continue;
        uintptr_t obs = mem::ReadMem<uintptr_t>(pawn + m_pObserverServices);
        if (!obs) continue;
        int mode = mem::ReadMem<int>(obs + m_iObserverMode);
        if (mode <= 0) continue;                         
        uint32_t oh = mem::ReadMem<uint32_t>(obs + m_hObserverTarget);
        if (!oh || oh == 0xFFFFFFFF) continue;
        uintptr_t oe = mem::ReadMem<uintptr_t>(
            entList + 0x8 * ((oh & 0x7FFF) >> 9) + 0x10);
        if (!oe) continue;

        uintptr_t observed = mem::ReadMem<uintptr_t>(oe + 0x70 * (oh & 0x1FF));
        if (!observed) continue;
        bool match = false;
        if (observed == lpPawn) { match = true; }
        if (!match && observed == lpCtrl) { match = true; }
        if (!match)
        {
            uint32_t fp = mem::ReadMem<uint32_t>(observed + m_hPawn);
            if (fp && fp != 0xFFFFFFFF)
            {
                uintptr_t fpe = mem::ReadMem<uintptr_t>(
                    entList + 0x8 * ((fp & 0x7FFF) >> 9) + 0x10);
                if (fpe)
                {
                    uintptr_t fpawn = mem::ReadMem<uintptr_t>(fpe + 0x70 * (fp & 0x1FF));
                    if (fpawn == lpPawn) match = true;
                }
            }
        }

        if (!match) continue;

        std::string nm = mem::ReadString(ctrl + m_iszPlayerName);
        res.push_back({ nm.empty() ? "unknown" : nm });
    }
    return res;
}


// Main entity reader 
std::vector<EntityData> EntityCache::ReadEntities() {
    std::vector<EntityData> results;
    results.reserve(64);

    uintptr_t lpPawn = g_data->localPlayerPawn;
    uintptr_t entList = g_data->entList;
    if (!entList || !lpPawn) return results;

    int     localTeam = g_data->localTeam;
    Vector3 lo = mem::ReadMem<Vector3>(lpPawn + m_vOldOrigin);

    EntitySnapshot prevSnap;
    {
        std::lock_guard<std::mutex> l(mtx);
        prevSnap = cached_entities;
    }
    const std::vector<EntityData>* prev = prevSnap ? prevSnap.get() : nullptr;

    auto findPrev = [&](uintptr_t addr) -> const EntityData* {
        if (!prev) return nullptr;
        for (const auto& e : *prev) if (e.pawnAddress == addr) return &e;
        return nullptr;
        };

    bool doRadar = g_cfg.radar_hack;

    for (int i = 1; i < 65; ++i) {
        uintptr_t le = mem::ReadMem<uintptr_t>(entList + 0x8 * ((i & 0x7FFF) >> 9) + 0x10); if (!le)   continue;
        uintptr_t ctrl = mem::ReadMem<uintptr_t>(le + 0x70 * (i & 0x1FF));                     if (!ctrl) continue;
        uintptr_t ph = mem::ReadMem<uintptr_t>(ctrl + m_hPawn);                              if (!ph)   continue;
        uintptr_t pe = mem::ReadMem<uintptr_t>(entList + 0x8 * ((ph & 0x7FFF) >> 9) + 0x10); if (!pe)  continue;
        uintptr_t pawn = mem::ReadMem<uintptr_t>(pe + 0x70 * (ph & 0x1FF));
        if (!pawn || pawn == lpPawn) continue;

        EntityData data{};
        data.pawnAddress = pawn;
        data.health = mem::ReadMem<int>(pawn + m_iHealth);
        if (data.health <= 0 || data.health > 100) continue;

        data.team = mem::ReadMem<int>(pawn + m_iTeamNum);
        data.isEnemy = (data.team != localTeam);
        if (!g_cfg.show_teammates && !data.isEnemy) continue;

        if (g_cfg.armor_esp)
            data.armor = mem::ReadMem<int>(pawn + m_ArmorValue);

        data.isScoped = mem::ReadMem<bool>(pawn + m_bIsScoped);
        data.isWalking = mem::ReadMem<bool>(pawn + m_bIsWalking);

        if (doRadar && data.isEnemy) {
            const EntityData* prev_e = findPrev(pawn);
            bool              alreadyWritten = prev_e && prev_e->radarWritten;
            if (!alreadyWritten) {
                uintptr_t spottedBase = pawn + m_entitySpottedState;
                mem::WriteToMem<bool>(spottedBase + m_bSpotted, true);
                mem::WriteToMem<uint32_t>(spottedBase + m_bSpottedByMask, 0xFFFFFFFFu);
                mem::WriteToMem<uint32_t>(spottedBase + m_bSpottedByMask + 4, 0xFFFFFFFFu);
            }
            data.radarWritten = true;
            data.isSpotted = true;
        }
        else {
            data.isSpotted = mem::ReadMem<bool>(pawn + m_entitySpottedState + m_bSpotted);
        }

        data.origin = mem::ReadMem<Vector3>(pawn + m_vOldOrigin);
        {
            float dx = data.origin.x - lo.x;
            float dy = data.origin.y - lo.y;
            float dz = data.origin.z - lo.z;
            data.distance = sqrtf(dx * dx + dy * dy + dz * dz) * 0.0254f;
        }

        if (g_cfg.economy_esp) {
            uintptr_t ms = mem::ReadMem<uintptr_t>(ctrl + m_pInGameMoneyServices);
            if (ms) data.money = mem::ReadMem<int>(ms + m_iAccount);
        }
        if (g_cfg.rank_esp)
            data.rank = mem::ReadMem<int>(ctrl + m_iCompetitiveRanking);

        if (g_cfg.weapon_esp)
            data.weapon = GetWeaponName(pawn);

        uintptr_t gsn = mem::ReadMem<uintptr_t>(pawn + m_pGameSceneNode);
        if (gsn) {
            uintptr_t bm = mem::ReadMem<uintptr_t>(gsn + m_modelState + 0x80);
            if (bm) {
                struct BoneEntry { float x, y, z; float pad[5]; };
                BoneEntry raw[25] = {};
                mem::ReadBuffer(bm, raw, sizeof(raw));
                for (int k = 0; k < 25; ++k)
                    data.bones[k] = { raw[k].x, raw[k].y, raw[k].z };
                data.hasBoneData = (raw[BONE_HEAD].x || raw[BONE_HEAD].y || raw[BONE_HEAD].z);
            }
        }

        data.headBone = data.bones[BONE_HEAD];
        data.head = data.hasBoneData
            ? data.headBone
            : Vector3{ data.origin.x, data.origin.y, data.origin.z + 72.f };

        data.name = mem::ReadString(ctrl + m_iszPlayerName);
        data.valid = true;
        results.push_back(std::move(data));
    }
    return results;
}

//  DrawESP  (render namespace)
namespace render {

    static void Draw3DBox(ImDrawList* dl, const view_matrix_t& vm,
        const Vector3& mn, const Vector3& mx, ImU32 col, float t = 1.5f)
    {
        Vector3 pts[8] = {
            {mn.x,mn.y,mn.z},{mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},{mx.x,mn.y,mn.z},
            {mn.x,mn.y,mx.z},{mn.x,mx.y,mx.z},{mx.x,mx.y,mx.z},{mx.x,mn.y,mx.z}
        };
        Vector3 sp[8];
        for (int i = 0; i < 8; i++) {
            sp[i] = pts[i].w2s(vm);
            if (sp[i].z < 0.01f) return;
        }
        auto L = [&](int a, int b) {
            dl->AddLine({ sp[a].x,sp[a].y }, { sp[b].x,sp[b].y }, col, t);
            };
        L(0, 1); L(1, 2); L(2, 3); L(3, 0);
        L(4, 5); L(5, 6); L(6, 7); L(7, 4);
        L(0, 4); L(1, 5); L(2, 6); L(3, 7);
    }

    static void DrawCornerBox(ImDrawList* dl, float x, float y,
        float w, float h, ImU32 col, float t = 1.5f)
    {
        float cw = w * 0.22f, ch = h * 0.22f;
        dl->AddLine({ x,     y }, { x + cw,  y }, col, t);
        dl->AddLine({ x,     y }, { x,        y + ch }, col, t);
        dl->AddLine({ x + w, y }, { x + w - cw, y }, col, t);
        dl->AddLine({ x + w, y }, { x + w,   y + ch }, col, t);
        dl->AddLine({ x,     y + h }, { x + cw,  y + h }, col, t);
        dl->AddLine({ x,     y + h }, { x,        y + h - ch }, col, t);
        dl->AddLine({ x + w, y + h }, { x + w - cw, y + h }, col, t);
        dl->AddLine({ x + w, y + h }, { x + w,   y + h - ch }, col, t);
    }

    static void DrawSnapLine(ImDrawList* dl, float sx, float sy,
        float tx, float ty, ImU32 colTip)
    {
        float mx = (sx + tx) * 0.5f, my = (sy + ty) * 0.5f;
        dl->AddLine({ sx,sy }, { mx,my }, IM_COL32(255, 255, 255, 120), 1.f);
        dl->AddLine({ mx,my }, { tx,ty }, colTip, 1.f);
    }

    static void DrawHealthBar(ImDrawList* dl,
        float left, float top, float boxW, float boxH,
        int hp, int style)
    {
        RGB& c = hgrad.c[std::clamp(hp, 0, 100)];
        ImU32 fc = IM_COL32(c.R, c.G, c.B, 255);
        float frac = hp / 100.f;

        if (style == 0) {
            float bh = boxH * frac;
            float x = left - 7.f;
            dl->AddRectFilled({ x - 1, top - 1 }, { x + 4, top + boxH + 1 }, IM_COL32(0, 0, 0, 180), 2.f);
            dl->AddRectFilled({ x, top + (boxH - bh) }, { x + 3, top + boxH }, fc, 1.5f);
            if (bh > 2.f)
                dl->AddLine({ x, top + (boxH - bh) }, { x + 3, top + (boxH - bh) }, IM_COL32(c.R, c.G, c.B, 80), 2.f);
        }
        else {
            float bw = boxW * frac;
            float y = top + boxH + 4.f;
            dl->AddRectFilled({ left - 1, y - 1 }, { left + boxW + 1, y + 4 }, IM_COL32(0, 0, 0, 180), 2.f);
            dl->AddRectFilled({ left, y }, { left + bw, y + 3 }, fc, 1.5f);
        }
    }

    static void DrawArmorBar(ImDrawList* dl,
        float left, float top, float boxW, float boxH,
        int armor, int style, bool healthIsVertical)
    {
        if (armor <= 0) return;
        float frac = std::clamp(armor, 0, 100) / 100.f;
        ImU32 fc = IM_COL32(80, 140, 255, 255);

       if (style == 0) {
    float xo = healthIsVertical ? 13.f : 7.f;
    float bh = boxH * frac;
    float x = left - xo;

    dl->AddRectFilled(
        { x - 1, top - 1 },
        { x + 4, top + boxH + 1 },
        IM_COL32(0, 0, 0, 180),
        2.f
    );

    dl->AddRectFilled(
        { x, top + (boxH - bh) },
        { x + 3, top + boxH },
        fc,
        1.5f
    );
}
        else {
            float yOff = (g_cfg.health_esp && g_cfg.health_bar_style == 1) ? 8.f : 4.f;
            float bw = boxW * frac;
            float y = top + boxH + yOff;
            dl->AddRectFilled({ left - 1, y - 1 }, { left + boxW + 1, y + 4 }, IM_COL32(0, 0, 0, 180), 2.f);
            dl->AddRectFilled({ left, y }, { left + bw, y + 3 }, fc, 1.5f);
        }
    }

    static void DrawWeaponTag(ImDrawList* dl, float cx, float y, const char* wep) {
        ImVec2 ts = ImGui::CalcTextSize(wep);
        float  pw = ts.x + 8.f, ph = ts.y + 4.f;
        float  px = cx - pw * 0.5f;
        dl->AddRectFilled({ px,y }, { px + pw, y + ph }, IM_COL32(15, 15, 25, 200), 3.f);
        dl->AddRect({ px,y }, { px + pw, y + ph }, IM_COL32(0, 200, 255, 100), 3.f, 0, 0.8f);
        dl->AddText({ px + 4.f, y + 2.f }, IM_COL32(0, 200, 255, 255), wep);
    }

    static void DrawSpectatorOverlay(const std::vector<SpectatorEntry>& specs)
    {
        if (!g_cfg.spectator_list) return;

        //  layout constants 
        static constexpr float PANEL_W = 236.f;
        static constexpr float HEADER_H = 34.f;
        static constexpr float ROW_H = 38.f;
        static constexpr float PAD_BOT = 8.f;
        static constexpr float AVATAR_R = 13.f;
        static constexpr float AVATAR_CX = 25.f;   // center-x of avatar inside row

        static ImVec2 s_pos = { 10.f, 60.f };        // persists across frames

        int   count = (int)specs.size();
        float totalH = HEADER_H + std::max(1, count) * ROW_H + PAD_BOT;

        //  open ImGui window (no title bar, no resize, WE control position) 
        ImGui::SetNextWindowPos(s_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ PANEL_W, totalH }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.f);           // we paint our own background

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoMove |   // we drive s_pos ourselves
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav;

        ImGui::Begin("##spec_panel", nullptr, kFlags);
        {
            auto* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetWindowPos();
            float  sx = p0.x, sy = p0.y;

            //  header drag 
            // Only fires when the overlay window is receiving mouse input, which
            // only happens when g_cfg.menu_opened == true (overlay removes
            // WS_EX_TRANSPARENT on menu open). So you can reposition the panel
            // by dragging its header while the menu is open.
            ImVec2 hMin = { sx, sy };
            ImVec2 hMax = { sx + PANEL_W, sy + HEADER_H };
            if (ImGui::IsMouseHoveringRect(hMin, hMax) &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemActive())
            {
                ImVec2 d = ImGui::GetIO().MouseDelta;
                s_pos.x = std::clamp(s_pos.x + d.x, 0.f, (float)screenWidth - PANEL_W);
                s_pos.y = std::clamp(s_pos.y + d.y, 0.f, (float)screenHeight - totalH);
            }

            //  panel chrome 
            // drop shadow
            dl->AddRectFilled({ sx + 4,sy + 4 }, { sx + PANEL_W + 4,sy + totalH + 4 },
                IM_COL32(0, 0, 0, 65), 9.f);
            // body fill
            dl->AddRectFilled({ sx,sy }, { sx + PANEL_W,sy + totalH },
                IM_COL32(10, 8, 14, 234), 9.f);
            // red left accent stripe
            dl->AddRectFilled({ sx,sy }, { sx + 3.f,sy + totalH },
                IM_COL32(220, 30, 50, 248), 2.f);
            // outer border
            dl->AddRect({ sx,sy }, { sx + PANEL_W,sy + totalH },
                IM_COL32(175, 22, 42, 115), 9.f, 0, 1.f);

            //  header background 
            dl->AddRectFilledMultiColor(
                { sx,sy }, { sx + PANEL_W, sy + HEADER_H },
                IM_COL32(34, 10, 18, 252), IM_COL32(34, 10, 18, 252),
                IM_COL32(12, 8, 14, 100), IM_COL32(12, 8, 14, 100));
            dl->AddLine({ sx + 3,sy + HEADER_H }, { sx + PANEL_W,sy + HEADER_H },
                IM_COL32(220, 30, 50, 48), 1.f);

            // drag-grip dots — 3 stacked dots top-right, visual cue that the header
            // is draggable
            for (int gi = 0; gi < 3; ++gi)
                dl->AddCircleFilled({ sx + PANEL_W - 11.f, sy + 10.f + gi * 5.5f },
                    1.9f, IM_COL32(190, 75, 95, 165), 6);

            // camera / eye icon (two rings + filled pupil)
            float eX = sx + 15.f, eY = sy + HEADER_H * 0.5f;
            dl->AddCircle({ eX, eY }, 7.f, IM_COL32(200, 25, 45, 215), 16, 1.6f);
            dl->AddCircleFilled({ eX, eY }, 3.5f, IM_COL32(220, 30, 50, 235), 12);
            dl->AddCircleFilled({ eX - 1.2f, eY - 1.2f }, 1.4f, IM_COL32(255, 255, 255, 205), 8);

            // header label
            char hdr[48];
            snprintf(hdr, sizeof(hdr), "SPECTATORS  (%d)", count);
            ImVec2 hs = ImGui::CalcTextSize(hdr);
            float  hlx = sx + 29.f, hly = sy + (HEADER_H - hs.y) * 0.5f;
            dl->AddText({ hlx + 1,hly + 1 }, IM_COL32(0, 0, 0, 185), hdr);
            dl->AddText({ hlx,  hly }, IM_COL32(235, 215, 225, 255), hdr);

            //  rows 
            // Avatar palette: 8 vivid distinct hues, consistent per name hash
            static const ImU32 kPalette[8] = {
                IM_COL32(222,68, 68, 255),   // red
                IM_COL32(68, 162,226,255),   // blue
                IM_COL32(68, 207,128,255),   // green
                IM_COL32(207,147,48, 255),   // gold
                IM_COL32(167,68, 226,255),   // purple
                IM_COL32(226,128,48, 255),   // orange
                IM_COL32(48, 207,207,255),   // cyan
                IM_COL32(226,68, 167,255),   // pink
            };

            float ry = sy + HEADER_H;
            float t = (float)ImGui::GetTime();

            if (count == 0)
            {
                const char* emp = "No spectators";
                ImVec2 es = ImGui::CalcTextSize(emp);
                dl->AddText(
                    { sx + (PANEL_W - es.x) * 0.5f, ry + (ROW_H - es.y) * 0.5f },
                    IM_COL32(95, 80, 100, 155), emp);
            }

            for (int idx = 0; idx < count; ++idx)
            {
                const auto& sp = specs[idx];
                float        rowY = ry + idx * ROW_H;
                float        rowCY = rowY + ROW_H * 0.5f;

                // alternate row shade
                if (idx & 1)
                    dl->AddRectFilled({ sx + 3,rowY }, { sx + PANEL_W,rowY + ROW_H },
                        IM_COL32(255, 255, 255, 5));
                // row divider
                dl->AddLine({ sx + 8,rowY + ROW_H - 1 }, { sx + PANEL_W - 8,rowY + ROW_H - 1 },
                    IM_COL32(255, 255, 255, 10), 1.f);

                //  profile-picture avatar 
                // Colour is deterministic from the name hash so it's stable frame-to-frame
                uint32_t nh = 5381u;
                for (unsigned char c : sp.name) nh = nh * 33u ^ c;
                ImU32 aCol = kPalette[nh % 8];

                // decompose to build a slightly brighter rim colour
                int aR = (aCol >> IM_COL32_R_SHIFT) & 0xFF;
                int aG = (aCol >> IM_COL32_G_SHIFT) & 0xFF;
                int aB = (aCol >> IM_COL32_B_SHIFT) & 0xFF;
                ImU32 aRim = IM_COL32(
                    std::min(255, aR + 55),
                    std::min(255, aG + 55),
                    std::min(255, aB + 55), 255);

                float avX = sx + AVATAR_CX, avY = rowCY;

                // shadow under circle
                dl->AddCircleFilled({ avX + 1.5f, avY + 1.5f }, AVATAR_R + 1.2f,
                    IM_COL32(0, 0, 0, 85), 30);
                // base fill
                dl->AddCircleFilled({ avX, avY }, AVATAR_R, aCol, 30);
                // specular top-left highlight → faked 3-D sphere look
                dl->AddCircleFilled({ avX - AVATAR_R * 0.28f, avY - AVATAR_R * 0.34f },
                    AVATAR_R * 0.58f, IM_COL32(255, 255, 255, 40), 22);
                // bottom-right darkening
                dl->AddCircleFilled({ avX + AVATAR_R * 0.20f, avY + AVATAR_R * 0.24f },
                    AVATAR_R * 0.52f, IM_COL32(0, 0, 0, 30), 22);
                // white outer ring
                dl->AddCircle({ avX, avY }, AVATAR_R + 0.5f,
                    IM_COL32(255, 255, 255, 52), 30, 1.4f);
                // coloured inner rim
                dl->AddCircle({ avX, avY }, AVATAR_R - 1.2f,
                    aRim, 30, 0.8f);

                // initials — up to two chars (first letter of each word in name)
                char ini[3] = { 0,0,0 };
                if (!sp.name.empty())
                {
                    ini[0] = (char)toupper((unsigned char)sp.name[0]);
                    size_t spi = sp.name.find(' ');
                    if (spi != std::string::npos && spi + 1 < sp.name.size())
                        ini[1] = (char)toupper((unsigned char)sp.name[spi + 1]);
                }
                ImVec2 its = ImGui::CalcTextSize(ini);
                float  itX = avX - its.x * 0.5f;
                float  itY = avY - its.y * 0.5f;
                dl->AddText({ itX + 0.8f, itY + 0.8f }, IM_COL32(0, 0, 0, 155), ini);
                dl->AddText({ itX,      itY }, IM_COL32(255, 255, 255, 248), ini);

                //  name + "spectating you" sub-label 
                float nameX = sx + AVATAR_CX + AVATAR_R + 10.f;
                float nameMaxW = PANEL_W - (nameX - sx) - 28.f;   // leave room for dot

                // truncate with ".." if too long
                std::string dn = sp.name;
                while (dn.size() > 1 && ImGui::CalcTextSize(dn.c_str()).x > nameMaxW)
                    dn.pop_back();
                if (dn.size() < sp.name.size() && dn.size() > 2)
                {
                    dn.pop_back(); dn.pop_back(); dn += "..";
                }

                float lh = ImGui::GetTextLineHeight();
                float nameY = rowCY - lh * 0.65f;

                // drop shadow, then main text
                dl->AddText({ nameX + 1, nameY + 1 }, IM_COL32(0, 0, 0, 172), dn.c_str());
                dl->AddText({ nameX,   nameY }, IM_COL32(235, 220, 230, 255), dn.c_str());

                // sub-label at 10 px — uses explicit font size overload
                dl->AddText(ImGui::GetFont(), 10.f,
                    { nameX, nameY + lh + 1.5f },
                    IM_COL32(155, 110, 135, 185),
                    "spectating you");

                //  pulsing live dot (top-right of row) 
                float pulse = 0.55f + 0.45f * sinf(t * 4.2f + idx * 1.1f);
                int   dotA = (int)(pulse * 228.f);
                float dotX = sx + PANEL_W - 12.f;
                dl->AddCircleFilled({ dotX, rowCY }, 4.5f,
                    IM_COL32(220, 30, 50, dotA), 14);
                dl->AddCircle({ dotX, rowCY }, 4.5f,
                    IM_COL32(255, 80, 80, (int)(pulse * 112.f)), 14, 1.f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void DrawESP(const std::vector<EntityData>& entities,
        const std::vector<SpectatorEntry>& spectators,
        const GameData& gd)
    {
        DrawSpectatorOverlay(spectators);

        if (!g_cfg.esp_enabled) return;
        if (!gd.IsValid())      return;

        auto dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;

        const view_matrix_t& vm = gd.view_matrix;
        float                scx = (float)screenWidth * 0.5f;
        float                scy = (float)screenHeight;

        for (const auto& e : entities) {
            if (!e.valid) continue;

            Vector3 hp = e.hasBoneData ? e.headBone : e.head;
            Vector3 sf = e.origin.w2s(vm);
            Vector3 sh = hp.w2s(vm);
            if (sh.z < 0.01f || sf.z < 0.01f) continue;
            float boxH = sf.y - sh.y; if (boxH < 5.f) continue;
            float boxW = boxH / 1.6f;
            float left = sh.x - boxW * 0.5f;
            float top = sh.y - 3.f;

            float* bc = e.isEnemy ? g_cfg.box_color : g_cfg.team_box_color;
            ImU32  boxCol = IM_COL32(
                (int)(bc[0] * 255), (int)(bc[1] * 255), (int)(bc[2] * 255), (int)(bc[3] * 255));

            if (g_cfg.box_esp) {
                if (g_cfg.box_style == 0) {
                    dl->AddRect({ left,top }, { left + boxW, top + boxH + 5 }, IM_COL32(0, 0, 0, 100), 0, 0, 3.f);
                    dl->AddRect({ left,top }, { left + boxW, top + boxH + 5 }, boxCol, 0, 0, 1.5f);
                }
                else if (g_cfg.box_style == 1) {
                    DrawCornerBox(dl, left - 1, top - 1, boxW + 2, boxH + 6, IM_COL32(0, 0, 0, 100), 3.f);
                    DrawCornerBox(dl, left, top, boxW, boxH + 5, boxCol, 1.5f);
                }
                else {
                    Vector3 minP = { e.origin.x - 15.f, e.origin.y - 15.f, e.origin.z };
                    Vector3 maxP = { e.origin.x + 15.f, e.origin.y + 15.f, hp.z + 5.f };
                    Draw3DBox(dl, vm, minP, maxP, IM_COL32(0, 0, 0, 100), 3.f);
                    Draw3DBox(dl, vm, minP, maxP, boxCol, 1.5f);
                }
            }

            if (g_cfg.head_dot) {
                float* hdc = e.isEnemy ? g_cfg.head_dot_color : g_cfg.team_head_dot_color;
                float  r = std::max(2.5f, boxH / 14.f);
                dl->AddCircleFilled({ sh.x,sh.y }, r + 1.5f, IM_COL32(0, 0, 0, 160), 12);
                dl->AddCircleFilled({ sh.x,sh.y }, r,
                    IM_COL32((int)(hdc[0] * 255), (int)(hdc[1] * 255), (int)(hdc[2] * 255), 255), 12);
            }

            if (g_cfg.name_esp && !e.name.empty()) {
                std::string dn = e.name;
                if (g_cfg.rank_esp && e.rank > 0) {
                    char rb[12]; snprintf(rb, sizeof(rb), "[%d] ", e.rank); dn = rb + dn;
                }
                float* nc = e.isEnemy ? g_cfg.name_color : g_cfg.team_name_color;
                ImVec2 ts = ImGui::CalcTextSize(dn.c_str());
                float  nx = sh.x - ts.x * 0.5f, ny = top - ts.y - 4.f;
                dl->AddText({ nx + 1,ny + 1 }, IM_COL32(0, 0, 0, 200), dn.c_str());
                dl->AddText({ nx,  ny },
                    IM_COL32((int)(nc[0] * 255), (int)(nc[1] * 255), (int)(nc[2] * 255), 255), dn.c_str());
            }

            if (g_cfg.health_esp)
                DrawHealthBar(dl, left, top, boxW, boxH + 5.f, e.health, g_cfg.health_bar_style);

            if (g_cfg.armor_esp && e.armor > 0)
                DrawArmorBar(dl, left, top, boxW, boxH + 5.f,
                    e.armor, g_cfg.armor_bar_style,
                    g_cfg.health_esp && g_cfg.health_bar_style == 0);

            float textY = top + boxH + 8.f;
            if (g_cfg.health_esp && g_cfg.health_bar_style == 1) textY += 5.f;
            if (g_cfg.armor_esp && g_cfg.armor_bar_style == 1) textY += 5.f;

            if (g_cfg.distance_esp) {
                char   db[32]; snprintf(db, sizeof(db), "%.0fm", e.distance);
                ImVec2 ds = ImGui::CalcTextSize(db);
                float* dc = g_cfg.distance_color;
                dl->AddText({ sh.x - ds.x * 0.5f + 1, textY + 1 }, IM_COL32(0, 0, 0, 180), db);
                dl->AddText({ sh.x - ds.x * 0.5f,     textY },
                    IM_COL32((int)(dc[0] * 255), (int)(dc[1] * 255), (int)(dc[2] * 255), 255), db);
                textY += 13.f;
            }
            if (g_cfg.weapon_esp && !e.weapon.empty()) {
                DrawWeaponTag(dl, sh.x, textY, e.weapon.c_str());
                textY += 16.f;
            }
            if (g_cfg.economy_esp && e.money > 0) {
                char   mb[16]; snprintf(mb, sizeof(mb), "$%d", e.money);
                ImVec2 ms2 = ImGui::CalcTextSize(mb);
                dl->AddText({ sh.x - ms2.x * 0.5f, textY }, IM_COL32(0, 220, 80, 255), mb);
                textY += 13.f;
            }

            if (g_cfg.skeleton_esp && e.hasBoneData) {
                float* sc_cfg = e.isEnemy ? g_cfg.skeleton_color : g_cfg.team_skeleton_color;
                ImU32  sCol = IM_COL32((int)(sc_cfg[0] * 255), (int)(sc_cfg[1] * 255), (int)(sc_cfg[2] * 255), 220);
                ImU32  sGlow = IM_COL32((int)(sc_cfg[0] * 128), (int)(sc_cfg[1] * 128), (int)(sc_cfg[2] * 128), 50);

                static const std::pair<int, int> conns[] = {
                    {BONE_HEAD,    BONE_NECK      },
                    {BONE_NECK,    BONE_SPINE_0   }, {BONE_SPINE_0,   BONE_PELVIS    },
                    {BONE_NECK,    BONE_L_SHOULDER}, {BONE_L_SHOULDER,BONE_L_ELBOW  }, {BONE_L_ELBOW, BONE_L_WRIST},
                    {BONE_NECK,    BONE_R_SHOULDER}, {BONE_R_SHOULDER,BONE_R_ELBOW  }, {BONE_R_ELBOW, BONE_R_WRIST},
                    {BONE_PELVIS,  BONE_L_HIP     }, {BONE_L_HIP,     BONE_L_KNEE   }, {BONE_L_KNEE,  BONE_L_ANKLE},
                    {BONE_PELVIS,  BONE_R_HIP     }, {BONE_R_HIP,     BONE_R_KNEE   }, {BONE_R_KNEE,  BONE_R_ANKLE}
                };
                for (const auto& conn : conns) {
                    Vector3 b1w = e.bones[conn.first], b2w = e.bones[conn.second];
                    if (!(b1w.x || b1w.y || b1w.z) || !(b2w.x || b2w.y || b2w.z)) continue;
                    Vector3 b1 = b1w.w2s(vm), b2 = b2w.w2s(vm);
                    if (b1.z > 0.01f && b2.z > 0.01f) {
                        dl->AddLine({ b1.x,b1.y }, { b2.x,b2.y }, sGlow, 3.5f);
                        dl->AddLine({ b1.x,b1.y }, { b2.x,b2.y }, sCol, 1.5f);
                    }
                }
            }

            if (g_cfg.snaplines_esp) {
                float* slc = e.isEnemy ? g_cfg.snapline_color : g_cfg.team_snapline_color;
                ImU32  tipCol = IM_COL32(
                    (int)(slc[0] * 255), (int)(slc[1] * 255), (int)(slc[2] * 255), 200);
                DrawSnapLine(dl, scx, scy, sf.x, sf.y, tipCol);
            }
        }
    }

} // namespace render
