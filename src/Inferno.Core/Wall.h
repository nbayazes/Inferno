#pragma once

#include "Types.h"
#include "Utility.h"

namespace Inferno {
    enum class WallFlag : uint8 {
        None,
        Destroyed = BIT(0), // Converts a blastable wall to an illusionary wall
        DoorOpened = BIT(1), // Door is opened and no longer has collision
        DoorLocked = BIT(3), // Door cannot be opened
        DoorAuto = BIT(4), // Door closes automatically
        IllusionOff = BIT(5), // Illusionary wall off state
        Switch = BIT(6), // Unused, maybe Exploding state
        BuddyProof = BIT(7)
    };

    enum class WallState : uint8 {
        Closed = 0,
        DoorOpening = 1,
        DoorWaiting = 2,
        DoorClosing = 3,
        DoorOpen = 4,
        Cloaking = 5,
        Decloaking = 6
    };

    enum class WallKey : uint8 {
        None = BIT(0),
        Blue = BIT(1),
        Red = BIT(2),
        Gold = BIT(3),
    };

    enum class WallType : uint8 {
        None = 0,
        Destroyable = 1, // Hostage and guidebot doors
        Door = 2, // Solid wall. Opens when triggered.
        Illusion = 3, // Wall with no collision
        Open = 4, // Invisible wall with no collision (Fly-through trigger)
        Closed = 5, // Solid wall. Fades in or out when triggered.
        WallTrigger = 6, // For shootable triggers on a segment side
        Cloaked = 7, // Solid, transparent wall that fades in or out when triggered. Similar to Closed but untextured.
    };

    struct Wall {
        Tag Tag;
        WallType Type = WallType::None;
        float HitPoints = 0; // For destroyable walls
        uint16 ExplodeTimeElapsed = 0;
        WallID LinkedWall = WallID::None; // only used at runtime for doors, should be saved as none from editor.
        WallFlag Flags = WallFlag::None;
        WallState State = WallState::Closed;
        TriggerID Trigger = TriggerID::None; // Trigger for this wall
        DClipID Clip = DClipID::None; // Animation to play for a door
        WallKey Keys = WallKey::None; // Required keys to open a door
        TriggerID ControllingTrigger = TriggerID::None; // Which trigger causes something to happen here. Should be saved as none from editor.
        sbyte cloak_value = 0; // Fade percentage if this wall is cloaked

        Option<bool> BlocksLight; // Editor override

        bool IsValid() const {
            return Tag.Segment != SegID::None;
        }

        // Returns true if wall collides with objects
        bool IsSolid() const {
            if (Type == WallType::Illusion) return false;
            if (Type == WallType::Door && HasFlag(WallFlag::DoorOpened)) return false;
            if (Type == WallType::Destroyable && HasFlag(WallFlag::Destroyed)) return false;
            if (Type == WallType::Open) return false;
            return true;
        }

        bool HasFlag(WallFlag flag) const { return bool(Flags & flag); }
        void SetFlag(WallFlag flag) { Flags |= flag; }
        void ClearFlag(WallFlag flag) { Flags &= ~flag; }

        void SetFlag(WallFlag flag, bool state) {
            if (state) SetFlag(flag);
            else ClearFlag(flag);
        }

        bool IsKeyDoor() const {
            if (Type != WallType::Door) return false;
            return Keys > WallKey::None;
        }

        static constexpr auto CloakStep = 1.0f / 31.0f;

        constexpr float CloakValue() const { return float(cloak_value % 32) * CloakStep; }

        constexpr void CloakValue(float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            cloak_value = sbyte(value / CloakStep);
        }
    };

    struct ActiveDoor {
        WallID Front = WallID::None;
        WallID Back = WallID::None;
        float Time = -1;
        int Parts = 0;
        bool IsAlive() const { return Time >= 0; }
    };

    constexpr int16 MAX_TRIGGER_TARGETS = 10;

    enum class TriggerType : uint8 {
        OpenDoor = 0,
        CloseDoor = 1,
        Matcen = 2,
        Exit = 3,
        SecretExit = 4,
        IllusionOff = 5,
        IllusionOn = 6,
        UnlockDoor = 7,
        LockDoor = 8,
        OpenWall = 9, // Wall Closed -> Open
        CloseWall = 10, // Wall Open -> Closed
        IllusoryWall = 11, // Makes a wall illusory (fly-through)
        LightOff = 12,
        LightOn = 13,
        NumTriggerTypes
    };

    // Trigger flags for Descent 1
    enum class TriggerFlagD1 : uint16 {
        None,
        OpenDoor = BIT(0), // Control Trigger
        ShieldDamage = BIT(1), // Shield Damage Trigger. Not properly implemented
        EnergyDrain = BIT(2), // Energy Drain Trigger. Not properly implemented
        Exit = BIT(3), // End of level Trigger
        On = BIT(4), // Whether Trigger is active. Not properly implemented
        OneShot = BIT(5), // If Trigger can only be triggered once. Not properly implemented
        Matcen = BIT(6), // Trigger for materialization centers
        IllusionOff = BIT(7), // Switch Illusion OFF trigger
        SecretExit = BIT(8), // Exit to secret level
        IllusionOn = BIT(9), // Switch Illusion ON trigger
    };

    enum class TriggerFlag : uint8 {
        None,
        NoMessage = BIT(0),
        OneShot = BIT(1),
        Disabled = BIT(2)
    };

    struct Trigger {
        TriggerType Type = TriggerType::OpenDoor; // D2 type
        union {
            TriggerFlag Flags; // D2 flags
            TriggerFlagD1 FlagsD1{}; // D1 flags
        };

        int32 Value = 0; // used for shield and energy drain triggers in D1
        int32 Time = -1; // reduced every frame by passed time until 0
        //int8 linkNum = 0; // unused
        //int8 targetCount = 0;
        ResizeArray<Tag, MAX_TRIGGER_TARGETS> Targets{};

        bool HasFlag(TriggerFlag flag) const { return bool(Flags & flag); }
        void SetFlag(TriggerFlag flag) { Flags |= flag; }

        bool HasFlag(TriggerFlagD1 flag) const { return bool(FlagsD1 & flag); }
        void SetFlag(TriggerFlagD1 flag) { Inferno::SetFlag(FlagsD1, flag); }
    };
}
