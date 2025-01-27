#pragma once

#include "Input.h"
#include "Types.h"
#include "Utility.h"

/*
 * Input binding system
 *
 * Input is combined all enabled devices and translated them into
 * game commands. The exception to this is linear axes from joysticks and gamepads.
 *
 * Each input device stores raw state for the buttons and axes on it.
 * The input system updates these each tick.
 *
 * Each device stores two bindings for each action. Certain actions can only have an axis assigned to them.
 * An axis can be assigned as a digital input to any action.
 *
 * Gamepad triggers are treated as a half-axis and can only be bound to specific actions.
 */

namespace Inferno {
    // Bindable in-game actions
    enum class GameAction : uint16 {
        None,
        SlideLeft,
        SlideRight,
        LeftRightAxis,
        SlideUp,
        SlideDown,
        UpDownAxis,
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
        Throttle,

        FirePrimary,
        FireSecondary,
        RearView,

        FireOnceEventIndex, // Actions past this index are only fired on button down

        FireFlare = FireOnceEventIndex,
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
        Pause,
        Count
    };

    struct GameCommand {
        GameAction Id;
        std::function<void()> Action;
    };

    string_view GetActionLabel(GameAction action);

    enum class BindType {
        None,
        Button, // A key or button with a binary state
        Axis, // Full range axis
        AxisPlus, // Half range axes like triggers, treat as 0 to 1
        AxisMinus, // Half range axes like triggers, treat as 0 to -1
        AxisButtonPlus, // Axis treated as a binary button
        AxisButtonMinus, // Axis treated as a binary button
        Hat // an 8-way hat that can only be in a single state
    };

    // Returns the type of binding this action is compatible with
    inline BindType GetActionBindType(GameAction action) {
        switch (action) {
            case GameAction::LeftRightAxis:
            case GameAction::UpDownAxis:
            case GameAction::ForwardReverseAxis:
            case GameAction::PitchAxis:
            case GameAction::YawAxis:
            case GameAction::RollAxis:
            case GameAction::Throttle:
                return BindType::Axis;
            case GameAction::RollRight:
            case GameAction::PitchUp:
            case GameAction::YawRight:
            case GameAction::SlideUp:
            case GameAction::SlideRight:
            case GameAction::SlideLeft:
            case GameAction::Forward:
                return BindType::AxisPlus;
            case GameAction::RollLeft:
            case GameAction::PitchDown:
            case GameAction::YawLeft:
            case GameAction::SlideDown:
            case GameAction::Reverse:
                return BindType::AxisMinus;
            default:
                return BindType::Button;
        }
    }

    struct GameBinding {
        GameAction action = GameAction::None;
        uint8 id = 0; // Axis, Hat, Button, key id
        BindType type = BindType::None;
        bool invert = false;
        uint8 innerDeadzone = 16;
        uint8 outerDeadzone = 255;

        float GetInvertSign() const { return invert ? -1.0f : 1.0f; }

        bool operator==(const GameBinding& other) const {
            return type == other.type && action == other.action;
        }

        static GameBinding EMPTY;
    };


    constexpr uint BIND_SLOTS = 2;

    // Stores the bindings for an input device
    struct InputDeviceBinding {
        string guid; // identifies the input device for controllers and joysticks
        Input::InputType type = Input::InputType::Unknown;

        std::array<std::array<GameBinding, BIND_SLOTS>, (uint)GameAction::Count> bindings = { {} };
        //const GameBinding& Get(GameAction action) const { return bindings[(int)action]; }

        // Returns true if neither binding is set
        bool IsUnset(GameAction action) const {
            if (action >= GameAction::Count) return false;
            return bindings[(int)action][0].type == BindType::None &&
                   bindings[(int)action][1].type == BindType::None;
        }

        // Clear existing bindings using this binding
        void UnbindOthers(const GameBinding& binding, uint slot) {
            for (auto& group : bindings) {
                for (size_t g = 0; g < group.size(); g++) {
                    auto& existing = group[g];

                    if ((existing.type == BindType::Button && binding.type != BindType::Button) ||
                        (binding.type == BindType::Button && existing.type != BindType::Button))
                        continue; // skip mismatched types (only compare buttons to buttons, and non-buttons to other non-buttons)

                    if (existing.action == binding.action) {
                        if (existing.id == binding.id && g != slot)
                            existing = {}; // Clear other slot
                    }
                    else {
                        if (existing.id == binding.id && existing.action != binding.action)
                            existing = {}; // Clear binding on other action
                    }
                }
            }
        }

        void Bind(GameBinding binding, uint slot = 0) {
            if (binding.action >= GameAction::Count) return;

            if (binding.type == BindType::None)
                binding.type = BindType::Button;

            UnbindOthers(binding, slot);
            auto index = std::clamp(slot, 0u, (uint)GameAction::Count);
            bindings[(uint)binding.action][index] = binding;
        }

        // Returns bindings for an action
        std::span<GameBinding> GetBinding(GameAction action) {
            if (action >= GameAction::Count) return {};
            return bindings[(uint)action];
        }

        GameBinding* GetBinding(GameAction action, int slot) {
            if (action >= GameAction::Count) return nullptr;
            return &bindings[(uint)action][slot];
        }

        // Returns the binding label for an action
        string GetBindingLabel(GameAction action, int slot);
    };

    void ResetGamepadBindings(InputDeviceBinding& device);
    void ResetMouseBindings(InputDeviceBinding& device);
    void ResetKeyboardBindings(InputDeviceBinding& device);

    class GameBindings {
        List<InputDeviceBinding> _devices;
        InputDeviceBinding _keyboard{};
        InputDeviceBinding _mouse{};

    public:
        GameBindings() {
            _mouse.type = Input::InputType::Mouse;
            _keyboard.type = Input::InputType::Keyboard;
        }

        InputDeviceBinding& GetKeyboard() { return _keyboard; }
        InputDeviceBinding& GetMouse() { return _mouse; }

        InputDeviceBinding* GetDevice(string_view guid) {
            for (auto& device : _devices) {
                if (device.guid == guid)
                    return &device;
            }

            return nullptr;
        }

        span<InputDeviceBinding> GetDevices() { return _devices; }

        InputDeviceBinding& AddDevice(string_view guid, Input::InputType type) {
            if (auto device = GetDevice(guid))
                return *device;

            auto& device = _devices.emplace_back(string(guid), type);
            if (type == Input::InputType::Gamepad)
                ResetGamepadBindings(device);

            return device;
        }

        bool Pressed(GameAction action);

        bool Held(GameAction action);

        bool Released(GameAction action);

        // Returns the axis state summed across all controllers, scaled by sensitivity and deadzone
        float LinearAxis(GameAction action) const;
    };

    namespace Game {
        inline GameBindings Bindings;
    }
}
