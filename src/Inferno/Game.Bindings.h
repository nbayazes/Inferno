#pragma once

#include "Input.h"
#include "Types.h"
#include "Utility.h"

namespace Inferno {
    // Bindable in-game actions
    enum class GameAction {
        None,
        SlideLeft,
        SlideRight,
        SlideLeftRightAxis,
        SlideUp,
        SlideDown,
        SlideUpDownAxis,
        Forward,
        Reverse,
        ForwardReverseAxis,
        RollLeft,
        RollRight,
        RollAxis,
        PitchUp,
        PitchDown,
        PitchAxis,
        YawLeft,
        YawRight,
        YawAxis,
        Afterburner,

        FirePrimary,
        FireSecondary,
        RearView,

        FireOnceEventIndex, // Actions past this index are only fired on button down

        FireFlare,
        DropBomb,

        CyclePrimary,
        CycleSecondary,
        CycleBomb,

        // Bindings for selecting weapons on each slot
        Weapon1,
        Weapon2,
        Weapon3,
        Weapon4,
        Weapon5,
        Weapon6,
        Weapon7,
        Weapon8,
        Weapon9,
        Weapon10,

        Automap,
        Headlight,
        Converter,
        Count
    };

    inline bool IsAxisAction(GameAction action) {
        switch (action) {
            case GameAction::SlideLeftRightAxis:
            case GameAction::SlideUpDownAxis:
            case GameAction::ForwardReverseAxis:
            case GameAction::PitchAxis:
            case GameAction::YawAxis:
            case GameAction::RollAxis:
                return true;
            default:
                return false;
        }
    }

    struct GameCommand {
        GameAction Id;
        std::function<void()> Action;
    };

    struct GameBinding {
        GameAction Action = GameAction::None;
        Input::Keys Key = Input::Keys::None;
        Input::MouseButtons Mouse = Input::MouseButtons::None;
        Input::InputAxis Axis = Input::InputAxis::None;
        Input::MouseAxis MouseAxis = Input::MouseAxis::None;
        // Gamepad / Joystick?
        // Controller ID
        // Controller Button / Axis

        string GetShortcutLabel() const;

        void Clear() {
            Key = {};
            Mouse = {};
        }

        bool HasValue() const {
            return Action != GameAction::None && (Key != Input::Keys::None || Mouse != Input::MouseButtons::None || Axis != Input::InputAxis::None);
        }

        //bool operator==(const GameBinding& rhs) const {
        //    return Key == rhs.Key || Mouse == rhs.Mouse;
        //}
    };


    class GameBindings {
        List<GameBinding> _bindings;
        Array<bool, (uint)GameAction::Count> _state = {};
        Array<string, (uint)GameAction::Count> _labels = {};

    public:
        GameBindings() { RestoreDefaults(); }

        void Clear() { _bindings.clear(); }

        // Adds a new binding and unbinds any existing actions using the same shortcut
        void Add(const GameBinding& binding);
        span<GameBinding> GetBindings() { return _bindings; }
        const string& GetLabel(GameAction action) const;

        // Unbinds any existing usages of this key or mouse button
        void UnbindExisting(const GameBinding& binding) {
            using namespace Input;

            for (auto& b : _bindings) {
                if (&b == &binding) continue;

                if (binding.Key != Keys::None && b.Key == binding.Key)
                    b.Key = Keys::None;

                if (binding.Mouse != MouseButtons::None && b.Mouse == binding.Mouse)
                    b.Mouse = MouseButtons::None;

                if (binding.MouseAxis != MouseAxis::None && b.MouseAxis == binding.MouseAxis)
                    b.MouseAxis = MouseAxis::None;

                if (binding.Axis != InputAxis::None && b.Axis == binding.Axis)
                    b.Axis = InputAxis::None;
            }
        }

        GameBinding* TryFind(GameAction action, Input::InputType type) {
            return Seq::find(_bindings, [&](const GameBinding& b) {
                if(b.Action != action) return false;

                using enum Input::InputType;

                switch(type) {
                    case Keyboard:
                        return b.Key != Input::Keys::None;
                    case Mouse:
                        return b.Mouse != Input::MouseButtons::None || b.MouseAxis != Input::MouseAxis::None;
                    case Gamepad:
                        return b.Axis != Input::InputAxis::None; // todo: gamepad buttons
                    default:
                        return b.HasValue();
                }
            });
        }

        void Update() {
            ResetState();

            // If any bindings are down for the action, set it as true
            for (auto& binding : _bindings) {
                if (binding.Action > GameAction::FireOnceEventIndex) {
                    if (Input::IsKeyPressed(binding.Key) || Input::IsMouseButtonPressed(binding.Mouse))
                        _state[(uint)binding.Action] = true;
                }
                else {
                    if (Input::IsKeyDown(binding.Key) || Input::IsMouseButtonDown(binding.Mouse))
                        _state[(uint)binding.Action] = true;
                }
            }
        }

        bool Pressed(GameAction action) const {
            return _state[(uint)action];
        }

        void RestoreDefaults();
        void ResetState() { ranges::fill(_state, false); }

        static constexpr bool IsReservedKey(Input::Keys key) {
            using Input::Keys;
            return key == Keys::Escape || key == Keys::LeftWindows || key == Keys::RightWindows;
        }
    };

    namespace Game {
        inline GameBindings Bindings;
    }
}
