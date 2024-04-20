#pragma once

#include "Graphics/ShaderLibrary.h"
#include "Level.h"
#include "Types.h"

namespace Inferno {
    struct TerrainInfo {
        string SurfaceTexture;
        string Heightmap;
        int ExitX, ExitY;
        float ExitAngle;

        Vector3 StationDir;
        int StationX, StationY;

        string SatelliteTexture;
        float SatelliteSize;
        float SatelliteHeight = 400.0f;
        Vector3 SatelliteDir;
        bool SatelliteAdditive = false;
        float SatelliteAspectRatio = 1; // Ratio to use when drawing sprite
        Color SatelliteColor = { 1, 1, 1 };
        Color AtmosphereColor = { 0.3f, 0.4f, 1, 0.5f };

        List<ObjectVertex> Vertices;
        List<uint16> Indices;

        Matrix Transform;
        Matrix3x3 InverseTransform;
        List<Vector3> EscapePath;
    };

    void UpdateEscapeSequence(float dt);

    TerrainInfo ParseEscapeInfo(Level& level, span<string> lines);
}