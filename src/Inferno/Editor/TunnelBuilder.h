#pragma once
#include "Level.h"
#include "Editor.Selection.h"

namespace Inferno::Editor {
    struct BezierCurve {
        Array<Vector3, 4> Points{};
        Array<float, 2> Length = { 1, 1 };

        Vector3 Compute(float u) const;
        void Transform(const Matrix& m) const;
    };

    struct BezierCurve2 {
        Array<Vector3, 4> Points{};
    };

    struct PathNode {
        Matrix Rotation;
        Vector3 Vertex; // absolute and unrotated vertices
        Vector3 Axis; // axis of rotation from last node to this node
        float Angle; // rotation angle around z axis
    };

    struct TunnelNode {
        //PointTag Tag;

        Vector3 Point;
        Vector3 Normal;
        Array<Vector3, 4> Vertices;
        //Array<ubyte, 4> OppVertexIndex;
        Matrix Rotation; // orientation of tunnel end side
        //double Sign;
        //eUpdateStatus	m_updateStatus;
    };

    struct TunnelPath {
        TunnelNode Start, End;
        List<PathNode> Nodes;
    };

    void CreateTunnel(Level& level, PointTag start, PointTag end, int steps, float startLength, float endLength);
    void ClearTunnel();
    void CreateTunnelSegments(Level& level, const TunnelPath& path, PointTag start, PointTag end);

    inline List<Vector3> TunnelBuilderPath;
    inline List<Vector3> TunnelBuilderPoints;
    inline List<Vector3> DebugTunnelPoints;
    inline TunnelPath DebugTunnel;

    inline BezierCurve2 TunnelBuilderHandles; // For preview

    constexpr float MinTunnelLength = 10;
    constexpr float MaxTunnelLength = 200;
}