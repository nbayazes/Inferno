#pragma once
#include "Formats/BBM.h"
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
        Color Light = { 1, 1, 1, 1 };
        Vector3 LightDir = Vector3(0, -1, 0);
        Color StarColor = { 1, 1, 1 };

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

    struct TerrainGenerationInfo {
        float Size = 1500; // World units along x and y
        uint Density = 64; // Vertex count along x and y
        float Height = -60; // Height scale
        float NoiseScale = 2.6f; // Amount to scale the noise by
        uint64 Seed = 0;

        float Height2 = 30; // Height scale
        float NoiseScale2 = 8; // Amount to scale the noise by

        float TextureScale = 80.0f; // How big each repeat of the texture is in world units
        float FlattenRadius = 200; // Flatten area around exit
        float FrontFlattenRadius = 140; // Flatten area in front of exit

        float CraterStrength = 60; // Raises the outer edges of the terrain
    };

    void GenerateTerrain(TerrainInfo& info, const TerrainGenerationInfo& args);

    void LoadTerrain(const Bitmap2D& bitmap, TerrainInfo& info, uint cellDensity, float heightScale = 1.0f, float gridScale = 40.0f);

    inline TerrainGenerationInfo TerrainGenInfo;
}
