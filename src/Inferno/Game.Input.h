#pragma once

namespace Inferno {
    class Camera;

    void CheckGlobalHotkeys();
    void HandleEditorDebugInput(float dt);
    void GenericCameraController(Camera& camera, float speed, bool orbit = false);
    void HandleAutomapInput();
    bool ConfirmedInput();
    void HandleInput();
    void HandleShipInput(float dt);
}
