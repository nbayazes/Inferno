#pragma once
#include "DirectX.h"
#include "Types.h"
#include "Input.h"
#include "Object.h"

namespace Inferno {
    void CheckGlobalHotkeys();
    void HandleEditorDebugInput(float dt);
    void FixedUpdateInput();
    void HandleInputImmediate(float dt);
}
