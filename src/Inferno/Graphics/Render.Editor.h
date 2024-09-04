#pragma once
#include "Camera.h"
#include "CameraContext.h"
#include "Level.h"

namespace Inferno::Render {
    void CreateEditorResources();
    void ReleaseEditorResources();
    void DrawEditor(GraphicsContext& ctx, Level& level);
    void DrawObjectOutline(const Object&, const Camera& camera);
}
