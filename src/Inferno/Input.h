#pragma once

#include <DirectXTK12/Mouse.h>
#include <DirectXTK12/Keyboard.h>

namespace Inferno::Input {
    using MouseState = DirectX::Mouse::ButtonStateTracker::ButtonState;
    inline DirectX::Keyboard::KeyboardStateTracker Keyboard;
    inline DirectX::Mouse::ButtonStateTracker Mouse;
    inline DirectX::SimpleMath::Vector2 MouseDelta;
    inline DirectX::SimpleMath::Vector2 MousePosition;
    inline int WheelDelta;
    inline Vector2 DragStart; // Mouse drag start position in screen coordinates

    inline bool ControlDown;
    inline bool ShiftDown;
    inline bool AltDown;

    void Update();
    void Initialize(HWND hwnd);

    // Returns true while a key is held down
    bool IsKeyDown(DirectX::Keyboard::Keys);

    // Returns true when a key is first pressed
    bool IsKeyPressed(DirectX::Keyboard::Keys);

    void ResetState();

    enum class SelectionState {
        None,
        Preselect, // Mouse button pressed
        BeginDrag, // Fires after preselect and the cursor moves
        Dragging, // Mouse is moving with button down
        ReleasedDrag, // Mouse button released after dragging
        Released // Button released. Does not fire if dragging
    };

    inline SelectionState DragState, LeftDragState, RightDragState;

    enum class MouseMode {
        Normal,
        Mouselook,
        Orbit
    };

    MouseMode GetMouseMode();
    void SetMouseMode(MouseMode);

    // Workaround for the relative mouse mode not summing deltas properly
    void ProcessRawMouseInput(UINT message, WPARAM, LPARAM);
}