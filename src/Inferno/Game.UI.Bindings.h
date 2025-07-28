#pragma once

#include "Game.UI.Controls.h"
#include "Game.Bindings.h"

namespace Inferno::UI {
    class BindingControl : public ControlBase {
        string _label;
        bool _held = false;
        bool _dragging = false;
        bool _hovered = false;
        bool _hovered2 = false;
        bool _hovered3 = false;
        float _textHeight = 24;
        bool _waitingForInput = false;
        GameAction _action;
        string _shortcut;
        string _shortcut2;
        Vector2 _mouseDelta;
        BindType _bindType; // The action is an axis. Tracks delta changes to the mouse or controllers to assign an axis
        bool _showSecondBind = true;
        bool _showInvert = false;
        uint _slot = 0; // Slot when binding started
        gsl::strict_not_null<int*> _column;
        gsl::strict_not_null<InputDeviceBinding*> _bindings;

    public:
        float LabelWidth = 200;
        float ValueWidth = 150;
        float InvertWidth = 150;
        float Spacing = 2; // Horizontal spacing between boxes

        string MenuActionSound = MENU_SELECT_SOUND; // Sound when picking an item in the popup menu

        std::function<void()> OnChange; // Called when a binding changes

        BindingControl(GameAction action, InputDeviceBinding& device, int& column)
            : _action(action), _column(&column), _bindings(&device) {
            Padding = Vector2(0, Spacing);
            _label = GetActionLabel(action);
            auto labelSize = MeasureString(_label, FontSize::Small);
            //LabelWidth = labelSize.x;
            _textHeight = labelSize.y;
            Size = Vector2(60, SMALL_CONTROL_HEIGHT);
            //ValueWidth = size.x - LabelWidth;
            ActionSound = MENU_SELECT_SOUND;
            _bindType = GetActionBindType(action);

            if (_bindType == BindType::Axis) {
                _showInvert = true;
                _showSecondBind = false;
            }

            RefreshBinding();
        }

        void RefreshBinding() {
            _shortcut = _bindings->GetBindingLabel(_action, 0);
            _shortcut2 = _bindings->GetBindingLabel(_action, 1);
        }

        ControlBase* HitTestCursor() override {
            // Ignore due to control storing three internal selection states and being in a list
            // This is not ideal
            return nullptr;
        }

        void HandleBindInput(GameBinding& binding) {
            using Input::Keys;
            using Input::MouseButtons;

            bool cancel = Input::OnKeyPressed(Keys::Escape);

            if (auto device = Input::GetExactDevice(_bindings->path); device && device->IsGamepad())
                cancel |= device->ButtonWasPressed(SDL_GAMEPAD_BUTTON_START);

            if (cancel) {
                //selectedAction = GameAction::None; // Cancel the assignment
                _waitingForInput = false;
                CaptureCursor(false);
                CaptureInput(false);
                Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
                return;
            }

            auto finishBinding = [this, &binding] {
                binding.action = _action;
                _bindings->UnbindOthers(binding, _slot); // Clear existing
                _waitingForInput = false;
                if (OnChange) OnChange();
                CaptureCursor(false);
                CaptureInput(false);
                Sound::Play2D(SoundResource{ ActionSound });
                Input::ResetState();
            };

            uint8 bindId{};
            bool dir{};

            switch (_bindings->type) {
                case Input::InputType::Keyboard:
                    for (Keys key = Keys::Back; key <= Keys::OemClear; key = Keys((unsigned char)key + 1)) {
                        if (Input::OnKeyPressed(key)) {
                            //if (GameBindings::IsReservedKey(key))
                            //    continue;

                            binding.id = key;
                            binding.type = BindType::Button;
                            finishBinding();

                            break;
                        }
                    }
                    break;
                case Input::InputType::Mouse:
                    if (_bindType == BindType::Axis) {
                        _mouseDelta += Input::MouseDelta;
                        if (std::abs(_mouseDelta.x) > 25) {
                            binding.id = (uint8)Input::MouseAxis::MouseX;
                            binding.type = BindType::Axis;
                        }
                        else if (std::abs(_mouseDelta.y) > 25) {
                            binding.id = (uint8)Input::MouseAxis::MouseY;
                            binding.type = BindType::Axis;
                        }
                    }
                    else {
                        for (auto btn = Input::MouseButtons::LeftClick; btn <= Input::MouseButtons::WheelDown; btn = Input::MouseButtons((uint8)btn + 1)) {
                            if (Input::MouseButtonPressed(btn)) {
                                binding.id = (uint8)btn;
                                binding.type = BindType::Button;
                                finishBinding();
                                break;
                            }
                        }
                    }

                    break;
                case Input::InputType::Gamepad:
                    if (auto joystick = Input::GetExactDevice(_bindings->path)) {
                        if (_bindType == BindType::Axis) {
                            if (joystick->CheckAxisPressed(bindId, dir)) {
                                bool halfAxis = bindId == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || bindId == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;

                                // Don't allow binding half axis inputs to a full axis action
                                if (!halfAxis) {
                                    binding.id = bindId;
                                    binding.type = BindType::Axis;
                                    finishBinding();
                                }
                            }
                        }
                        else {
                            if (joystick->CheckButtonDown(bindId)) {
                                binding.id = bindId;
                                binding.type = BindType::Button;
                                finishBinding();
                            }

                            if (joystick->CheckAxisPressed(bindId, dir)) {
                                // this doesn't seem necessary anymore, tested on PS5 and xbox controllers
                                //bool halfAxis = bindId == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || bindId == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                                //if (halfAxis) {
                                //    binding.id = bindId;
                                //    binding.type = dir ? BindType::AxisPlus : BindType::AxisMinus;
                                //}
                                //else {
                                binding.id = bindId;
                                binding.type = dir ? BindType::AxisButtonPlus : BindType::AxisButtonMinus;
                                //}

                                finishBinding();
                            }
                        }
                    }
                    break;
                case Input::InputType::Joystick:
                    if (auto joystick = Input::GetExactDevice(_bindings->path)) {
                        if (_bindType == BindType::Axis) {
                            if (joystick->CheckAxisPressed(bindId, dir)) {
                                binding.id = bindId;
                                binding.type = BindType::Axis;
                                finishBinding();
                            }
                        }
                        else {
                            if (joystick->CheckButtonDown(bindId)) {
                                binding.id = bindId;
                                binding.type = BindType::Button;
                                finishBinding();
                            }
                            if (joystick->CheckHat(bindId)) {
                                binding.id = bindId;
                                binding.type = BindType::Hat;
                                finishBinding();
                            }
                        }
                    }
                    break;
            }
        }

        void OnUpdate() override {
            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            _hovered = Visible && RectangleContains(boxPosition, Vector2(ValueWidth * GetScale(), ScreenSize.y), Input::MousePosition);
            _hovered2 = Visible && RectangleContains(boxPosition + Vector2((ValueWidth + Spacing) * GetScale(), 0), Vector2(ValueWidth * GetScale(), ScreenSize.y), Input::MousePosition);
            _hovered3 = Visible && RectangleContains(GetInvertCheckboxPosition(), GetInvertCheckboxSize(), Input::MousePosition);

            if (Input::MouseButtonPressed(Input::MouseButtons::LeftClick)) {
                if (_hovered) {
                    *_column = 0;
                    SetSelection(this);
                }
                else if (_bindType != BindType::Axis && _hovered2) {
                    *_column = 1;
                    SetSelection(this);
                }
                else if (_bindType == BindType::Axis && _hovered3) {
                    *_column = 2;
                    ToggleInvert();
                    SetSelection(this);
                }
            }

            if (_waitingForInput) {
                if (auto binding = _bindings->GetBinding(_action, _slot))
                    HandleBindInput(*binding);
            }
            else if (Input::MouseButtonPressed(Input::MouseButtons::LeftClick)) {
                if (_bindType == BindType::Axis) {
                    if (_hovered)
                        StartBinding(0);
                }
                else {
                    if (_hovered)
                        StartBinding(0);
                    else if (_hovered2)
                        StartBinding(1);
                }
            }
            else if (Input::ControlDown && Input::OnKeyPressed(Input::Keys::D) && Focused) {
                // Delete binding
                if (auto binding = _bindings->GetBinding(_action, *_column)) {
                    *binding = {}; // clear
                    Sound::Play2D(SoundResource{ ActionSound });
                    if (OnChange) OnChange();
                }
            }
        }

        void StartBinding(uint slot) {
            _slot = slot;
            _waitingForInput = true;
            _mouseDelta = Vector2::Zero;
            CaptureCursor(true);
            CaptureInput(true);
            Sound::Play2D(SoundResource{ ActionSound });
        }

        void ToggleInvert() const {
            if (auto binding = _bindings->GetBinding(_action, 0)) {
                binding->invert = !binding->invert;
                Sound::Play2D(SoundResource{ ActionSound });
            }
        }

        bool OnMenuAction(Input::MenuActionState action) override {
            if (!_waitingForInput) {
                auto& column = *_column;

                if (action == MenuAction::Confirm) {
                    if (_bindType == BindType::Axis && column == 2) {
                        ToggleInvert();
                    }
                    else {
                        StartBinding(column == 1 ? 1 : 0);
                    }

                    return true;
                }

                if (action == MenuAction::Left) {
                    if (_bindType == BindType::Axis) {
                        column = column <= 0 ? 2 : 0;
                    }
                    else {
                        column = column <= 0 ? 1 : 0;
                    }

                    return true;
                }

                if (action == MenuAction::Right) {
                    // Skip columns due to axis inputs hiding the second binding
                    if (_bindType == BindType::Axis) {
                        column = column >= 2 ? 0 : 2;
                    }
                    else {
                        column = column >= 1 ? 0 : 1;
                    }
                    return true;
                }
            }

            return false;
        }

        void OnSelect() override {
            auto& column = *_column;

            if (_bindType == BindType::Axis) {
                if (column == 1) column = 0;
            }
            else {
                if (column == 2) column = 1;
            }
        }

        Vector2 GetInvertCheckboxPosition() const {
            return { ScreenPosition.x + (LabelWidth + ValueWidth * 2 + Spacing * 2 + 25) * GetScale(), ScreenPosition.y };
        }

        Vector2 GetInvertCheckboxSize() const {
            float size = ScreenSize.y - Padding.y * GetScale();
            return { size, size };
        }

        void OnDraw() override {
            constexpr Color textColor(0.8f, 0.8f, 0.8f);

            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            auto column = *_column;

            {
                // Label Background
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                //cbi.Size = Vector2(ValueWidth * GetScale(), ScreenSize.y) - border * 2;
                cbi.Size.x = LabelWidth * GetScale() - 2 * GetScale();
                cbi.Size.y = ScreenSize.y - Padding.y * GetScale();
                cbi.Texture = Render::Materials->White().Handle();
                //cbi.Color = borderColor * 0.3f;
                //cbi.Color = Focused ? FOCUSED_BUTTON : IDLE_BUTTON;
                cbi.Color = IDLE_BUTTON;
                cbi.Color *= 0.3f;
                cbi.Color.A(1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                //dti.Color = Focused /*|| Hovered*/ ? ACCENT_COLOR : textColor;
                dti.Color = textColor;
                dti.Position = ScreenPosition;
                dti.Position.y += Padding.y * GetScale();
                dti.Position.x += 2 * GetScale();
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            {
                // Value Background
                auto color = _waitingForInput && column == 0 ? ACCENT_GLOW : _hovered ? ACCENT_COLOR : Focused && column == 0 ? FOCUSED_BUTTON : IDLE_BUTTON;

                Render::CanvasBitmapInfo cbi;
                cbi.Position = boxPosition;
                //cbi.Size = Vector2(ValueWidth * GetScale(), ScreenSize.y) - border * 2;
                cbi.Size.x = ValueWidth * GetScale();
                cbi.Size.y = ScreenSize.y - Padding.y * GetScale();
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = color * 0.4f;
                cbi.Color.A(1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Value
                auto valueLabel = _waitingForInput && column == 0 ? _bindType == BindType::Axis ? "move axis" : "press button" : _shortcut;
                auto valueSize = MeasureString(valueLabel, FontSize::Small).x;

                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                //dti.Color = _waitingForInput ? ACCENT_GLOW : Focused || _hovered ? ACCENT_COLOR : textColor;
                dti.Color = _waitingForInput && column == 0 ? FOCUS_COLOR : (Focused || _hovered) && column == 0 ? ACCENT_COLOR : textColor;
                //dti.Color = ACCENT_COLOR;
                //dti.Position = Vector2(ScreenPosition.x + LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f, ScreenPosition.y);
                dti.Position.x = ScreenPosition.x + (LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f) * GetScale();
                dti.Position.y = ScreenPosition.y + Padding.y * GetScale();
                Render::UICanvas->DrawRaw(valueLabel, dti, Layer + 1);
            }

            if (_showSecondBind) {
                {
                    // Value Background 2
                    auto color = _waitingForInput && column == 1 ? ACCENT_GLOW : _hovered2 ? ACCENT_COLOR : Focused && column == 1 ? FOCUSED_BUTTON : IDLE_BUTTON;

                    Render::CanvasBitmapInfo cbi;
                    cbi.Position = boxPosition;
                    cbi.Position.x += (ValueWidth + Spacing) * GetScale();
                    cbi.Size.x = ValueWidth * GetScale();
                    cbi.Size.y = ScreenSize.y - Padding.y * GetScale();
                    cbi.Texture = Render::Materials->White().Handle();
                    cbi.Color = color * 0.4f;
                    cbi.Color.A(1);
                    Render::UICanvas->DrawBitmap(cbi, Layer);
                }

                {
                    // Value 2
                    auto valueLabel = _waitingForInput && column == 1 ? _bindType == BindType::Axis ? "move axis" : "press a key" : _shortcut2;
                    auto valueSize = MeasureString(valueLabel, FontSize::Small).x;

                    Render::DrawTextInfo dti;
                    dti.Font = FontSize::Small;
                    dti.Color = _waitingForInput && column == 1 ? FOCUS_COLOR : (Focused || _hovered2) && column == 1 ? ACCENT_COLOR : textColor;
                    dti.Position.x = ScreenPosition.x + (ValueWidth + Spacing + LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f) * GetScale();
                    dti.Position.y = ScreenPosition.y + Padding.y * GetScale();
                    Render::UICanvas->DrawRaw(valueLabel, dti, Layer + 1);
                }
            }

            if (_showInvert) {
                if (auto binding = _bindings->GetBinding(_action, 0)) {
                    // Invert checkbox
                    auto color = _hovered3 ? ACCENT_COLOR : Focused && column == 2 ? FOCUSED_BUTTON : IDLE_BUTTON;

                    Render::CanvasBitmapInfo cbi;
                    cbi.Position = GetInvertCheckboxPosition();
                    cbi.Size = GetInvertCheckboxSize();
                    cbi.Texture = Render::Materials->White().Handle();
                    cbi.Color = binding->invert ? ACCENT_GLOW : color * 0.4f;
                    cbi.Color.A(1);
                    Render::UICanvas->DrawBitmap(cbi, Layer);
                }
            }
        }
    };

    inline std::array KeyboardInputs = {
        GameAction::Forward,
        GameAction::Reverse,
        GameAction::SlideLeft,
        GameAction::SlideRight,
        GameAction::SlideUp,
        GameAction::SlideDown,
        GameAction::Afterburner,

        GameAction::PitchUp,
        GameAction::PitchDown,
        GameAction::YawLeft,
        GameAction::YawRight,
        GameAction::RollLeft,
        GameAction::RollRight,

        GameAction::FirePrimary,
        GameAction::FireSecondary,
        GameAction::FireFlare,
        GameAction::DropBomb,

        GameAction::Automap,
        GameAction::RearView,

        GameAction::Headlight,
        GameAction::EnergyConverter,

        GameAction::CyclePrimary,
        GameAction::CycleSecondary,
        GameAction::CycleBomb,

        GameAction::Weapon1,
        GameAction::Weapon2,
        GameAction::Weapon3,
        GameAction::Weapon4,
        GameAction::Weapon5,
        GameAction::Weapon6,
        GameAction::Weapon7,
        GameAction::Weapon8,
        GameAction::Weapon9,
        GameAction::Weapon10,
    };

    inline std::array MouseInputs = {
        GameAction::PitchAxis,
        GameAction::YawAxis,
        GameAction::RollAxis,

        GameAction::FirePrimary,
        GameAction::FireSecondary,
        GameAction::FireFlare,
        GameAction::DropBomb,

        GameAction::CyclePrimary,
        GameAction::CycleSecondary,
        GameAction::CycleBomb,

        GameAction::Headlight,
        GameAction::EnergyConverter,
        GameAction::Automap,
        GameAction::RearView,
        GameAction::Afterburner,

        GameAction::Forward,
        GameAction::Reverse,
        GameAction::SlideLeft,
        GameAction::SlideRight,
        GameAction::SlideUp,
        GameAction::SlideDown,

        GameAction::PitchUp,
        GameAction::PitchDown,
        GameAction::YawLeft,
        GameAction::YawRight,
        GameAction::RollLeft,
        GameAction::RollRight,

        GameAction::Weapon1,
        GameAction::Weapon2,
        GameAction::Weapon3,
        GameAction::Weapon4,
        GameAction::Weapon5,
        GameAction::Weapon6,
        GameAction::Weapon7,
        GameAction::Weapon8,
        GameAction::Weapon9,
        GameAction::Weapon10,
    };

    inline std::array GamepadInputs = {
        GameAction::Forward,
        GameAction::Reverse,
        GameAction::SlideLeft,
        GameAction::SlideRight,
        GameAction::SlideUp,
        GameAction::SlideDown,
        GameAction::YawLeft,
        GameAction::YawRight,
        GameAction::PitchUp,
        GameAction::PitchDown,
        GameAction::RollLeft,
        GameAction::RollRight,
        GameAction::Afterburner,

        GameAction::LeftRightAxis,
        GameAction::UpDownAxis,
        GameAction::ForwardReverseAxis,
        GameAction::PitchAxis,
        GameAction::YawAxis,
        GameAction::RollAxis,

        GameAction::FirePrimary,
        GameAction::FireSecondary,
        GameAction::FireFlare,
        GameAction::DropBomb,

        GameAction::Automap,
        GameAction::RearView,

        GameAction::Headlight,
        GameAction::EnergyConverter,

        GameAction::CyclePrimary,
        GameAction::CycleSecondary,
        GameAction::CycleBomb,

        GameAction::Weapon1,
        GameAction::Weapon2,
        GameAction::Weapon3,
        GameAction::Weapon4,
        GameAction::Weapon5,
        GameAction::Weapon6,
        GameAction::Weapon7,
        GameAction::Weapon8,
        GameAction::Weapon9,
        GameAction::Weapon10,
    };

    class BindingDialog : public DialogBase {
        List<BindingControl*> _bindingControls;
        List<Input::InputDevice> _devices;
        ListBox2* _bindingList = nullptr;
        ComboSelect* _deviceList = nullptr;
        gsl::strict_not_null<int*> _index; // the selected control. 0 is keyboard, 1 is mouse, 1 > is controllers and joysticks
        int _column = 0; // 0 to 2. Binding 1, Binding 2, Invert
        Label* _footer = nullptr;

    public:
        void UpdateBindingList(span<GameAction> actions, InputDeviceBinding& device) {
            if (!_bindingList) return;

            _bindingList->Children.clear();
            _bindingControls.clear();
            _column = 0;

            //if (device.type == Input::InputType::Gamepad) {
            //    _footer->SetText("start cancels, back clears binding, hold back to clear all");
            //}
            //else {
            //    _footer->SetText("esc cancels, ctrl+r resets all, ctrl+d clears binding");
            //}

            for (auto& action : actions) {
                auto child = _bindingList->AddChild<BindingControl>(action, device, _column);
                _bindingControls.push_back(child);

                child->OnChange = [this] {
                    RefreshBindings();
                };
            }
        }

        BindingDialog(int& deviceIndex) : DialogBase("customize bindings"), _index(&deviceIndex) {
            Size = Vector2(620, 460);

            auto deviceNames = GetDeviceNames();
            if (*_index > deviceNames.size() - 1) *_index = 0;

            _deviceList = AddChild<ComboSelect>("Input Device", deviceNames, *_index);
            _deviceList->LabelWidth = 225;
            //inputDropdown->ValueWidth = 350;
            _deviceList->Size = Vector2(Size.x - DIALOG_PADDING * 2, CONTROL_HEIGHT);

            _deviceList->Position = Vector2(DIALOG_PADDING, DIALOG_HEADER_PADDING);
            _deviceList->OnChange = [this](int index) {
                if (index == 0) {
                    UpdateBindingList(KeyboardInputs, Game::Bindings.GetKeyboard());
                }
                else if (index == 1) {
                    UpdateBindingList(MouseInputs, Game::Bindings.GetMouse());
                }
                else if (index > 1) {
                    if (auto device = Seq::tryItem(_devices, index - 2)) {
                        if (auto binds = Game::Bindings.GetExact(*device)) {
                            UpdateBindingList(GamepadInputs, *binds);
                        } else {
                            SPDLOG_WARN("No bindings found for device {}:{}", device->name, device->path);
                        }
                    }
                    else {
                        // Couldn't find the input device, reset to keyboard
                        *_index = 0;
                        UpdateBindingList(KeyboardInputs, Game::Bindings.GetKeyboard());
                    }
                }
            };

            _deviceList->OpenCallback = [this] { _deviceList->SetValues(GetDeviceNames()); };

            // Add headers
            {
                auto y = DIALOG_HEADER_PADDING + CONTROL_HEIGHT + 10;

                auto actionHeader = AddChild<Label>("Action", FontSize::Small);
                actionHeader->Color = BLUE_TEXT;
                actionHeader->Position = Vector2(80, y);

                auto bindHeader = AddChild<Label>("Bind 1", FontSize::Small);
                bindHeader->Color = BLUE_TEXT;
                bindHeader->Position = Vector2(270, y);

                auto bindHeader2 = AddChild<Label>("Bind 2", FontSize::Small);
                bindHeader2->Color = BLUE_TEXT;
                bindHeader2->Position = Vector2(420, y);

                auto invertHeader = AddChild<Label>("Invert", FontSize::Small);
                invertHeader->Position = Vector2(530, y);
                invertHeader->Color = BLUE_TEXT;

                _footer = AddChild<Label>("esc cancels, ctrl+r resets all, ctrl+d clears binding", FontSize::Small);
                _footer->Color = IDLE_BUTTON;
                _footer->Position = Vector2(DIALOG_PADDING + 5, 429);
            }

            _bindingList = AddChild<ListBox2>(20, Size.x - DIALOG_PADDING * 3);
            _bindingList->Position = Vector2(DIALOG_PADDING, DIALOG_HEADER_PADDING + CONTROL_HEIGHT * 2 + 8);
            _deviceList->OnChange(deviceIndex);
        }

        bool OnMenuAction(Input::MenuActionState action) override {
            if (action == MenuAction::Confirm) {
                Selection->OnConfirm();
            }

            // let regular navigation move out of this control
            return DialogBase::OnMenuAction(action);
        }

        void OnUpdate() override {
            DialogBase::OnUpdate();

            if (Input::ControlDown && Input::OnKeyPressed(Input::Keys::R)) {
                // Reset all bindings
                if (*_index == 0) {
                    auto& keyboard = Game::Bindings.GetKeyboard();
                    ResetKeyboardBindings(keyboard);
                    UpdateBindingList(KeyboardInputs, keyboard);
                }
                else if (*_index == 1) {
                    auto& mouse = Game::Bindings.GetMouse();
                    ResetMouseBindings(mouse);
                    UpdateBindingList(MouseInputs, mouse);
                }
                else if (*_index > 1) {
                    auto& device = _devices.at(*_index - 2);

                    if (auto binds = Game::Bindings.GetExact(device)) {
                        ResetGamepadBindings(*binds);
                        UpdateBindingList(GamepadInputs, *binds);
                    }
                }
                Sound::Play2D(SoundResource{ ActionSound });
            }
        }

    private:
        void RefreshBindings() const {
            for (auto& control : _bindingControls) {
                control->RefreshBinding();
            }
        }

        List<string> GetDeviceNames() {
            List<string> deviceNames = { "Keyboard", "Mouse" };

            _devices = Input::GetDevices(); // Copy the current gamepads

            for (auto& gamepad : _devices) {
                deviceNames.push_back(gamepad.name);
            }

            return deviceNames;
        }
    };
}
