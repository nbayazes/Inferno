#pragma once

#include "Game.UI.Controls.h"
#include "Game.Bindings.h"

namespace Inferno::UI {
    struct BindingEntry {
        //GameAction Action{};
        string Label;
        GameBinding Binding;
    };

    class BindingControl : public ControlBase {
        string _label;
        bool _held = false;
        string _valueText;
        bool _dragging = false;
        bool _hovered = false;
        float _textHeight = 24;
        bool _waitingForInput = false;
        GameAction _action;
        string _keyShortcut;

    public:
        float LabelWidth = 200;
        float ValueWidth = 150;
        string MenuActionSound = MENU_SELECT_SOUND; // Sound when picking an item in the popup menu

        std::function<void()> OnChange; // Called when a binding changes

        BindingControl(GameAction action) : _action(action) {
            Padding = Vector2(0, 2);
            _label = Game::Bindings.GetLabel(action);
            auto labelSize = MeasureString(_label, FontSize::Small);
            //LabelWidth = labelSize.x;
            _textHeight = labelSize.y;
            Size = Vector2(60, _textHeight + 2);
            //ValueWidth = size.x - LabelWidth;
            ActionSound = MENU_SELECT_SOUND;
            RefreshBinding();
        }

        void RefreshBinding() {
            if (auto binding = Game::Bindings.TryFind(_action)) {
                _keyShortcut = binding->GetShortcutLabel();
            }
            else {
                _keyShortcut = "";
            }
        }

        static Ptr<ComboSelect> Create(string_view label, const List<string>& values, int& index) {
            return make_unique<ComboSelect>(label, values, index);
        }

        void OnUpdate() override {
            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            _hovered = RectangleContains(boxPosition, Vector2(ValueWidth * GetScale(), ScreenSize.y), Input::MousePosition);

            auto finishBinding = [this](const GameBinding& binding) {
                if (auto entry = Game::Bindings.TryFind(binding.Action)) {
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
                //Input::ResetState();
                //Game::Bindings.ResetState();
            };

            if (_waitingForInput) {
                using Input::Keys;
                using Input::MouseButtons;

                // Check keyboard input
                for (Keys key = Keys::Back; key <= Keys::OemClear; key = Keys((unsigned char)key + 1)) {
                    if (Input::IsKeyPressed(key)) {
                        if (key == Keys::Escape) {
                            //selectedAction = GameAction::None; // Cancel the assignment
                            _waitingForInput = false;
                            CaptureCursor(false);
                            CaptureInput(false);
                            Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
                            break;
                        }

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

                // Check mouse input
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

                // todo: check controller / joystick input
            }
            else if ((Input::MenuConfirm() && Focused) ||
                (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) && _hovered)) {
                _waitingForInput = true;
                CaptureCursor(true);
                CaptureInput(true);
                Sound::Play2D(SoundResource{ ActionSound });
            }
        }

        void OnDraw() override {
            constexpr Color textColor(0.8f, 0.8f, 0.8f);

            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);

            {
                // Label Background
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                //cbi.Size = Vector2(ValueWidth * GetScale(), ScreenSize.y) - border * 2;
                cbi.Size.x = LabelWidth * GetScale() - 2 * GetScale();
                cbi.Size.y = ScreenSize.y - Padding.y * GetScale();
                cbi.Texture = Render::Materials->White().Handle();
                //cbi.Color = borderColor * 0.3f;
                cbi.Color = Focused ? FOCUSED_BUTTON : IDLE_BUTTON;
                cbi.Color *= 0.3f;
                cbi.Color.A(1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                dti.Color = Focused /*|| Hovered*/ ? ACCENT_COLOR : textColor;
                dti.Position = ScreenPosition;
                dti.Position.y += Padding.y * GetScale();
                dti.Position.x += 2 * GetScale();
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            {
                // Value Background
                auto color = _waitingForInput ? ACCENT_GLOW : _hovered ? ACCENT_COLOR : Focused ? FOCUSED_BUTTON : IDLE_BUTTON;

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
                auto valueLabel = _waitingForInput ? "press a key" : _keyShortcut;
                auto valueSize = MeasureString(valueLabel, FontSize::Small).x;

                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                dti.Color = _waitingForInput ? ACCENT_GLOW : Focused || _hovered ? ACCENT_COLOR : textColor;
                //dti.Color = ACCENT_COLOR;
                //dti.Position = Vector2(ScreenPosition.x + LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f, ScreenPosition.y);
                dti.Position.x = ScreenPosition.x + (LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f) * GetScale();
                dti.Position.y = ScreenPosition.y + Padding.y * GetScale();
                Render::UICanvas->DrawRaw(valueLabel, dti, Layer + 1);
            }
        }
    };

    class BindingDialog : public DialogBase {
        List<GameBinding> _bindings;
        List<BindingControl*> _bindingControls;

    public:
        BindingDialog() : DialogBase("keyboard controls") {
            Size = Vector2(620, 460);

            auto stackPanel = AddChild<StackPanel>();
            stackPanel->Position = Vector2(DIALOG_PADDING, DIALOG_CONTENT_PADDING);

            const auto addAction = [&, this](GameAction action) {
                auto child = stackPanel->AddChild<BindingControl>(action);
                _bindingControls.push_back(child);

                child->OnChange = [this] {
                    RefreshBindings();
                };
            };

            addAction(GameAction::Forward);
            addAction(GameAction::Reverse);
            addAction(GameAction::SlideLeft);
            addAction(GameAction::SlideRight);
            addAction(GameAction::SlideUp);
            addAction(GameAction::SlideDown);

            addAction(GameAction::PitchUp);
            addAction(GameAction::PitchDown);
            addAction(GameAction::YawLeft);
            addAction(GameAction::YawRight);
            addAction(GameAction::RollLeft);
            addAction(GameAction::RollRight);

            addAction(GameAction::FirePrimary);
            addAction(GameAction::FireSecondary);
            addAction(GameAction::FireFlare);
            addAction(GameAction::DropBomb);

            addAction(GameAction::Afterburner);
            addAction(GameAction::Headlight);
            addAction(GameAction::Automap);
            addAction(GameAction::Converter);

            addAction(GameAction::CyclePrimary);
            addAction(GameAction::CycleSecondary);
            addAction(GameAction::CycleBomb);
        }

    private:
        void RefreshBindings() const {
            for (auto& control : _bindingControls) {
                control->RefreshBinding();
            }
        }
    };
}
