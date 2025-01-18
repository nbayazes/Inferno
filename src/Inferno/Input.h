#pragma once

#include <bitset>
#include <DirectXTK12/Keyboard.h>
#include <SDL3/SDL_gamepad.h>

namespace Inferno::Input {
    using Keys = DirectX::Keyboard::Keys;
    enum class InputType { Unknown, Keyboard, Mouse, Gamepad };

    struct Gamepad {
        string guid{}; // guid used to save and restore bindings
        string name; // display name
        bool connected = false;
        uint32 id = 0; // joystick ID from SDL
    };

    List<Gamepad> GetGamepads();

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

    enum class MouseAxis : uint8_t {
        None,
        MouseX,
        MouseY
    };

    // Controller or Joystick axis
    enum class InputAxis : uint8_t {
        // Controller Axis
        None,
        LeftStick,
        RightStick,
        LeftTrigger,
        RightTrigger,
        // Joystick Axis
        Axis0,
        Axis1,
        Axis2,
        Axis3,
        Axis4,
        Axis5,
        Axis6,
        Axis7
    };

    // Input actions in menus. Used to consolidate input from multiple devices.
    enum class MenuAction {
        None, Left, Right, Up, Down, Confirm, Cancel, NextPage, PreviousPage
    };

    // Returns the menu action for this update.
    // Note: Only a single action can be returned at a time which might cause issues for diagonal inputs.
    MenuAction GetMenuAction();

    inline DirectX::SimpleMath::Vector2 MouseDelta;
    inline DirectX::SimpleMath::Vector2 MousePosition;
    inline DirectX::SimpleMath::Vector2 DragStart; // Mouse drag start position in screen coordinates

    // Sums of all linear inputs (controller, joystick)
    inline float Pitch; 
    inline float Yaw;
    inline float Roll;

    inline Vector3 Thrust; // Sum of all thrust inputs this update. (xyz: left/right, up/down, forward/rev)

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
    void Shutdown();

    // Returns true while a key is held down
    bool IsKeyDown(Keys);

    // Returns true when a key is first pressed or on OS repeat with a flag.
    bool IsKeyPressed(Keys, bool onRepeat = false);

    // Returns true when a key is first released
    bool IsKeyReleased(Keys);

    std::bitset<256> GetPressedKeys();
    std::bitset<256> GetRepeatedKeys();

    bool IsControllerButtonDown(SDL_GamepadButton);

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
