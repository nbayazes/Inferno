#pragma once

#include <bitset>
#include <DirectXTK12/Keyboard.h>
#include <SDL3/SDL_gamepad.h>
#include "Utility.h"

namespace Inferno {
    // Input actions in menus. Used to consolidate input from multiple devices.
    enum class MenuAction {
        None, Left, Right, Up, Down, Confirm, Cancel, NextPage, PreviousPage, Count
    };
}

namespace Inferno::Input {
    using Keys = DirectX::Keyboard::Keys;

    constexpr std::array XBOX_BUTTON_LABELS = {
        "a", "b", "x", "y",
        "back", "guide", "start",
        "l-stick", "r-stick", "l-shoulder", "r-shoulder",
        "up", "down", "left", "right",
        "paddle1", "paddle2", "paddle3", "paddle4", // unused
        "misc0", "misc1", "misc2", "misc3", "misc4", "misc5"
    };

    constexpr std::array PS_BUTTON_LABELS = {
        "cross", "circle", "square", "triangle",
        "create", "PS", "options",
        "l3", "r3", "l1", "r1",
        "up", "down", "left", "right",
        "mute", 
        "paddle2", "paddle3", "paddle4", // unused
        "misc1",
        "touchpad", "misc2", "misc3", "misc4", "misc5"
    };

    enum class InputType { Unknown, Keyboard, Mouse, Gamepad, Joystick };

    enum class HatDirection { Centered, Left, Right, Up, Down };

    struct Joystick {
        string guid; // guid used to save and restore bindings
        string name; // display name
        //bool connected = false;
        uint32 id = 0; // joystick ID from SDL
        int numButtons = 0, numAxes = 0, numHats = 0;
        SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN; // if unknown, treat as a joystick

        string GetButtonLabel(uint8 button) const {
            if (button < XBOX_BUTTON_LABELS.size()) {
                switch (type) {
                    case SDL_GAMEPAD_TYPE_XBOX360:
                    case SDL_GAMEPAD_TYPE_XBOXONE:
                        return XBOX_BUTTON_LABELS[button];
                    case SDL_GAMEPAD_TYPE_PS3:
                    case SDL_GAMEPAD_TYPE_PS4:
                    case SDL_GAMEPAD_TYPE_PS5:
                        return PS_BUTTON_LABELS[button];
                }
            }

            return fmt::format("button {}", button);
        }


        // State for inputs
        std::array<float, 8> axes, axesPrevious; // axis values, normalized to -1 to 1

        uint8 hat;
        std::bitset<32> buttons, pressed, released, previous;
        std::array<float, 3> gyro{};  // gyroscope
        std::array<float, 3> accel{}; // accelerometer

        float innerDeadzone = 0.1f;
        float outerDeadzone = 1.0f;

        bool IsGamepad() const {
            return type != SDL_GAMEPAD_TYPE_UNKNOWN;
        }

        // true when button is first pressed or held down
        bool Held(uint8 button) const {
            if (button > buttons.size()) return false;
            return buttons[button] || pressed[button];
        }

        // true when button is first pressed
        bool ButtonDown(uint8 button) const {
            if (button > buttons.size()) return false;
            return buttons[button];
        }

        // true when button is released
        bool ButtonUp(uint8 button) const {
            if (button > buttons.size()) return false;
            return released[button];
        }

        // Returns true when an axis crosses a threshold value
        bool AxisPressed(uint8 axis, bool positive, float threshold = 0.3f) const {
            if (axis > axes.size()) return false;
            threshold = abs(threshold);

            if (positive) {
                return axes[axis] >= threshold && axesPrevious[axis] < threshold;
            }
            else {
                return axes[axis] <= -threshold && axesPrevious[axis] > -threshold;
            }
        }

        bool AxisReleased(uint8 axis, bool positive, float threshold = 0.3f) const {
            if (axis > axes.size()) return false;
            threshold = abs(threshold);

            if (positive) {
                return axes[axis] < threshold && axesPrevious[axis] >= threshold;
            }
            else {
                return axes[axis] > -threshold && axesPrevious[axis] <= -threshold;
            }
        }

        // Returns true if an axis was pressed. Parameters provide the values.
        bool CheckAxisPressed(uint8& axis, bool& dir) const {
            for (uint8 i = 0; i < axes.size(); i++) {
                if (AxisPressed(i, true)) {
                    axis = i;
                    dir = true;
                    return true;
                }
                else if (AxisPressed(i, false)) {
                    axis = i;
                    dir = false;
                    return true;
                }
            }

            return false;
        }

        bool CheckButtonDown(uint8& button) const {
            for (uint8 i = 0; i < buttons.size(); i++) {
                if (ButtonDown(i)) {
                    button = i;
                    return true;
                }
            }

            return false;
        }

        bool HatDirection(HatDirection dir) const {
            switch (dir) {
                case HatDirection::Centered:
                    return hat == SDL_HAT_CENTERED;
                case HatDirection::Up:
                    return HasFlag(hat, (uint8)SDL_HAT_UP);
                case HatDirection::Right:
                    return HasFlag(hat, (uint8)SDL_HAT_RIGHT);
                case HatDirection::Down:
                    return HasFlag(hat, (uint8)SDL_HAT_DOWN);
                case HatDirection::Left:
                    return HasFlag(hat, (uint8)SDL_HAT_LEFT);
                default:
                    return false;
            }
        }

        void Update() {
            buttons.reset();
            released.reset();

            //axisReleased.reset();
            //axisPressed.reset();
            //pressed.reset();

            previous = buttons;
            axesPrevious = axes;
        }

        void ResetState() {
            buttons.reset();
            released.reset();
            pressed.reset();
            previous.reset();

            //axisPressed.reset();
            //axisReleased.reset();
            //axisHeld.reset();
            //axisPressed.reset();
        }

        void Press(uint8 button) {
            if (button > buttons.size()) return;
            buttons[button] = true;
            pressed[button] = true;
        }

        void Release(uint8 button) {
            if (button > buttons.size()) return;
            buttons[button] = false;
            pressed[button] = false;
            released[button] = true;
        }

        //string GetAxisLabel(uint8 axis) const {
        //    
        //}

        //string GetHatLabel(uint8 hat) const {
        //    
        //}
    };

    List<Joystick> GetJoysticks();

    // Returns the state of a joystick with the given guid.
    // Filters to devices enabled in the global settings by default.
    Joystick* GetJoystick(string_view guid, bool enabled = true);

    Vector2 CircularDampen(const Vector2& input, float innerDeadzone, float outerDeadzone);
    float LinearDampen(float value, float innerDeadzone, float outerDeadzone);

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

    class MenuActionState {
        std::bitset<(int)MenuAction::Count> _state;

    public:
        void Set(MenuAction action) {
            if (action >= MenuAction::Count) return;
            _state[(int)action] = true;
        }

        bool IsSet(MenuAction action) const {
            if (action >= MenuAction::Count) return false;
            return _state[(int)action];
        }

        void Reset() {
            _state.reset();
        }

        bool HasAction() const {
            return _state.any();
        }

        bool operator==(MenuAction action) const { return IsSet(action); }
    };

    inline MenuActionState MenuActions;

    // Returns the menu action for this update.
    // Note: Only a single action can be returned at a time which might cause issues for diagonal inputs.
    //MenuAction GetMenuAction();

    inline DirectX::SimpleMath::Vector2 MouseDelta;
    inline DirectX::SimpleMath::Vector2 MousePosition;
    inline DirectX::SimpleMath::Vector2 DragStart; // Mouse drag start position in screen coordinates

    // Sums of all linear inputs (controller, joystick)
    //inline float Pitch;
    //inline float Yaw;
    //inline float Roll;

    //inline Vector3 Thrust; // Sum of all thrust inputs this update. (xyz: left/right, up/down, forward/rev)

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
        Preselect,    // Mouse button pressed
        BeginDrag,    // Fires after preselect and the cursor moves
        Dragging,     // Mouse is moving with button down
        ReleasedDrag, // Mouse button released after dragging
        Released      // Button released. Does not fire if dragging
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
