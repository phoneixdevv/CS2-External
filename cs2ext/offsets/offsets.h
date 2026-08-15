#pragma once
// Source: https://github.com/a2x/cs2-dumper
// Dumped: 2026-08-10 22:53:50 UTC ← LATEST[cite: 8, 9]
#include <cstdint>
#include <cstddef>

//  Module-level pointers (client.dll base + offset) 
constexpr uintptr_t dwEntityList = 0x2554050;             //[cite: 8]
constexpr uintptr_t dwLocalPlayerController = 0x2383DA0;    //[cite: 8]
constexpr uintptr_t dwLocalPlayerPawn = 0x23A9118;        //[cite: 8]
constexpr uintptr_t dwViewMatrix = 0x23AE550;             //[cite: 8]
constexpr uintptr_t dwViewAngles = 0x23BF1A8;             //[cite: 8]
constexpr uintptr_t dwGlobalVars = 0x2094D38;             //[cite: 8]
constexpr uintptr_t dwPlantedC4 = 0x2373888;              //[cite: 8]
constexpr uintptr_t dwGameRules = 0x23A8BD8;              //[cite: 8]
constexpr uintptr_t dwGlowManager = 0x23A5908;            //[cite: 8]
constexpr uintptr_t dwCSGOInput = 0x23BEB20;              //[cite: 8]
constexpr uintptr_t dwForceJump = 0x2099510;
constexpr uintptr_t dwForceAttack = 0x2099000;
constexpr uintptr_t m_vecVelocity = 0x430;

//  C_BaseEntity 
constexpr uintptr_t m_iHealth = 0x34C;       // int32
constexpr uintptr_t m_iTeamNum = 0x3E7;      // uint8
constexpr uintptr_t m_fFlags = 0x3F4;        // uint32
constexpr uintptr_t m_pGameSceneNode = 0x330;   // CGameSceneNode*
constexpr uintptr_t m_nSubclassID = 0x380;   // CUtlStringToken (weapon vdata lookup)

//  C_BasePlayerPawn 
constexpr uintptr_t m_vOldOrigin = 0x13B8;     // Vector
constexpr uintptr_t m_pWeaponServices = 0x1208; // CPlayer_WeaponServices*
constexpr uintptr_t m_pCameraServices = 0x1240; // CPlayer_CameraServices*
constexpr uintptr_t m_pObserverServices = 0x1220; // CPlayer_ObserverServices*

//  C_BaseModelEntity 
constexpr uintptr_t m_vecViewOffset = 0xE78;   // CNetworkViewOffsetVector
constexpr uintptr_t m_Glow = 0xDE0;
constexpr uintptr_t m_iGlowType = 0x30;        // within CGlowProperty
constexpr uintptr_t m_glowColorOverride = 0x40; // within CGlowProperty (Color ABGR)
constexpr uintptr_t m_bGlowing = 0x51;         // within CGlowProperty (bool)

//  CCSPlayerBase_CameraServices 
constexpr uintptr_t m_iFOV = 0x290;          // uint32
constexpr uintptr_t m_iFOVStart = 0x294;
constexpr uintptr_t m_flFOVTime = 0x298;
constexpr uintptr_t m_flFOVRate = 0x29C;

//  C_CSPlayerPawn 
constexpr uintptr_t m_pAimPunchServices = 0x14B8; // CCSPlayer_AimPunchServices*
constexpr uintptr_t m_aimPunchAngle = 0x50;    // QAngle inside AimPunchServices
constexpr uintptr_t m_aimPunchAngleVel = 0x5C;
constexpr uintptr_t m_flFlashDuration = 0x1428;  // float32 (C_CSPlayerPawnBase)[cite: 9]
constexpr uintptr_t m_bIsWalking = 0x1C58;     // bool
constexpr uintptr_t m_entitySpottedState = 0x1C60; // EntitySpottedState_t base
constexpr uintptr_t m_bSpotted = 0x8;          // bool within EntitySpottedState_t
constexpr uintptr_t m_bSpottedByMask = 0xC;    // uint32[2] bitmask
constexpr uintptr_t m_bIsScoped = 0x1C78;      // bool
constexpr uintptr_t m_bResumeZoom = 0x1C79;    // bool
constexpr uintptr_t m_bIsDefusing = 0x1C7A;    // bool
constexpr uintptr_t m_ArmorValue = 0x1CA4;     // int32
constexpr uintptr_t m_angEyeAngles = 0x3350;   // QAngle (server-side)
constexpr uintptr_t m_iIDEntIndex = 0x342C;    // CEntityIndex (crosshair target)
constexpr uintptr_t m_iShotsFired = 0x1C8C;    // int32

//  CPlayer_WeaponServices 
constexpr uintptr_t m_hActiveWeapon = 0x60;    // CHandle<C_BasePlayerWeapon>
constexpr uintptr_t m_hMyWearables = 0x1180;   // CHandle (glove entity)
constexpr uintptr_t m_hMyWeapons = 0x48;       // CHandle array

constexpr uintptr_t m_AttributeManager = 0x11A8; // C_AttributeContainer (INLINE)
constexpr uintptr_t m_Item = 0x50;             // C_EconItemView offset within C_AttributeContainer[cite: 9]
constexpr uintptr_t m_iItemIDHigh = 0x1D0;      // within C_EconItemView[cite: 9]
constexpr uintptr_t m_iItemIDLow = 0x1D4;       // within C_EconItemView[cite: 9]
constexpr uintptr_t m_nFallbackPaintKit = 0x1680; // DIRECT on weapon entity (C_EconEntity)
constexpr uintptr_t m_nFallbackSeed = 0x1684;   // DIRECT on weapon entity
constexpr uintptr_t m_flFallbackWear = 0x1688;   // DIRECT on weapon entity
constexpr uintptr_t m_nFallbackStatTrak = 0x1689; // DIRECT on weapon entity

// Weapon vdata name (for weapon name display)
constexpr uintptr_t m_szName = 0x720;          // CGlobalSymbol within CCSWeaponBaseVData

//  C_BasePlayerWeapon 
constexpr uintptr_t m_iClip1 = 0x1700;
constexpr uintptr_t m_pReserveAmmo = 0x1708;

//  CBasePlayerController 
constexpr uintptr_t m_hPawn = 0x6BC;           // CHandle<C_BasePlayerPawn>
constexpr uintptr_t m_iszPlayerName = 0x6F4;   // char[128]
constexpr uintptr_t m_hPlayerPawn = 0x914;     // CHandle<C_CSPlayerPawn>

//  CCSPlayerController 
constexpr uintptr_t m_pInGameMoneyServices = 0x810;
constexpr uintptr_t m_iAccount = 0x40;         // within CCSPlayerController_InGameMoneyServices
constexpr uintptr_t m_iCompetitiveRanking = 0x888;

//  CPlayer_ObserverServices 
constexpr uintptr_t m_iObserverMode = 0x48;    // uint8
constexpr uintptr_t m_hObserverTarget = 0x4C;    // CHandle

//  Animation / Bones 
constexpr uintptr_t m_modelState = 0x140;
constexpr uintptr_t m_skeletonInstance = 0x80;   // bone matrix ptr offset within modelState

//  Bomb / C_PlantedC4 
constexpr uintptr_t m_bBombTicking = 0x11A0;
constexpr uintptr_t m_flC4Blow = 0x11D0;
constexpr uintptr_t m_flDefuseCountDown = 0x11F0;
constexpr uintptr_t m_bBombDefused = 0x11F4;

constexpr uintptr_t m_nAgentItem = 0x620;      // C_EconItemView (inline on pawn)
constexpr uintptr_t m_nPawnCharacterDefIndex = 0x92A; // uint16 on CCSPlayerController
constexpr uintptr_t m_vecNetworkableLoadout = 0x40; // within InventoryServices[cite: 9]