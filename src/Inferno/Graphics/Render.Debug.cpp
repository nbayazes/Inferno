#include "pch.h"
#include "Render.Debug.h"
#include "Render.h"
#include "ShaderLibrary.h"
#include "Buffers.h"

namespace Inferno::Render::Debug {
    using namespace DirectX;

    class LineBatch {
        UploadBuffer<FlatVertex> _vertices;

    public:
        LineBatch(int vertexCapacity) : _vertices(vertexCapacity, L"Line batch") {}

        void Begin() {
            _vertices.Begin();
        }

        void End(const GraphicsContext& ctx, auto effect) {
            _vertices.End();
            if (_vertices.GetElementCount() == 0) return;
            auto cmdList = ctx.GetCommandList();
             
            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = _vertices.GetGPUVirtualAddress();
            vbv.SizeInBytes = _vertices.GetSizeInBytes();
            vbv.StrideInBytes = _vertices.GetStride();
            cmdList->IASetVertexBuffers(0, 1, &vbv);

            Render::Adapter->GetGraphicsContext().ApplyEffect(effect);

            FlatShader::Constants constants = { ctx.Camera.ViewProjection, { 1, 1, 1, 1 } };
            effect.Shader->SetConstants(cmdList, constants);

            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            cmdList->DrawInstanced(_vertices.GetElementCount(), 1, 0, 0);
        }

        void DrawLine(const FlatVertex& v0, const FlatVertex& v1) {
            FlatVertex verts[] = { v0, v1 };
            _vertices.Copy(verts);
        }

        void DrawLines(span<FlatVertex> verts) {
            _vertices.Copy(verts);
        }
    };

    class PolygonBatch {
        UploadBuffer<FlatVertex> _vertices;
        uint16 _elementCount = 0;

    public:
        PolygonBatch(int vertexCapacity) : _vertices(vertexCapacity, L"Polygon batch") {}

        void Begin() {
            _vertices.Begin();
        }

        void End(const GraphicsContext& ctx, auto effect) {
            _vertices.End();
            if (_elementCount == 0) return;
            auto cmdList = ctx.GetCommandList();

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = _vertices.GetGPUVirtualAddress();
            vbv.SizeInBytes = _vertices.GetSizeInBytes();
            vbv.StrideInBytes = _vertices.GetStride();
            cmdList->IASetVertexBuffers(0, 1, &vbv);

            Render::Adapter->GetGraphicsContext().ApplyEffect(effect);
            FlatShader::Constants constants = { ctx.Camera.ViewProjection, { 1, 1, 1, 1 } };
            effect.Shader->SetConstants(cmdList, constants);

            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->DrawInstanced(_elementCount, 1, 0, 0);

            _elementCount = 0;
        }

        void DrawTriangle(const FlatVertex& v0, const FlatVertex& v1, const FlatVertex& v2) {
            FlatVertex verts[] = { v0, v1, v2 };
            _vertices.Copy(verts);
            _elementCount += 3;
        }
    };

    namespace {
        struct DeviceResources {
            DeviceResources()
                : Batch(Render::Device), LineBatch(30000), PolygonBatch(20000), AdditivePolygonBatch(20000) {}

            PrimitiveBatch<FlatVertex> Batch;
            LineBatch LineBatch;
            PolygonBatch PolygonBatch, AdditivePolygonBatch;
        };

        Ptr<DeviceResources> Resources;
    }

    class GizmoArrow {
        List<FlatVertex> _vertices;
        List<uint16> _indices;

    public:
        GizmoArrow(float size, float length, Color color = { 1, 1, 1, 1 }) {
            int tesselation = 8;
            float coneHeight = size * 6;
            float cylinderHeight = length - coneHeight / 2;
            List<GeometricPrimitive::VertexType> vertices;
            GeometricPrimitive::CreateCylinder(vertices, _indices, cylinderHeight, size, tesselation, false);

            for (size_t i = 0; i < vertices.size(); i++) {
                auto& cv = vertices[i];
                cv.position.y += cylinderHeight / 2.0f;
                auto tx = cv.position.x;
                cv.position.x = cv.position.y;
                cv.position.y = tx;
                _vertices.push_back({ cv.position, color });
            }

            List<GeometricPrimitive::VertexType> coneVertices;
            List<uint16> coneIndices;
            GeometricPrimitive::CreateCone(coneVertices, coneIndices, size * 4, coneHeight, tesselation, false);

            for (size_t i = 0; i < coneVertices.size(); i++) {
                auto& cv = coneVertices[i];
                // shift cone to end of cylinder
                cv.position.y += length - coneHeight / 2;
                auto tx = cv.position.x;
                cv.position.x = cv.position.y;
                cv.position.y = tx;
                _vertices.push_back({ cv.position, color });
            }

            for (size_t i = 0; i < coneIndices.size(); i++) {
                _indices.push_back((uint16)vertices.size() + coneIndices[i]);
            }
        }

        span<FlatVertex> GetVertices() { return _vertices; }
        span<uint16> GetIndices() { return _indices; }
    };

    GizmoArrow Arrow(0.3f, 10.0f);

    class Cube {
        List<FlatVertex> _vertices;
        List<uint16> _indices;

    public:
        Cube(float size, Color color = { 1, 1, 1, 1 }) {
            List<GeometricPrimitive::VertexType> vertices;
            GeometricPrimitive::CreateCube(vertices, _indices, size, false);

            for (size_t i = 0; i < vertices.size(); i++) {
                auto& cv = vertices[i];
                //cv.position.y += length / 2.0f;
                auto tx = cv.position.x;
                cv.position.x = cv.position.y;
                cv.position.y = tx;
                _vertices.push_back({ cv.position, color });
            }
        }

        span<FlatVertex> GetVertices() { return _vertices; }
        span<uint16> GetIndices() { return _indices; }
    };

    Cube GizmoCube(1.0f);

    void Initialize() {
        Resources = MakePtr<DeviceResources>();
    }

    void Shutdown() {
        Resources.reset();
    }

    void DrawLine(const FlatVertex& v0, const FlatVertex& v1) {
        Resources->LineBatch.DrawLine(v0, v1);
    }

    void DrawLine(const Vector3& v0, const Vector3& v1, const Color& color) {
        if (color.w <= 0) return;
        Resources->LineBatch.DrawLine({ v0, color }, { v1, color });
    }

    void DrawLine(const Vector3& v0, const Vector3& v1, const Color& color0, const Color& color1) {
        Resources->LineBatch.DrawLine({ v0, color0 }, { v1, color1 });
    }

    void DrawLines(span<FlatVertex> verts) {
        Resources->LineBatch.DrawLines(verts);
    }

    void DrawCross(const Vector3& p, const Color& color) {
        Resources->LineBatch.DrawLine({ p - Vector3::UnitX, color }, { p + Vector3::UnitX, color });
        Resources->LineBatch.DrawLine({ p - Vector3::UnitY, color }, { p + Vector3::UnitY, color });
        Resources->LineBatch.DrawLine({ p - Vector3::UnitZ, color }, { p + Vector3::UnitZ, color });
    }

    void DrawPoint(const Vector3& p, const Color& color, const Camera& camera) {
        auto right = camera.GetRight();
        auto up = camera.Up;
        auto scale = (camera.Position - p).Length() * 0.006f;

        Vector3 v0 = p - right * scale - up * scale;
        Vector3 v1 = p + right * scale - up * scale;
        Vector3 v2 = p + right * scale + up * scale;
        Vector3 v3 = p - right * scale + up * scale;
        Resources->PolygonBatch.DrawTriangle({ v0, color }, { v1, color }, { v2, color });
        Resources->PolygonBatch.DrawTriangle({ v2, color }, { v3, color }, { v0, color });
    }

    void DrawTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Color& color) {
        Resources->PolygonBatch.DrawTriangle({ v0, color }, { v1, color }, { v2, color });
    }

    void DrawAdditiveTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Color& color) {
        Resources->AdditivePolygonBatch.DrawTriangle({ v0, color }, { v1, color }, { v2, color });
    }

    bool InFrame = false;

    void BeginFrame() {
        if (InFrame) throw Exception("Already began a frame");

        Resources->LineBatch.Begin();
        Resources->PolygonBatch.Begin();
        Resources->AdditivePolygonBatch.Begin();
        InFrame = true;
    }

    void EndFrame(const GraphicsContext& ctx) {
        if (!InFrame) throw Exception("Must call BeginFrame() first");
        for (auto& point : DebugPoints)
            DrawPoint(point, { 1, 0, 0 }, ctx.Camera);

        for (auto& point : DebugPoints2)
            DrawPoint(point, { 0, 1, 0 }, ctx.Camera);

        for (int i = 0; i + 1 < DebugLines.size(); i += 2)
            DrawLine(DebugLines[i], DebugLines[i + 1], { 1, 0, 0 });

        Resources->LineBatch.End(ctx, Render::Effects->Line);
        Resources->PolygonBatch.End(ctx, Render::Effects->Flat);
        Resources->AdditivePolygonBatch.End(ctx, Render::Effects->FlatAdditive);
        InFrame = false;
    }

    // Draws a crosshair in front of the camera
    void DrawCrosshair(float size, const Camera& camera) {
        auto center = camera.Position + camera.GetForward() * 10;
        auto right = camera.GetRight();
        auto up = camera.Up;

        Color color(0, 1, 0);
        DrawLine(center - right * size, center - right * (size / 2), color);
        DrawLine(center + right * size, center + right * (size / 2), color);

        DrawLine(center - up * size, center - up * (size / 2), color);
        DrawLine(center + up * size, center + up * (size / 2), color);
    }

    template <class TVertex>
    void DrawMesh(span<TVertex> vertices, span<uint16> indices) {
        Resources->Batch.DrawIndexed(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, indices.data(), indices.size(), vertices.data(), vertices.size());
    }

    void DrawArrow(ID3D12GraphicsCommandList* cmdList, const Matrix& transform, const Color& color) {
        Adapter->GetGraphicsContext().ApplyEffect(Effects->Flat);

        FlatShader::Constants constants;
        constants.Transform = transform;
        constants.Tint = color;
        Shaders->Flat.SetConstants(cmdList, constants);

        Resources->Batch.Begin(cmdList);
        DrawMesh(Arrow.GetVertices(), Arrow.GetIndices());
        Resources->Batch.End();
        //Stats::DrawCalls++;
    }

    void DrawCube(ID3D12GraphicsCommandList* cmdList, const Matrix& transform, const Color& color) {
        Adapter->GetGraphicsContext().ApplyEffect(Effects->Flat);

        FlatShader::Constants constants;
        constants.Transform = transform;
        constants.Tint = color;
        Shaders->Flat.SetConstants(cmdList, constants);

        Resources->Batch.Begin(cmdList);
        DrawMesh(GizmoCube.GetVertices(), GizmoCube.GetIndices());
        Resources->Batch.End();
        //Stats::DrawCalls++;
    }

    void DrawCircle(float radius, const Matrix& transform, const Color& color) {
        Vector3 p0 = Vector3::Transform({ radius, 0, 0 }, transform);
        constexpr int Steps = 32;

        for (int i = 0; i <= Steps; i++) {
            Vector3 p;
            p.x = std::cos(XM_2PI * (i / (float)Steps)) * radius;
            p.y = std::sin(XM_2PI * (i / (float)Steps)) * radius;
            p = Vector3::Transform(p, transform);
            DrawLine(p0, p, color);
            p0 = p;
        }
    }

    // Draws a solid circle that always faces the camera
    void DrawSolidCircle(const Vector3& position, float radius, const Color& color, const Camera& camera, uint steps) {
        auto transform = Matrix::CreateBillboard(position, camera.Position, camera.Up);
        Vector3 p0 = Vector3::Transform({ radius, 0, 0 }, transform);

        for (uint i = 0; i <= steps; i++) {
            Vector3 p;
            p.x = std::cos(XM_2PI * (i / (float)steps)) * radius;
            p.y = std::sin(XM_2PI * (i / (float)steps)) * radius;
            p = Vector3::Transform(p, transform);
            DrawTriangle(p0, p, position, color);
            p0 = p;
        }
    }

    void DrawFacingSquare(const Vector3& p, float size, const Color& color, const Camera& camera) {
        auto right = camera.GetRight();
        auto up = camera.Up;
        //auto scale = (Render::Camera.Position - p).Length() * 0.006f;

        Vector3 v0 = p - right * size;
        Vector3 v1 = p - up * size;
        Vector3 v2 = p + right * size;
        Vector3 v3 = p + up * size;

        DrawLine(v0, v1, color);
        DrawLine(v1, v2, color);
        DrawLine(v2, v3, color);
        DrawLine(v3, v0, color);
    }

    void DrawRing(float radius, float thickness, const Matrix& transform, const Color& color) {
        float radius2 = std::max(radius - thickness, 0.0f);
        Vector3 v0 = Vector3::Transform({ radius, 0, 0 }, transform);
        Vector3 v2 = Vector3::Transform({ radius2, 0, 0 }, transform);

        constexpr int Steps = 32;
        for (int i = 0; i <= Steps; i++) {
            Vector3 v1;
            v1.x = std::cos(XM_2PI * (i / (float)Steps)) * radius;
            v1.y = std::sin(XM_2PI * (i / (float)Steps)) * radius;
            v1 = Vector3::Transform(v1, transform);

            Vector3 v3;
            v3.x = std::cos(XM_2PI * (i / (float)Steps)) * radius2;
            v3.y = std::sin(XM_2PI * (i / (float)Steps)) * radius2;
            v3 = Vector3::Transform(v3, transform);

            DrawTriangle(v0, v1, v2, color);
            DrawTriangle(v2, v1, v3, color);
            v0 = v1;
            v2 = v3;
        }
    }

    void DrawSolidArc(float radius, float thickness, float length, float offset, const Matrix& transform, const Color& color) {
        float radius2 = std::max(radius - thickness, 0.0f);

        constexpr int Steps = 18;
        auto polar = [&length, &offset, &transform](float radius, int i) {
            Vector3 v;
            v.x = std::cos(length * (i / (float)Steps) + offset) * radius;
            v.y = std::sin(length * (i / (float)Steps) + offset) * radius;
            return Vector3::Transform(v, transform);
        };

        Vector3 v0 = polar(radius, 0);
        Vector3 v2 = polar(radius2, 0);

        for (int i = 1; i <= Steps; i++) {
            Vector3 v1 = polar(radius, i);
            Vector3 v3 = polar(radius2, i);
            DrawTriangle(v0, v1, v2, color);
            DrawTriangle(v2, v1, v3, color);

            v0 = v1;
            v2 = v3;
        }
    }


    void DrawArc(float radius, float radians, float offset, const Matrix& transform, const Color& color) {
        Option<Vector3> p0 = {};

        constexpr int Steps = 18;
        for (int i = 0; i <= Steps; i++) {
            Vector3 p;
            p.x = std::cos(radians * (i / (float)Steps) + offset) * radius;
            p.y = std::sin(radians * (i / (float)Steps) + offset) * radius;
            p = Vector3::Transform(p, transform);
            if (p0)
                DrawLine(*p0, p, color);
            p0 = p;
        }
    }

    void DrawWallMarker(const ConstFace& face, const Color& color, float height) {
        auto center = face.Center() + face.AverageNormal() * height;

        DrawLine(face[0], center, color);
        DrawLine(face[1], center, color);
        DrawLine(face[2], center, color);
        DrawLine(face[3], center, color);
    }

    void DrawArrow(const Vector3& start, const Vector3& end, const Color& color, const Camera& camera) {
        auto dir = end - start;
        dir.Normalize();

        DrawLine(start, end, color);
        auto up = dir.Cross(camera.GetForward());
        up.Normalize();

        auto p0 = end - dir * 2 + up * 2;
        auto p1 = end - dir * 2 - up * 2;
        DrawLine(end, p0, color);
        DrawLine(end, p1, color);
    }

    void DrawPlane(const Vector3& pos, const Vector3& right, const Vector3& up, const Color& color, float size) {
        auto p0 = pos + right * size + up * size;
        auto p1 = pos - right * size + up * size;
        auto p2 = pos - right * size - up * size;
        auto p3 = pos + right * size - up * size;
        Color fill = color;
        fill.w = 0.1f;
        DrawTriangle(p0, p1, p2, fill);
        DrawTriangle(p2, p3, p0, fill);

        DrawLine(p0, p1, color);
        DrawLine(p1, p2, color);
        DrawLine(p2, p3, color);
        DrawLine(p3, p0, color);
    }

    void DrawBoundingBox(const DirectX::BoundingOrientedBox& bounds, const Color& color) {
        Array<XMFLOAT3, DirectX::BoundingOrientedBox::CORNER_COUNT> corners{};
        bounds.GetCorners(corners.data());

        Render::Debug::DrawLine(corners[0], corners[1], color);
        Render::Debug::DrawLine(corners[1], corners[2], color);
        Render::Debug::DrawLine(corners[2], corners[3], color);
        Render::Debug::DrawLine(corners[3], corners[0], color);

        Render::Debug::DrawLine(corners[0], corners[4], color);
        Render::Debug::DrawLine(corners[1], corners[5], color);
        Render::Debug::DrawLine(corners[2], corners[6], color);
        Render::Debug::DrawLine(corners[3], corners[7], color);

        Render::Debug::DrawLine(corners[4], corners[5], color);
        Render::Debug::DrawLine(corners[5], corners[6], color);
        Render::Debug::DrawLine(corners[6], corners[7], color);
        Render::Debug::DrawLine(corners[7], corners[4], color);
    }

    void DrawCanvasBox(float left, float right, float top, float bottom, const Color& color) {
        Vector2 pixels[4]{};
        auto size = Render::Adapter->GetOutputSize();

        pixels[0].x = (left + 1) * size.x * 0.5f;
        pixels[0].y = (1 - bottom) * size.y * 0.5f;

        pixels[1].x = (right + 1) * size.x * 0.5f;
        pixels[1].y = (1 - bottom) * size.y * 0.5f;

        pixels[2].x = (right + 1) * size.x * 0.5f;
        pixels[2].y = (1 - top) * size.y * 0.5f;

        pixels[3].x = (left + 1) * size.x * 0.5f;
        pixels[3].y = (1 - top) * size.y * 0.5f;

        CanvasPayload payload{};
        payload.Texture = Render::Materials->White().Handle();
        auto hex = color.RGBA().v;
        payload.V0 = CanvasVertex(pixels[0], {}, hex);
        payload.V1 = CanvasVertex(pixels[1], {}, hex);
        payload.V2 = CanvasVertex(pixels[2], {}, hex);
        payload.V3 = CanvasVertex(pixels[3], {}, hex);
        DebugCanvas->Draw(payload);
    }

    void OutlineSegment(const Level& level, Segment& seg, const Color& color, const Color* fill) {
        auto vs = seg.GetVertices(level);

        // Draw each of the 12 edges
        for (int i = 0; i < 12; i++) {
            auto& vi = VERTS_OF_EDGE[i];
            auto v1 = vs[vi[0]];
            auto v2 = vs[vi[1]];
            Debug::DrawLine(*v1, *v2, color);
        }

        if (seg.Type != SegmentType::None && fill) {
            for (auto& side : SIDE_IDS)
                Debug::DrawSide(level, seg, side, *fill);
        }
    }

    void OutlineRoom(Level& level, const Room& room, const Color& color) {
        for (auto& segId : room.Segments) {
            if (auto seg = level.TryGetSegment(segId))
                Debug::OutlineSegment(level, *seg, color);
        }
    }

    void DrawSide(Level& level, Tag tag, const Color& color) {
        auto [seg, side] = level.GetSegmentAndSide(tag);
        auto& i = side.GetRenderIndices();
        auto& v = level.Vertices;
        auto si = seg.GetVertexIndices(tag.Side);

        DrawTriangle(v[si[i[0]]], v[si[i[1]]], v[si[i[2]]], color);
        DrawTriangle(v[si[i[3]]], v[si[i[4]]], v[si[i[5]]], color);
    }

    void DrawSide(const Level& level, Segment& seg, SideID side, const Color& color) {
        auto& i = seg.GetSide(side).GetRenderIndices();
        auto& v = level.Vertices;
        auto si = seg.GetVertexIndices(side);

        DrawTriangle(v[si[i[0]]], v[si[i[1]]], v[si[i[2]]], color);
        DrawTriangle(v[si[i[3]]], v[si[i[4]]], v[si[i[5]]], color);
    }

    void DrawSideOutline(Level& level, Tag tag, const Color& color) {
        auto [seg, side] = level.GetSegmentAndSide(tag);
        auto& v = level.Vertices;
        auto si = seg.GetVertexIndices(tag.Side);

        DrawLine(v[si[0]], v[si[1]], color);
        DrawLine(v[si[1]], v[si[2]], color);
        DrawLine(v[si[2]], v[si[3]], color);
        DrawLine(v[si[3]], v[si[0]], color);
    }

    void DrawSideOutline(const Level& level, const Segment& seg, SideID side, const Color& color) {
        auto& v = level.Vertices;
        auto si = seg.GetVertexIndices(side);

        DrawLine(v[si[0]], v[si[1]], color);
        DrawLine(v[si[1]], v[si[2]], color);
        DrawLine(v[si[2]], v[si[3]], color);
        DrawLine(v[si[3]], v[si[0]], color);
    }
}
