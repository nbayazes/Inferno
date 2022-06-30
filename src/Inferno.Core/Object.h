#pragma once

#include "AI.h"
#include "Weapon.h"

namespace Inferno {
    //	Time assigned to immortal objects, about 9 hours.
    constexpr fix LIFE_IMMORTAL = 0x7fff;

    // Control types - what tells this object what do do
    enum class ControlType : uint8 {
        None = 0,       //doesn't move (or change movement)
        AI = 1,         //driven by AI
        Explosion = 2,  //explosion sequencer
        Flying = 4,     //the player is flying
        Slew = 5,       //slewing. Usually a player.
        FlyThrough = 6, //the flythrough system
        Weapon = 9,     //laser, etc.
        Repaircen = 10, //under the control of the repair center
        Morph = 11,     //this object is being morphed
        Debris = 12,    //this is a piece of debris
        Powerup = 13,   //animating powerup blob
        Light = 14,     //doesn't actually do anything
        Remote = 15,    //controlled by another net player
        Reactor = 16,   //the control center/main reactor 
    };

    enum class RenderType : uint8 {
        None = 0,         //does not render
        Polyobj = 1,      //a polygon model
        Fireball = 2,     //a fireball (sprite)
        Laser = 3,        //a laser
        Hostage = 4,      //a hostage
        Powerup = 5,      //a powerup
        Morph = 6,        //a robot being morphed
        WeaponVClip = 7, //a weapon that renders as a vclip
    };

    // misc object flags
    enum class ObjectFlag : uint8 {
        Exploding = 1,       //this object is exploding
        ShouldBeDead = 2,  //this object should be dead, so next time we can, we should delete this object.
        Destroyed = 4,       //this has been killed, and is showing the dead version
        Silent = 8,          //this makes no sound when it hits a wall.  Added by MK for weapons, if you extend it to other types, do it completely!
        Attached = 16,       //this object is a fireball attached to another object
        Harmless = 32,       //this object does no damage.  Added to make quad lasers do 1.5 damage as normal lasers.
        PlayerDropped = 64, //this object was dropped by the player...
    };

    enum class PhysicsFlag : int16 {
        TurnRoll = 0x01,       // roll when turning
        AutoLevel = 0x02,      // level object with closest side
        Bounce = 0x04,         // bounce (not slide) when hit will
        Wiggle = 0x08,         // wiggle while flying
        Stick = 0x10,          // object sticks (stops moving) when hits wall
        Piercing = 0x20,     // object keeps going even after it hits another object (eg, fusion cannon)
        UseThrust = 0x40,    // this object uses its thrust
        BouncedOnce = 0x80,   // Weapon has bounced once.
        FreeSpinning = 0x100, // Drag does not apply to rotation of this object
        BouncesTwice = 0x200, // This weapon bounces twice, then dies
    };

    //inline constexpr PhysicsFlag operator & (PhysicsFlag a, PhysicsFlag b) {
    //    using T = std::underlying_type_t<PhysicsFlag>;
    //    return PhysicsFlag((T)a & (T)b);
    //}

    //inline PhysicsFlag& operator &= (PhysicsFlag& a, PhysicsFlag b) {
    //    using T = std::underlying_type_t<PhysicsFlag>;
    //    return (PhysicsFlag&)((T&)a &= (T)b);
    //}

    //inline PhysicsFlag operator | (PhysicsFlag lhs, PhysicsFlag rhs) {
    //    using T = std::underlying_type_t<PhysicsFlag>;
    //    return PhysicsFlag((T)lhs | (T)rhs);
    //}

    //inline PhysicsFlag& operator |= (PhysicsFlag& lhs, PhysicsFlag rhs) {
    //    return lhs = lhs | rhs;
    //}

    enum class PowerupType : uint8 {
        ExtraLife = 0,
        Energy = 1,
        ShieldBoost = 2,
        Laser = 3,

        KeyBlue = 4,
        KeyRed = 5,
        KeyGold = 6,

        HoardOrb = 7,

        Missile1 = 10,
        Missile4 = 11,

        QuadFire = 12,

        Vulcan = 13,
        Spreadfire = 14,
        Plasma = 15,
        Fusion = 16,
        ProximityBomb = 17,
        Homing1 = 18,
        Homing4 = 19,
        SmartMissile = 20,
        Mega = 21,
        VulcanAmmo = 22,
        Cloak = 23,
        Turbo = 24,
        Invulnerability = 25,
        MEGAWOW = 27, // Cheat code
        Gauss = 28,
        Helix = 29,
        Phoenix = 30,
        Omega = 31,

        SuperLaser = 32,
        FullMap = 33,
        Converter = 34,
        AmmoRack = 35,
        Afterburner = 36,
        Headlight = 37,

        FlashMissile1 = 38,
        FlashMissile4 = 39,

        GuidedMissile1 = 40,
        GuidedMissile4 = 41,

        SmartBomb = 42,

        MercuryMissile1 = 43,
        MercuryMissile4 = 44,

        EarthshakerMissile = 45,

        FlagBlue = 46,
        FlagRed = 47,
    };

    // Object types
    enum class ObjectType : uint8 {
        None = 255,     // unused object
        SecretExitReturn = 254, // Editor only secret exit return. Not serialized.
        Wall = 0,       // Not actually an object. Used for collisions
        Fireball = 1,   // Explosion
        Robot = 2,
        Hostage = 3,
        Player = 4,
        Weapon = 5,
        Camera = 6,
        Powerup = 7,
        Debris = 8,     // remains of a destroyed robot
        Reactor = 9,
        Clutter = 11,   // Unused
        Ghost = 12,     // Dead player / spectator
        Light = 13,     // Unused
        Coop = 14,      // Co-op player
        Marker = 15,    // A marker placed by the player
    };

    enum class MovementType : uint8 {
        None = 0,
        Physics = 1,  // Affected by physics
        Spinning = 3, // Spins in place
    };

    struct VClipData {
        VClipID ID;
        float FrameTime;
        uint8 Frame;
    };

    // Object signature
    enum class ObjSig : uint16 {};

    struct PhysicsData {
        Vector3 Velocity;
        Vector3 Thrust;     // Constant force applied
        float Mass;
        float Drag;
        float Brakes;
        Vector3 AngularVelocity;
        Vector3 AngularThrust;  // Rotational acceleration
        float TurnRoll;   // Rotation caused by turn banking
        PhysicsFlag Flags;

        bool HasFlag(PhysicsFlag flag) {
            return (int16)Flags & (int16)flag;
        }
    };

    struct ModelData {
        ModelID ID;
        Array<Vector3, MAX_SUBMODELS> Angles; // angles for each subobject
        int subobj_flags; // specify which subobjs to draw
        LevelTexID TextureOverride = LevelTexID::None; // If set, draw all faces using this texture
        int alt_textures = -1; // Used for multiplayer ship colors
    };

    struct RobotAI {
        AIBehavior Behavior = AIBehavior::Normal;
        Array<sbyte, 11> Flags{};
        SegID HideSegment{}; // Segment to go to for hiding.
        short HideIndex{};   // Index in Path_seg_points
        short PathLength{};  // Length of hide path.
        int16 CurrentPathIndex{}; // Current index in path.
        bool DyingSoundPlaying{};
        ObjID DangerLaserID{};  // what is a danger laser? for dodging?
        ObjSig DangerLaserSig{};
        double DyingStartTime{}; // Time at which this robot started dying.
        AIRuntime ail{};
    };

    struct WeaponData {
        ObjectType ParentType{}; // The type of the parent of this object
        ObjID Parent = ObjID::None;     // The object's parent's number
        ObjSig ParentSig = ObjSig(-1);

        double CreationTime{}; // Absolute time of creation.
        /* hitobj_pos specifies the next position to which a value should be
         * written. That position may have a defined value if the array has
         * wrapped, but should be treated as write-only in the general case.
         *
         * hitobj_count tells how many elements in hitobj_values[] are
         * valid.  Its valid values are [0, hitobj_values.size()].  When
         * hitobj_count == hitobj_values.size(), hitobj_pos wraps around and
         * begins erasing the oldest elements first.
         */
        uint8 hitobj_pos{}, hitobj_count{};
        Array<ObjID, 83> hitobj_values{};
        ObjID TrackingTarget{}; // Object this object is tracking.
        float Multiplier{}; // Power if this is a fusion bolt
        fix64 last_afterburner_time{}; // Time at which this object last created afterburner blobs.
    };

    struct ExplosionInfo {
        float SpawnTime{};  // when lifeleft is < this, spawn another
        float DeleteTime{}; // when to delete object
        ObjID DeleteObject{}; // and what object to delete
        ObjID Parent = ObjID::None;     // explosion is attached to this object
        ObjID PrevAttach = ObjID::None; // previous explosion in attach list
        ObjID NextAttach = ObjID::None; // next explosion in attach list
    };

    struct LightInfo {
        float Intensity{};
    };

    struct PowerupControlInfo {
        fix64 CreationTime{};
        int Count{}; // how many/much we pick up (vulcan cannon only?)
        bool IsSpew{}; // Player spew?
    };

    // Descent 2
    struct ReactorControlInfo {
        // Orientation and position of guns
        Array<Vector3, 8> GunPoints, GunDirs;
    };

    struct PlayerData {
        ObjID objnum;   // What object number this player is.
        float energy;     // Amount of energy remaining.
        float homing_object_dist; // Distance of nearest homing object.
        float Fusion_charge;
        float Omega_charge = 1;
        float Omega_recharge_delay;
        uint32 PowerupFlags;
        ObjID Killer = ObjID::None; // who killed the player
        uint16 vulcan_ammo;
        uint16 primary_weapon_flags;
        uint16 secondary_weapon_flags;
        bool Player_eggs_dropped;
        bool FakingInvul;
        bool lavafall_hiss_playing;
        uint8 missile_gun;
        LaserLevel LaserLevel;
        Array<uint8, 10> secondary_ammo; // How much ammo of each type.
        uint8 Spreadfire_toggle;
        uint8 Primary_last_was_super;
        uint8 Secondary_last_was_super;
        uint8 Helix_orientation;
        int16 net_killed_total; // Number of times killed total
        int16 net_kills_total;  // Number of net kills total
        int16 KillGoalCount;    // Num of players killed this level

        union {
            struct {
                int score;				// Current score.
                int last_score;			// Score at beginning of current level.
                uint16 hostages_rescued_total; // Total number of hostages rescued.
                uint8 hostages_on_board;
            } mission;
            struct {
                uint8 orbs;
            } hoard;
        };
        enum {
            max_hoard_orbs = 12,
        };

        float cloak_time;         // Time cloaked
        float invulnerable_time;  // Time invulnerable
        float Next_flare_fire_time;
        float Next_laser_fire_time;
        float Next_missile_fire_time;
        float Last_bumped_local_player;
        float Auto_fire_fusion_cannon_time;
    };

    struct ControlData {
        ControlType Type = ControlType::None;
        union {
            struct ExplosionInfo Explosion; //debris also uses this
            struct LightInfo Light;
            struct PowerupControlInfo Powerup;
            struct RobotAI AI;
            struct ReactorControlInfo Reactor;
            struct WeaponData Weapon;
            struct PlayerData Player {}; // be sure to init using the largest struct
        };
    };

    struct RenderData {
        RenderType Type;
        union {
            struct ModelData Model {}; // polygon model
            struct VClipData VClip;   // vclip
        };
    };

    struct MovementData {
        MovementType Type = MovementType::None;
        union {
            struct PhysicsData Physics {}; // a physics object
            struct Vector3 SpinRate; // for spinning objects
        };
    };

    struct ContainsData {
        ObjectType Type = ObjectType::None;  // Type of object this object contains (eg, spider contains powerup)
        int8 ID = 0;    // ID of object this object contains (eg, id = blue type = key)
        int8 Count = 0; // number of objects of type:id this object contains
    };

    struct Object {
        ObjSig Signature{};     // Every object ever has a unique signature
        ObjectType Type{};
        int8 ID{};              // Index in powerup, robot, etc. list (subtype)
        ObjectFlag Flags{};          // misc flags
        SegID Segment{};        // segment number containing object
        float Radius = 2; // radius of object for collision detection
        float Shields = 100;    // Starts at maximum, when <0, object dies..
        Vector3 last_pos{};  // where object was last frame
        ContainsData Contains{};
        sbyte matcen_creator{}; // Materialization center that created this object, high bit set if matcen-created
        fix Life = LIFE_IMMORTAL; // how long until despawn
        MovementData Movement;
        RenderData Render;
        ControlData Control;

        Matrix Transform, PrevTransform;
        Vector3 Position() const { return Transform.Translation(); }
        Vector3 Position(float alpha) const {
            auto vec = Transform.Translation() - PrevTransform.Translation();
            return PrevTransform.Translation() + vec * alpha;
        }

        Matrix GetTransform(float alpha) const {
            Matrix m = PrevTransform;
            m.Translation(Position(alpha));
            // todo: rotation
            return m;
        }
    };
}
