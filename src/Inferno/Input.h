#pragma once

#include <bitset>
#include <DirectXTK12/Keyboard.h>

namespace Inferno::Input {
    using Keys = DirectX::Keyboard::Keys;

    enum class MouseButtons : uint8_t {
        None,
        LeftClick, // Disambiguate from Keys::Left / Right when serializing
        RightClick,
        MiddleClick,
        X1,
        X2,
        WheelUp,
        WheelDown
    };

    inline DirectX::SimpleMath::Vector2 MouseDelta;
    inline DirectX::SimpleMath::Vector2 MousePosition;
    inline DirectX::SimpleMath::Vector2 DragStart; // Mouse drag start position in screen coordinates

    int GetWheelDelta();
    //inline bool ScrolledUp() { return GetWheelDelta() > 0; }
    //inline bool ScrolledDown() { return GetWheelDelta() < 0; }

    // Special conditions that check for left or right modifier keys. Also works reliably in editor mode.
    inline bool ControlDown;
    inline bool ShiftDown;
    inline bool AltDown;
    inline bool HasFocus = true; // Window has focus

    void Update();
    void Initialize(HWND);

    // Returns true while a key is held down
    bool IsKeyDown(Keys);

    // Returns true when a key is first pressed or on OS repeat with a flag.
    bool IsKeyPressed(Keys, bool onRepeat = false);

    // Returns true when a key is first released
    bool IsKeyReleased(Keys);

    std::bitset<256> GetPressedKeys();
    std::bitset<256> GetRepeatedKeys();

    // Returns true while a key is held down
    bool IsMouseButtonDown(MouseButtons);

    // Returns true when a key is first pressed
    bool IsMouseButtonPressed(MouseButtons);

    // Returns true when a key is first released
    bool IsMouseButtonReleased(MouseButtons);

    bool MouseMoved();

    void ResetState();

    void NextFrame();

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

    void ProcessMessage(UINT message, WPARAM, LPARAM);

    std::string KeyToString(Keys key);

    enum class EventType {
        KeyPress,
        KeyRelease,
        KeyRepeat,
        MouseBtnPress,
        MouseBtnRelease,
        MouseWheel,
        MouseMoved,
        Reset
    };

    void QueueEvent(EventType type, WPARAM keyCode = 0, int64_t flags = 0);
}
