#pragma once

#include "Pig.h"
#include "Types.h"

namespace Inferno {
    constexpr auto MAX_SUBMODELS = 10; //how many animating sub-objects per model
    constexpr ubyte ROOT_SUBMODEL = 255;

    enum class Animation : int8 {
        Rest = 0,
        Alert = 1,
        Fire = 2,
        Recoil = 3,
        Flinch = 4
    };

    using AnimationAngles = Array<Vector3, MAX_SUBMODELS>;

    struct AnimationState {
        float Timer = 0;
        float Duration = 0;
        Animation Animation = Animation::Rest;
        AnimationAngles DeltaAngles{};

        bool IsPlayingAnimation() const { return Timer < Duration; }
    };

    //describes the position of a certain joint
    struct JointPos {
        short ID; // joint number
        Vector3 Angle;
    };

    struct SubmodelGlow {
        int16 Face;
        int16 Glow;
    };

    struct ExpandedPoint {
        Vector3 Point;
        int16 TexSlot = -1; // Texture slot for this robot
    };

    struct Submodel {
        int Pointer; // Offset to submodel data chunk
        Vector3 Offset; // Joint offset to submodel origin
        Vector3 Normal; // norm for sep plane
        Vector3 Point; // point on sep plane
        float Radius;
        ubyte Parent;
        Vector3 Min, Max; // Geometric min/max
        DirectX::BoundingOrientedBox Bounds;

        // Mesh data
        List<uint16> Indices;
        List<Vector2> UVs;
        List<uint16> FlatIndices;
        List<int16> TMaps;
        List<Color> FlatVertexColors;
        List<SubmodelGlow> Glows;
        List<SubmodelGlow> FlatGlows;

        // expanded values so that each face gets its own vertices / uvs
        List<ExpandedPoint> ExpandedPoints;
        // The top level list corresponds to the texture slot
        List<List<uint16>> ExpandedIndices;
        List<Color> ExpandedColors;
    };

    // Parallax Object Format
    struct Model {
        uint DataSize;
        List<Submodel> Submodels;
        Vector3 MinBounds, MaxBounds;
        float Radius = 5;
        uint8 TextureCount;
        ushort FirstTexture;
        ubyte SimplerModel; //alternate model with less detail (0 if none, model_num+1 else)
        List<Vector3> angles; // was in POF data, maybe not used at runtime?
        List<Vector3> Vertices;
        List<Vector3> Normals, FlatNormals; // 1 normal per three Vertices

        struct Bank {
            uint8 Submodel = 0;
            Vector3 Point, Normal;
        };

        List<Bank> Guns;
        List<List<Vector3>> Animation; // Animation angles
        List<string> Textures;
        string FileName; // pof file name if loaded from one

        // Gets the joint offset of a submodel
        Vector3 GetSubmodelOffset(int index) const {
            if (!Seq::inRange(Submodels, index)) return Vector3::Zero;

            auto submodelOffset = Vector3::Zero;
            auto* smc = &Submodels[index];
            while (smc->Parent != ROOT_SUBMODEL) {
                submodelOffset += smc->Offset;
                smc = &Submodels[smc->Parent];
            }

            return submodelOffset;
        }

        // Gets the geometric center of a submodel
        Vector3 GetSubmodelCenter(int index) const {
            if (!Seq::inRange(Submodels, index)) return Vector3::Zero;
            return GetSubmodelOffset(index) + Submodels[index].Bounds.Center;
        }
    };

    // Decodes interpreter data into a polymodel.
    // If decoding a D1 model a palette is required to map solid colors properly.
    void DecodeInterpreterData(Model& model, span<ubyte> data, const Palette* palette = nullptr);

    // Reads a .pof file
    Model ReadPof(span<byte> pof, const Palette* palette = nullptr);
}
