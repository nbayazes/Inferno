#include "pch.h"
#include "Graphics.Debug.h"
#include "Graphics/Render.Debug.h"

namespace Inferno::Graphics {
    namespace {
        const Camera* DebugCamera = nullptr;
    }

    void ResetDebug() {
        Render::Debug::DebugPoints.clear();
        Render::Debug::DebugPoints2.clear();
        Render::Debug::DebugLines.clear();
    }

    void DrawPoint(const Vector3& p, const Color& color) {
        ASSERT(DebugCamera);
        if (!DebugCamera) return;
        Render::Debug::DrawPoint(p, color, *DebugCamera);
    }

    void DrawLine(const Vector3& v0, const Vector3& v1, const Color& color) {
        Render::Debug::DrawLine(v0, v1, color);
    }

    void SetDebugCamera(const Camera& camera) {
        DebugCamera = &camera;
    }

    void BeginFrame() {
        Render::Metrics::BeginFrame();
        Render::Debug::BeginFrame();
    }
}
