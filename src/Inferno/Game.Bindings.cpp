#include "pch.h"
#include "Game.Bindings.h"

namespace Inferno {
    using Mouse = Input::MouseButtons;
    using Input::Keys;

    string GameBinding::GetShortcutLabel() const {
        if (Key != Input::Keys::None)
            return Input::KeyToString(Key);

        if (Mouse != Mouse::None) {
            switch (Mouse) {
                case Mouse::LeftClick: return "Left click";
                case Mouse::RightClick: return "Right click";
                case Mouse::MiddleClick: return "Middle click";
                case Mouse::X1: return "Mouse 4";
                case Mouse::X2: return "Mouse 5";
                case Mouse::WheelUp: return "Wheel up";
                case Mouse::WheelDown: return "Wheel down";
            }
        }

        return "";
    }

    const string& GameBindings::GetLabel(GameAction action) const {
        return _labels[(uint)action];
    }

    void GameBindings::Add(const GameBinding& binding) {
        if (binding.Action == GameAction::None) return;
        if (binding.Key == Keys::Escape) return;

        UnbindExisting(binding);
        _bindings.push_back(binding);
    }

    void GameBindings::RestoreDefaults() {
        auto setLabel = [this](GameAction action, const string& str) {
            _labels[(uint)action] = str;
        };

        ranges::fill(_labels, string("undefined"));
        //for (auto& label : _labels)
        //    label = string("undefined");

        setLabel(GameAction::FirePrimary, "Fire primary");
        setLabel(GameAction::FireSecondary, "Fire secondary");
        setLabel(GameAction::DropBomb, "Drop bomb");
        setLabel(GameAction::FireFlare, "Fire flare");
        setLabel(GameAction::SlideLeft, "Slide left");
        setLabel(GameAction::SlideRight, "Slide right");
        setLabel(GameAction::SlideUp, "Slide up");
        setLabel(GameAction::SlideDown, "Slide down");
        setLabel(GameAction::Forward, "Forward");
        setLabel(GameAction::Reverse, "Reverse");
        setLabel(GameAction::PitchUp, "Pitch up");
        setLabel(GameAction::PitchDown, "Pitch down");
        setLabel(GameAction::YawLeft, "Yaw left");
        setLabel(GameAction::YawRight, "Yaw right");
        setLabel(GameAction::Afterburner, "Afterburner");
        setLabel(GameAction::Automap, "Automap");
        setLabel(GameAction::Converter, "Converter");
        setLabel(GameAction::CyclePrimary, "Cycle primary");
        setLabel(GameAction::CycleSecondary, "Cycle secondary");
        setLabel(GameAction::CycleBomb, "Cycle bomb");
        setLabel(GameAction::Headlight, "Headlight");
        setLabel(GameAction::RollLeft, "Roll left");
        setLabel(GameAction::RollRight, "Roll right");

        _bindings.clear();
        Add({ .Action = GameAction::Forward, .Key = Keys::W });
        Add({ .Action = GameAction::Reverse, .Key = Keys::S });
        Add({ .Action = GameAction::SlideLeft, .Key = Keys::A });
        Add({ .Action = GameAction::SlideRight, .Key = Keys::D });
        Add({ .Action = GameAction::SlideUp, .Key = Keys::Space });
        Add({ .Action = GameAction::SlideDown, .Key = Keys::LeftShift });
        Add({ .Action = GameAction::RollLeft, .Key = Keys::Q });
        Add({ .Action = GameAction::RollRight, .Key = Keys::E });

        //Bind({ .Action = GameAction::RollLeft, .Key = Keys::NumPad7 });
        //Bind({ .Action = GameAction::RollRight, .Key = Keys::NumPad9 });
        Add({ .Action = GameAction::YawLeft, .Key = Keys::NumPad4 });
        Add({ .Action = GameAction::YawRight, .Key = Keys::NumPad6 });
        Add({ .Action = GameAction::PitchUp, .Key = Keys::NumPad5 });
        Add({ .Action = GameAction::PitchDown, .Key = Keys::NumPad8 });

        Add({ .Action = GameAction::FirePrimary, .Mouse = Mouse::LeftClick });
        Add({ .Action = GameAction::FireSecondary, .Mouse = Mouse::RightClick });
        //Bind({ .Action = GameAction::FireFlare, .Mouse = Mouse::X1 });
        Add({ .Action = GameAction::FireFlare, .Key = Keys::F });
        Add({ .Action = GameAction::DropBomb, .Mouse = Mouse::MiddleClick });

        Add({ .Action = GameAction::CyclePrimary, .Mouse = Mouse::WheelUp });
        Add({ .Action = GameAction::CycleSecondary, .Mouse = Mouse::WheelDown });
        Add({ .Action = GameAction::CycleBomb, .Key = Keys::X });
        Add({ .Action = GameAction::Afterburner, .Key = Keys::LeftControl });
    }
}
