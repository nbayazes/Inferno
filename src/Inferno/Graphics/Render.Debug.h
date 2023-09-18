#pragma once

#include "ShaderLibrary.h"
#include "Level.h"
#include "Face.h"

namespace Inferno::Render::Debug {
    void Initialize();
    void Shutdown();

    constexpr float WallMarkerOffset = 1.0f;

    // Immediately draws an arrow
    void DrawArrow(ID3D12GraphicsCommandList* cmdList, const Matrix& transform, const Color& color);
    void DrawCube(ID3D12GraphicsCommandList* cmdList, const Matrix& transform, const Color& color);

    // Queues a line to be drawn at the end of frame
    void DrawLine(const FlatVertex& v0, const FlatVertex& v1);
    void DrawLine(const Vector3& v0, const Vector3& v1, const Color& color);
    void DrawLines(span<FlatVertex> verts);
    void DrawCross(const Vector3& p, const Color& color);
    void DrawPoint(const Vector3& p, const Color& color);

    void DrawTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Color& color);

    // Draws a circle on the x/y plane with the given radius
    void DrawCircle(float radius, const Matrix& transform, const Color& color);
    void DrawSolidCircle(const Vector3& position, float radius, const Color& color);

    void DrawFacingSquare(const Vector3& p, float size, const Color& color);

    void DrawRing(float radius, float thickness, const Matrix& transform, const Color& color);
    // Drawns an arc on the x/y plane with the given radius and angle offset
    // The arc goes counter clockwise from x = 0
    void DrawArc(float radius, float radians, float offset, const Matrix& transform, const Color& color);
    void DrawSolidArc(float radius, float thickness, float length, float offset, const Matrix& transform, const Color& color);

    void DrawWallMarker(const Face& face, const Color& color, float height = Debug::WallMarkerOffset);

    void DrawArrow(const Vector3& start, const Vector3& end, const Color& color);

    void BeginFrame();
    void EndFrame(ID3D12GraphicsCommandList* cmdList);

    void DrawCrosshair(float size);

    void DrawSide(Level&, Tag, const Color&);
    void DrawSide(const Level& level, Segment& seg, SideID side, const Color& color);
    void DrawSideOutline(Level&, Tag, const Color&);
    void DrawSideOutline(const Level& level, const Segment& seg, SideID side, const Color& color);
    void DrawPlane(const Vector3& pos, const Vector3& right, const Vector3& up, const Color& color, float size);

    void DrawBoundingBox(const DirectX::BoundingOrientedBox&, const Color&);

    void OutlineSegment(const Level& level, Segment& seg, const Color& color, const Color* fill = nullptr);
    void OutlineRoom(Level& level, const Room& room, const Color& color);

    inline List<Vector3> DebugPoints, DebugPoints2;
    inline List<Vector3> DebugLines;
}

namespace Inferno::Render::Metrics {
    inline int64 Present, QueueLevel, ExecuteRenderCommands, ImGui, PresentCall;
    inline int64 Debug;
    inline int64 DrawTransparent;
    inline int64 FindNearestLight;

    inline void BeginFrame() {
        Present = 0;
        PresentCall = 0;
        Debug = 0;
        DrawTransparent = 0;
        FindNearestLight = 0;
        QueueLevel = 0;
        ImGui = 0;
        ExecuteRenderCommands = 0;
    }
}