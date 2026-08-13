
#define NOMINMAX
#include "Esp.h"
#include "render.h"
#include "features/aimbot.h"
#include "features/triggerbot.h"
#include "features/misc.h"
#include "../imgui/imgui.h"
#include <cmath>
#include <algorithm>

static struct { RGB c[101]; } hgrad;
static struct HGI {
    HGI() {
        for (int i = 0; i <= 100; i++) {
            float p = i / 100.f;
            if (p >= 0.5f) hgrad.c[i] = { (int)(255 * (1.f - (p - 0.5f) * 0.4f)), (int)(255 * p), 0, 255 };
            else           hgrad.c[i] = { 255, (int)(255 * p * 2.f), 0, 255 };
        }
    }
} hgInit;

bool isScoped;
EntityCache::EntityCache(UINT, UINT, int)
    : should_run(false), client_dll(0), g_data(nullptr) {
    update_interval_ms = 1;
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
    cached_entities.clear(); cached_spectators.clear(); cached_bomb = {};
}

void EntityCache::WorkerLoop() {
    while (should_run) {
        if (client_dll && g_data && g_data->IsValid()) {
            auto ne = ReadEntities();
            auto nb = ReadBombData();
            auto ns = ReadSpectators();
            {
                std::lock_guard<std::mutex> l(mtx);
                cached_entities = std::move(ne);
                cached_bomb = nb;
                cached_spectators = std::move(ns);
            }
            SetFOV(g_cfg.custom_fov);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    }
}

std::vector<EntityData>  EntityCache::GetEntities() { std::lock_guard<std::mutex> l(mtx); return cached_entities; }
BombData                 EntityCache::GetBombData() { std::lock_guard<std::mutex> l(mtx); return cached_bomb; }
std::vector<std::string> EntityCache::GetSpectators() { std::lock_guard<std::mutex> l(mtx); return cached_spectators; }

Vector3 EntityCache::GetBonePosition(uintptr_t pawn, int boneId) {
    uintptr_t gsn = mem::ReadMem<uintptr_t>(pawn + m_pGameSceneNode); if (!gsn) return {};
    uintptr_t bm = mem::ReadMem<uintptr_t>(gsn + m_modelState + 0x80); if (!bm) return {};
    return mem::ReadMem<Vector3>(bm + boneId * 32);
}

std::string EntityCache::GetWeaponName(uintptr_t pawn) {
    uintptr_t ws = mem::ReadMem<uintptr_t>(pawn + m_pWeaponServices); if (!ws) return "";
    uint32_t  awh = mem::ReadMem<uint32_t>(ws + m_hActiveWeapon); if (!awh || awh == 0xFFFFFFFF) return "";
    uintptr_t le = mem::ReadMem<uintptr_t>(g_data->entList + 0x8 * ((awh & 0x7FFF) >> 9) + 0x10); if (!le) return "";
    uintptr_t we = mem::ReadMem<uintptr_t>(le + 0x70 * (awh & 0x1FF)); if (!we) return "";
    uintptr_t vd = mem::ReadMem<uintptr_t>(we + m_nSubclassID + 0x8); if (!vd) return "";
    uintptr_t np = mem::ReadMem<uintptr_t>(vd + m_szName); if (!np) return "";
    std::string n = mem::ReadString(np, 64);
    if (n.find("weapon_") == 0) n = n.substr(7);
    size_t p = n.find_last_of('_');
    if (p != std::string::npos && p + 1 < n.size()) n = n.substr(p + 1);
    for (char c : n) if (c < 0x20 || c > 0x7E) return "";
    return n;
}

void EntityCache::SetFOV(float fov) {
    if (!g_cfg.fov_changer || !g_data || !g_data->localPlayerPawn) return;
    if (mem::ReadMem<bool>(g_data->localPlayerPawn + m_bIsScoped))
    {
        isScoped = true;
		return;
    }else{
        isScoped = false;
	}
    uintptr_t cs = mem::ReadMem<uintptr_t>(g_data->localPlayerPawn + m_pCameraServices); if (!cs) return;
    uint32_t tf = (uint32_t)std::clamp(fov, 1.f, 120.f);
    mem::WriteToMem<uint32_t>(cs + m_iFOV, tf);
    mem::WriteToMem<float>(cs + m_flFOVRate, 0.f);
    mem::WriteToMem<float>(cs + m_flFOVTime, 0.f);
}


BombData EntityCache::ReadBombData() {
    BombData b{};
    uintptr_t c4 = mem::ReadMem<uintptr_t>(client_dll + dwPlantedC4); if (!c4) return b;
    b.isValid = b.isPlanted = true;
    b.isTicking = mem::ReadMem<bool>(c4 + m_bBombTicking);
    b.isDefused = mem::ReadMem<bool>(c4 + m_bBombDefused);
    float blow = mem::ReadMem<float>(c4 + m_flC4Blow);
    uintptr_t gv = mem::ReadMem<uintptr_t>(client_dll + dwGlobalVars);
    float ct = gv ? mem::ReadMem<float>(gv + 0x10) : 0.f;
    if (b.isTicking && blow > 0) b.timer = std::max(0.f, blow - ct);
    float dc = mem::ReadMem<float>(c4 + m_flDefuseCountDown);
    if (dc > 0 && ct > 0) b.defuseTimer = std::max(0.f, dc - ct);
    uintptr_t gsn = mem::ReadMem<uintptr_t>(c4 + m_pGameSceneNode);
    if (gsn) {
        uintptr_t bm = mem::ReadMem<uintptr_t>(gsn + m_modelState + 0x80);
        b.position = bm ? mem::ReadMem<Vector3>(bm) : mem::ReadMem<Vector3>(c4 + m_vOldOrigin);
    }
    return b;
}

std::vector<std::string> EntityCache::ReadSpectators() {
    std::vector<std::string> res;
    if (!g_cfg.spectator_list || !g_data || !g_data->localPlayerPawn || !g_data->entList) return res;
    for (int i = 0; i < 64; ++i) {
        uintptr_t le = mem::ReadMem<uintptr_t>(g_data->entList + 0x8 * ((i & 0x7FFF) >> 9) + 0x10); if (!le) continue;
        uintptr_t ctrl = mem::ReadMem<uintptr_t>(le + 0x70 * (i & 0x1FF)); if (!ctrl) continue;
        uintptr_t ph = mem::ReadMem<uintptr_t>(ctrl + m_hPawn); if (!ph) continue;
        uintptr_t pe = mem::ReadMem<uintptr_t>(g_data->entList + 0x8 * ((ph & 0x7FFF) >> 9) + 0x10); if (!pe) continue;
        uintptr_t pawn = mem::ReadMem<uintptr_t>(pe + 0x70 * (ph & 0x1FF));
        if (!pawn || pawn == g_data->localPlayerPawn) continue;
        int om = mem::ReadMem<int>(pawn + m_iObserverMode); if (!om) continue;
        uint32_t oh = mem::ReadMem<uint32_t>(pawn + m_hObserverTarget); if (!oh || oh == 0xFFFFFFFF) continue;
        uintptr_t oe = mem::ReadMem<uintptr_t>(g_data->entList + 0x8 * ((oh & 0x7FFF) >> 9) + 0x10); if (!oe) continue;
        uintptr_t ot = mem::ReadMem<uintptr_t>(oe + 0x70 * (oh & 0x1FF));
        if (ot == g_data->localPlayerPawn) {
            std::string nm = mem::ReadString(ctrl + m_iszPlayerName);
            if (!nm.empty()) res.push_back(nm);
        }
    }
    return res;
}

std::vector<EntityData> EntityCache::ReadEntities() {
    std::vector<EntityData> results; results.reserve(64);
    uintptr_t lpPawn = g_data->localPlayerPawn;
    uintptr_t entList = g_data->entList;
    if (!entList || !lpPawn) return results;
    int localTeam = g_data->localTeam;
    Vector3 lo = mem::ReadMem<Vector3>(lpPawn + m_vOldOrigin);

    for (int i = 1; i < 65; ++i) {
        uintptr_t le = mem::ReadMem<uintptr_t>(entList + 0x8 * ((i & 0x7FFF) >> 9) + 0x10); if (!le) continue;
        uintptr_t ctrl = mem::ReadMem<uintptr_t>(le + 0x70 * (i & 0x1FF)); if (!ctrl) continue;
        uintptr_t ph = mem::ReadMem<uintptr_t>(ctrl + m_hPawn); if (!ph) continue;
        uintptr_t pe = mem::ReadMem<uintptr_t>(entList + 0x8 * ((ph & 0x7FFF) >> 9) + 0x10); if (!pe) continue;
        uintptr_t pawn = mem::ReadMem<uintptr_t>(pe + 0x70 * (ph & 0x1FF));
        if (!pawn || pawn == lpPawn) continue;

        EntityData data{};
        data.pawnAddress = pawn;
        data.health = mem::ReadMem<int>(pawn + m_iHealth);
        if (data.health <= 0 || data.health > 100) continue;

        data.team = mem::ReadMem<int>(pawn + m_iTeamNum);
        if (!g_cfg.show_teammates && data.team == localTeam) continue;
        data.isEnemy = (data.team != localTeam);

        data.armor = mem::ReadMem<int>(pawn + m_ArmorValue);
        data.isDefusing = mem::ReadMem<bool>(pawn + m_bIsDefusing);
        data.isScoped = mem::ReadMem<bool>(pawn + m_bIsScoped);
        data.isWalking = mem::ReadMem<bool>(pawn + m_bIsWalking);

        if (g_cfg.radar_hack && data.isEnemy) {
            uintptr_t spottedBase = pawn + m_entitySpottedState;
            if (!mem::ReadMem<bool>(spottedBase + m_bSpotted))
                mem::WriteToMem<bool>(spottedBase + m_bSpotted, true);
            mem::WriteToMem<uint32_t>(spottedBase + m_bSpottedByMask, 0xFFFFFFFFu);
            mem::WriteToMem<uint32_t>(spottedBase + m_bSpottedByMask + sizeof(uint32_t), 0xFFFFFFFFu);
            data.isSpotted = true;
        }
        else {
            data.isSpotted = mem::ReadMem<bool>(pawn + m_entitySpottedState + m_bSpotted);
        }

        data.origin = mem::ReadMem<Vector3>(pawn + m_vOldOrigin);
        data.distance = sqrtf(
            powf(data.origin.x - lo.x, 2.f) +
            powf(data.origin.y - lo.y, 2.f) +
            powf(data.origin.z - lo.z, 2.f)) * 0.0254f;

        uintptr_t ms = mem::ReadMem<uintptr_t>(ctrl + m_pInGameMoneyServices);
        if (ms) data.money = mem::ReadMem<int>(ms + m_iAccount);
        data.rank = mem::ReadMem<int>(ctrl + m_iCompetitiveRanking);
        data.weapon = GetWeaponName(pawn);

        struct BoneEntry { float x, y, z; float pad[5]; };
        {
            uintptr_t gsn = mem::ReadMem<uintptr_t>(pawn + m_pGameSceneNode);
            if (gsn) {
                uintptr_t bm = mem::ReadMem<uintptr_t>(gsn + m_modelState + 0x80);
                if (bm) {
                    BoneEntry raw[25] = {};
                    mem::ReadBuffer(bm, raw, sizeof(raw));
                    for (int k = 0; k < 25; ++k)
                        data.bones[k] = { raw[k].x, raw[k].y, raw[k].z };
                    data.hasBoneData = (raw[BONE_HEAD].x || raw[BONE_HEAD].y || raw[BONE_HEAD].z);
                }
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

//DrawESP 
namespace render {

    static void Draw3DBox(ImDrawList* dl, const view_matrix_t& vm,
        const Vector3& min, const Vector3& max, ImU32 col, float t = 1.5f) {
        Vector3 pts[8] = {
            {min.x,min.y,min.z},{min.x,max.y,min.z},{max.x,max.y,min.z},{max.x,min.y,min.z},
            {min.x,min.y,max.z},{min.x,max.y,max.z},{max.x,max.y,max.z},{max.x,min.y,max.z}
        };
        Vector3 sp[8];
        for (int i = 0; i < 8; i++) {
            sp[i] = pts[i].w2s(vm);
            if (sp[i].z < 0.01f) return;
        }
        // Bottom
        dl->AddLine(ImVec2(sp[0].x, sp[0].y), ImVec2(sp[1].x, sp[1].y), col, t);
        dl->AddLine(ImVec2(sp[1].x, sp[1].y), ImVec2(sp[2].x, sp[2].y), col, t);
        dl->AddLine(ImVec2(sp[2].x, sp[2].y), ImVec2(sp[3].x, sp[3].y), col, t);
        dl->AddLine(ImVec2(sp[3].x, sp[3].y), ImVec2(sp[0].x, sp[0].y), col, t);
        // Top
        dl->AddLine(ImVec2(sp[4].x, sp[4].y), ImVec2(sp[5].x, sp[5].y), col, t);
        dl->AddLine(ImVec2(sp[5].x, sp[5].y), ImVec2(sp[6].x, sp[6].y), col, t);
        dl->AddLine(ImVec2(sp[6].x, sp[6].y), ImVec2(sp[7].x, sp[7].y), col, t);
        dl->AddLine(ImVec2(sp[7].x, sp[7].y), ImVec2(sp[4].x, sp[4].y), col, t);
        // Vertical
        dl->AddLine(ImVec2(sp[0].x, sp[0].y), ImVec2(sp[4].x, sp[4].y), col, t);
        dl->AddLine(ImVec2(sp[1].x, sp[1].y), ImVec2(sp[5].x, sp[5].y), col, t);
        dl->AddLine(ImVec2(sp[2].x, sp[2].y), ImVec2(sp[6].x, sp[6].y), col, t);
        dl->AddLine(ImVec2(sp[3].x, sp[3].y), ImVec2(sp[7].x, sp[7].y), col, t);
    }

    static void DrawCornerBox(ImDrawList* dl, float x, float y, float w, float h, ImU32 col, float t = 1.5f) {
        float cw = w * 0.22f, ch = h * 0.22f;
        dl->AddLine(ImVec2(x, y), ImVec2(x + cw, y), col, t);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + ch), col, t);
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w - cw, y), col, t);
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + ch), col, t);
        dl->AddLine(ImVec2(x, y + h), ImVec2(x + cw, y + h), col, t);
        dl->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - ch), col, t);
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - cw, y + h), col, t);
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - ch), col, t);
    }

    static void DrawSnapLineGradient(ImDrawList* dl, float sx, float sy, float tx, float ty, ImU32 colTip) {
        dl->AddLine(ImVec2(sx, sy), ImVec2((sx + tx) * 0.5f, (sy + ty) * 0.5f), IM_COL32(255, 255, 255, 120), 1.f);
        dl->AddLine(ImVec2((sx + tx) * 0.5f, (sy + ty) * 0.5f), ImVec2(tx, ty), colTip, 1.f);
    }

    static void DrawHealthBar(ImDrawList* dl, float x, float y, float h, int hp) {
        float bh = h * (hp / 100.f);
        dl->AddRectFilled(ImVec2(x - 1, y - 1), ImVec2(x + 4, y + h + 1), IM_COL32(0, 0, 0, 180), 2.f);
        RGB& c = hgrad.c[std::clamp(hp, 0, 100)];
        ImU32 fillCol = IM_COL32(c.R, c.G, c.B, 255);
        dl->AddRectFilled(ImVec2(x, y + (h - bh)), ImVec2(x + 3, y + h), fillCol, 1.5f);
        if (bh > 2.f)
            dl->AddLine(ImVec2(x, y + (h - bh)), ImVec2(x + 3, y + (h - bh)), IM_COL32(c.R, c.G, c.B, 80), 2.f);
    }

    static void DrawWeaponTag(ImDrawList* dl, float cx, float y, const char* wep) {
        ImVec2 ts = ImGui::CalcTextSize(wep);
        float pw = ts.x + 8.f, ph = ts.y + 4.f;
        float px = cx - pw * 0.5f;
        dl->AddRectFilled(ImVec2(px, y), ImVec2(px + pw, y + ph), IM_COL32(15, 15, 25, 200), 3.f);
        dl->AddRect(ImVec2(px, y), ImVec2(px + pw, y + ph), IM_COL32(0, 200, 255, 100), 3.f, 0, 0.8f);
        dl->AddText(ImVec2(px + 4.f, y + 2.f), IM_COL32(0, 200, 255, 255), wep);
    }

    void DrawESP(const std::vector<EntityData>& entities,
        const BombData& bomb,
        const std::vector<std::string>& spectators,
        const GameData& gd)
    {
        if (!gd.IsValid()) return;

        features::NoFlash(gd);
        features::BunnyHop(gd);
        features::Triggerbot(entities, gd);
        features::Aimbot(entities, gd);
        features::CustomCrosshair();
        features::DrawBombTimerOverlay(bomb);
        features::DrawSpectatorList(spectators);

        if (!g_cfg.esp_enabled) return;
        auto dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;

        const view_matrix_t& vm = gd.view_matrix;
        float scx = (float)screenWidth * 0.5f;
        float scy = (float)screenHeight;

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
            ImU32 boxCol = IM_COL32(
                (int)(bc[0] * 255), (int)(bc[1] * 255), (int)(bc[2] * 255), (int)(bc[3] * 255));

            // Box
            if (g_cfg.box_esp) {
                if (g_cfg.box_style == 0) {
                    dl->AddRect(ImVec2(left, top), ImVec2(left + boxW, top + boxH + 5), IM_COL32(0, 0, 0, 100), 0, 0, 3.f);
                    dl->AddRect(ImVec2(left, top), ImVec2(left + boxW, top + boxH + 5), boxCol, 0, 0, 1.5f);
                }
                else if (g_cfg.box_style == 1) {
                    DrawCornerBox(dl, left - 1, top - 1, boxW + 2, boxH + 6, IM_COL32(0, 0, 0, 100), 3.f);
                    DrawCornerBox(dl, left, top, boxW, boxH + 5, boxCol, 1.5f);
                }
                else if (g_cfg.box_style == 2) {
                    Vector3 minP = { e.origin.x - 15.f,e.origin.y - 15.f,e.origin.z };
                    Vector3 maxP = { e.origin.x + 15.f,e.origin.y + 15.f,hp.z + 5.f };
                    Draw3DBox(dl, vm, minP, maxP, IM_COL32(0, 0, 0, 100), 3.f);
                    Draw3DBox(dl, vm, minP, maxP, boxCol, 1.5f);
                }
            }

            // Head dot
            if (g_cfg.head_dot) {
                float r = std::max(2.5f, boxH / 14.f);
                dl->AddCircleFilled(ImVec2(sh.x, sh.y), r + 1.5f, IM_COL32(0, 0, 0, 160), 12);
                dl->AddCircleFilled(ImVec2(sh.x, sh.y), r,
                    IM_COL32((int)(g_cfg.head_dot_color[0] * 255),
                        (int)(g_cfg.head_dot_color[1] * 255),
                        (int)(g_cfg.head_dot_color[2] * 255), 255), 12);
            }

            // Name
            if (g_cfg.name_esp && !e.name.empty()) {
                std::string dn = e.name;
                if (g_cfg.rank_esp && e.rank > 0) {
                    char rb[12]; snprintf(rb, sizeof(rb), "[%d] ", e.rank); dn = rb + dn;
                }
                ImVec2 ts = ImGui::CalcTextSize(dn.c_str());
                float nx = sh.x - ts.x * 0.5f, ny = top - ts.y - 4.f;
                dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 200), dn.c_str());
                dl->AddText(ImVec2(nx, ny),
                    IM_COL32((int)(g_cfg.name_color[0] * 255), (int)(g_cfg.name_color[1] * 255),
                        (int)(g_cfg.name_color[2] * 255), 255), dn.c_str());
            }

            // Health bar
            if (g_cfg.health_esp)
                DrawHealthBar(dl, left - 7.f, top, boxH + 5.f, e.health);

            // Armor bar
            if (g_cfg.armor_esp && e.armor > 0) {
                float xo = g_cfg.health_esp ? 13.f : 7.f;
                float bh2 = (boxH + 5.f) * (std::clamp(e.armor, 0, 100) / 100.f);
                dl->AddRectFilled(ImVec2(left - xo - 1, top - 1), ImVec2(left - xo + 4, top + boxH + 6), IM_COL32(0, 0, 0, 180), 2.f);
                dl->AddRectFilled(ImVec2(left - xo, top + (boxH + 5.f - bh2)), ImVec2(left - xo + 3, top + boxH + 5), IM_COL32(80, 140, 255, 255), 1.5f);
            }

            // Text below box
            float textY = top + boxH + 8.f;
            if (g_cfg.distance_esp) {
                char db[32]; snprintf(db, sizeof(db), "%.0fm", e.distance);
                ImVec2 ds = ImGui::CalcTextSize(db);
                dl->AddText(ImVec2(sh.x - ds.x * 0.5f + 1, textY + 1), IM_COL32(0, 0, 0, 180), db);
                dl->AddText(ImVec2(sh.x - ds.x * 0.5f, textY),
                    IM_COL32((int)(g_cfg.distance_color[0] * 255), (int)(g_cfg.distance_color[1] * 255),
                        (int)(g_cfg.distance_color[2] * 255), 255), db);
                textY += 13.f;
            }
            if (g_cfg.weapon_esp && !e.weapon.empty()) {
                DrawWeaponTag(dl, sh.x, textY, e.weapon.c_str());
                textY += 16.f;
            }
            if (g_cfg.economy_esp && e.money > 0) {
                char mb[16]; snprintf(mb, sizeof(mb), "$%d", e.money);
                ImVec2 ms2 = ImGui::CalcTextSize(mb);
                dl->AddText(ImVec2(sh.x - ms2.x * 0.5f, textY), IM_COL32(0, 220, 80, 255), mb);
                textY += 13.f;
            }
            if (e.isDefusing) {
                const char* ds = "[DEFUSING]";
                ImVec2 dss = ImGui::CalcTextSize(ds);
                dl->AddText(ImVec2(sh.x - dss.x * 0.5f, textY), IM_COL32(255, 140, 0, 255), ds);
            }

            // Skeleton
            if (g_cfg.skeleton_esp && e.hasBoneData) {
                RGB sc2 = { (int)(g_cfg.skeleton_color[0] * 255),(int)(g_cfg.skeleton_color[1] * 255),
                           (int)(g_cfg.skeleton_color[2] * 255),(int)(g_cfg.skeleton_color[3] * 255) };
                ImU32 sCol = IM_COL32(sc2.R, sc2.G, sc2.B, 220);
                ImU32 sGlow = IM_COL32(sc2.R / 2, sc2.G / 2, sc2.B / 2, 50);
                static const std::pair<int, int> conns[] = {
                    {BONE_HEAD,BONE_NECK},{BONE_NECK,BONE_SPINE_0},{BONE_SPINE_0,BONE_PELVIS},
                    {BONE_NECK,BONE_L_SHOULDER},{BONE_L_SHOULDER,BONE_L_ELBOW},{BONE_L_ELBOW,BONE_L_WRIST},
                    {BONE_NECK,BONE_R_SHOULDER},{BONE_R_SHOULDER,BONE_R_ELBOW},{BONE_R_ELBOW,BONE_R_WRIST},
                    {BONE_PELVIS,BONE_L_HIP},{BONE_L_HIP,BONE_L_KNEE},{BONE_L_KNEE,BONE_L_ANKLE},
                    {BONE_PELVIS,BONE_R_HIP},{BONE_R_HIP,BONE_R_KNEE},{BONE_R_KNEE,BONE_R_ANKLE}
                };
                for (const auto& conn : conns) {
                    Vector3 b1w = e.bones[conn.first], b2w = e.bones[conn.second];
                    if (!(b1w.x || b1w.y || b1w.z) || !(b2w.x || b2w.y || b2w.z)) continue;
                    Vector3 b1 = b1w.w2s(vm), b2 = b2w.w2s(vm);
                    if (b1.z > 0.01f && b2.z > 0.01f) {
                        dl->AddLine(ImVec2(b1.x, b1.y), ImVec2(b2.x, b2.y), sGlow, 3.5f);
                        dl->AddLine(ImVec2(b1.x, b1.y), ImVec2(b2.x, b2.y), sCol, 1.5f);
                    }
                }
            }

            // Snap lines
            if (g_cfg.snaplines_esp) {
                ImU32 tipCol = e.isEnemy
                    ? IM_COL32((int)(g_cfg.snapline_color[0] * 255), (int)(g_cfg.snapline_color[1] * 255),
                        (int)(g_cfg.snapline_color[2] * 255), 200)
                    : IM_COL32(80, 140, 255, 200);
                DrawSnapLineGradient(dl, scx, scy, sf.x, sf.y, tipCol);
            }
        }
    }

} 