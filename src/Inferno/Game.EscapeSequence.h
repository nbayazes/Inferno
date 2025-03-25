#pragma once

#include "Camera.h"
#include "Game.Terrain.h"
#include "Level.h"

namespace Inferno {

    // Returns true when playing an escape sequence
    bool UpdateEscapeSequence(float dt);

    void UpdateEscapeCamera(float dt);

    void StartEscapeSequence(Tag start);
    void StopEscapeSequence();
    void DebugEscapeSequence();

    // Returns true when the mine has exploded on the surface
    bool MineCollapsed(); 

    bool CreateEscapePath(Level& level, TerrainInfo& info, Tag start, bool makeWallsIllusions);

    TerrainInfo ParseEscapeInfo(span<string> lines);

    enum class EscapeScene {
        None,
        Start, // Camera still in first person
        LookBack, // Camera looking backwards at player
        Outside
    };

    EscapeScene GetEscapeScene();

    inline Camera CinematicCamera;
}
