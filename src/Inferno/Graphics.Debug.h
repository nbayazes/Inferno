#pragma once
#include "Camera.h"
#include "Face.h"
#include "VertexTypes.h"
#include "Level.h"

namespace Inferno::Graphics {
    void ResetDebug();

    void DrawPoint(const Vector3& p, const Color& color);
    void DrawLine(const Vector3& v0, const Vector3& v1, const Color& color);

    void SetDebugCamera(const Camera& camera);

    void BeginFrame();
}
