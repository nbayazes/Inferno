#pragma once

#include "Camera.h"
#include "Game.Terrain.h"
#include "Level.h"

namespace Inferno {

    // Returns true when playing an escape sequence
    bool UpdateEscapeSequence(float dt);

    void UpdateEscapeCamera(float dt);

    void StartEscapeSequence();
    void DebugEscapeSequence();

    TerrainInfo ParseEscapeInfo(Level& level, span<string> lines);

    enum class EscapeScene {
        None,
        Start, // Camera still in first person
        LookBack, // Camera looking backwards at player
        Outside
    };

    EscapeScene GetEscapeScene();

    inline Camera CinematicCamera;
}
