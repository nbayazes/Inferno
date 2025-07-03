#include "pch.h"
#include "Game.Bindings.h"

namespace Inferno {
    using Mouse = Input::MouseButtons;
    using Input::Keys;

    namespace {
        std::array<string, (int)GameAction::Count> ActionLabels;
    }

    string InputDeviceBinding::GetBindingLabel(GameAction action, int slot) {
        auto binding = GetBinding(action, slot);
        if (!binding) return "unknown";

        switch (type) {
            case Input::InputType::Unknown:
                return fmt::format("B{}", binding->id);
            case Input::InputType::Keyboard:
                return Input::KeyToString((Input::Keys)binding->id);
            case Input::InputType::Mouse:
                if (binding->type == BindType::Axis) {
                    using enum Input::MouseAxis;
                    switch ((Input::MouseAxis)binding->id) {
                        case None: return "";
                        case MouseX: return "X-Axis";
                        case MouseY: return "Y-Axis";
                        default: return fmt::format("axis {}", binding->id);
                    }
                }
                else if (binding->type == BindType::Button) {
                    using enum Input::MouseButtons;
                    switch ((Input::MouseButtons)binding->id) {
                        case LeftClick: return "Left click";
                        case RightClick: return "Right click";
                        case MiddleClick: return "Middle click";
                        //case X1: return "button 4";
                        //case X2: return "button 5";
                        case WheelUp: return "Wheel up";
                        case WheelDown: return "Wheel down";
                        default: return fmt::format("button {}", binding->id);
                    }
                }
                break;
            case Input::InputType::Gamepad:
            {
                if (binding->type == BindType::Button && binding->id < Input::PS_BUTTON_LABELS.size()) {
                    auto gamepadType = SDL_GAMEPAD_TYPE_UNKNOWN;
                    if (auto device = Input::GetDevice(guid))
                        gamepadType = device->type;

                    switch (gamepadType) {
                        default:
                            return Input::XBOX_BUTTON_LABELS[binding->id];
                        case SDL_GAMEPAD_TYPE_PS3:
                        case SDL_GAMEPAD_TYPE_PS4:
                        case SDL_GAMEPAD_TYPE_PS5:
                            return Input::PS_BUTTON_LABELS[binding->id];
                    }
                }
                else if (binding->type == BindType::Axis) {
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFTX)
                        return "LEFT X";
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFTY)
                        return "LEFT Y";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHTX)
                        return "RIGHT X";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHTY)
                        return "RIGHT Y";
                }
                else if (binding->type == BindType::AxisPlus || binding->type == BindType::AxisMinus) {
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                        return "L2";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                        return "R2";
                }
                else if (binding->type == BindType::AxisButtonPlus) {
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFTX)
                        return "LEFT X+";
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFTY)
                        return "LEFT Y+";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHTX)
                        return "RIGHT X+";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHTY)
                        return "RIGHT Y+";
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                        return "L2";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                        return "R2";
                }
                else if (binding->type == BindType::AxisButtonMinus) {
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFTX)
                        return "LEFT X-";
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFTY)
                        return "LEFT Y-";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHTX)
                        return "RIGHT X-";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHTY)
                        return "RIGHT Y-";
                    if (binding->id == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                        return "L2";
                    if (binding->id == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                        return "R2";
                }
                break;
            case Input::InputType::Joystick:
                break;
            }
        }

        return "";
    }

    bool GameBindings::Pressed(GameAction action) {
        for (auto& device : _devices) {
            if (auto joystick = Input::GetDevice(device.guid)) {
                for (auto& binding : device.bindings[(int)action]) {
                    switch (binding.type) {
                        case BindType::Button:
                            if (joystick->ButtonWasPressed(binding.id))
                                return true;
                            break;
                        case BindType::AxisButtonPlus:
                            if (joystick->AxisPressed(binding.id, true))
                                return true;
                            break;

                        case BindType::AxisButtonMinus:
                            if (joystick->AxisPressed(binding.id, false))
                                return true;
                            break;

                        case BindType::Hat:
                            if (joystick->HatDirection((Input::HatDirection)binding.id))
                                return true;
                            break;
                    }
                }
            }
        }

        for (auto& binding : _keyboard.bindings[(int)action]) {
            if (Input::OnKeyPressed((Input::Keys)binding.id))
                return true;
        }

        for (auto& binding : _mouse.bindings[(int)action]) {
            if (Input::MouseButtonPressed((Input::MouseButtons)binding.id))
                return true;
        }

        return false;
    }

    bool GameBindings::Held(GameAction action) {
        for (auto& device : _devices) {
            if (auto joystick = Input::GetDevice(device.guid)) {
                for (auto& binding : device.bindings[(int)action]) {
                    switch (binding.type) {
                        case BindType::Button:
                            if (joystick->ButtonHeld(binding.id))
                                return true;
                            break;
                        case BindType::AxisButtonPlus:
                            if (joystick->axes[binding.id] > 0.3f)
                                return true;
                            break;

                        case BindType::AxisButtonMinus:
                            if (joystick->axes[binding.id] < -0.3f)
                                return true;
                            break;

                        case BindType::Hat:
                            if (joystick->HatDirection((Input::HatDirection)binding.id))
                                return true;
                            break;
                    }
                }
            }
        }

        for (auto& binding : _keyboard.bindings[(int)action]) {
            if (Input::IsKeyDown((Input::Keys)binding.id))
                return true;
        }

        for (auto& binding : _mouse.bindings[(int)action]) {
            if (Input::IsMouseButtonDown((Input::MouseButtons)binding.id))
                return true;
        }

        return false;
    }

    float GameBindings::LinearAxis(GameAction action) const {
        float value = 0;

        for (auto& deviceBindings : _devices) {
            if (auto device = Input::GetDevice(deviceBindings.guid)) {
                for (auto& binding : deviceBindings.bindings[(int)action]) {
                    if (!Seq::inRange(device->axes, binding.id)) continue;
                    if (binding.type == BindType::None) continue;

                    float invert = binding.invert ? -1.0f : 1.0f;

                    auto deadzone = deviceBindings.sensitivity.GetDeadzone(action);
                    auto sensitivity = deviceBindings.sensitivity.GetSensitivity(action);

                    if (binding.type == BindType::AxisPlus) {
                        value += Input::LinearDampen(device->axes[binding.id], deadzone, 1, sensitivity) * invert;
                    }
                    else if (binding.type == BindType::AxisMinus) {
                        value += Input::LinearDampen(device->axes[binding.id], deadzone, 1, sensitivity) * invert;
                    }
                    else if (binding.type == BindType::Axis) {
                        if (device->type != SDL_GAMEPAD_TYPE_UNKNOWN) {
                            // Playstation or Xbox controllers. Merge together and use circular dampening
                            Vector2 stick;

                            if (binding.id == SDL_GAMEPAD_AXIS_LEFTX || binding.id == SDL_GAMEPAD_AXIS_LEFTY)
                                stick = Vector2{ device->axes[SDL_GAMEPAD_AXIS_LEFTX], device->axes[SDL_GAMEPAD_AXIS_LEFTY] };
                            else if (binding.id == SDL_GAMEPAD_AXIS_RIGHTX || binding.id == SDL_GAMEPAD_AXIS_RIGHTY)
                                stick = Vector2{ device->axes[SDL_GAMEPAD_AXIS_RIGHTX], device->axes[SDL_GAMEPAD_AXIS_RIGHTY] };

                            stick = Input::CircularDampen(stick, deadzone, 1) * sensitivity;

                            if (binding.id == SDL_GAMEPAD_AXIS_LEFTX || binding.id == SDL_GAMEPAD_AXIS_RIGHTX)
                                value += stick.x * invert;
                            else
                                value += stick.y * invert;
                        }
                        else {
                            value += Input::LinearDampen(device->axes[binding.id], deadzone, 1, sensitivity) * invert;
                        }
                    }
                }
            }
        }

        return value;

        //for (auto& joystick : Input::GetJoysticks()) {
        //    if (joystick.type == SDL_GAMEPAD_TYPE_UNKNOWN && !Settings::Inferno.EnableJoystick) continue;
        //    if (joystick.type != SDL_GAMEPAD_TYPE_UNKNOWN && !Settings::Inferno.EnableGamepad) continue;

        //    // check all bindings
        //    if (_ps5bindings.guid == joystick.guid) {
        //        auto axes = joystick.axes;

        //        // Apply circular dampening to the two sticks
        //        Vector2 leftStick = { axes[SDL_GAMEPAD_AXIS_LEFTX], axes[SDL_GAMEPAD_AXIS_LEFTY] };
        //        leftStick = Input::CircularDampen(leftStick, joystick.innerDeadzone, joystick.outerDeadzone);
        //        axes[SDL_GAMEPAD_AXIS_LEFTX] = leftStick.x;
        //        axes[SDL_GAMEPAD_AXIS_LEFTY] = leftStick.y;

        //        Vector2 rightStick = { axes[SDL_GAMEPAD_AXIS_RIGHTX], axes[SDL_GAMEPAD_AXIS_RIGHTY] };
        //        rightStick = Input::CircularDampen(rightStick, joystick.innerDeadzone, joystick.outerDeadzone);
        //        axes[SDL_GAMEPAD_AXIS_RIGHTX] = rightStick.x;
        //        axes[SDL_GAMEPAD_AXIS_RIGHTY] = rightStick.y;
        //    }
        //}
    }

    string_view GetActionLabel(GameAction action) {
        if (action >= GameAction::Count) return "";

        auto setLabel = [](GameAction action, const string& str) {
            ActionLabels[(uint)action] = str;
        };

        if (ActionLabels[(int)GameAction::FirePrimary].empty()) {
            ranges::fill(ActionLabels, string("undefined"));
            setLabel(GameAction::FirePrimary, "Fire primary");
            setLabel(GameAction::FireSecondary, "Fire secondary");
            setLabel(GameAction::DropBomb, "Drop bomb");
            setLabel(GameAction::FireFlare, "Fire flare");
            setLabel(GameAction::SlideLeft, "Slide left");
            setLabel(GameAction::SlideRight, "Slide right");
            setLabel(GameAction::LeftRightAxis, "Slide left/right");
            setLabel(GameAction::SlideUp, "Slide up");
            setLabel(GameAction::SlideDown, "Slide down");
            setLabel(GameAction::UpDownAxis, "Slide up/down");
            setLabel(GameAction::Forward, "Forward");
            setLabel(GameAction::Reverse, "Reverse");
            setLabel(GameAction::ForwardReverseAxis, "Forward/Reverse");
            setLabel(GameAction::PitchUp, "Pitch up");
            setLabel(GameAction::PitchDown, "Pitch down");
            setLabel(GameAction::PitchAxis, "Pitch");
            setLabel(GameAction::YawLeft, "Yaw left");
            setLabel(GameAction::YawRight, "Yaw right");
            setLabel(GameAction::YawAxis, "Yaw");
            setLabel(GameAction::Afterburner, "Afterburner");
            setLabel(GameAction::Automap, "Automap");
            setLabel(GameAction::EnergyConverter, "Converter");
            setLabel(GameAction::CyclePrimary, "Cycle primary");
            setLabel(GameAction::CycleSecondary, "Cycle secondary");
            setLabel(GameAction::CycleBomb, "Cycle bomb");
            setLabel(GameAction::Headlight, "Headlight");
            setLabel(GameAction::RollLeft, "Roll left");
            setLabel(GameAction::RollRight, "Roll right");
            setLabel(GameAction::RollAxis, "Roll");
            setLabel(GameAction::RearView, "Rear view");

            setLabel(GameAction::Weapon1, "Laser cannon");
            setLabel(GameAction::Weapon2, "Vulcan/Gauss");
            setLabel(GameAction::Weapon3, "Spreadfire/Helix");
            setLabel(GameAction::Weapon4, "Plasma/phoenix");
            setLabel(GameAction::Weapon5, "fusion/omega");
            setLabel(GameAction::Weapon6, "concussion/flash");
            setLabel(GameAction::Weapon7, "homing/guided");
            setLabel(GameAction::Weapon8, "prox/smart mine");
            setLabel(GameAction::Weapon9, "smart/mercury");
            setLabel(GameAction::Weapon10, "mega/earthshaker");
        }

        return ActionLabels[(int)action];
    }

    void ResetKeyboardBindings(InputDeviceBinding& device) {
        device.bindings = {};

        device.Bind({ .action = GameAction::Forward, .id = Keys::W });
        device.Bind({ .action = GameAction::SlideLeft, .id = Keys::A });
        device.Bind({ .action = GameAction::Reverse, .id = Keys::S });
        device.Bind({ .action = GameAction::SlideRight, .id = Keys::D });
        device.Bind({ .action = GameAction::SlideUp, .id = Keys::Space });
        device.Bind({ .action = GameAction::SlideDown, .id = Keys::LeftShift });
        device.Bind({ .action = GameAction::RollLeft, .id = Keys::Q });
        device.Bind({ .action = GameAction::RollRight, .id = Keys::E });

        device.Bind({ .action = GameAction::PitchUp, .id = Keys::Down });
        device.Bind({ .action = GameAction::PitchDown, .id = Keys::Up });
        device.Bind({ .action = GameAction::YawLeft, .id = Keys::Left });
        device.Bind({ .action = GameAction::YawRight, .id = Keys::Right });
        device.Bind({ .action = GameAction::FirePrimary, .id = Keys::NumPad0 });
        device.Bind({ .action = GameAction::FireSecondary, .id = Keys::NumPad1 });

        device.Bind({ .action = GameAction::Afterburner, .id = Keys::LeftControl });

        device.Bind({ .action = GameAction::Headlight, .id = Keys::OemTilde });
        device.Bind({ .action = GameAction::FireFlare, .id = Keys::F });
        device.Bind({ .action = GameAction::Automap, .id = Keys::Tab });
        device.Bind({ .action = GameAction::Pause, .id = Keys::Escape });
        device.Bind({ .action = GameAction::RearView, .id = Keys::R });
        device.Bind({ .action = GameAction::EnergyConverter, .id = Keys::T });
        device.Bind({ .action = GameAction::DropBomb, .id = Keys::B });
        device.Bind({ .action = GameAction::CycleBomb, .id = Keys::C });

        device.Bind({ .action = GameAction::Weapon1, .id = Keys::D1 });
        device.Bind({ .action = GameAction::Weapon2, .id = Keys::D2 });
        device.Bind({ .action = GameAction::Weapon3, .id = Keys::D3 });
        device.Bind({ .action = GameAction::Weapon4, .id = Keys::D4 });
        device.Bind({ .action = GameAction::Weapon5, .id = Keys::D5 });
        device.Bind({ .action = GameAction::Weapon6, .id = Keys::D6 });
        device.Bind({ .action = GameAction::Weapon7, .id = Keys::D7 });
        device.Bind({ .action = GameAction::Weapon8, .id = Keys::D8 });
        device.Bind({ .action = GameAction::Weapon9, .id = Keys::D9 });
        device.Bind({ .action = GameAction::Weapon10, .id = Keys::D0 });
    }

    void ResetMouseBindings(InputDeviceBinding& device) {
        device.bindings = {};

        device.Bind({ .action = GameAction::FirePrimary, .id = (int)Input::MouseButtons::LeftClick });
        device.Bind({ .action = GameAction::FireSecondary, .id = (int)Input::MouseButtons::RightClick });

        device.Bind({ .action = GameAction::DropBomb, .id = (int)Input::MouseButtons::MiddleClick });

        device.Bind({ .action = GameAction::YawAxis, .id = (int)Input::MouseAxis::MouseX, .type = BindType::Axis });
        device.Bind({ .action = GameAction::PitchAxis, .id = (int)Input::MouseAxis::MouseY, .type = BindType::Axis, .invert = true });

        device.Bind({ .action = GameAction::CyclePrimary, .id = (int)Input::MouseButtons::WheelUp });
        device.Bind({ .action = GameAction::CycleSecondary, .id = (int)Input::MouseButtons::WheelDown });
    }

    void ResetGamepadBindings(InputDeviceBinding& device, float deadzone) {
        device.bindings = {};
        device.sensitivity.rotationDeadzone = Vector3{ deadzone, deadzone, deadzone };

        device.Bind({ .action = GameAction::ForwardReverseAxis, .id = SDL_GAMEPAD_AXIS_LEFTY, .type = BindType::Axis });
        device.Bind({ .action = GameAction::LeftRightAxis, .id = SDL_GAMEPAD_AXIS_LEFTX, .type = BindType::Axis });
        device.Bind({ .action = GameAction::PitchAxis, .id = SDL_GAMEPAD_AXIS_RIGHTY, .type = BindType::Axis });
        device.Bind({ .action = GameAction::YawAxis, .id = SDL_GAMEPAD_AXIS_RIGHTX, .type = BindType::Axis });

        device.Bind({ .action = GameAction::Automap, .id = SDL_GAMEPAD_BUTTON_BACK, .type = BindType::Button });
        device.Bind({ .action = GameAction::Pause, .id = SDL_GAMEPAD_BUTTON_START, .type = BindType::Button });

        device.Bind({ .action = GameAction::FirePrimary, .id = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, .type = BindType::Button });
        device.Bind({ .action = GameAction::FireSecondary, .id = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, .type = BindType::Button });

        device.Bind({ .action = GameAction::SlideDown, .id = SDL_GAMEPAD_AXIS_LEFT_TRIGGER, .type = BindType::AxisPlus });
        device.Bind({ .action = GameAction::SlideUp, .id = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, .type = BindType::AxisPlus });

        // Sprint is usually on left stick
        device.Bind({ .action = GameAction::Afterburner, .id = SDL_GAMEPAD_BUTTON_LEFT_STICK, .type = BindType::Button });
        device.Bind({ .action = GameAction::EnergyConverter, .id = SDL_GAMEPAD_BUTTON_RIGHT_STICK, .type = BindType::Button });

        //device.Bind({ .action = GameAction::FireFlare, .id = SDL_GAMEPAD_AXIS_LEFT_TRIGGER, .type = BindType::AxisButtonPlus }, 1);

        // Face buttons
        device.Bind({ .action = GameAction::FireFlare, .id = SDL_GAMEPAD_BUTTON_EAST, .type = BindType::Button });
        device.Bind({ .action = GameAction::DropBomb, .id = SDL_GAMEPAD_BUTTON_NORTH, .type = BindType::Button });

        device.Bind({ .action = GameAction::RollLeft, .id = SDL_GAMEPAD_BUTTON_WEST, .type = BindType::Button });
        device.Bind({ .action = GameAction::RollRight, .id = SDL_GAMEPAD_BUTTON_SOUTH, .type = BindType::Button });

        // Dpad bindings
        device.Bind({ .action = GameAction::CyclePrimary, .id = SDL_GAMEPAD_BUTTON_DPAD_UP, .type = BindType::Button });
        device.Bind({ .action = GameAction::CycleSecondary, .id = SDL_GAMEPAD_BUTTON_DPAD_DOWN, .type = BindType::Button });
        device.Bind({ .action = GameAction::CycleBomb, .id = SDL_GAMEPAD_BUTTON_DPAD_LEFT, .type = BindType::Button });
        device.Bind({ .action = GameAction::Headlight, .id = SDL_GAMEPAD_BUTTON_DPAD_RIGHT, .type = BindType::Button });

        // Ran out of bindings for xbox, but rear view is rarely used anyway
        device.Bind({ .action = GameAction::RearView, .id = SDL_GAMEPAD_BUTTON_TOUCHPAD, .type = BindType::Button });
    }
}
