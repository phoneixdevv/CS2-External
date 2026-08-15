#pragma once
#include <Windows.h>
#include <vector>
#include <deque>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include "../mem/memory.h"
#include "../offsets/offsets.h"
#include "../math/math.h"
#include "config.h"

struct Color4 {
    int R, G, B, A;
};
typedef Color4 RGB;
extern bool isScoped;

inline Color4 green = { 0,   255, 0,   255 };
inline Color4 red = { 255, 0,   0,   255 };
inline Color4 white = { 255, 255, 255, 255 };
inline Color4 black = { 0,   0,   0,   255 };
inline Color4 blue = { 0,   100, 255, 255 };
inline Color4 yellow = { 255, 255, 0,   255 };
inline Color4 cyan = { 0,   255, 255, 255 };
inline Color4 orange = { 255, 165, 0,   255 };
inline Color4 purple = { 128, 0,   255, 255 };

enum BoneId : int {
    BONE_ORIGIN = 0,
    BONE_PELVIS = 1,
    BONE_SPINE_0 = 2,
    BONE_SPINE_1 = 3,
    BONE_SPINE_2 = 4,
    BONE_NECK = 6,
    BONE_HEAD = 7,
    BONE_L_SHOULDER = 9,
    BONE_L_ELBOW = 10,
    BONE_L_WRIST = 11,
    BONE_R_SHOULDER = 13,
    BONE_R_ELBOW = 14,
    BONE_R_WRIST = 15,
    BONE_L_HIP = 17,
    BONE_L_KNEE = 18,
    BONE_L_ANKLE = 19,
    BONE_R_HIP = 20,
    BONE_R_KNEE = 21,
    BONE_R_ANKLE = 22,
    BONE_CHEST = 23
};

struct EntityData {
    Vector3     origin;
    Vector3     head;
    Vector3     headBone;
    Vector3     bones[25];
    int         health = 0;
    int         armor = 0;
    int         team = 0;
    int         money = 0;
    int         rank = 0;
    std::string name;
    std::string weapon;
    float       distance = 0.0f;
    bool        isScoped = false;
    bool        isWalking = false;
    bool        isSpotted = false;
    bool        isEnemy = false;
    uintptr_t   pawnAddress = 0;
    bool        valid = false;
    bool        hasBoneData = false;
    bool        radarWritten = false;
    explicit operator bool() const { return valid; }
};

struct SpectatorEntry {
    std::string name;
};

struct GameData {
    std::mutex     mtx;
    std::uintptr_t localPlayerPawn = 0;
    std::uintptr_t localPlayerController = 0;
    std::uintptr_t entList = 0;
    view_matrix_t  view_matrix;
    int            localTeam = 0;
    int            localPlayerIndex = 0;
    std::uintptr_t client = 0;
    DWORD          pid = 0;
    bool           isInGame = false;

    bool IsValid() const {
        return localPlayerPawn != 0 && entList != 0 && isInGame;
    }
};

//  Snapshot types 
using EntitySnapshot = std::shared_ptr<const std::vector<EntityData>>;
using SpectatorSnapshot = std::shared_ptr<const std::vector<SpectatorEntry>>;

class EntityCache {
public:
    explicit EntityCache(UINT rn = 144, UINT rd = 1, int tick = 128);
    ~EntityCache();

    EntitySnapshot    GetEntities();
    SpectatorSnapshot GetSpectators();

    void Start(uintptr_t client, const GameData* gd);
    void Stop();

    //  FIXED: atomic so main-thread writes are immediately visible to worker 
    std::atomic<int> update_interval_ms{ 4 };

private:
    void WorkerLoop();
    std::vector<EntityData>     ReadEntities();
    std::vector<SpectatorEntry> ReadSpectators();
    std::string GetWeaponName(uintptr_t pawn);
    void        SetFOV(float fov);

    std::mutex        mtx;
    EntitySnapshot    cached_entities;
    SpectatorSnapshot cached_spectators;
    std::thread       worker_thread;
    std::atomic<bool> should_run{ false };
    uintptr_t         client_dll = 0;
    const GameData* g_data = nullptr;
};