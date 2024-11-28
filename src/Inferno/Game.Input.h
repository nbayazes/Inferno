#pragma once

namespace Inferno {
    class Camera;

    void CheckGlobalHotkeys();
    void HandleEditorDebugInput(float dt);
    void GenericCameraController(Camera& camera, float speed, bool orbit = false);
    bool ConfirmedInput();
    // Same as handle input, but is only called on game ticks
    void HandleFixedUpdateInput(float dt);
    void HandleInput(float dt);
    void HandleShipInput(float dt);
}
