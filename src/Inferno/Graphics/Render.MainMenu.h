#pragma once

namespace Inferno {
    class GraphicsContext;

    void DrawMainMenuBackground(GraphicsContext& ctx);
    void CreateMainMenuResources();

    extern Vector3 MenuCameraTarget, MenuCameraPosition;
}