#include "pch.h"
#include "Render.Debug.h"
#include "Render.h"
#include "ShaderLibrary.h"
#include "Buffers.h"
#include "Utility.h"

namespace Inferno::Render::Debug {
    using namespace DirectX;

    class LineBatch {
        UploadBuffer<FlatVertex> Vertices;
    public:
        LineBatch(int vertexCapacity) :
            Vertices(vertexCapacity) {}

        void Begin() {
            Vertices.Begin();
        }

        void End(ID3D12GraphicsCommandList* cmdList, auto effect) {
            Vertices.End();

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = Vertices.GetGPUVirtualAddress();
            vbv.SizeInBytes = Vertices.GetSizeInBytes();
            vbv.StrideInBytes = Vertices.Stride;
            cmdList->IASetVertexBuffers(0, 1, &vbv);

            //auto& effect = Render::Effects->Line;
            effect.Apply(cmdList);
            FlatShader::Constants constants = { Render::ViewProjection, { 1, 1, 1, 1 } };
            effect.Shader->SetConstants(cmdList, constants);

            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            cmdList->DrawInstanced(Vertices.GetElementCount(), 1, 0, 0);
        }

        void DrawLine(const FlatVertex& v0, const FlatVertex& v1) {
            FlatVertex verts[] = { v0, v1 };
            Vertices.Copy(verts);
        }

        void DrawLines(span<FlatVertex> verts) {
            Vertices.Copy(verts);
        }
    };

    class PolygonBatch {
        UploadBuffer<FlatVertex> Vertices;
        uint16 _elementCount = 0;
    public:
        PolygonBatch(int vertexCapacity) :
            Vertices(vertexCapacity) {}

        void Begin() {
            Vertices.Begin();
        }

        void End(ID3D12GraphicsCommandList* cmdList, auto effect) {
            Vertices.End();

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = Vertices.GetGPUVirtualAddress();
            vbv.SizeInBytes = Vertices.GetSizeInBytes();
            vbv.StrideInBytes = Vertices.Stride;
            cmdList->IASetVertexBuffers(0, 1, &vbv);

            effect.Apply(cmdList);
            FlatShader::Constants constants = { Render::ViewProjection, { 1, 1, 1, 1 } };
            effect.Shader->SetConstants(cmdList, constants);

            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->DrawInstanced(_elementCount, 1, 0, 0);

            _elementCount = 0;
        }

        void DrawTriangle(const FlatVertex& v0, const FlatVertex& v1, const FlatVertex& v2) {
            FlatVertex verts[] = { v0, v1, v2 };
            Vertices.Copy(verts);
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

    void DrawLines(span<FlatVertex> verts) {
        Resources->LineBatch.DrawLines(verts);
    }

    void DrawCross(const Vector3& p, const Color& color) {
        Resources->LineBatch.DrawLine({ p - Vector3::UnitX, color }, { p + Vector3::UnitX, color });
        Resources->LineBatch.DrawLine({ p - Vector3::UnitY, color }, { p + Vector3::UnitY, color });
        Resources->LineBatch.DrawLine({ p - Vector3::UnitZ, color }, { p + Vector3::UnitZ, color });
    }

    void DrawPoint(const Vector3& p, const Color& color) {
        auto right = Render::Camera.GetRight();
        auto up = Render::Camera.Up;
        auto scale = (Render::Camera.Position - p).Length() * 0.006f;

        Vector3 v0 = p - right * scale - up * scale;
        Vector3 v1 = p + right * scale - up * scale;
        Vector3 v2 = p + right * scale + up * scale;
        Vector3 v3 = p - right * scale + up * scale;
        Resources->PolygonBatch.DrawTriangle({ v0, color }, { v1, color }, { v2, color });
        Resources->PolygonBatch.DrawTriangle({ v2, color }, { v3, color }, { v0, color });
    }

    void DrawTriangle(const Vector3 v0, const Vector3 v1, const Vector3 v2, const Color& color) {
        Resources->PolygonBatch.DrawTriangle({ v0, color }, { v1, color }, { v2, color });
    }

    void DrawAdditiveTriangle(const Vector3 v0, const Vector3 v1, const Vector3 v2, const Color& color) {
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

    void EndFrame(ID3D12GraphicsCommandList* cmdList) {
        if (!InFrame) throw Exception("Must call BeginFrame() first");
        Resources->LineBatch.End(cmdList, Render::Effects->Line);
        Resources->PolygonBatch.End(cmdList, Render::Effects->Flat);
        Resources->AdditivePolygonBatch.End(cmdList, Render::Effects->FlatAdditive);
        InFrame = false;
    }

    // Draws a crosshair in front of the camera
    void DrawCrosshair(float size) {
        auto center = Render::Camera.Position + Render::Camera.GetForward() * 10;
        auto right = Render::Camera.GetRight();
        auto up = Render::Camera.Up;
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
        Effects->Flat.Apply(cmdList);

        FlatShader::Constants constants;
        constants.Transform = transform;
        constants.Tint = color;
        Shaders->Flat.SetConstants(cmdList, constants);

        Resources->Batch.Begin(cmdList);
        DrawMesh(Arrow.GetVertices(), Arrow.GetIndices());
        Resources->Batch.End();
        DrawCalls++;
    }

    void DrawCube(ID3D12GraphicsCommandList* cmdList, const Matrix& transform, const Color& color) {
        Effects->Flat.Apply(cmdList);

        FlatShader::Constants constants;
        constants.Transform = transform;
        constants.Tint = color;
        Shaders->Flat.SetConstants(cmdList, constants);

        Resources->Batch.Begin(cmdList);
        DrawMesh(GizmoCube.GetVertices(), GizmoCube.GetIndices());
        Resources->Batch.End();
        DrawCalls++;
    }

    void DrawCircle(float radius, const Matrix& transform, const Color& color) {
        Vector3 p0 = Vector3::Transform({ radius, 0 , 0 }, transform);
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

    // Draws a circle that always faces the camera
    void DrawFacingCircle(const Vector3& position, float radius, const Color& color) {
        auto transform = Matrix::CreateBillboard(position, Camera.Position, Camera.Up);
        Vector3 p0 = Vector3::Transform({ radius, 0, 0 }, transform);

        constexpr int Steps = 16;
        for (int i = 0; i <= Steps; i++) {
            Vector3 p;
            p.x = std::cos(XM_2PI * (i / (float)Steps)) * radius;
            p.y = std::sin(XM_2PI * (i / (float)Steps)) * radius;
            p = Vector3::Transform(p, transform);
            DrawTriangle(p0, p, position, color);
            p0 = p;
        }
    }

    void DrawFacingSquare(const Vector3& p, float size, const Color& color) {
        auto right = Render::Camera.GetRight();
        auto up = Render::Camera.Up;
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
        Vector3 v0 = Vector3::Transform({ radius, 0 , 0 }, transform);
        Vector3 v2 = Vector3::Transform({ radius2, 0 , 0 }, transform);

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
        auto Polar = [&](float radius, int i) {
            Vector3 v;
            v.x = std::cos(length * (i / (float)Steps) + offset) * radius;
            v.y = std::sin(length * (i / (float)Steps) + offset) * radius;
            return Vector3::Transform(v, transform);
        };

        Vector3 v0 = Polar(radius, 0);
        Vector3 v2 = Polar(radius2, 0);

        for (int i = 1; i <= Steps; i++) {
            Vector3 v1 = Polar(radius, i);
            Vector3 v3 = Polar(radius2, i);
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

    void DrawWallMarker(const Face& face, const Color& color, float height) {
        auto center = face.Center() + face.AverageNormal() * height;

        DrawLine(face[0], center, color);
        DrawLine(face[1], center, color);
        DrawLine(face[2], center, color);
        DrawLine(face[3], center, color);
    }

    void DrawArrow(const Vector3& start, const Vector3& end, const Color& color) {
        auto dir = end - start;
        dir.Normalize();

        DrawLine(start, end, color);
        auto up = dir.Cross(Camera.GetForward());
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

    void DrawSide(Level& level, Tag tag, const Color& color) {
        auto [seg, side] = level.GetSegmentAndSide(tag);
        auto i = side.GetRenderIndices();
        auto& v = level.Vertices;
        auto si = seg.GetVertexIndices(tag.Side);

        DrawTriangle(v[si[i[0]]], v[si[i[1]]], v[si[i[2]]], color);
        DrawTriangle(v[si[i[3]]], v[si[i[4]]], v[si[i[5]]], color);
    }

    void DrawSide(Level& level, Segment& seg, SideID side, const Color& color) {
        auto i = seg.GetSide(side).GetRenderIndices();
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

    void DrawSideOutline(Level& level, Segment& seg, SideID side, const Color& color) {
        auto i = seg.GetSide(side).GetRenderIndices();
        auto& v = level.Vertices;
        auto si = seg.GetVertexIndices(side);

        DrawLine(v[si[0]], v[si[1]], color);
        DrawLine(v[si[1]], v[si[2]], color);
        DrawLine(v[si[2]], v[si[3]], color);
        DrawLine(v[si[3]], v[si[0]], color);
    }
}
