#pragma once
#include "../Esp.h"

namespace features {
	void BunnyHop(const GameData& gd);
	void NoFlash(const GameData& gd);
	void CustomCrosshair();
	void DrawBombTimerOverlay(const BombData& bomb);
	void DrawSpectatorList(const std::vector<std::string>& spectators);

} 
