#include "pch.h"
#include "Input.h"
#include "imgui_local.h"

using namespace DirectX::SimpleMath;

namespace Inferno::Input {
    namespace {
        DirectX::Keyboard _keyboard;
        DirectX::Mouse _mouse;
    }

    Vector2 MousePrev, DragEnd;
    constexpr float DRAG_WINDOW = 3.0f;
    Vector2 MouselookStartPosition, WindowCenter;
    HWND Hwnd;
    int RawX, RawY;

    MouseMode ActualMouseMode{}, RequestedMouseMode{};
    int WheelPrev = 0;

    SelectionState UpdateDragState(DirectX::Mouse::ButtonStateTracker::ButtonState buttonState, SelectionState dragState) {
        switch (buttonState) {
            case MouseState::PRESSED:
                // Don't allow a drag to start when the cursor is over imgui.
                if (ImGui::GetCurrentContext()->HoveredWindow != nullptr) return SelectionState::None;

                DragStart = Input::MousePosition;
                return SelectionState::Preselect;

            case MouseState::RELEASED:
                DragEnd = Input::MousePosition;

                if (dragState == SelectionState::Dragging)
                    return SelectionState::ReleasedDrag;
                else if (dragState != SelectionState::None)
                    return SelectionState::Released;
                break;

            case MouseState::HELD:
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

            case MouseState::UP:
                return SelectionState::None;
        }

        return dragState;
    }

    void Update() {
        if (RequestedMouseMode != ActualMouseMode) {
            ActualMouseMode = RequestedMouseMode;
            RawX = RawY = 0;

            if (ActualMouseMode != MouseMode::Normal) {
                RECT r{}, frame{};
                GetClientRect(Hwnd, &r);
                POINT center = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
                WindowCenter = Vector2{ (float)center.x, (float)center.y };
            }

            ShowCursor(ActualMouseMode == MouseMode::Normal);
        }

        auto keyboardState = _keyboard.GetState();
        auto mouseState = _mouse.GetState();
        Keyboard.Update(keyboardState);
        Mouse.Update(mouseState);

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
            MousePosition = Vector2{ (float)mouseState.x, (float)mouseState.y };
            MouseDelta = MousePrev - MousePosition;
            MousePrev = MousePosition;
        }

        WheelDelta = WheelPrev - mouseState.scrollWheelValue;
        WheelPrev = mouseState.scrollWheelValue;

        AltDown = keyboardState.LeftAlt || keyboardState.RightAlt;
        ShiftDown = keyboardState.LeftShift || keyboardState.RightShift;
        ControlDown = keyboardState.LeftControl || keyboardState.RightControl;

        if (RightDragState == SelectionState::None)
            LeftDragState = UpdateDragState(Mouse.leftButton, LeftDragState);

        if (LeftDragState == SelectionState::None)
            RightDragState = UpdateDragState(Mouse.rightButton, RightDragState);

        DragState = SelectionState((int)LeftDragState | (int)RightDragState);
    }

    void InitRawMouseInput(HWND hwnd);

    void Initialize(HWND hwnd) {
        Hwnd = hwnd;
        _mouse.SetWindow(hwnd);
        InitRawMouseInput(hwnd);
    }

    bool IsKeyDown(DirectX::Keyboard::Keys key) {
        return Keyboard.lastState.IsKeyDown(key);
    }

    bool IsKeyPressed(DirectX::Keyboard::Keys key) {
        return Keyboard.IsKeyPressed(key);
    }

    MouseMode GetMouseMode() { return ActualMouseMode; }

    void SetMouseMode(MouseMode enable) {
        RequestedMouseMode = enable;
    }

    void ResetState() {
        _keyboard.Reset();
        Keyboard.Reset();
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

    void ProcessRawMouseInput(UINT message, WPARAM, LPARAM lParam) {
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

        if (message == WM_INPUT) {
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
        }
    }
}
