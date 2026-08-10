#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../imgui/imgui.h"
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <regex>

#pragma comment(lib, "winhttp.lib")

static void RunCheckerThread();
static std::string DotAnim();

enum class OcState : int {
    IDLE = 0,
    CHECKING,
    OK_CS2_CLOSED,
    OK_CS2_RUNNING,
    OUTDATED_PATCHING,
    OUTDATED_DONE,
    NET_ERROR,
};

struct OcContext {
    std::mutex  mtx;
    OcState     state = OcState::IDLE;
    std::string statusLine;
    std::string detailLine;
    std::string fullReport;
    int         okCount = 0;
    int         badCount = 0;
    int         missCount = 0;
    int         manualCount = 0;
};

static OcContext g_oc;

static std::mutex                                 g_liveMtx;
static std::unordered_map<std::string, uintptr_t> g_liveOffsets;
static std::atomic<bool>                          g_patchApplied{ false };

uintptr_t GetOffset(const char* name) {
    std::lock_guard<std::mutex> lk(g_liveMtx);
    auto it = g_liveOffsets.find(name);
    return it != g_liveOffsets.end() ? it->second : 0;
}

static bool IsCs2Running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Siberdeyiz Logo Renderer (from SVG paths)
static void DrawSiberdeyizLogo(ImDrawList* dl, ImVec2 origin, float scale, ImU32 colBottom, ImU32 colTop) {
    // Path 1 (Bottom Ribbon)
    ImVec2 p1[] = {
        {169.f, 244.f}, {169.f, 310.f}, {318.f, 387.f}, {328.f, 394.f},
        {209.f, 454.f}, {192.f, 465.f}, {250.f, 499.f}, {261.f, 501.f},
        {421.f, 419.f}, {430.f, 412.f}, {430.f, 376.f}, {426.f, 372.f}, {214.f, 264.f}
    };
    ImVec2 scaledP1[13];
    for (int i = 0; i < 13; ++i) {
        scaledP1[i] = ImVec2(origin.x + p1[i].x * scale, origin.y + p1[i].y * scale);
    }
    dl->AddConvexPolyFilled(scaledP1, 13, colBottom);

    // Path 2 (Top Ribbon)
    ImVec2 p2[] = {
        {170.f, 188.f}, {171.f, 229.f}, {419.f, 354.f}, {430.f, 357.f},
        {430.f, 292.f}, {270.f, 208.f}, {275.f, 203.f}, {407.f, 138.f},
        {395.f, 129.f}, {353.f, 105.f}, {340.f, 100.f}
    };
    ImVec2 scaledP2[11];
    for (int i = 0; i < 11; ++i) {
        scaledP2[i] = ImVec2(origin.x + p2[i].x * scale, origin.y + p2[i].y * scale);
    }
    dl->AddConvexPolyFilled(scaledP2, 11, colTop);
}


struct OcEntry {
    const char* name;
    const char* jsonFile;
    const char* jsonPath;
    uintptr_t   compiled;
};

static const OcEntry k_oc[] = {
    { "dwEntityList",            "offsets",    "client.interfaces.dwEntityList",                     0x254FE80 },
    { "dwLocalPlayerController", "offsets",    "client.interfaces.dwLocalPlayerController",          0x237FB80 },
    { "dwLocalPlayerPawn",       "offsets",    "client.interfaces.dwLocalPlayerPawn",                0x23A5238 },
    { "dwViewMatrix",            "offsets",    "client.interfaces.dwViewMatrix",                     0x23AA340 },
    { "dwViewAngles",            "offsets",    "client.interfaces.dwViewAngles",                     0x23BAE18 },
    { "dwGlobalVars",            "offsets",    "client.interfaces.dwGlobalVars",                     0x2090D60 },
    { "dwPlantedC4",             "offsets",    "client.interfaces.dwPlantedC4",                      0x236F658 },
    { "dwForceJump",             "offsets",    "client.interfaces.dwForceJump",                      0x2095490 },
    { "dwForceAttack",           "offsets",    "client.interfaces.dwForceAttack",                    0x2094F80 },
    { "m_iHealth",               "client.dll", "classes.C_BaseEntity.fields.m_iHealth",              0x34C     },
    { "m_iTeamNum",              "client.dll", "classes.C_BaseEntity.fields.m_iTeamNum",             0x3E7     },
    { "m_fFlags",                "client.dll", "classes.C_BaseEntity.fields.m_fFlags",               0x3F4     },
    { "m_pGameSceneNode",        "client.dll", "classes.C_BaseEntity.fields.m_pGameSceneNode",       0x330     },
    { "m_vOldOrigin",            "client.dll", "classes.C_BasePlayerPawn.fields.m_vOldOrigin",       0x13B8    },
    { "m_pWeaponServices",       "client.dll", "classes.C_BasePlayerPawn.fields.m_pWeaponServices",  0x1208    },
    { "m_pCameraServices",       "client.dll", "classes.C_BasePlayerPawn.fields.m_pCameraServices",  0x1240    },
    { "m_pObserverServices",     "client.dll", "classes.C_BasePlayerPawn.fields.m_pObserverServices",0x1220    },
    { "m_vecViewOffset",         "client.dll", "classes.C_BaseModelEntity.fields.m_vecViewOffset",   0xE78     },
    { "m_Glow",                  "client.dll", "classes.C_BaseModelEntity.fields.m_Glow",            0xDE0     },
    { "m_flFlashDuration",       "client.dll", "classes.C_CSPlayerPawn.fields.m_flFlashDuration",    0x1428    },
    { "m_bIsWalking",            "client.dll", "classes.C_CSPlayerPawn.fields.m_bIsWalking",         0x1C58    },
    { "m_entitySpottedState",    "client.dll", "classes.C_CSPlayerPawn.fields.m_entitySpottedState", 0x1C60    },
    { "m_bIsScoped",             "client.dll", "classes.C_CSPlayerPawn.fields.m_bIsScoped",          0x1C78    },
    { "m_bIsDefusing",           "client.dll", "classes.C_CSPlayerPawn.fields.m_bIsDefusing",        0x1C7A    },
    { "m_ArmorValue",            "client.dll", "classes.C_CSPlayerPawn.fields.m_ArmorValue",         0x1CA4    },
    { "m_iIDEntIndex",           "client.dll", "classes.C_CSPlayerPawn.fields.m_iIDEntIndex",        0x342C    },
    { "m_iShotsFired",           "client.dll", "classes.C_CSPlayerPawn.fields.m_iShotsFired",        0x1C8C    },
    { "m_angEyeAngles",          "client.dll", "classes.C_CSPlayerPawn.fields.m_angEyeAngles",       0x3350    },
    { "m_pAimPunchServices",     "client.dll", "classes.C_CSPlayerPawn.fields.m_pAimPunchServices",  0x14B8    },
    { "m_AttributeManager",      "client.dll", "classes.C_EconEntity.fields.m_AttributeManager",     0x11A8    },
    { "m_nFallbackPaintKit",     "client.dll", "classes.C_EconEntity.fields.m_nFallbackPaintKit",    0x1680    },
    { "m_nFallbackSeed",         "client.dll", "classes.C_EconEntity.fields.m_nFallbackSeed",        0x1684    },
    { "m_flFallbackWear",        "client.dll", "classes.C_EconEntity.fields.m_flFallbackWear",       0x1688    },
    { "m_nFallbackStatTrak",     "client.dll", "classes.C_EconEntity.fields.m_nFallbackStatTrak",    0x168C    },
    { "m_hPawn",                 "client.dll", "classes.CBasePlayerController.fields.m_hPawn",       0x6BC     },
    { "m_iszPlayerName",         "client.dll", "classes.CBasePlayerController.fields.m_iszPlayerName",0x6F4    },
    { "m_hPlayerPawn",           "client.dll", "classes.CCSPlayerController.fields.m_hPlayerPawn",   0x914     },
    { "m_pInGameMoneyServices",  "client.dll", "classes.CCSPlayerController.fields.m_pInGameMoneyServices",0x810},
    { "m_iCompetitiveRanking",   "client.dll", "classes.CCSPlayerController.fields.m_iCompetitiveRanking",0x888},
    { "m_bBombTicking",          "client.dll", "classes.C_PlantedC4.fields.m_bBombTicking",          0x11A0    },
    { "m_flC4Blow",              "client.dll", "classes.C_PlantedC4.fields.m_flC4Blow",              0x11D0    },
    { "m_flDefuseCountDown",     "client.dll", "classes.C_PlantedC4.fields.m_flDefuseCountDown",     0x11F0    },
    { "m_bBombDefused",          "client.dll", "classes.C_PlantedC4.fields.m_bBombDefused",          0x11F4    },
    { "m_nAgentItem",            "client.dll", "classes.C_CSPlayerPawn.fields.m_nAgentItem",           0x620     },
    { "m_nPawnCharacterDefIndex","client.dll", "classes.CCSPlayerController.fields.m_nPawnCharacterDefIndex", 0x92A },
    { "m_vecNetworkableLoadout", "client.dll", "classes.CCSPlayerController_InventoryServices.fields.m_vecNetworkableLoadout", 0x40 },
};
static constexpr size_t k_ocCount = sizeof(k_oc) / sizeof(k_oc[0]);

static std::unordered_map<std::string, uintptr_t> ReadDiskOffsets(const std::string& exeDir) {
    std::unordered_map<std::string, uintptr_t> result;
    const std::string candidates[] = {
        exeDir + "offsets\\offsets.h",
        exeDir + "..\\offsets\\offsets.h",
        exeDir + "offsets.h",
        exeDir + "cheats\\offsets\\offsets.h",
    };
    std::ifstream f;
    for (const auto& p : candidates) {
        f.open(p);
        if (f.is_open()) break;
    }
    if (!f.is_open()) return result;
    std::string line;
    while (std::getline(f, line)) {
        size_t cpos = line.find("constexpr uintptr_t ");
        if (cpos == std::string::npos) continue;
        size_t nameStart = cpos + 20;
        size_t eq = line.find('=', nameStart);
        if (eq == std::string::npos) continue;
        std::string name = line.substr(nameStart, eq - nameStart);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        size_t valStart = eq + 1;
        while (valStart < line.size() && (line[valStart] == ' ' || line[valStart] == '\t')) ++valStart;
        size_t valEnd = valStart;
        while (valEnd < line.size() && line[valEnd] != ';' && line[valEnd] != '/' &&
            line[valEnd] != ' ' && line[valEnd] != '\t') ++valEnd;
        std::string valStr = line.substr(valStart, valEnd - valStart);
        if (valStr.empty()) continue;
        try {
            size_t n = 0;
            uintptr_t val = (valStr.size() > 2 && valStr[0] == '0' && (valStr[1] == 'x' || valStr[1] == 'X'))
                ? (uintptr_t)std::stoull(valStr, &n, 16)
                : (uintptr_t)std::stoull(valStr, &n, 10);
            if (n > 0) result[name] = val;
        }
        catch (...) { continue; }
    }
    return result;
}

static std::string JsonExtract(const std::string& json, const char* dotPath) {
    std::vector<std::string> segs;
    {
        const char* p = dotPath;
        while (true) {
            const char* dot = strchr(p, '.');
            segs.push_back(dot ? std::string(p, dot) : std::string(p));
            if (!dot) break;
            p = dot + 1;
        }
    }
    size_t searchPos = 0;
    for (size_t si = 0; si < segs.size(); ++si) {
        std::string needle = "\"" + segs[si] + "\"";
        size_t found = json.find(needle, searchPos);
        if (found == std::string::npos) return "";
        size_t colon = json.find(':', found + needle.size());
        if (colon == std::string::npos) return "";
        size_t vs = colon + 1;
        while (vs < json.size() && (json[vs] == ' ' || json[vs] == '\t' ||
            json[vs] == '\r' || json[vs] == '\n')) ++vs;
        if (vs >= json.size()) return "";
        if (si == segs.size() - 1) {
            if (json[vs] == '"') {
                size_t qe = json.find('"', vs + 1);
                return qe == std::string::npos ? "" : json.substr(vs + 1, qe - vs - 1);
            }
            else if (json[vs] == '{' || json[vs] == '[') {
                return "";
            }
            else {
                size_t ve = vs;
                while (ve < json.size() && json[ve] != ',' && json[ve] != '\n' &&
                    json[ve] != '}' && json[ve] != ']' && json[ve] != ' ') ++ve;
                return json.substr(vs, ve - vs);
            }
        }
        else {
            if (json[vs] == '{') searchPos = vs + 1;
            else return "";
        }
    }
    return "";
}

static bool ParseHex(const std::string& s, uintptr_t& out) {
    if (s.empty()) return false;
    try {
        size_t n = 0;
        out = (uintptr_t)(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')
            ? std::stoull(s, &n, 16) : std::stoull(s, &n, 10));
        return n > 0;
    }
    catch (...) { return false; }
}

static std::string HttpGet(const wchar_t* host, const wchar_t* path) {
    std::string result;
    HINTERNET hS = WinHttpOpen(L"CS2OC/4.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return result;
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return result; }
    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return result; }
    if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hR, nullptr)) {
        char buf[8192]; DWORD rd = 0;
        while (WinHttpReadData(hR, buf, sizeof(buf), &rd) && rd > 0)
            result.append(buf, rd);
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return result;
}

static std::string GetExeDir() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    size_t p = s.find_last_of("\\/");
    return p != std::string::npos ? s.substr(0, p + 1) : s;
}

static void WriteReferenceHeader(const std::string& exeDir,
    const std::vector<uintptr_t>& outVals,
    const std::vector<bool>& wasManual) {
    std::ostringstream ss;
    ss << "#pragma once\n// Auto-generated by offset_updater v4\n"
        << "#include <cstdint>\n#include <cstddef>\n\n";
    for (size_t i = 0; i < k_ocCount; ++i) {
        ss << "constexpr uintptr_t " << k_oc[i].name
            << " = 0x" << std::hex << outVals[i] << std::dec << ";";
        if (wasManual[i])
            ss << " // MANUALLY PROTECTED (user edit preserved)";
        else if (outVals[i] != k_oc[i].compiled)
            ss << " // auto-updated (was 0x" << std::hex << k_oc[i].compiled << std::dec << ")";
        ss << "\n";
    }

    ss << R"(
constexpr uintptr_t m_modelState            = 0x140;
constexpr uintptr_t m_skeletonInstance      = 0x80;
constexpr uintptr_t m_Item                  = 0x50;
constexpr uintptr_t m_iItemIDHigh           = 0x1D0;
constexpr uintptr_t m_iItemIDLow            = 0x1D4;
constexpr uintptr_t m_iGlowType             = 0x30;
constexpr uintptr_t m_glowColorOverride     = 0x40;
constexpr uintptr_t m_bGlowing              = 0x51;
constexpr uintptr_t m_iFOV                  = 0x290;
constexpr uintptr_t m_aimPunchAngle         = 0x50;
constexpr uintptr_t m_aimPunchAngleVel      = 0x5C;
constexpr uintptr_t m_bSpotted              = 0x8;
constexpr uintptr_t m_bSpottedByMask        = 0xC;
constexpr uintptr_t m_iObserverMode         = 0x48;
constexpr uintptr_t m_hObserverTarget       = 0x4C;
constexpr uintptr_t m_hActiveWeapon         = 0x60;
constexpr uintptr_t m_iAccount              = 0x40;
constexpr uintptr_t m_iClip1               = 0x1700;
constexpr uintptr_t m_pReserveAmmo         = 0x1708;
constexpr uintptr_t m_nSubclassID          = 0x380;
constexpr uintptr_t m_szName               = 0x720;
constexpr uintptr_t m_hMyWearables         = 0x1180;
constexpr uintptr_t m_hMyWeapons           = 0x48;
constexpr uintptr_t m_vecNetworkableLoadout= 0x40;
)";
    std::ofstream f(exeDir + "offsets_updated.h");
    if (f) f << ss.str();
}

static void RunCheckerThread() {
    static const wchar_t* HOST = L"raw.githubusercontent.com";
    static const wchar_t* P_OFF = L"/a2x/cs2-dumper/main/output/windows/offsets.json";
    static const wchar_t* P_CLI = L"/a2x/cs2-dumper/main/output/windows/client.dll.json";

    std::string exeDir = GetExeDir();
    auto diskOffsets = ReadDiskOffsets(exeDir);
    bool hasDisk = !diskOffsets.empty();

    std::string jOff = HttpGet(HOST, P_OFF);
    std::string jCli = HttpGet(HOST, P_CLI);

    if (jOff.empty() && jCli.empty()) {
        {
            std::lock_guard<std::mutex> lk(g_liveMtx);
            g_liveOffsets.clear();
            for (size_t i = 0; i < k_ocCount; ++i) {
                auto it = diskOffsets.find(k_oc[i].name);
                g_liveOffsets[k_oc[i].name] = (it != diskOffsets.end()) ? it->second : k_oc[i].compiled;
            }
        }
        std::lock_guard<std::mutex> lk(g_oc.mtx);
        g_oc.state = OcState::NET_ERROR;
        g_oc.statusLine = "Network error. Using disk offsets.";
        return;
    }

    std::ostringstream report;
    std::vector<uintptr_t> outVals(k_ocCount);
    std::vector<bool>      wasManual(k_ocCount, false);
    int ok = 0, bad = 0, miss = 0, manual = 0;

    for (size_t i = 0; i < k_ocCount; ++i) {
        const OcEntry& e = k_oc[i];
        const std::string& j = (std::string(e.jsonFile) == "offsets") ? jOff : jCli;
        std::string raw = JsonExtract(j, e.jsonPath);
        uintptr_t ghVal = e.compiled;
        bool ghParsed = !raw.empty() && ParseHex(raw, ghVal);

        uintptr_t diskVal = e.compiled;
        auto dit = diskOffsets.find(e.name);
        if (hasDisk && dit != diskOffsets.end()) diskVal = dit->second;

        bool userManuallyEdited = hasDisk && (diskVal != e.compiled);

        if (userManuallyEdited) {
            outVals[i] = diskVal;
            wasManual[i] = true;
            report << "[MANUAL]   " << e.name << " = 0x" << std::hex << diskVal << std::dec << " (protected)\n";
            ++manual;
        }
        else if (!ghParsed) {
            outVals[i] = diskVal;
            report << "[MISSING]  " << e.name << " (keeping disk: 0x" << std::hex << diskVal << std::dec << ")\n";
            ++miss;
        }
        else if (ghVal == diskVal) {
            outVals[i] = diskVal;
            report << "[OK]       " << e.name << " = 0x" << std::hex << diskVal << std::dec << "\n";
            ++ok;
        }
        else {
            outVals[i] = ghVal;
            report << "[UPDATED]  " << e.name << "  disk=0x" << std::hex << diskVal << "  gh=0x" << ghVal << std::dec << "\n";
            ++bad;
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_liveMtx);
        g_liveOffsets.clear();
        for (size_t i = 0; i < k_ocCount; ++i)
            g_liveOffsets[k_oc[i].name] = outVals[i];
    }

    {
        std::lock_guard<std::mutex> lk(g_oc.mtx);
        g_oc.okCount = ok;
        g_oc.badCount = bad;
        g_oc.missCount = miss;
        g_oc.manualCount = manual;
        g_oc.fullReport = report.str();
    }

    WriteReferenceHeader(exeDir, outVals, wasManual);

    if (bad == 0 && miss == 0) {
        if (IsCs2Running()) {
            std::lock_guard<std::mutex> lk(g_oc.mtx);
            g_oc.state = OcState::OK_CS2_RUNNING;
            g_oc.statusLine = "";
        }
        else {
            std::lock_guard<std::mutex> lk(g_oc.mtx);
            g_oc.state = OcState::OK_CS2_CLOSED;
            g_oc.statusLine = manual > 0 ? std::to_string(manual) + " manual offsets protected, all others OK." : "All offsets up to date.";
            g_oc.detailLine = "Open CS2 to continue.";
        }
    }
    else {
        {
            std::lock_guard<std::mutex> lk(g_oc.mtx);
            g_oc.state = OcState::OUTDATED_PATCHING;
            g_oc.statusLine = "Patching offsets at runtime";
            g_oc.detailLine = std::to_string(bad) + " updated" + (manual > 0 ? ", " + std::to_string(manual) + " protected" : "") + ".";
        }
        std::lock_guard<std::mutex> lk(g_oc.mtx);
        g_oc.state = OcState::OUTDATED_DONE;
        g_oc.statusLine = "Offsets live-patched!";
        g_oc.detailLine = std::to_string(bad) + " updated, " + std::to_string(ok) + " OK" + (manual > 0 ? ", " + std::to_string(manual) + " protected" : "") + ".";
    }
}

void PatchRuntimeOffsets() {
    if (g_patchApplied.load()) return;
    g_patchApplied.store(true);
}

static std::string DotAnim() {
    static DWORD last = 0; static int dots = 0;
    DWORD now = GetTickCount();
    if (now - last > 400) { dots = (dots + 1) % 4; last = now; }
    return std::string(dots, '.');
}

void OffsetChecker_RunStartup() {
    {
        std::lock_guard<std::mutex> lk(g_oc.mtx);
        if (g_oc.state != OcState::IDLE) return;
        g_oc.state = OcState::CHECKING;
        g_oc.statusLine = "Checking offsets";
        g_oc.detailLine = "";
    }
    std::thread(RunCheckerThread).detach();
}

bool OffsetChecker_DrawStartupOverlay() {
    OcState     state;
    std::string status, detail, report;
    {
        std::lock_guard<std::mutex> lk(g_oc.mtx);
        state = g_oc.state;
        status = g_oc.statusLine;
        detail = g_oc.detailLine;
        report = g_oc.fullReport;
    }

    if (state == OcState::IDLE) return false;

    if (state == OcState::OK_CS2_RUNNING) {
        if (!g_patchApplied.load()) PatchRuntimeOffsets();

        // Oyun çalışırken kapanırsa hileyi de otomatik kapat
        static DWORD lastExitPoll = 0;
        DWORD now = GetTickCount();
        if (now - lastExitPoll > 1000) {
            lastExitPoll = now;
            if (!IsCs2Running()) {
                exit(0); // CS2 kapandı, hile de kapanıyor
            }
        }
        return false;
    }
    if (state == OcState::OUTDATED_DONE && IsCs2Running()) {
        if (!g_patchApplied.load()) PatchRuntimeOffsets();
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(10, 5, 5, 230));

    const float cardW = 560.f, cardH = 250.f;
    float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
    float x0 = cx - cardW * 0.5f, y0 = cy - cardH * 0.5f;
    float x1 = cx + cardW * 0.5f, y1 = cy + cardH * 0.5f;

    // Kırmızı - Siyah Tema Renkleri
    ImU32 borderCol = IM_COL32(200, 40, 40, 255);
    if (state == OcState::OK_CS2_CLOSED)     borderCol = IM_COL32(220, 60, 60, 255);
    if (state == OcState::OUTDATED_PATCHING) borderCol = IM_COL32(240, 140, 30, 255);
    if (state == OcState::OUTDATED_DONE)     borderCol = IM_COL32(230, 40, 40, 255);
    if (state == OcState::NET_ERROR)         borderCol = IM_COL32(180, 20, 20, 255);

    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(18, 12, 12, 250), 10.f);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), borderCol, 10.f, 0, 2.f);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + 38.f), IM_COL32(28, 14, 14, 255), 10.f);

    // Siberdeyiz Logosunu Başlığa Ekle
    DrawSiberdeyizLogo(dl, ImVec2(x0 + 12.f, y0 + 7.f), 0.045f, IM_COL32(160, 160, 160, 255), IM_COL32(240, 240, 240, 255));

    const char* title = "     SiberdeyizCS2.gg  |  Offset Checker";
    ImVec2 tsz = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(cx - tsz.x * 0.5f + 10.f, y0 + 10.f), IM_COL32(240, 180, 180, 255), title);

    float textY = y0 + 62.f;

    if (state == OcState::CHECKING) {
        std::string anim = status + DotAnim();
        ImVec2 msz = ImGui::CalcTextSize(anim.c_str());
        dl->AddText(ImVec2(cx - msz.x * 0.5f, textY), IM_COL32(240, 180, 180, 255), anim.c_str());
        DWORD t = GetTickCount();
        for (int i = 0; i < 8; ++i) {
            float angle = (float)i / 8.f * 6.2832f + (t / 160.f);
            float px2 = cx + cosf(angle) * 22.f, py2 = y0 + 140.f + sinf(angle) * 22.f;
            float alpha = (float)((i + (int)(t / 160)) % 8) / 8.f * 190.f + 65.f;
            dl->AddCircleFilled(ImVec2(px2, py2), 4.f, IM_COL32(240, 50, 50, (int)alpha));
        }
    }
    else if (state == OcState::OK_CS2_CLOSED || state == OcState::OUTDATED_DONE) {
        ImVec2 m1sz = ImGui::CalcTextSize(status.c_str());
        dl->AddText(ImVec2(cx - m1sz.x * 0.5f, textY), IM_COL32(240, 80, 80, 255), status.c_str());
        ImVec2 dsz = ImGui::CalcTextSize(detail.c_str());
        dl->AddText(ImVec2(cx - dsz.x * 0.5f, textY + 24.f), IM_COL32(180, 130, 130, 255), detail.c_str());
        float pulse = (sinf((float)GetTickCount() / 420.f) + 1.f) * 0.5f;
        ImU32 pulseCol = IM_COL32((int)(200 + 55 * pulse), (int)(40 * pulse), (int)(40 * pulse), 255);
        const char* hint = ">> Press DEL on your keyboard and close the cheat. <<";
        ImVec2 hsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cx - hsz.x * 0.5f, y1 - 46.f), pulseCol, hint);
        static DWORD lastPoll = 0; DWORD now = GetTickCount();
        if (now - lastPoll > 800) {
            lastPoll = now;
            if (IsCs2Running()) { std::lock_guard<std::mutex> lk(g_oc.mtx); g_oc.state = OcState::OK_CS2_RUNNING; }
        }
    }
    else if (state == OcState::OUTDATED_PATCHING) {
        std::string anim = status + DotAnim();
        ImVec2 msz = ImGui::CalcTextSize(anim.c_str());
        dl->AddText(ImVec2(cx - msz.x * 0.5f, textY), IM_COL32(240, 180, 40, 255), anim.c_str());
        float prog = (sinf((float)GetTickCount() / 280.f) + 1.f) * 0.5f;
        float bx0 = x0 + 44.f, bx1 = x1 - 44.f, by = textY + 52.f;
        dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + 10.f), IM_COL32(48, 32, 32, 255), 4.f);
        dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * prog, by + 10.f), IM_COL32(240, 60, 60, 200), 4.f);
        ImVec2 dsz = ImGui::CalcTextSize(detail.c_str());
        dl->AddText(ImVec2(cx - dsz.x * 0.5f, textY + 72.f), IM_COL32(180, 150, 150, 255), detail.c_str());
    }
    else if (state == OcState::NET_ERROR) {
        ImVec2 msz = ImGui::CalcTextSize(status.c_str());
        dl->AddText(ImVec2(cx - msz.x * 0.5f, textY), IM_COL32(230, 70, 60, 255), status.c_str());
        const char* sub = "Could not reach GitHub. Check internet.";
        ImVec2 ssz = ImGui::CalcTextSize(sub);
        dl->AddText(ImVec2(cx - ssz.x * 0.5f, textY + 26.f), IM_COL32(180, 90, 90, 255), sub);
        ImGui::SetNextWindowPos(ImVec2(cx - 74.f, y1 - 44.f));
        ImGui::SetNextWindowSize(ImVec2(148.f, 34.f));
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin("##ocretry", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        if (ImGui::Button("Retry", ImVec2(-1, 26))) {
            { std::lock_guard<std::mutex> lk(g_oc.mtx); g_oc.state = OcState::CHECKING; g_oc.statusLine = "Checking offsets"; }
            std::thread(RunCheckerThread).detach();
        }
        ImGui::End();
    }
    return true;
}

void OffsetChecker_DrawMenuStatus() {
    OcState     state;
    std::string status;
    int ok, bad, miss, manual2;
    {
        std::lock_guard<std::mutex> lk(g_oc.mtx);
        state = g_oc.state;
        status = g_oc.statusLine;
        ok = g_oc.okCount;
        bad = g_oc.badCount;
        miss = g_oc.missCount;
        manual2 = g_oc.manualCount;
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.f));
    ImGui::SeparatorText("Offset Status  (a2x/cs2-dumper)");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (state == OcState::CHECKING) {
        ImGui::TextColored(ImVec4(1.f, 0.6f, 0.3f, 1.f), "%s", (status + DotAnim()).c_str());
    }
    else if (state == OcState::OK_CS2_RUNNING || state == OcState::OK_CS2_CLOSED) {
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "All good — %d offsets current", ok);
        if (manual2 > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.8f, 1.f), "  %d user-protected (manual edits preserved)", manual2);
        ImGui::TextDisabled("  No recompile needed.");
    }
    else if (state == OcState::OUTDATED_DONE) {
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.6f, 1.f), "Live-patched: %d updated, %d OK", bad, ok);
        if (manual2 > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.8f, 1.f), "  %d manual offsets protected from overwrite", manual2);
        ImGui::TextDisabled("  Running from runtime table — no recompile.");
    }
    else if (state == OcState::NET_ERROR) {
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "Network error — using disk/compiled fallback.");
    }

    ImGui::Spacing();
    std::string fullReport;
    { std::lock_guard<std::mutex> lk(g_oc.mtx); fullReport = g_oc.fullReport; }
    if (!fullReport.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.7f, 1.f));
        ImGui::SeparatorText("Diff Log");
        ImGui::PopStyleColor();
        ImGui::InputTextMultiline("##oclog",
            const_cast<char*>(fullReport.c_str()), fullReport.size() + 1,
            ImVec2(-1.f, 260.f), ImGuiInputTextFlags_ReadOnly);
    }
}