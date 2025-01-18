#pragma once

#include "Game.UI.Controls.h"
#include "Game.Bindings.h"

namespace Inferno::UI {
    struct BindingEntry {
        //GameAction Action{};
        string Label;
        GameBinding Binding;
    };

    enum class BindSource { Any, Keyboard, Mouse, Controller };

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
        string _keyShortcut;
        string _keyShortcut2;
        Vector2 _mouseDelta;
        bool _isAxis = false; // Tracks delta changes to the mouse or controllers to assign an axis
        bool _showSecondBind = true;
        bool _showInvert = false;
        Input::InputType _type;
        gsl::strict_not_null<int*> _column;
        bool _invert = false; // todo: use binding

    public:
        float LabelWidth = 200;
        float ValueWidth = 150;
        float InvertWidth = 150;
        float Spacing = 2; // Horizontal spacing between boxes

        string MenuActionSound = MENU_SELECT_SOUND; // Sound when picking an item in the popup menu
        BindSource Source = BindSource::Any; // What devices to check for binding

        std::function<void()> OnChange; // Called when a binding changes

        BindingControl(GameAction action, Input::InputType type, int& column)
            : _action(action), _type(type), _column(&column) {
            Padding = Vector2(0, Spacing);
            _label = Game::Bindings.GetLabel(action);
            auto labelSize = MeasureString(_label, FontSize::Small);
            //LabelWidth = labelSize.x;
            _textHeight = labelSize.y;
            Size = Vector2(60, SMALL_CONTROL_HEIGHT);
            //ValueWidth = size.x - LabelWidth;
            ActionSound = MENU_SELECT_SOUND;
            if (IsAxisAction(action)) {
                _isAxis = true;
                _showInvert = true;
                _showSecondBind = false;
            }
            RefreshBinding();
        }

        void RefreshBinding() {
            if (_isAxis) {
                _keyShortcut = "axis";
            }

            if (auto binding = Game::Bindings.TryFind(_action, _type)) {
                _keyShortcut = binding->GetShortcutLabel();
            }
            else {
                _keyShortcut = "";
            }
        }

        void OnUpdate() override {
            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            _hovered = Visible && RectangleContains(boxPosition, Vector2(ValueWidth * GetScale(), ScreenSize.y), Input::MousePosition);
            _hovered2 = Visible && RectangleContains(boxPosition + Vector2((ValueWidth + Spacing) * GetScale(), 0), Vector2(ValueWidth * GetScale(), ScreenSize.y), Input::MousePosition);
            _hovered3 = Visible && RectangleContains(boxPosition + Vector2((ValueWidth * 2 + Spacing * 2 + 25) * GetScale(), 0), Vector2(ScreenSize.y, ScreenSize.y), Input::MousePosition);

            if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick)) {
                if (_hovered) {
                    *_column = 0;
                    SetSelection(this);
                }
                else if (_hovered2) {
                    *_column = 1;
                    SetSelection(this);
                }
                else if (_hovered3) {
                    *_column = 2;
                    SetSelection(this);
                }
            }

            auto finishBinding = [this](const GameBinding& binding) {
                if (auto entry = Game::Bindings.TryFind(binding.Action, _type)) {
                    Game::Bindings.UnbindExisting(binding);
                    *entry = binding; // Update existing binding
                }
                else {
                    Game::Bindings.Add(binding); // Add a new one
                }

                //_shortcutLabel = binding.GetShortcutLabel();
                _waitingForInput = false;
                if (OnChange) OnChange();
                CaptureCursor(false);
                CaptureInput(false);
                Sound::Play2D(SoundResource{ ActionSound });
                //Input::ResetState();
                //Game::Bindings.ResetState();
            };

            if (_waitingForInput) {
                using Input::Keys;
                using Input::MouseButtons;

                if (Input::IsKeyPressed(Keys::Escape)) {
                    //selectedAction = GameAction::None; // Cancel the assignment
                    _waitingForInput = false;
                    CaptureCursor(false);
                    CaptureInput(false);
                    Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
                    return;
                }

                // Check keyboard input
                if (_type == Input::InputType::Keyboard) {
                    for (Keys key = Keys::Back; key <= Keys::OemClear; key = Keys((unsigned char)key + 1)) {
                        if (Input::IsKeyPressed(key)) {
                            if (GameBindings::IsReservedKey(key))
                                continue;

                            // assign the binding
                            //if (auto binding = Game::Bindings.TryFind(_action)) {
                            //    binding->Key = key;
                            //    binding->Mouse = MouseButtons::None;
                            //    binding->Action = GameAction::None;
                            //    finishBinding(*binding);
                            //    //Game::Bindings.UnbindExisting(*binding);
                            //    //_shortcutLabel = binding->GetShortcutLabel();
                            //    //_waitingForInput = false;
                            //    break;
                            //}
                            //else {
                            GameBinding newBinding{ .Action = _action, .Key = key };
                            //Game::Bindings.UnbindExisting(newBinding);
                            //Game::Bindings.Add(newBinding);
                            finishBinding(newBinding);

                            //_shortcutLabel = newBinding.GetShortcutLabel();
                            //_waitingForInput = false;
                            break;
                            //}
                        }
                    }
                }

                // Check mouse input
                if (_type == Input::InputType::Mouse) {
                    if (_isAxis) {
                        _mouseDelta += Input::MouseDelta;
                        if (std::abs(_mouseDelta.x) > 25) {
                            GameBinding newBinding{ .Action = _action, .MouseAxis = Input::MouseAxis::MouseX };
                            finishBinding(newBinding);
                        }
                        else if (std::abs(_mouseDelta.y) > 25) {
                            GameBinding newBinding{ .Action = _action, .MouseAxis = Input::MouseAxis::MouseY };
                            finishBinding(newBinding);
                        }
                    }
                    else {
                        for (auto btn = Input::MouseButtons::LeftClick; btn <= Input::MouseButtons::WheelDown; btn = Input::MouseButtons((uint8)btn + 1)) {
                            if (Input::IsMouseButtonPressed(btn)) {
                                //if (auto binding = Game::Bindings.TryFind(_action)) {
                                //    binding->Mouse = btn;
                                //    binding->Key = Keys::None;
                                //    binding->Action = GameAction::None;
                                //    //Game::Bindings.UnbindExisting(*binding);

                                //    //_shortcutLabel = binding->GetShortcutLabel();
                                //    //_waitingForInput = false;
                                //    finishBinding(*binding);
                                //    break;
                                //}
                                //else {

                                GameBinding newBinding{ .Action = _action, .Mouse = btn };

                                //Game::Bindings.UnbindExisting(newBinding);
                                //Game::Bindings.Add(newBinding);

                                //_shortcutLabel = newBinding.GetShortcutLabel();
                                //_waitingForInput = false;
                                finishBinding(newBinding);
                                break;
                                //}
                            }
                        }
                    }
                }
                // todo: check controller / joystick input
            }
            else if (((Input::IsKeyPressed(Input::Keys::Enter) && Focused)
                    || (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) && _hovered))
                && (*_column == 0 || *_column == 1)) {
                StartBinding();
            }
        }

        void StartBinding() {
            _waitingForInput = true;
            _mouseDelta = Vector2::Zero;
            CaptureCursor(true);
            CaptureInput(true);
            Sound::Play2D(SoundResource{ ActionSound });
        }

        void SetColumn(int index) const {
            int controls = _isAxis ? 2 : 1;
            if (index > controls) index = 0;
            else if (index < 0) index = controls;
            *_column = index;
        }

        bool HandleMenuAction(Input::MenuAction action) override {
            if (!_waitingForInput) {
                if (action == Input::MenuAction::Confirm) {
                    if (*_column == 2) {
                        _invert = !_invert;
                        Sound::Play2D(SoundResource{ ActionSound });
                    }
                    else {
                        StartBinding();
                    }

                    return true;
                }

                if (action == Input::MenuAction::Left) {
                    SetColumn(*_column - 1);
                    return true;
                }

                if (action == Input::MenuAction::Right) {
                    SetColumn(*_column + 1);
                    return true;
                }
            }
            //else {

            //}

            return false;
        }

        void OnSelect() override {
            if (!_isAxis && *_column > 1)
                *_column = 1;
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
                auto valueLabel = _waitingForInput && column == 0 ? _isAxis ? "move axis" : "press a key" : _keyShortcut;
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

            //if (_showSecondBind) {
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
            //}

            {
                // Value 2
                auto valueLabel = _waitingForInput && column == 1 ? _isAxis ? "move axis" : "press a key" : _keyShortcut2;
                auto valueSize = MeasureString(valueLabel, FontSize::Small).x;

                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                dti.Color = _waitingForInput && column == 1 ? FOCUS_COLOR : (Focused || _hovered2) && column == 1 ? ACCENT_COLOR : textColor;
                dti.Position.x = ScreenPosition.x + (ValueWidth + Spacing + LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f) * GetScale();
                dti.Position.y = ScreenPosition.y + Padding.y * GetScale();
                Render::UICanvas->DrawRaw(valueLabel, dti, Layer + 1);
            }

            if (_showInvert) {
                // Invert checkbox
                auto color = _invert ? ACCENT_GLOW : _hovered3 ? ACCENT_COLOR : Focused && column == 2 ? FOCUSED_BUTTON : IDLE_BUTTON;

                Render::CanvasBitmapInfo cbi;
                cbi.Position = boxPosition;
                cbi.Position.x += (ValueWidth * 2 + Spacing * 2 + 25) * GetScale();

                cbi.Size.x = cbi.Size.y = ScreenSize.y - Padding.y * GetScale();
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = color * 0.4f;
                cbi.Color.A(1);
                Render::UICanvas->DrawBitmap(cbi, Layer);

                //if(_invert) {
                //    cbi.Position = boxPosition + Vector2(1,1);
                //    cbi.Position.x += (ValueWidth * 2 + Spacing * 2 + 25) * GetScale();

                //    cbi.Size.x = cbi.Size.y = ScreenSize.y - (Padding.y + 2) * GetScale();
                //    cbi.Texture = Render::Materials->White().Handle();
                //    cbi.Color = ACCENT_GLOW;
                //    cbi.Color.A(1);
                //    Render::UICanvas->DrawBitmap(cbi, Layer);
                //}
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
        GameAction::Converter,

        GameAction::CyclePrimary,
        GameAction::CycleSecondary,
        GameAction::CycleBomb,
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
        GameAction::Converter,
        GameAction::Automap,
        GameAction::RearView,
        GameAction::Afterburner,

        GameAction::Forward,
        GameAction::Reverse,
        GameAction::SlideLeft,
        GameAction::SlideRight,
        GameAction::SlideUp,
        GameAction::SlideDown,
    };

    inline std::array GamepadInputs = {
        GameAction::Forward,
        GameAction::Reverse,
        GameAction::SlideLeft,
        GameAction::SlideRight,
        GameAction::SlideUp,
        GameAction::SlideDown,
        GameAction::Afterburner,

        GameAction::SlideLeftRightAxis,
        GameAction::SlideUpDownAxis,
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
        GameAction::Converter,

        GameAction::CyclePrimary,
        GameAction::CycleSecondary,
        GameAction::CycleBomb,
    };


    class BindingDialog : public DialogBase {
        List<BindingControl*> _bindingControls;
        List<Input::Gamepad> _gamepads;
        ListBox2* _bindingList = nullptr;
        int _index = 0; // the selected control. 0 is keyboard, 1 is mouse, 1 > is controllers and joysticks
        int _column = 0; // 0 to 2. Binding 1, Binding 2, Invert

    public:
        void UpdateBindingList(span<GameAction> actions, Input::InputType type) {
            if (!_bindingList) return;

            _bindingList->Children.clear();
            _bindingControls.clear();
            _column = 0;

            for (auto& action : actions) {
                auto child = _bindingList->AddChild<BindingControl>(action, type, _column);
                _bindingControls.push_back(child);

                child->OnChange = [this] {
                    RefreshBindings();
                };
            }
        }

        BindingDialog() : DialogBase("customize bindings") {
            Size = Vector2(620, 460);

            List<string> deviceNames = { "Keyboard", "Mouse" };

            if (Settings::Inferno.EnableGamepads) {
                _gamepads = Input::GetGamepads(); // Copy the current gamepads
                for (auto& gamepad : _gamepads) {
                    deviceNames.push_back(gamepad.name);
                }
            }

            auto inputDropdown = AddChild<ComboSelect>("Input Device", deviceNames, _index);
            inputDropdown->LabelWidth = 225;
            //inputDropdown->ValueWidth = 350;
            inputDropdown->Size = Vector2(Size.x - DIALOG_PADDING * 2, CONTROL_HEIGHT);

            inputDropdown->Position = Vector2(DIALOG_PADDING, DIALOG_CONTENT_PADDING);
            inputDropdown->OnChange = [this](int index) {
                if (index == 0) {
                    UpdateBindingList(KeyboardInputs, Input::InputType::Keyboard);
                }
                else if (index == 1) {
                    UpdateBindingList(MouseInputs, Input::InputType::Mouse);
                }
                else if (index > 1) {
                    UpdateBindingList(GamepadInputs, Input::InputType::Gamepad);
                }
            };

            // Add headers
            {
                auto y = DIALOG_CONTENT_PADDING + CONTROL_HEIGHT + 10;

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

                //auto footer = AddChild<Label>("esc cancels, ctrl+r resets all, ctrl+d clears binding", FontSize::Small);
                auto footer = AddChild<Label>("esc cancels, ctrl+r resets all, ctrl+d clears binding", FontSize::Small);
                footer->Color = IDLE_BUTTON;
                footer->Position = Vector2(DIALOG_PADDING + 5, 425);
            }

            _bindingList = AddChild<ListBox2>(20, Size.x - DIALOG_PADDING * 3);
            _bindingList->Position = Vector2(DIALOG_PADDING, DIALOG_CONTENT_PADDING + CONTROL_HEIGHT * 2 + 8);

            UpdateBindingList(KeyboardInputs, Input::InputType::Keyboard);
        }

        bool HandleMenuAction(Input::MenuAction action) override {
            if (Selection == _bindingList) {
                switch (action) {
                    case Input::MenuAction::Left:
                        _column--;
                        if (_column < 0) _column = 2;
                        break;
                    case Input::MenuAction::Right:
                        _column++;
                        if (_column > 2) _column = 0;
                        break;
                    case Input::MenuAction::Up:
                        if (_bindingList->GetIndex() == 0)
                            break; // let regular navigation move out of this control

                        _bindingList->SelectPrevious();
                        return true;
                    case Input::MenuAction::Down:
                        if (_bindingList->GetIndex() >= _bindingList->Children.size() - 1)
                            break; // let regular navigation move out of this control

                        _bindingList->SelectNext();
                        return true;
                    case Input::MenuAction::Confirm:
                        // start binding or toggle invert
                        Selection->OnConfirm();
                        break;
                    /*case Input::MenuAction::Cancel:
                        break;*/
                }
            }

            return DialogBase::HandleMenuAction(action);
        }

    private:
        void RefreshBindings() const {
            //for (size_t i = 0; i < _bindingControls.size(); i++) {
            //    _bindingControls[i]->RefreshBinding();
            //}

            for (auto& control : _bindingControls) {
                control->RefreshBinding();
            }
        }
    };
}
