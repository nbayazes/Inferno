#pragma once

#include "Streams.h"

namespace Inferno {
    constexpr auto MIN_OBJFILE_VERSION = 1807;
    constexpr auto OBJFILE_VERSION = 2300;

    enum PolymodelFlags {
        PMF_NONE = 0,
        PMF_LIGHTMAP_RES = 1,
        PMF_TIMED = 2,// Uses new timed animation
        PMF_ALPHA = 4,// Has alpha per vertex qualities
        PMF_FACING = 8,// Has a submodel that is always facing
        PMF_NOT_RESIDENT = 16, // This polymodel is not in memory
        PMF_SIZE_COMPUTED = 32, // This polymodel's size is computed
    };

    struct PolymodelFace {
        struct Vertex {
            short Index;
            Vector2 UV;
        };

        List<Vertex> Vertices;

        Color Color = { 1, 1, 1 };
        short TexNum = -1;

        Vector3 Normal;
        Vector3 Min, Max;
    };

    struct OutrageSubmodel {
        struct Vertex {
            Vector3 Position;
            Vector3 Normal;
            float Alpha = 1;
        };

        Vector3 Min, Max;
        int Parent;
        Vector3 Normal, Point, Offset;
        float Radius;

        int TreeOffset, DataOffset;
        Vector3 GeometricCenter;

        string Name, Props;
        int MovementType, MovementAxis;

        List<Vertex> Vertices;
        List<PolymodelFace> Faces;
    };

    struct OutrageModel {
        PolymodelFlags Flags;
        int Version; // equals major * 100 + minor
        int MajorVersion;
        float Radius;
        Vector3 Min, Max;
        List<OutrageSubmodel> Submodels;
        List<string> Textures;

        static OutrageModel Read(StreamReader& r);
    };
};