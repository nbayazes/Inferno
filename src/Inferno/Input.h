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

    struct InputDevice {
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
        std::array<float, 8> axes, axesPrevious, axisRepeatTimer; // axis values, normalized to -1 to 1
        std::bitset<8> axisHeld, axisRepeat;

        uint8 hat;
        std::bitset<32> buttonPressed, buttonHeld, buttonReleased, buttonPrev, buttonRepeat;
        std::array<float, 32> buttonRepeatTimer;
        std::array<float, 3> gyro{}; // gyroscope
        std::array<float, 3> accel{}; // accelerometer

        float repeatDelay = 0.5f; // Time before holding a button or axis repeats
        float repeatSpeed = 0.04f; // Time between repeats

        float axisThreshold = 0.3f; // How far an axis must travel to count as 'pressed'

        bool IsGamepad() const {
            return type != SDL_GAMEPAD_TYPE_UNKNOWN;
        }

        bool IsXBoxController() const {
            return type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE;
        }

        // true when button is first pressed or held down
        bool ButtonHeld(uint8 button) const {
            if (button > buttonPressed.size()) return false;
            return buttonPressed[button] || buttonHeld[button];
        }

        // true when button is first pressed. optionally can check for repeats
        bool ButtonWasPressed(uint8 button, bool repeat = false) const {
            if (button > buttonPressed.size()) return false;
            if (repeat && buttonRepeat[button])
                return true;

            return buttonPressed[button];
            //return buttonPressed[button] || (repeat && buttonRepeat[button]);
        }

        // true when button is released
        bool ButtonWasReleased(uint8 button) const {
            if (button > buttonPressed.size()) return false;
            return buttonReleased[button];
        }

        // Returns true when an axis crosses a threshold value
        bool AxisPressed(uint8 axis, bool positive, bool repeat = false) const {
            if (axis > axes.size()) return false;
            auto threshold = abs(axisThreshold);

            if (positive) {
                if (axes[axis] >= threshold) {
                    if (axesPrevious[axis] < threshold) {
                        return true; // crossed threshold
                    }
                    else if (repeat) {
                        if (axisRepeat[axis])
                            return true;
                    }
                }
            }
            else {
                if (axes[axis] <= -threshold) {
                    if (axesPrevious[axis] > -threshold) {
                        return true; // crossed threshold
                    }
                    else if (repeat) {
                        if (axisRepeat[axis])
                            return true;
                    }
                }
            }

            return false;
        }

        bool AxisReleased(uint8 axis, bool positive) const {
            if (axis > axes.size()) return false;
            auto threshold = abs(axisThreshold);

            if (positive) {
                return axes[axis] < threshold && axesPrevious[axis] >= threshold;
            }
            else {
                return axes[axis] > -threshold && axesPrevious[axis] <= -threshold;
            }
        }

        // Returns true if any axis was pressed. Returns state through parameters.
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
        
        // Returns true if hat is pressed in a direction. Returns direction through parameter.
        bool CheckHat(uint8& hat) const {
            for (uint8 i = 1; i <= 4; i++) {
                if (HatDirection(Input::HatDirection(i))) {
                    hat = i;
                    return true;
                }
            }

            return false;
        }

        bool CheckButtonDown(uint8& button) const {
            for (uint8 i = 0; i < buttonPressed.size(); i++) {
                if (ButtonWasPressed(i)) {
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

        void Update(float dt) {
            buttonReleased.reset();
            axisRepeat.reset();
            buttonRepeat.reset();
            buttonPrev = buttonPressed;
            buttonPressed.reset();
            axesPrevious = axes;

            for (uint8 i = 0; i < buttonHeld.size(); i++) {
                if (buttonPrev[i]) {
                    buttonRepeatTimer[i] = repeatDelay;
                }
                else if (buttonHeld[i]) {
                    buttonRepeatTimer[i] -= dt;

                    if (buttonRepeatTimer[i] <= 0) {
                        buttonRepeatTimer[i] = repeatSpeed;
                        buttonRepeat[i] = true;
                    }
                }
            }

            // Update axis hold state
            for (uint8 i = 0; i < axes.size(); i++) {
                bool wasHeld = axisHeld[i];
                axisHeld[i] = axes[i] >= axisThreshold || axes[i] <= -axisThreshold;

                if (axisHeld[i]) {
                    if (wasHeld) {
                        // Axis is held for several updates
                        axisRepeatTimer[i] -= dt;

                        if (axisRepeatTimer[i] <= 0) {
                            // Trigger a repeat
                            axisRepeatTimer[i] += repeatSpeed;
                            axisRepeat[i] = true;
                        }
                    }
                    else {
                        // Newly moved axis, reset the timer
                        axisRepeatTimer[i] = repeatDelay;
                    }
                }
                else {
                    axisRepeatTimer[i] = 0;
                }
            }
        }

        void ResetState() {
            buttonPressed.reset();
            buttonReleased.reset();
            buttonHeld.reset();
            buttonPrev.reset();
            ranges::fill(buttonRepeatTimer, 0.0f);
            ranges::fill(axisRepeatTimer, 0.0f);
        }

        void Press(uint8 button) {
            if (button > buttonPressed.size()) return;
            buttonPressed[button] = true;
            buttonHeld[button] = true;
        }

        void Release(uint8 button) {
            if (button > buttonPressed.size()) return;
            buttonPressed[button] = false;
            buttonHeld[button] = false;
            buttonReleased[button] = true;
        }
    };

    List<InputDevice> GetDevices();

    // Returns the state of a joystick with the given guid.
    // Filters to devices enabled in the global settings by default.
    InputDevice* GetDevice(string_view guid, bool enabled = true);

    // Called when a new input device is added
    inline std::function<void(InputDevice&)> AddDeviceCallback;

    Vector2 CircularDampen(const Vector2& input, float innerDeadzone, float outerDeadzone);
    float LinearDampen(float value, float innerDeadzone, float outerDeadzone, float linearity);

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

    inline DirectX::SimpleMath::Vector2 MouseDelta;
    inline DirectX::SimpleMath::Vector2 MousePosition;
    inline DirectX::SimpleMath::Vector2 DragStart; // Mouse drag start position in screen coordinates

    int GetWheelDelta();

    // Special conditions that check for left or right modifier keys. Also works reliably in editor mode.
    inline bool ControlDown;
    inline bool ShiftDown;
    inline bool AltDown;
    inline bool HasFocus = true; // Window has focus

    void Update(float dt);
    void Initialize(HWND);
    void Shutdown();

    // Returns true while a key is held down
    bool IsKeyDown(Keys);

    // Returns true when a key is first pressed or on OS repeat with a flag.
    bool OnKeyPressed(Keys, bool onRepeat = false);

    // Returns true when a key is first released
    bool OnKeyReleased(Keys);

    std::bitset<256> GetPressedKeys();
    std::bitset<256> GetRepeatedKeys();

    // Returns true while a key is held down
    bool IsMouseButtonDown(MouseButtons);

    // Returns true when a key is first pressed
    bool MouseButtonPressed(MouseButtons);

    // Returns true when a key is first released
    bool MouseButtonReleased(MouseButtons);

    // Returns true when a gamepad button is first pressed, or on repeat.
    bool OnControllerButtonPressed(SDL_GamepadButton, bool onRepeat = false);

    bool MouseMoved();

    void ResetState();

    void NextFrame(float dt);

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
