#pragma once
//   0 = FOV-priority   — closest pixel to crosshair (default)
//   1 = Distance       — closest world-space enemy
//   2 = Hybrid         — fovPx * (distM / 100) — balances both
#include "../Esp.h"
namespace features {
	void Aimbot(const std::vector<EntityData>& entities, const GameData& gd);
}