#pragma once
#include "Level.h"
#include "Object.h"

namespace Inferno::Game {
    void SelfDestructMine();

    // returns true if the reactor was destroyed. False if already destroyed.
    bool DestroyReactor(Object& obj);
    void UpdateReactorCountdown(float dt);
    void UpdateReactor(Inferno::Object& reactor);
    void UpdateReactorAI(const Inferno::Object& reactor, float dt);
    void InitReactor(const Inferno::Level& level, Object& reactor);
}
