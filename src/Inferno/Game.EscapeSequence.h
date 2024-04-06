#pragma once

#include "Graphics/ShaderLibrary.h"
#include "Level.h"
#include "Types.h"

namespace Inferno {
    struct EscapeInfo {
        string TerrainTexture;
        string Heightmap;
        int ExitX, ExitY;
        float ExitAngle;
        string PlanetTexture;
        int StationX, StationY;
        float SatelliteSize;
        Vector3 SatelliteDir, StationDir;

        List<ObjectVertex> Vertices;
        List<uint16> Indices;

        Matrix TerrainTransform;
        List<Vector3> PlayerPath;
    };

    void UpdateEscapeSequence(float dt);

    EscapeInfo ParseEscapeInfo(Level& level, span<string> lines);
}