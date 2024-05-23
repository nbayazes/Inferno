#pragma once

#include "Level.h"
#include "Types.h"
#include "VertexTypes.h"

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
        Matrix ExitTransform;

        Matrix3x3 InverseTransform;
        List<Vector3> EscapePath;

        ModelID ExitModel = ModelID::None;
        int SurfacePathIndex = 0; // Node where the player has cleared the exit
        int LookbackPathIndex = 0; // Node where the camera should switch from first to third person

        Tag ExitTag;
    };

    // Returns true when playing an escape sequence
    bool UpdateEscapeSequence(float dt);

    void UpdateEscapeCamera(float dt);

    void DebugEscapeSequence();

    TerrainInfo ParseEscapeInfo(Level& level, span<string> lines);
}
