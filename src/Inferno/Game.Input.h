#pragma once

namespace Inferno {
    void CheckGlobalHotkeys();
    void HandleEditorDebugInput(float dt);
    void HandleAutomapInput();
    bool ConfirmedInput();
    void HandleInput();
    void HandleShipInput(float dt);
}
