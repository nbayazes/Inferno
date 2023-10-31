#include "pch.h"
#include "Input.h"
#include "imgui_local.h"
#include "Game.h"
#include "Shell.h"
#include "PlatformHelpers.h"
#include <bitset>
#include <vector>

using namespace DirectX::SimpleMath;

namespace Inferno::Input {
    namespace {
        Vector2 MousePrev, DragEnd;
        constexpr float DRAG_WINDOW = 3.0f;
        Vector2 WindowCenter;
        HWND Hwnd;
        int RawX, RawY;

        MouseMode ActualMouseMode{}, RequestedMouseMode{};
        int WheelDelta;

        template<size_t N> struct ButtonState {
            std::bitset<N> pressed, released;
            std::bitset<N> current, previous;

            void Reset() {
                pressed.reset();
                released.reset();
                current.reset();
                previous.reset();
            }

            // Call this before handling a frame's input events
            void NextFrame() {
                pressed.reset();
                released.reset();
                previous = current;
            }

            // These functions assume that events will arrive in the correct order
            void Press(uint8_t key) {
                if (key >= N)
                    return;
                pressed[key] = true;
                current[key] = true;
            }

            void Release(uint8_t key) {
                if (key >= N)
                    return;
                released[key] = true;
                current[key] = false;
            }
        };

        ButtonState<256> _keyboard;
        ButtonState<5> _mouseButtons;

        struct InputEvent {
            EventType type;
            uint8_t keyCode;
            int64_t flags;
        };

        std::vector<InputEvent> _inputEventQueue;

        void HandleInputEvents() {
            _keyboard.NextFrame();
            _mouseButtons.NextFrame();
            WheelDelta = 0;

            for (auto &event : _inputEventQueue) {
                switch (event.type) {
                    case EventType::KeyPress:
                    case EventType::KeyRelease:
                        if (event.keyCode == VK_SHIFT || event.keyCode == VK_CONTROL || event.keyCode == VK_MENU)
                        {
                            // The keystroke flags carry extra information for Shift, Alt & Ctrl to
                            // disambiguate between the left and right variants of each key
                            bool isExtendedKey = (HIWORD(event.flags) & KF_EXTENDED) == KF_EXTENDED;
                            int scanCode = LOBYTE(HIWORD(event.flags)) | (isExtendedKey ? 0xe000 : 0);
                            event.keyCode = static_cast<uint8_t>(LOWORD(MapVirtualKeyW(static_cast<UINT>(scanCode), MAPVK_VSC_TO_VK_EX)));
                        }

                        if (event.type == EventType::KeyPress)
                            _keyboard.Press(event.keyCode);
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
                        break;

                    case EventType::Reset:
                        _keyboard.Reset();
                        _mouseButtons.Reset();
                        break;
                }
            }

            _inputEventQueue.clear();
        }
    }

    int GetWheelDelta() { return WheelDelta; }

    SelectionState UpdateDragState(MouseButtons button, SelectionState dragState) {
        if (_mouseButtons.pressed[button]) {
            // Don't allow a drag to start when the cursor is over imgui.
            if (ImGui::GetCurrentContext()->HoveredWindow != nullptr) return SelectionState::None;

            DragStart = Input::MousePosition;
            return SelectionState::Preselect;
        }
        else if (_mouseButtons.released[button]) {
            DragEnd = Input::MousePosition;

            if (dragState == SelectionState::Dragging)
                return SelectionState::ReleasedDrag;
            else if (dragState != SelectionState::None)
                return SelectionState::Released;
            return dragState;
        }
        else if (_mouseButtons.previous[button]) {
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
            LeftDragState = UpdateDragState(MouseButtons::Left, LeftDragState);

        if (LeftDragState == SelectionState::None)
            RightDragState = UpdateDragState(MouseButtons::Right, RightDragState);

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

    bool IsKeyPressed(Keys key) {
        return _keyboard.pressed[key];
    }

    bool IsKeyReleased(Keys key) {
        return _keyboard.released[key];
    }

    bool IsMouseButtonDown(MouseButtons button) {
        return _mouseButtons.pressed[button] || _mouseButtons.previous[button];
    }

    bool IsMouseButtonPressed(MouseButtons button) {
        return _mouseButtons.pressed[button];
    }

    bool IsMouseButtonReleased(MouseButtons button) {
        return _mouseButtons.released[button];
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
        switch(message) {
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
                Input::QueueEvent(Input::EventType::MouseBtnPress, Input::MouseButtons::Left);
                break;

            case WM_LBUTTONUP:
                Input::QueueEvent(Input::EventType::MouseBtnRelease, Input::MouseButtons::Left);
                break;

            case WM_RBUTTONDOWN:
                Input::QueueEvent(Input::EventType::MouseBtnPress, Input::MouseButtons::Right);
                break;

            case WM_RBUTTONUP:
                Input::QueueEvent(Input::EventType::MouseBtnRelease, Input::MouseButtons::Right);
                break;

            case WM_MBUTTONDOWN:
                Input::QueueEvent(Input::EventType::MouseBtnPress, Input::MouseButtons::Middle);
                break;

            case WM_MBUTTONUP:
                Input::QueueEvent(Input::EventType::MouseBtnRelease, Input::MouseButtons::Middle);
                break;

            case WM_XBUTTONDOWN:
                Input::QueueEvent(Input::EventType::MouseBtnPress, Input::MouseButtons::X1 + GET_XBUTTON_WPARAM(wParam) - XBUTTON1);
                break;

            case WM_XBUTTONUP:
                Input::QueueEvent(Input::EventType::MouseBtnRelease, Input::MouseButtons::X1 + GET_XBUTTON_WPARAM(wParam) - XBUTTON1);
                break;

            case WM_MOUSEWHEEL:
                Input::QueueEvent(Input::EventType::MouseWheel, 0, GET_WHEEL_DELTA_WPARAM(wParam));
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
    }

    void ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        ProcessMouseInput(message, wParam, lParam);

        switch (message) {
            case WM_SYSKEYDOWN:
                Input::QueueEvent(Input::EventType::KeyPress, wParam, lParam);
                break;

            case WM_KEYDOWN:
                Input::QueueEvent(Input::EventType::KeyPress, wParam, lParam);
                break;

            case WM_KEYUP:
            case WM_SYSKEYUP:
                Input::QueueEvent(Input::EventType::KeyRelease, wParam, lParam);
                break;
            
            case WM_ACTIVATE:
                Input::QueueEvent(Input::EventType::Reset);
                break;

            case WM_ACTIVATEAPP:
                Input::QueueEvent(Input::EventType::Reset);
                break;
        }
    }
}
