#pragma once

#include "AI.h"

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
        None = 0,           // Invisible
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

    enum class PowerupID : uint8 {
        ExtraLife = 0,
        Energy = 1,
        ShieldBoost = 2,
        Laser = 3,

        KeyBlue = 4,
        KeyRed = 5,
        KeyGold = 6,

        HoardOrb = 7,

        Concussion1 = 10,
        Concussion4 = 11,

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

    constexpr float CLOAK_TIME = 10.0f;
    constexpr float INVULN_TIME = 30.0f;

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
        None = 0, // No physics or movement
        Physics = 1,  // Affected by physics
        Spinning = 3, // Spins in place
    };

    struct VClipData {
        VClipID ID = VClipID::None;
        float FrameTime = 0;
        uint8 Frame = 0;
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
        Vector3 SpinRate; // Fixed speed rotation. Was part of Spinning type.
    };

    struct ModelData {
        ModelID ID = ModelID::None;
        Array<Vector3, MAX_SUBMODELS> Angles{}; // angles for each subobject
        int subobj_flags = 0; // specify which subobjs to draw
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

        bool SmartMineFlag() const { return Flags[4] & 0x02; }

        bool IsCloaked() const { return Flags[6]; }
    };

    struct WeaponData {
        ObjectType ParentType{}; // The type of the parent of this object
        ObjID Parent = ObjID::None;     // The object's parent's number
        ObjSig ParentSig = ObjSig(-1);

        float AliveTime = 0; // How long the weapon has been alive
        bool SineMovement = false;

        ObjID TrackingTarget = ObjID::None; // Object this object is tracking.
        float Multiplier{}; // Power if this is a fusion bolt
        float SoundDelay = 0;
        bool DetonateMine = false;

        uint8 HitIndex = 0;
        Array<ObjSig, 10> RecentHits{}; // to prevent piercing weapons from hitting the same obj multiple times

        void AddRecentHit(ObjSig id) {
            if (HitIndex >= RecentHits.size())
                HitIndex = 0;

            RecentHits[HitIndex++] = id;
        }
    };

    struct ExplosionObjectInfo {
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
        Array<Vector3, 8> GunPoints{}, GunDirs{};
    };

    struct ControlData {
        ControlType Type = ControlType::None;
        union {
            ExplosionObjectInfo Explosion; //debris also uses this
            LightInfo Light;
            PowerupControlInfo Powerup;
            RobotAI AI{}; // be sure to init using the largest struct
            WeaponData Weapon;
            //struct ReactorControlInfo Reactor; // Not in original data
        };
    };

    constexpr auto sz = sizeof(RobotAI);

    struct RenderData {
        RenderType Type;
        Color Emissive;
        float Rotation = 0;
        union {
            ModelData Model{}; // polygon model
            VClipData VClip;   // vclip
        };
    };

    struct ContainsData {
        ObjectType Type = ObjectType::None;  // Type of object this object contains (eg, spider contains powerup)
        int8 ID = 0;    // ID of object this object contains (eg, id = blue type = key)
        int8 Count = 0; // number of objects of type:id this object contains
    };

    constexpr float NEVER_THINK = -1;

    enum class ObjectMask {
        Any = 0, // No masking
        Enemy = 1 >> 1, // Reactor or robot
        Player = 1 >> 2, // Player or Coop
        Powerup = 1 >> 3 // Powerup or hostage
    };

    struct Object {
        ObjSig Signature{};     // Unique signature for each object
        ObjectType Type{};
        int8 ID{};              // Index in powerups, robots, etc. Also used for player and co-op IDs.
        ObjectFlag Flags{};
        SegID Segment{};        // segment number containing object
        float Radius = 2;       // radius of object for collision detection
        float HitPoints = 100;
        ContainsData Contains{};
        sbyte matcen_creator{}; // Materialization center that created this object, high bit set if matcen-created
        float Lifespan = FLT_MAX; // how long before despawning
        ObjID Parent = ObjID::None; // Parent for projectiles, maybe attached objects

        MovementType Movement;
        PhysicsData Physics;
        RenderData Render;
        ControlData Control;

        Vector3 LastHitForce; // Tracks the force applies by recent hits

        Vector3 Position; // The current "real" position
        Matrix3x3 Rotation; // The current "real" rotation
        Vector3 LastPosition; // The position from the previous update. Used for graphics interpolation.
        Matrix3x3 LastRotation; // The rotation from the previous update. Used for graphics interpolation.

        float NextThinkTime = NEVER_THINK;

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

        // Gets the render position
        Vector3 GetPosition(float lerp) const {
            return Vector3::Lerp(LastPosition, Position, lerp);
        }

        // Gets the render rotation
        Matrix GetRotation(float lerp) const {
            return Matrix::Lerp(LastRotation, Rotation, lerp);
        }

        // Transform object position and rotation by a matrix
        void Transform(const Matrix& m) {
            Rotation *= m;
            Position = Vector3::Transform(Position, m);
        }

        static bool IsAliveFn(const Object& obj) {
            return obj.Lifespan >= 0 && obj.HitPoints >= 0;
        }

        void ApplyDamage(float damage) {
            HitPoints -= damage;
            if (HitPoints < 0)
                Destroy();
        }

        void Destroy() { Flags |= ObjectFlag::Destroyed; }

        bool IsAlive() const { return IsAliveFn(*this); }

        float Distance(const Object& obj) const {
            return Vector3::Distance(Position, obj.Position);
        }

        bool PassesMask(ObjectMask mask) const {
            if (mask == ObjectMask::Any) return true;

            switch (Type) {
                case ObjectType::Reactor:
                case ObjectType::Robot:
                    return HasFlag(mask, ObjectMask::Enemy);

                case ObjectType::Player:
                case ObjectType::Coop:
                    return HasFlag(mask, ObjectMask::Player);

                case ObjectType::Powerup:
                case ObjectType::Hostage:
                    return HasFlag(mask, ObjectMask::Powerup);

                default:
                    return false;
            }
        }
    };
}
