#include "pch.h"
#include "Input.h"
#include <bitset>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <vector>
#include "Game.h"
#include "imgui_local.h"
#include "PlatformHelpers.h"
#include "Settings.h"
#include "Shell.h"

using namespace DirectX::SimpleMath;

namespace Inferno::Input {
    namespace {
        // Gamepads should store their inputs between each update.
        // Holding a stick steady should continue that movement
        Vector2 GamepadLeftStick, GamepadRightStick;

        Vector2 MousePrev, DragEnd;
        constexpr float DRAG_WINDOW = 3.0f;
        Vector2 WindowCenter;
        HWND Hwnd;
        int RawX, RawY;
        bool MouseRecentlyMoved = false;
        DirectX::SimpleMath::Vector2 PrevMousePosition;

        MouseMode ActualMouseMode{}, RequestedMouseMode{};
        int WheelDelta;

        template <size_t N>
        struct ButtonState {
            std::bitset<N> pressed, released, repeat;
            std::bitset<N> current, previous;

            constexpr static size_t Size() { return N; }

            bool Controller = false; // When set to true, holds button pressed state until a release is called

            void Reset() {
                pressed.reset();
                repeat.reset();
                released.reset();
                current.reset();
                previous.reset();
                MouseRecentlyMoved = false;
            }

            // Call this before handling a frame's input events
            void NextFrame() {
                if (!Controller) {
                    pressed.reset();
                    released.reset();
                    repeat.reset();
                }

                previous = current;
                MouseRecentlyMoved = false;
            }

            // These functions assume that events will arrive in the correct order
            void Press(uint8_t key) {
                if (key >= N)
                    return;
                pressed[key] = true;
                current[key] = true;
                repeat[key] = true;
            }

            void Repeat(uint8_t key) {
                repeat[key] = true;
            }

            void Release(uint8_t key) {
                if (key >= N)
                    return;
                released[key] = true;
                current[key] = false;
                repeat[key] = false;

                if (Controller) {
                    pressed[key] = false;
                }
            }
        };

        ButtonState<256> _keyboard;
        ButtonState<8> _mouseButtons;
        ButtonState<SDL_GAMEPAD_BUTTON_COUNT> _controller = { .Controller = true };

        struct InputEvent {
            EventType type;
            uint8_t keyCode;
            int64_t flags;
        };

        std::vector<InputEvent> _inputEventQueue;

        void HandleInputEvents() {
            for (auto& event : _inputEventQueue) {
                switch (event.type) {
                    case EventType::KeyRepeat:
                    case EventType::KeyPress:
                    case EventType::KeyRelease:
                        if (event.keyCode == VK_SHIFT || event.keyCode == VK_CONTROL || event.keyCode == VK_MENU) {
                            // The keystroke flags carry extra information for Shift, Alt & Ctrl to
                            // disambiguate between the left and right variants of each key
                            bool isExtendedKey = (HIWORD(event.flags) & KF_EXTENDED) == KF_EXTENDED;
                            int scanCode = LOBYTE(HIWORD(event.flags)) | (isExtendedKey ? 0xe000 : 0);
                            event.keyCode = static_cast<uint8_t>(LOWORD(MapVirtualKeyW(static_cast<UINT>(scanCode), MAPVK_VSC_TO_VK_EX)));
                        }

                        if (event.type == EventType::KeyPress)
                            _keyboard.Press(event.keyCode);
                        else if (event.type == EventType::KeyRepeat)
                            _keyboard.Repeat(event.keyCode);
                        else {
                            if (event.keyCode == VK_SHIFT) {
                                // For some reason, if both Shift keys are held down, only the last of the
                                // two registers a release event
                                _keyboard.Release(VK_RSHIFT);
                                _keyboard.Release(VK_LSHIFT);
                            }
                            _keyboard.Release(event.keyCode);
                        }
                        break;

                    case EventType::MouseBtnPress:
                        _mouseButtons.Press(event.keyCode);
                        break;

                    case EventType::MouseBtnRelease:
                        _mouseButtons.Release(event.keyCode);
                        break;

                    case EventType::MouseWheel:
                        WheelDelta += static_cast<int>(event.flags);

                        if (WheelDelta > 0)
                            _mouseButtons.Press((uint64)MouseButtons::WheelUp);
                        else if (WheelDelta < 0)
                            _mouseButtons.Press((uint64)MouseButtons::WheelDown);

                        break;

                    case EventType::Reset:
                        _keyboard.Reset();
                        _mouseButtons.Reset();
                        _controller.Reset();
                        break;

                    case EventType::MouseMoved:
                        MouseRecentlyMoved = true;
                        break;
                }
            }

            _inputEventQueue.clear();
        }
    }

    void NextFrame() {
        _keyboard.NextFrame();
        _mouseButtons.NextFrame();
        _controller.NextFrame();
        WheelDelta = 0;
    }

    int GetWheelDelta() { return WheelDelta; }

    SelectionState UpdateDragState(MouseButtons button, SelectionState dragState) {
        if (_mouseButtons.pressed[(uint64)button]) {
            // Don't allow a drag to start when the cursor is over imgui.
            if (ImGui::GetCurrentContext()->HoveredWindow != nullptr) return SelectionState::None;

            DragStart = Input::MousePosition;
            return SelectionState::Preselect;
        }
        else if (_mouseButtons.released[(uint64)button]) {
            DragEnd = Input::MousePosition;

            if (dragState == SelectionState::Dragging)
                return SelectionState::ReleasedDrag;
            else if (dragState != SelectionState::None)
                return SelectionState::Released;
            return dragState;
        }
        else if (_mouseButtons.previous[(uint64)button]) {
            if (dragState == SelectionState::Preselect &&
                Vector2::Distance(DragStart, Input::MousePosition) > DRAG_WINDOW) {
                // Don't allow a drag to start when the cursor is over imgui.
                if (ImGui::GetCurrentContext()->HoveredWindow != nullptr) return SelectionState::None;

                return SelectionState::BeginDrag;
            }
            else if (dragState == SelectionState::BeginDrag) {
                return SelectionState::Dragging;
            }
            return dragState;
        }
        else
            return SelectionState::None;
    }

    void QueueEvent(EventType type, WPARAM keyCode, int64_t flags) {
        _inputEventQueue.push_back({ type, static_cast<uint8_t>(keyCode), flags });
    }

    string GuidToString(SDL_GUID guid) {
        char buffer[33];
        SDL_GUIDToString(guid, buffer, (int)std::size(buffer));
        return { buffer };
    }

    bool GuidIsZero(string_view guid) {
        return ranges::all_of(guid, [](char c) { return c == '0'; });
    }

    class GamepadManager {
        std::list<Gamepad> _gamepads;

    public:
        Gamepad* FindGamepad(string_view guid) {
            for (auto& gamepad : _gamepads) {
                if (gamepad.guid == guid) return &gamepad;
            }

            return nullptr;
        }

        Gamepad* FindGamepad(SDL_JoystickID id) {
            for (auto& gamepad : _gamepads) {
                if (gamepad.id == id) return &gamepad;
            }

            return nullptr;
        }

        void AddGamepad(SDL_JoystickID id) {
            if (!SDL_IsGamepad(id)) {
                SPDLOG_WARN("Tried to add a non-gamepad {}", id);
                return;
            }

            auto gamepad = SDL_OpenGamepad(id);
            if (!gamepad) {
                SPDLOG_WARN("Unable to open gamepad");
                return;
            }

            auto guid = GuidToString(SDL_GetGamepadGUIDForID(id));
            auto name = SDL_GetGamepadNameForID(id);

            if (GuidIsZero(guid) || !name) {
                SPDLOG_WARN("Ignoring gamepad {} with no name (guid: {})", id, guid);
                return;
            }

            auto device = FindGamepad(guid); // check if gamepad was already connected

            if (!device) {
                device = &_gamepads.emplace_back(); // create a new device
            }

            device->connected = SDL_GamepadConnected(gamepad);

            if (!device->connected) {
                return;
            }

            device->name = name;
            device->guid = guid;
            device->id = id;

            // Name can be empty for wireless PS5 controllers that are turned off
            SPDLOG_INFO("Add gamepad {}: {} - {}", id, device->name, device->guid);

            if (SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true))
                SPDLOG_INFO("Enabled Accel");
            if (SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true))
                SPDLOG_INFO("Enabled Gyro");
        }

        void RemoveGamepad(SDL_JoystickID id) {
            SPDLOG_INFO("Remove gamepad {}", id);
            _gamepads.remove_if([id](auto g) { return g.id == id; });
            auto gamepad = SDL_GetGamepadFromID(id);
            SDL_CloseGamepad(gamepad);
        }

        List<Gamepad> GetGamepads() {
            return { _gamepads.begin(), _gamepads.end() };
        }
    };

    GamepadManager Gamepads;

    List<Gamepad> GetGamepads() {
        return Gamepads.GetGamepads();
    }

    Vector2 CircularDampen(const Vector2& input, float innerDeadzone, float outerDeadzone) {
        float magnitude = input.Length();
        float scale = (magnitude - innerDeadzone) / (outerDeadzone - innerDeadzone);
        return input * Saturate(scale) / magnitude;
    }

    void Update() {
        SDL_Event event;
        Pitch = Yaw = Roll = 0;
        Input::Thrust = Vector3::Zero;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                {
                    _controller.Release(event.gbutton.button);
                    break;
                }

                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                {
                    _controller.Press(event.gbutton.button);
                    break;
                }

                case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
                {
                    auto gamepad = SDL_GetGamepadFromID(event.gdevice.which);

                    float gyro[3], accel[3];
                    if (SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, gyro, std::size(gyro))) {
                        //SPDLOG_INFO("Gyro: {}, {}, {}", gyro[0], gyro[1], gyro[1]);
                        Pitch += gyro[0] * .75f;
                        Yaw += -gyro[1] * 1;
                        Roll += -gyro[2] * .125f;
                    }

                    if (SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_ACCEL, accel, std::size(accel))) {
                        //SPDLOG_INFO("Accel: {}, {}, {}", accel[0], accel[1], accel[1]);
                    }
                    break;
                }

                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                {
                    auto gamepad = SDL_GetGamepadFromID(event.gdevice.which);
                    Vector2 rightStick;
                    rightStick.x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
                    rightStick.y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
                    GamepadRightStick = CircularDampen({ rightStick.x, rightStick.y }, 0.1f, 1);

                    // todo: change based on mappings
                    Vector2 leftStick;
                    leftStick.x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                    leftStick.y = -SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                    GamepadLeftStick = CircularDampen({ leftStick.x, leftStick.y }, 0.1f, 1);
                    // thrust.y = ??; // unknown what to bind slide up/down to
                    break;
                }

                case SDL_EventType::SDL_EVENT_GAMEPAD_ADDED:
                {
                    Gamepads.AddGamepad(event.gdevice.which);
                    //AddGamepad(event.gdevice.which);
                    break;
                }

                case SDL_EventType::SDL_EVENT_GAMEPAD_REMOVED:
                {
                    Gamepads.RemoveGamepad(event.gdevice.which);
                    //RemoveGamepad(event.gdevice.which);
                    break;
                }
            }
        }

        if (Settings::Inferno.EnableGamepads) {
            Input::Yaw += GamepadRightStick.x;
            Input::Pitch += GamepadRightStick.y;

            Input::Thrust.x += GamepadLeftStick.x;
            Input::Thrust.z += GamepadLeftStick.y;
        }

        if (RequestedMouseMode != ActualMouseMode) {
            ActualMouseMode = RequestedMouseMode;
            RawX = RawY = 0;

            if (ActualMouseMode != MouseMode::Normal) {
                RECT r{};
                GetClientRect(Hwnd, &r);
                POINT center = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
                WindowCenter = Vector2{ (float)center.x, (float)center.y };
            }

            static bool isHidden = false;
            auto shouldHide = ActualMouseMode != MouseMode::Normal;
            if (isHidden != shouldHide) {
                ShowCursor(!shouldHide);
                isHidden = shouldHide;
            }
        }

        //auto mouseState = _mouse.GetState();

        HandleInputEvents();

        if (ActualMouseMode != MouseMode::Normal) {
            // keep the cursor in place in mouselook mode
            MousePrev = WindowCenter;
            MousePosition = WindowCenter;
            POINT pt{ (int)WindowCenter.x, (int)WindowCenter.y };
            ClientToScreen(Hwnd, &pt);
            SetCursorPos(pt.x, pt.y);

            //SPDLOG_INFO("Delta: {}, {}", MouseDelta.x, MouseDelta.y);
            //SPDLOG_INFO("Raw: {}, {}", RawX, RawY);
            MouseDelta.x = (float)RawX;
            MouseDelta.y = (float)RawY;
            RawX = RawY = 0;
        }
        else {
            //MousePosition = Vector2{ (float)mouseState.x, (float)mouseState.y };
            MouseDelta = MousePrev - MousePosition;
            MousePrev = MousePosition;
        }

        AltDown = _keyboard.current[Keys::LeftAlt] || _keyboard.current[Keys::RightAlt];
        ShiftDown = _keyboard.current[Keys::LeftShift] || _keyboard.current[Keys::RightShift];
        ControlDown = _keyboard.current[Keys::LeftControl] || _keyboard.current[Keys::RightControl];

        if (RightDragState == SelectionState::None)
            LeftDragState = UpdateDragState(MouseButtons::LeftClick, LeftDragState);

        if (LeftDragState == SelectionState::None)
            RightDragState = UpdateDragState(MouseButtons::RightClick, RightDragState);

        DragState = SelectionState((int)LeftDragState | (int)RightDragState);
    }

    void InitRawMouseInput(HWND hwnd);

    void Initialize(HWND hwnd) {
        Hwnd = hwnd;

        // Register the mouse for raw input
        InitRawMouseInput(hwnd);
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x1 /* HID_USAGE_PAGE_GENERIC */;
        rid.usUsage = 0x2 /* HID_USAGE_GENERIC_MOUSE */;
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = hwnd;
        if (!RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE)))
            throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "RegisterRawInputDevices");

        if (Inferno::Settings::Inferno.EnableGamepads) {
            SDL_SetGamepadEventsEnabled(true);

            int gamepadCount = 0;
            auto gamepadIds = SDL_GetGamepads(&gamepadCount);

            if (gamepadCount > 0) {
                SPDLOG_INFO("Connected gamepads:");

                for (size_t i = 0; i < gamepadCount; i++) {
                    Gamepads.AddGamepad(gamepadIds[i]);
                }
            }

            SDL_free(gamepadIds);
        }
    }

    void Shutdown() {}

    bool IsKeyDown(Keys key) {
        return _keyboard.pressed[key] || _keyboard.previous[key];
    }

    bool IsKeyPressed(Keys key, bool onRepeat) {
        return onRepeat ? _keyboard.repeat[key] : _keyboard.pressed[key];
    }

    bool IsKeyReleased(Keys key) {
        return _keyboard.released[key];
    }

    std::bitset<256> GetPressedKeys() { return _keyboard.pressed; }
    std::bitset<256> GetRepeatedKeys() { return _keyboard.repeat; }

    bool IsControllerButtonDown(SDL_GamepadButton button) {
        if (button >= _controller.Size()) return false;
        return _controller.pressed[button];
    }

    bool IsMouseButtonDown(MouseButtons button) {
        if (button == MouseButtons::None || (int)button > _mouseButtons.Size()) return false;
        return _mouseButtons.pressed[(uint64)button] || _mouseButtons.previous[(uint64)button];
    }

    bool IsMouseButtonPressed(MouseButtons button) {
        if (button == MouseButtons::None || (int)button > _mouseButtons.Size()) return false;
        return _mouseButtons.pressed[(uint64)button];
    }

    bool IsMouseButtonReleased(MouseButtons button) {
        if (button == MouseButtons::None || (int)button > _mouseButtons.Size()) return false;
        return _mouseButtons.released[(uint64)button];
    }

    MenuAction GetMenuAction() {
        if (IsKeyPressed(Keys::Enter, true) || IsKeyPressed(Keys::Space) ||
            IsControllerButtonDown(SDL_GAMEPAD_BUTTON_SOUTH))
            return MenuAction::Confirm;
        else if (IsKeyPressed(Keys::Escape, true) || IsControllerButtonDown(SDL_GAMEPAD_BUTTON_EAST))
            return MenuAction::Cancel;
        else if (IsKeyPressed(Keys::Left, true) || IsControllerButtonDown(SDL_GAMEPAD_BUTTON_DPAD_LEFT))
            return MenuAction::Left;
        else if (IsKeyPressed(Keys::Down, true) || IsControllerButtonDown(SDL_GAMEPAD_BUTTON_DPAD_DOWN))
            return MenuAction::Down;
        else if (IsKeyPressed(Keys::Up, true) || IsControllerButtonDown(SDL_GAMEPAD_BUTTON_DPAD_UP))
            return MenuAction::Up;
        else if (IsKeyPressed(Keys::Right, true) || IsControllerButtonDown(SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
            return MenuAction::Right;

        return MenuAction::None;
    }

    bool MouseMoved() {
        return MouseRecentlyMoved;
    }

    MouseMode GetMouseMode() { return ActualMouseMode; }

    void SetMouseMode(MouseMode enable) {
        RequestedMouseMode = enable;
    }

    void ResetState() {
        _keyboard.Reset();
        _mouseButtons.Reset();
        _inputEventQueue.clear();
    }

    ScopedHandle RelativeModeEvent, RelativeReadEvent;

    void InitRawMouseInput(HWND hwnd) {
        RelativeModeEvent.reset(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
        RelativeReadEvent.reset(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_MODIFY_STATE | SYNCHRONIZE));

        if (!RelativeModeEvent || !RelativeReadEvent)
            throw std::system_error(std::error_code((int)GetLastError(), std::system_category()), "CreateEventEx");

        SetEvent(RelativeModeEvent.get());
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_HOVER;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 1;

        if (!TrackMouseEvent(&tme))
            throw std::system_error(std::error_code((int)GetLastError(), std::system_category()), "TrackMouseEvent");
    }

    void ProcessMouseInput(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_INPUT:
            {
                HANDLE events[] = { RelativeModeEvent.get() };
                switch (WaitForMultipleObjectsEx((DWORD)std::size(events), events, false, 0, false)) {
                    default:
                    case WAIT_TIMEOUT:
                        break;

                    case WAIT_OBJECT_0:
                        ResetEvent(RelativeReadEvent.get());
                        RawX = RawY = 0;
                        break;

                    case WAIT_FAILED:
                        throw std::system_error(std::error_code((int)GetLastError(), std::system_category()), "WaitForMultipleObjectsEx");
                }

                RAWINPUT raw{};
                UINT rawSize = sizeof raw;

                UINT resultData = GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
                if (resultData == UINT(-1))
                    throw std::runtime_error("GetRawInputData");

                if (raw.header.dwType == RIM_TYPEMOUSE) {
                    RawX += raw.data.mouse.lLastX;
                    RawY += raw.data.mouse.lLastY;

                    ResetEvent(RelativeReadEvent.get());
                }
                return;
            }

            case WM_LBUTTONDOWN:
                Input::QueueEvent(EventType::MouseBtnPress, (WPARAM)Input::MouseButtons::LeftClick);
                break;

            case WM_LBUTTONUP:
                Input::QueueEvent(EventType::MouseBtnRelease, (WPARAM)Input::MouseButtons::LeftClick);
                break;

            case WM_RBUTTONDOWN:
                Input::QueueEvent(EventType::MouseBtnPress, (WPARAM)Input::MouseButtons::RightClick);
                break;

            case WM_RBUTTONUP:
                Input::QueueEvent(EventType::MouseBtnRelease, (WPARAM)Input::MouseButtons::RightClick);
                break;

            case WM_MBUTTONDOWN:
                Input::QueueEvent(EventType::MouseBtnPress, (WPARAM)Input::MouseButtons::MiddleClick);
                break;

            case WM_MBUTTONUP:
                Input::QueueEvent(EventType::MouseBtnRelease, (WPARAM)Input::MouseButtons::MiddleClick);
                break;

            case WM_XBUTTONDOWN:
                Input::QueueEvent(EventType::MouseBtnPress, (WPARAM)Input::MouseButtons::X1 + GET_XBUTTON_WPARAM(wParam) - XBUTTON1);
                break;

            case WM_XBUTTONUP:
                Input::QueueEvent(EventType::MouseBtnRelease, (WPARAM)Input::MouseButtons::X1 + GET_XBUTTON_WPARAM(wParam) - XBUTTON1);
                break;

            case WM_MOUSEWHEEL:
                Input::QueueEvent(EventType::MouseWheel, 0, GET_WHEEL_DELTA_WPARAM(wParam));
                return;

            case WM_MOUSEHOVER:
            case WM_MOUSEMOVE:
                break;

            default:
                return; // not a mouse event, return
        }

        // All mouse messages provide a new pointer position
        MousePosition.x = static_cast<short>(LOWORD(lParam)); // GET_X_LPARAM(lParam);
        MousePosition.y = static_cast<short>(HIWORD(lParam)); // GET_Y_LPARAM(lParam);

        if (MousePosition != PrevMousePosition)
            Input::QueueEvent(EventType::MouseMoved);

        PrevMousePosition = MousePosition;
    }

    void ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        ProcessMouseInput(message, wParam, lParam);

        switch (message) {
            case WM_SYSKEYDOWN:
                Input::QueueEvent(EventType::KeyPress, wParam, lParam);
                break;

            case WM_KEYDOWN:
            {
                // Ignore key repeat messages. Otherwise IsKeyPressed checks will repeat.
                WORD keyFlags = HIWORD(lParam);
                auto wasKeyDown = (keyFlags & KF_REPEAT) == KF_REPEAT;
                if (!wasKeyDown) {
                    Input::QueueEvent(EventType::KeyPress, wParam, lParam);
                }
                else {
                    Input::QueueEvent(EventType::KeyRepeat, wParam, lParam);
                }

                break;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP:
                Input::QueueEvent(EventType::KeyRelease, wParam, lParam);
                break;

            case WM_ACTIVATE:
                Input::QueueEvent(EventType::Reset);
                break;

            case WM_ACTIVATEAPP:
                Input::QueueEvent(EventType::Reset);
                break;
        }
    }


    string KeyToString(Keys key) {
        switch (key) {
            case Keys::Back: return "Backspace";
            case Keys::Tab: return "Tab";
            case Keys::Enter: return "Enter";
            case Keys::Escape: return "Esc";
            case Keys::Space: return "Space";
            case Keys::PageUp: return "PgUp";
            case Keys::PageDown: return "PgDn";
            case Keys::End: return "End";
            case Keys::Home: return "Home";
            case Keys::Left: return "Left arrow";
            case Keys::Up: return "Up arrow";
            case Keys::Right: return "Right arrow";
            case Keys::Down: return "Down arrow";
            case Keys::Insert: return "Ins";
            case Keys::Delete: return "Del";
            case Keys::LeftShift: return "L Shift";
            case Keys::RightShift: return "R Shift";
            case Keys::LeftControl: return "L Ctrl";
            case Keys::RightControl: return "R Ctrl";
            case Keys::LeftAlt: return "L Alt";
            case Keys::RightAlt: return "R Alt";

            // OEM keys
            case Keys::OemOpenBrackets: return "[";
            case Keys::OemCloseBrackets: return "]";
            case Keys::OemPlus: return "+";
            case Keys::OemMinus: return "-";
            case Keys::OemPipe: return "\\";
            case Keys::OemComma: return ",";
            case Keys::OemPeriod: return ".";
            case Keys::OemTilde: return "~";
            case Keys::OemQuestion: return "/";
            case Keys::OemSemicolon: return ";";
            case Keys::OemQuotes: return "'";

            // Numpad
            case Keys::Multiply: return "*";
            case Keys::Divide: return "/";
            case Keys::Subtract: return "-";
            case Keys::Add: return "+";
            case Keys::Decimal: return ".";
            case Keys::NumPad0: return "Pad0";
            case Keys::NumPad1: return "Pad1";
            case Keys::NumPad2: return "Pad2";
            case Keys::NumPad3: return "Pad3";
            case Keys::NumPad4: return "Pad4";
            case Keys::NumPad5: return "Pad5";
            case Keys::NumPad6: return "Pad6";
            case Keys::NumPad7: return "Pad7";
            case Keys::NumPad8: return "Pad8";
            case Keys::NumPad9: return "Pad9";

            case Keys::A: return "A";
            case Keys::B: return "B";
            case Keys::C: return "C";
            case Keys::D: return "D";
            case Keys::E: return "E";
            case Keys::F: return "F";
            case Keys::G: return "G";
            case Keys::H: return "H";
            case Keys::I: return "I";
            case Keys::J: return "J";
            case Keys::K: return "K";
            case Keys::L: return "L";
            case Keys::M: return "M";
            case Keys::N: return "N";
            case Keys::O: return "O";
            case Keys::P: return "P";
            case Keys::Q: return "Q";
            case Keys::R: return "R";
            case Keys::S: return "S";
            case Keys::T: return "T";
            case Keys::U: return "U";
            case Keys::V: return "V";
            case Keys::W: return "W";
            case Keys::X: return "X";
            case Keys::Y: return "Y";
            case Keys::Z: return "Z";

            case Keys::F1: return "F1";
            case Keys::F2: return "F2";
            case Keys::F3: return "F3";
            case Keys::F4: return "F4";
            case Keys::F5: return "F5";
            case Keys::F6: return "F6";
            case Keys::F7: return "F7";
            case Keys::F8: return "F8";
            case Keys::F9: return "F9";
            case Keys::F10: return "F10";
            case Keys::F11: return "F11";
            case Keys::F12: return "F12";

            case Keys::D1: return "1";
            case Keys::D2: return "2";
            case Keys::D3: return "3";
            case Keys::D4: return "4";
            case Keys::D5: return "5";
            case Keys::D6: return "6";
            case Keys::D7: return "7";
            case Keys::D8: return "8";
            case Keys::D9: return "9";

            default:
                return "";
        }
    }
}
