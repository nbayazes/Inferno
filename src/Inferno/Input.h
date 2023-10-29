#pragma once

#include <DirectXTK12/Mouse.h>
#include <DirectXTK12/Keyboard.h>

namespace Inferno::Input {
    using Keys = DirectX::Keyboard::Keys;
    enum MouseButtons : uint8_t {
        Left,
        Right,
        Middle,
        X1,
        X2
    };

    inline DirectX::SimpleMath::Vector2 MouseDelta;
    inline DirectX::SimpleMath::Vector2 MousePosition;
    inline int WheelDelta;
    inline DirectX::SimpleMath::Vector2 DragStart; // Mouse drag start position in screen coordinates

    inline bool ControlDown;
    inline bool ShiftDown;
    inline bool AltDown;
    inline bool HasFocus = true; // Window has focus

    void Update();
    void Initialize(HWND);

    // Returns true while a key is held down
    bool IsKeyDown(Keys);

    // Returns true when a key is first pressed
    bool IsKeyPressed(Keys);

    // Returns true when a key is first released
    bool IsKeyReleased(Keys);

    // Returns true while a key is held down
    bool IsMouseButtonDown(MouseButtons);

    // Returns true when a key is first pressed
    bool IsMouseButtonPressed(MouseButtons);

    // Returns true when a key is first released
    bool IsMouseButtonReleased(MouseButtons);

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

    enum class EventType {
        KeyPress,
        KeyRelease,
        MouseBtnPress,
        MouseBtnRelease,
        MouseWheel,
        Reset
    };

    void QueueEvent(EventType type, WPARAM keyCode = 0, int64_t flags = 0);
}