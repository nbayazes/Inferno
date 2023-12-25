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
        SlideUp,
        SlideDown,
        Forward,
        Reverse,
        RollLeft,
        RollRight,
        PitchUp,
        PitchDown,
        YawLeft,
        YawRight,
        Afterburner,

        FirePrimary,
        FireSecondary,

        FireOnceEventIndex, // Actions past this index are only fired on button down

        FireFlare,
        DropBomb,

        CyclePrimary,
        CycleSecondary,
        CycleBomb,

        Automap,
        Headlight,
        Converter,
        Count
    };

    struct GameCommand {
        GameAction Id;
        std::function<void()> Action;
    };

    struct GameBinding {
        GameAction Action = GameAction::None;
        Input::Keys Key = Input::Keys::None;
        Input::MouseButtons Mouse = Input::MouseButtons::None;
        // Gamepad / Joystick?

        string GetShortcutLabel() const;

        void Clear() {
            Key = {};
            Mouse = {};
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
        GameBindings() { Reset(); }

        void Clear() { _bindings.clear(); }

        // Adds a new binding and unbinds any existing actions using the same shortcut
        void Add(const GameBinding& binding);
        span<GameBinding> GetBindings() { return _bindings; }
        const string& GetLabel(GameAction action) const;

        void UnbindExisting(const GameBinding& binding) {
            for (auto& b : _bindings) {
                if (&b == &binding) continue;

                if (b.Key == binding.Key)
                    b.Key = Input::Keys::None;

                if (b.Mouse == binding.Mouse)
                    b.Mouse = Input::MouseButtons::None;
            }
        }

        GameBinding* TryFind(GameAction action) {
            return Seq::find(_bindings, [&](const GameBinding& b) { return b.Action == action; });
        }

        void Update() {
            // Clear state
            ranges::fill(_state, false);

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

        void Reset();

        bool IsReservedKey(Input::Keys key) const {
            using Input::Keys;
            return key == Keys::Escape || key == Keys::LeftWindows || key == Keys::RightWindows;
        }
    };

    namespace Game {
        inline GameBindings Bindings;
    }
}
