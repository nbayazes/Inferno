#pragma once

#include "Streams.h"
#include "Types.h"

namespace Inferno::Outrage {
    constexpr auto MIN_OBJFILE_VERSION = 1807;
    constexpr auto OBJFILE_VERSION = 2300;

    enum class ModelFlag {
        None = 0,
        LightmapRes = (1 << 0),
        Timed = (1 << 1),// Uses timed animation
        Alpha = (1 << 2),// Has alpha per vertex qualities
        Facing = (1 << 3),// Has a submodel that is always facing
        NotResident = (1 << 4), // This polymodel is not in memory
        SizeComputed = (1 << 5), // This polymodel's size is computed
    };

    enum class SubmodelFlag {
        Rotate = (1 << 0), // This subobject is a rotator
        Turret = (1 << 1), // This subobject is a turret that tracks
        Shell = (1 << 2), // This subobject is a door housing
        Frontface = (1 << 3), // This subobject contains the front face for the door
        Monitor1 = (1 << 4),
        Monitor2 = (1 << 5),
        Monitor3 = (1 << 6),
        Monitor4 = (1 << 7),
        Monitor5 = (1 << 8),
        Monitor6 = (1 << 9),
        Monitor7 = (1 << 10),
        Monitor8 = (1 << 11),
        Facing = (1 << 12), // This subobject always faces you
        Viewer = (1 << 13), // This subobject is marked as a 'viewer'.
        Layer = (1 << 14), // This subobject is marked as part of possible secondary model rendering.
        WeaponBat = (1 << 15), // This subobject is part of a weapon battery
        Glow = (1 << 16), // This subobject glows
        Custom = (1 << 17), // This subobject has textures/colors that are customizable
        Thruster = (1 << 18), // This is a thruster subobject
        Jitter = (1 << 19), // This object jitters by itself
        Headlight = (1 << 20) // This suboject is a headlight
    };

    struct ModelFace {
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

    struct Submodel {
        struct Vertex {
            Vector3 Position;
            Vector3 Normal;
            float Alpha = 1;
        };

        struct Keyframe {
            Vector3 Axis; // the axis of rotation for each keyframe
            int Angle; // The destination angles for each key frame
            Vector3 Position;
            int RotStartTime, PosStartTime;
            // the combined rotation matrices up to frame n
            Matrix3x3 Transform;
        };

        List<Keyframe> Keyframes;
        int NumKeyAngles = 0;
        int RotTrackMin = 0, RotTrackMax = 0;
        int NumKeyPos = 0;
        int PosTrackMin = 0, PosTrackMax = 0;

        Vector3 Min, Max;
        int Parent;
        Vector3 Normal; // Normal for separation plane
        Vector3 Point; // Point for separation plane
        Vector3 Offset; // Offset from parent
        float Radius;

        int TreeOffset, DataOffset;
        Vector3 GeometricCenter;

        string Name, Props;
        int MovementType, MovementAxis;
        float Rotation; // Fixed speed rotation along ? axis

        SubmodelFlag Flags;
        void SetFlag(SubmodelFlag flag) { Flags |= flag; }
        void ClearFlag(SubmodelFlag flag) { Flags &= ~flag; }
        bool HasFlag(SubmodelFlag flag) const { return (int)Flags & (int)flag; }

        List<Vertex> Vertices;
        List<ModelFace> Faces;

        Color Glow;
        float GlowSize;
    };

    // Descent 3 Outrage Object Format (OOF)
    struct Model {
        int Version; // equals major * 100 + minor
        int MajorVersion;
        float Radius;
        Vector3 Min, Max;
        List<Submodel> Submodels;
        List<string> Textures;
        List<int> TextureHandles;

        int FrameMin = 0, FrameMax = 0;

        ModelFlag Flags;

        struct Bank {
            int Parent = 0;
            Vector3 Point, Normal;
        };

        List<Bank> Guns;
        List<Bank> GroundPlanes;
        List<Bank> AttachPoints;
        List<bool> AttachPointsUsed;

        struct WeaponBattery {
            // Static Data  (Add to robot generic page)
            //unsigned short num_gps;
            //ubyte gp_index[MAX_WB_GUNPOINTS];
            List<ubyte> Gunpoints;

            // Turrets are listed from most important (greatest mobility) to least important
            List<ushort> Turrets;

            //ubyte num_turrets;
            //unsigned short turret_index[MAX_WB_TURRETS];
        };

        List<WeaponBattery> WeaponBatteries;

        void SetFlag(ModelFlag flag) { Flags |= flag; }
        void ClearFlag(ModelFlag flag) { Flags &= ~flag; }
        bool HasFlag(ModelFlag flag) const { return (int)Flags & (int)flag; }

        static Model Read(StreamReader& r);
    };
};