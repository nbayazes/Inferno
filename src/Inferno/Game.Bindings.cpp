#include "pch.h"
#include "Game.Bindings.h"

namespace Inferno {
    using Mouse = Input::MouseButtons;
    using Input::Keys;

    Array<bool, (uint)GameAction::Count> ActionState;


    string GameBinding::GetShortcutLabel() const {
        if (Key != Input::Keys::None)
            return Input::KeyToString(Key);

        if (Mouse != Mouse::None) {
            switch (Mouse) {
                case Mouse::Left: return "Left";
                case Mouse::Right: return "Right";
                case Mouse::Middle: return "Middle";
                case Mouse::X1: return "X1";
                case Mouse::X2: return "X2";
                case Mouse::WheelUp: return "Wheel up";
                case Mouse::WheelDown: return "Wheel down";
            }
        }

        return "";
    }

    void GameBindings::Bind(GameBinding binding) {
        if (binding.Action == GameAction::None) return;
        if (binding.Key == Keys::Escape) return;

        UnbindExisting(binding);
        _bindings.push_back(binding);
    }

    void GameBindings::Reset() {
        Bind({ .Action = GameAction::Forward, .Key = Keys::W });
        Bind({ .Action = GameAction::Reverse, .Key = Keys::S });
        Bind({ .Action = GameAction::SlideLeft, .Key = Keys::A });
        Bind({ .Action = GameAction::SlideRight, .Key = Keys::D });
        Bind({ .Action = GameAction::SlideUp, .Key = Keys::Space });
        Bind({ .Action = GameAction::SlideDown, .Key = Keys::LeftShift });
        Bind({ .Action = GameAction::RollLeft, .Key = Keys::Q });
        Bind({ .Action = GameAction::RollRight, .Key = Keys::E });

        Bind({ .Action = GameAction::RollLeft, .Key = Keys::NumPad7 });
        Bind({ .Action = GameAction::RollRight, .Key = Keys::NumPad9 });
        Bind({ .Action = GameAction::YawLeft, .Key = Keys::NumPad4 });
        Bind({ .Action = GameAction::YawRight, .Key = Keys::NumPad6 });
        Bind({ .Action = GameAction::PitchUp, .Key = Keys::NumPad5 });
        Bind({ .Action = GameAction::PitchDown, .Key = Keys::NumPad8 });

        Bind({ .Action = GameAction::FirePrimary, .Mouse = Mouse::Left });
        Bind({ .Action = GameAction::FireSecondary, .Mouse = Mouse::Right });
        Bind({ .Action = GameAction::FireFlare, .Mouse = Mouse::X1 });
        Bind({ .Action = GameAction::FireFlare, .Key = Keys::F });
        Bind({ .Action = GameAction::DropBomb, .Mouse = Mouse::Middle });

        Bind({ .Action = GameAction::CyclePrimary, .Mouse = Mouse::WheelUp });
        Bind({ .Action = GameAction::CycleSecondary, .Mouse = Mouse::WheelDown });
        Bind({ .Action = GameAction::CycleBomb, .Key = Keys::X });
        Bind({ .Action = GameAction::Afterburner, .Key = Keys::LeftControl });
    }
}
