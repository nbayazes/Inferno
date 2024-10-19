#include "pch.h"
#include "Input.h"
#include <bitset>
#include <vector>
#include "Game.h"
#include "imgui_local.h"
#include "PlatformHelpers.h"
#include "Shell.h"

using namespace DirectX::SimpleMath;

namespace Inferno::Input {
    namespace {
        Vector2 MousePrev, DragEnd;
        constexpr float DRAG_WINDOW = 3.0f;
        Vector2 WindowCenter;
        HWND Hwnd;
        int RawX, RawY;
        bool MouseRecentlyMoved = false;

        MouseMode ActualMouseMode{}, RequestedMouseMode{};
        int WheelDelta;

        template <size_t N>
        struct ButtonState {
            std::bitset<N> pressed, released, repeat;
            std::bitset<N> current, previous;

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
                pressed.reset();
                repeat.reset();
                released.reset();
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
            }
        };

        ButtonState<256> _keyboard;
        ButtonState<8> _mouseButtons;

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

    void Update() {
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
    }

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

    bool IsMouseButtonDown(MouseButtons button) {
        if (button == MouseButtons::None) return false;
        return _mouseButtons.pressed[(uint64)button] || _mouseButtons.previous[(uint64)button];
    }

    bool IsMouseButtonPressed(MouseButtons button) {
        if (button == MouseButtons::None) return false;
        return _mouseButtons.pressed[(uint64)button];
    }

    bool IsMouseButtonReleased(MouseButtons button) {
        return _mouseButtons.released[(uint64)button];
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
        Input::QueueEvent(EventType::MouseMoved);
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
