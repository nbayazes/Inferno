#pragma once

namespace Inferno {
    class Camera;

    void CheckDeveloperHotkeys();
    void HandleEditorDebugInput(float dt);
    void GenericCameraController(Camera& camera, float speed, bool orbit = false);
    // Same as handle input, but is only called on game ticks
    void HandleFixedUpdateInput(float dt);
    void HandleInput(float dt);
}
