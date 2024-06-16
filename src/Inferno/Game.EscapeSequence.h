#pragma once

#include "Game.Terrain.h"
#include "Level.h"

namespace Inferno {

    // Returns true when playing an escape sequence
    bool UpdateEscapeSequence(float dt);

    void UpdateEscapeCamera(float dt);

    void DebugEscapeSequence();

    TerrainInfo ParseEscapeInfo(Level& level, span<string> lines);
}
