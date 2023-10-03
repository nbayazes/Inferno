#pragma once
#include "Object.h"

namespace Inferno::Game {
    bool UpdateBoss(Inferno::Object& boss, float dt);
    void InitBoss();
    void StartBossDeath();
}
