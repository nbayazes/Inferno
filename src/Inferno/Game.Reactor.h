#pragma once
#include "Object.h"

namespace Inferno::Game {
    void DestroyReactor(Object& obj);
    void UpdateReactorCountdown(float dt);
    void UpdateReactorAI(const Inferno::Object& reactor, float dt);
}
