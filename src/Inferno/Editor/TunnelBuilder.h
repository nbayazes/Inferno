#pragma once
#include "Level.h"

namespace Inferno::Editor {

    struct BezierCurve {
        Array<Vector3, 4> Points{};

        float EstimateLength(int steps) const;
    };

    struct PathNode {
        Matrix Rotation;
        Vector3 Position; // absolute and unrotated vertices
        Array<Vector3, 4> Vertices;
        Vector3 Axis; // axis of rotation from last node to this node
        float Angle; // rotation angle around z axis
    };

    struct TunnelNode {
        Vector3 Point;
        Vector3 Normal;
        Vector3 Up;
        Array<Vector3, 4> Vertices;
        Matrix Rotation;
    };

    struct TunnelPath {
        TunnelNode Start, End;
        List<PathNode> Nodes;
        BezierCurve Curve; // For previews
    };

    // A begin or end selection of a tunnel
    struct TunnelHandle {
        PointTag Tag = { SegID::None };
        float Length = MIN_LENGTH;

        static constexpr float MIN_LENGTH = 5, MAX_LENGTH = 400;

        void Clamp() {
            Length = std::clamp(Length, MIN_LENGTH, MAX_LENGTH);
        }
    };

    struct TunnelArgs {
        TunnelHandle Start, End;
        int Steps = 5;
        bool Twist = true;

        static constexpr int MIN_STEPS = 2, MAX_STEPS = 100;

        void ClampInputs() {
            Steps = std::clamp(Steps, MIN_STEPS, MAX_STEPS);
            Start.Clamp();
            End.Clamp();
        }

        bool IsValid() const {
            return Steps >= MIN_STEPS && Start.Tag && End.Tag && Start.Tag != End.Tag;
        }
    };

    TunnelPath CreateTunnel(Level&, TunnelArgs&);
    void CreateTunnelSegments(Level&, TunnelArgs&);

    inline List<Vector3> DebugTunnelLines;
    inline TunnelPath PreviewTunnel;
    inline TunnelHandle PreviewTunnelStart, PreviewTunnelEnd;
}