#pragma once
// Development for educational purposes
// CS2 External — Offset Updater v3
//
// BREAKING CHANGE from v2:
//   v2 wrote offsets_updated.h and told user to recompile — useless.
//   v3 patches offsets AT RUNTIME via a global table.
//   After OffsetChecker_RunStartup() completes, all offset reads go through
//   GetOffset(name) which checks the live table first, falls back to compiled.
//   Zero recompile needed after a CS2 update — just restart the cheat.
//
// Usage:
//   1. Replace every hardcoded offset access in mem:: calls with GetOffset().
//      e.g.  mem::ReadMem<int>(pawn + m_iHealth)
//        →   mem::ReadMem<int>(pawn + GetOffset("m_iHealth"))
//      OR: call PatchCompiledOffsets() once after startup check completes,
//          which overwrites the constexpr shadow table at runtime (simpler).
//
//   2. Call OffsetChecker_RunStartup() once at program start.
//   3. Call OffsetChecker_DrawStartupOverlay() every frame — returns false
//      once CS2 is running and offsets are verified/patched.
//
// Public symbols (matching offset_updater.cpp):

#include "../imgui/imgui.h"
#include <cstdint>

// Launch async offset fetch + diff. Safe to call from any thread.
// No-op if already running.
void OffsetChecker_RunStartup();

// Call every frame BEFORE feature drawing.
// Returns true  → startup still active, block cheat features.
// Returns false → done, CS2 running, cheat active.
bool OffsetChecker_DrawStartupOverlay();

// Render a condensed status line inside an existing ImGui window/tab.
void OffsetChecker_DrawMenuStatus();

// ── Runtime offset table ─────────────────────────────────────────────────────
// After startup completes, live values fetched from GitHub are stored here.
// GetOffset() returns the live value if present, else the compiled fallback.
uintptr_t GetOffset(const char* name);

// Call once after OffsetChecker_DrawStartupOverlay() first returns false.
// Writes all live fetched values into the mutable runtime shadow, so existing
// code using the constexpr names still works via the macro bridge below.
void PatchRuntimeOffsets();

// ── Legacy compat shims (OffsetChecker_Run / OffsetChecker_DrawMenu) ─────────
// These were the v1 names declared in the old header — keep them so
// any call site that used the old API compiles without change.
inline void OffsetChecker_Run()      { OffsetChecker_RunStartup();              }
inline void OffsetChecker_DrawMenu() { OffsetChecker_DrawMenuStatus();          }
