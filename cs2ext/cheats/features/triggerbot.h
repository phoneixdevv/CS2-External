#pragma once
#include "../Esp.h"

namespace features {
void Triggerbot(const std::vector<EntityData>& entities, const GameData& gd);
void TriggerbotForceShoot(const GameData& gd);
void TriggerbotFlush();
} // namespace features
