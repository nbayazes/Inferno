#pragma once

#include "AI.h"
#include "Weapon.h"

namespace Inferno {
    // Control types - what tells this object what do do
    enum class ControlType : uint8 {
        None = 0,       // No movement
        AI = 1,
        Explosion = 2,  // Explosion sequence
        Flying = 4,
        Slew = 5,       // Editor camera?
        FlyThrough = 6,
        Weapon = 9,
        Repaircen = 10, // Unused
        Morph = 11,     // Matcen morphing in
        Debris = 12,    // Debris of destroyed robot
        Powerup = 13,
        Light = 14,     // Unused
        Remote = 15,    // Multiplayer
        Reactor = 16,
    };

    enum class RenderType : uint8 {
        None = 0,
        Model = 1,          // Object model  
        Fireball = 2,       // Animated effect
        Laser = 3,          // Weapon using a model?
        Hostage = 4,        // Axis aligned sprite
        Powerup = 5,        // Sprite
        Morph = 6,          // Robot being constructed by a matcen
        WeaponVClip = 7,    // Animated weapon projectile
    };

    // misc object flags
    enum class ObjectFlag : uint8 {
        Exploding = 1,
        ShouldBeDead = 2,   // Scheduled for deletion
        Destroyed = 4,      // this has been killed, and is showing the dead version
        Silent = 8,         // No sound when colliding
        Attached = 16,      // this object is a fireball attached to another object
        Harmless = 32,      // Does no damage
        PlayerDropped = 64, // Dropped by player (death?)
    };

    enum class PhysicsFlag : int16 {
        None = 0,
        TurnRoll = 0x01,        // roll when turning
        AutoLevel = 0x02,       // align object with nearby side
        Bounce = 0x04,          // bounce instead of slide when hitting a wall
        Wiggle = 0x08,          // wiggle while flying
        Stick = 0x10,           // object sticks (stops moving) when hits wall
        Piercing = 0x20,        // object keeps going even after it hits another object
        UseThrust = 0x40,       // this object uses its thrust
        BouncedOnce = 0x80,     // Weapon has bounced once
        FreeSpinning = 0x100,   // Drag does not apply to rotation of this object
        BouncesTwice = 0x200,   // This weapon bounces twice, then dies
    };

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
        Fireball = 1,   // Explosion effect. no collision?
        Robot = 2,
        Hostage = 3,
        Player = 4,
        Weapon = 5, // A projectile from a weapon?
        Camera = 6,
        Powerup = 7,
        Debris = 8,     // remains of a destroyed robot
        Reactor = 9,
        Clutter = 11,   // Unused, would be for random clutter placed in the level like barrels or boxes
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
        VClipID ID = VClipID::None;
        float FrameTime = 0;
        uint8 Frame = 0;
        float Rotation = 0;
    };

    // Object signature
    enum class ObjSig : uint16 {};

    struct PhysicsData {
        Vector3 Velocity;
        Vector3 InputVelocity; // Velocity after input but before collision
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

        void SmartMineFlag(bool value) {
            if (value) Flags[4] |= 0x02;
            else Flags[4] &= ~0x02;
        }

        bool SmartMineFlag() { return Flags[4] & 0x02; }
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
        Color Emissive;
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
        ContainsData Contains{};
        sbyte matcen_creator{}; // Materialization center that created this object, high bit set if matcen-created
        float Lifespan = FLT_MAX; // how long before despawning
        ObjID Parent = ObjID::None; // Parent for projectiles, maybe attached objects

        MovementData Movement;
        RenderData Render;
        ControlData Control;

        Vector3 Position, LastPosition;
        Matrix3x3 Rotation, LastRotation;

        Matrix GetTransform() const {
            Matrix m(Rotation);
            m.Translation(Position);
            return m;
        }

        Matrix GetLastTransform() const {
            Matrix m(LastRotation);
            m.Translation(LastPosition);
            return m;
        }

        void SetTransform(const Matrix& m) {
            DirectX::XMStoreFloat3x3(&Rotation, m);
            Position = m.Translation();
        }

        // Transform object position and rotation by a matrix
        void Transform(const Matrix& m) {
            Rotation *= m;
            Position = Vector3::Transform(Position, m);
        }

        static bool IsAlive(const Object& obj) { return obj.Lifespan >= 0; }
    };
}
