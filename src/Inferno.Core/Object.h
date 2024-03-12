#pragma once

#include "Types.h"
#include "AI.h"
#include "Dynamics.h"
#include "Polymodel.h"
#include "Weapon.h"

namespace Inferno {
    // Control types - what tells this object what do do
    enum class ControlType : uint8 {
        None = 0, // No movement
        AI = 1,
        Explosion = 2, // Explosion sequence
        Flying = 4,
        Slew = 5, // Editor camera?
        FlyThrough = 6,
        Weapon = 9,
        Repaircen = 10, // Unused
        Morph = 11, // Matcen morphing in
        Debris = 12, // Debris of destroyed robot
        Powerup = 13,
        Light = 14, // Unused
        Remote = 15, // Multiplayer
        Reactor = 16,
    };

    enum class RenderType : uint8 {
        None = 0, // Invisible
        Model = 1, // Object model  
        Fireball = 2, // Animated effect
        Laser = 3, // Weapon using a model?
        Hostage = 4, // Axis aligned sprite
        Powerup = 5, // Sprite
        Morph = 6, // Robot being constructed by a matcen
        WeaponVClip = 7, // Animated weapon projectile
    };

    // misc object flags
    enum class ObjectFlag : uint16 {
        None = 0,
        Exploding = 1 << 0, // Object is exploding with a delay. Prevents exploding immediately when hp reaches 0.
        Dead = 1 << 1, // Free to be reused
        Destroyed = 1 << 2, // Object has been destroyed from damage. Used by reactor to change model appearance.
        Silent = 1 << 3, // No sound when colliding
        Attached = 1 << 4, // Object is attached to another object or wall. Disables hit testing.
        Harmless = 1 << 5, // Does no damage
        PlayerDropped = 1 << 6, // Dropped by player (death?)
        AlwaysUpdate = 1 << 7, // Always update this object regardless of visibility. Thief, Weapons
        Updated = 1 << 8, // Was updated this frame
    };

    enum class PhysicsFlag : uint16 {
        None = 0,
        TurnRoll = 1 << 0, // roll when turning
        AutoLevel = 1 << 1, // align object with nearby side
        Bounce = 1 << 2, // bounce instead of slide when hitting a wall
        Wiggle = 1 << 3, // wiggle while flying
        Stick = 1 << 4, // object sticks (stops moving) when hits wall
        Piercing = 1 << 5, // object keeps going even after it hits another object
        UseThrust = 1 << 6, // this object uses its thrust
        BouncedOnce = 1 << 7, // Weapon has bounced once
        FixedAngVel = 1 << 8, // Drag does not apply to rotation of this object
        BouncesTwice = 1 << 9, // This weapon bounces twice, then dies
        SphereCollidePlayer = 1 << 10, // Use spheres when colliding with the player
        PointCollideWalls = 1 << 11, // Use raycasting against walls, otherwise use spheres
        Gravity = 1 << 12, // Apply gravity
        NoCollideRobots = 1 << 13, // Ignore collisions with robots, used by robots contained in robots.
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
        ProximityMine = 17,
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

        SmartMine = 42,

        MercuryMissile1 = 43,
        MercuryMissile4 = 44,

        EarthshakerMissile = 45,

        FlagBlue = 46,
        FlagRed = 47,
    };

    // Object types
    enum class ObjectType : uint8 {
        None = 255, // unused object
        SecretExitReturn = 254, // Editor only secret exit return. Not serialized.
        Wall = 0, // Not actually an object. Used for collisions
        Fireball = 1, // Explosion effect. no collision?
        Robot = 2,
        Hostage = 3,
        Player = 4,
        Weapon = 5, // A projectile from a weapon?
        Camera = 6,
        Powerup = 7,
        Debris = 8, // remains of a destroyed robot
        Reactor = 9,
        Clutter = 11, // Unused, would be for random clutter placed in the level like barrels or boxes
        Ghost = 12, // Dead player / spectator
        Light = 13, // Unused
        Coop = 14, // Co-op player
        Marker = 15, // A marker placed by the player
        Building = 16, // D3
        Door = 17 // D3
    };

    enum class MovementType : uint8 {
        None = 0, // No physics or movement
        Physics = 1, // Affected by physics
        Spinning = 3, // Spins in place
    };

    struct VClipData {
        VClipID ID = VClipID::None;
        float FrameTime = 0;
        uint8 Frame = 0;
        Color DirectLight; // Used by unlit sprites for dynamic lighting
    };

    struct PhysicsData {
        Vector3 Velocity, PrevVelocity;
        Vector3 Thrust; // Constant force applied
        float Mass;
        float Drag;
        float Brakes;
        Vector3 AngularVelocity; // Rotational velocity (pitch, yaw, roll)
        Vector3 AngularAcceleration;
        Vector3 AngularThrust; // Rotational acceleration from player input (pitch, yaw, roll)
        float TurnRoll; // Rotation caused by turn banking
        PhysicsFlag Flags{};
        Vector3 SpinRate; // Fixed speed rotation. Was part of Spinning type.
        int Bounces = 0; // Number of remaining bounces
        float Wiggle = 0; // Amplitude of wiggle
        float WiggleRate = 1; // How long one wiggle takes
        SecondOrderDynamics<float> BankState = { 1, 1, 0, 0 };

        bool CanBounce() const {
            return Bounces > 0 || HasFlag(Flags, PhysicsFlag::Bounce);
        }
    };

    struct ModelData {
        ModelID ID = ModelID::None;
        bool Outrage = false;
        Array<Vector3, MAX_SUBMODELS> Angles{}; // angles for each subobject
        int subobj_flags = 0; // specify which subobjs to draw
        LevelTexID TextureOverride = LevelTexID::None; // If set, draw all faces using this texture
        int alt_textures = -1; // Used for multiplayer ship colors
    };

    struct RobotAI {
        AIBehavior Behavior = AIBehavior::Normal;
        Array<sbyte, 11> Flags{};
        SegID HideSegment{}; // Segment to go to for hiding. Also used for roaming / station behavior.
        short HideIndex{}; // Index in Path_seg_points
        short PathLength{}; // Length of hide path.
        int16 CurrentPathIndex{}; // Current index in path.

        void SmartMineFlag(bool value) {
            if (value) Flags[4] |= 0x02;
            else Flags[4] &= ~0x02;
        }

        bool SmartMineFlag() const { return Flags[4] & 0x02; }

        bool IsCloaked() const { return Flags[6]; }
    };

    struct WeaponData {
        ObjectType ParentType{}; // The type of the parent of this object. Important for triggers and opening doors.
        ObjRef Parent;

        float AliveTime = 0; // How long the weapon has been alive
        bool SineMovement = false;

        ObjRef TrackingTarget = {}; // Object this weapon is tracking.
        float Multiplier{}; // Power if this is a fusion bolt
        bool DetonateMine = false;
        uint16 Flags = 0;

        uint8 HitIndex = 0;
        Array<ObjSig, 10> RecentHits{}; // to prevent piercing weapons from hitting the same obj multiple times

        void AddRecentHit(ObjSig id) {
            if (HitIndex >= RecentHits.size())
                HitIndex = 0;

            RecentHits[HitIndex++] = id;
        }
    };

    struct ExplosionObjectInfo {
        float SpawnTime{}; // when lifeleft is < this, spawn another
        float DeleteTime{}; // when to delete object
        ObjID DeleteObject{}; // and what object to delete
        ObjID Parent = ObjID::None; // explosion is attached to this object
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
            RobotAI AI;
            WeaponData Weapon{}; // be sure to init using the largest struct
            //struct ReactorControlInfo Reactor; // Not in original data
        };
    };

    struct RenderData {
        RenderType Type;
        Color Emissive;
        float Rotation = 0;

        union {
            ModelData Model{}; // polygon model
            VClipData VClip; // vclip
        };
    };

    struct ContainsData {
        ObjectType Type = ObjectType::None; // Type of object this object contains
        int8 ID = 0; // ID of object this object contains (type = powerup, id = blue key)
        uint8 Count = 0; // number of objects of type:id this object contains
    };

    constexpr double NEVER_THINK = -1;

    enum class ObjectMask : uint8 {
        None = 0,
        Robot = 1 << 0, // Reactor or robot
        Player = 1 << 1, // Player or Coop
        Powerup = 1 << 2, // Powerup or hostage
        Weapon = 1 << 3,
        Mine = 1 << 4 // Subtype of weapon. prox, smart or editor placed mine
    };

    constexpr double MAX_OBJECT_LIFE = 3600 * 100; // 100 hours

    enum class EffectFlags : uint8 {
        None,
        Cloaked = 1 << 0,
        Invulnerable = 1 << 1,
        PhaseIn = 1 << 2, // Becoming solid
        PhaseOut = 1 << 3,
        Ignited = 1 << 4
    };

    enum class Faction : uint8 {
        None = 0,
        Neutral = 1 << 0,
        Player = 1 << 1,
        Robot = 1 << 2
    };

    struct ObjectEffects {
        EffectFlags Flags;

        float CloakTimer; // Elapsed cloaking
        float CloakDuration; // How long cloaking lasts. -1 for forever.
        float CloakFlickerTimer; // Firing while cloaked causes the cloak to flicker

        float InvulnerableTimer;
        float InvulnerableDuration; // How long invulnerability lasts. -1 for forever.

        Color PhaseColor;
        float PhaseTimer;
        float PhaseDuration;

        float IgniteDuration;

        float GetCloakPercent() const { return Saturate(CloakTimer / CloakDuration); }

        // Returns 0 when solid, 1 when completely phased out
        float GetPhasePercent() const {
            if (HasFlag(Flags, EffectFlags::PhaseOut))
                return Saturate(PhaseTimer / PhaseDuration);
            else if (HasFlag(Flags, EffectFlags::PhaseIn))
                return 1 - Saturate(PhaseTimer / PhaseDuration);

            return 0;
        }
    };

    struct Object {
        ObjSig Signature{}; // Unique signature for each object
        ObjectType Type = ObjectType::None;
        int8 ID{}; // Index in powerups, robots, etc. Also used for player and co-op IDs.
        ObjectFlag Flags{};
        SegID Segment = SegID::None; // segment number containing object
        ObjectEffects Effects{};
        Faction Faction = Faction::Neutral; // What is this object allied with?

        float Radius = 2; // radius of object for collision detection
        float HitPoints = 100; // Objects are destroyed when hitpoints go under 0
        float MaxHitPoints = 100; // Starting maximum hit points
        ContainsData Contains{};
        MatcenID SourceMatcen = MatcenID::None; // Materialization center that created this object
        double Lifespan = MAX_OBJECT_LIFE; // how long before despawning. Missiles explode when expiring.
        ObjRef Parent; // Parent for projectiles, maybe attached objects

        MovementType Movement;
        PhysicsData Physics;
        RenderData Render;
        ControlData Control;

        Vector3 LastHitForce; // Tracks the force of the last hit. Used for debris.
        ObjSig LastHitObject = ObjSig::None; // Hack used by explosive weapons to fix rotation of direct hits

        Vector3 Position; // The current "real" position
        Matrix3x3 Rotation; // The current "real" rotation
        Vector3 PrevPosition; // The position from the previous update. Used for graphics interpolation.
        Matrix3x3 PrevRotation; // The rotation from the previous update. Used for graphics interpolation.

        // Lighting chunk
        struct {
            Color Color;
            float Radius = 0;
            DynamicLightMode Mode{};
        } Light;

        LerpedColor Ambient;

        double NextThinkTime = NEVER_THINK; // General purpose think timer
        float Scale = 1.0; // Model / sprite scale

        Matrix GetTransform() const {
            Matrix m(Rotation);
            m.Translation(Position);
            return m;
        }

        Matrix GetPrevTransform() const {
            Matrix m(PrevRotation);
            m.Translation(PrevPosition);
            return m;
        }

        Matrix GetTransform(float lerp) const {
            return Matrix::Lerp(GetPrevTransform(), GetTransform(), lerp);
        }

        void SetTransform(const Matrix& m) {
            DirectX::XMStoreFloat3x3(&Rotation, m);
            Position = m.Translation();
        }

        // Gets the render position
        Vector3 GetPosition(float lerp) const {
            return Vector3::Lerp(PrevPosition, Position, lerp);
        }

        // Gets the render rotation
        Matrix GetRotation(float lerp) const {
            return Matrix::Lerp(PrevRotation, Rotation, lerp);
        }

        // Transform object position and rotation by a matrix
        void Transform(const Matrix& m) {
            Rotation *= m;
            Position = Vector3::Transform(Position, m);
        }

        void ApplyDamage(float damage) {
            HitPoints -= damage;
        }

        bool IsAlive() const { return !HasFlag(Flags, ObjectFlag::Dead); }

        bool IsCloaked() const { return HasFlag(Effects.Flags, EffectFlags::Cloaked); }
        bool IsCloakEffective() const { return HasFlag(Effects.Flags, EffectFlags::Cloaked) && Effects.CloakFlickerTimer <= 0; }

        bool IsInvulnerable() const { return HasFlag(Effects.Flags, EffectFlags::Invulnerable); }

        void PhaseIn(float duration, const Color& color) {
            ASSERT(duration > 0);
            SetFlag(Effects.Flags, EffectFlags::PhaseIn);
            ClearFlag(Effects.Flags, EffectFlags::PhaseOut);
            Effects.PhaseDuration = duration;
            Effects.PhaseTimer = 0;
            Effects.PhaseColor = color;
        }

        void PhaseOut(float duration, const Color& color) {
            ASSERT(duration > 0);
            SetFlag(Effects.Flags, EffectFlags::PhaseOut);
            ClearFlag(Effects.Flags, EffectFlags::PhaseIn);
            Effects.PhaseDuration = duration;
            Effects.PhaseTimer = 0;
            Effects.PhaseColor = color;
        }

        bool IsPhasing() const {
            return HasFlag(Effects.Flags, EffectFlags::PhaseIn) || HasFlag(Effects.Flags, EffectFlags::PhaseOut);
        }

        // Returns true if homing weapons can lock onto this object
        bool CanHomingLock() const {
            return !IsPhasing() and !IsCloaked();
        }

        float Distance(const Object& obj) const {
            return Vector3::Distance(Position, obj.Position);
        }

        bool PassesMask(ObjectMask mask) const {
            if (mask == ObjectMask::None) return false;

            switch (Type) {
                case ObjectType::Reactor:
                case ObjectType::Robot:
                    return HasFlag(mask, ObjectMask::Robot);

                case ObjectType::Player:
                case ObjectType::Coop:
                    return HasFlag(mask, ObjectMask::Player);

                case ObjectType::Powerup:
                case ObjectType::Hostage:
                    return HasFlag(mask, ObjectMask::Powerup);

                case ObjectType::Weapon:
                    if (HasFlag(mask, ObjectMask::Mine))
                        return ID == (int8)WeaponID::ProxMine || ID == (int8)WeaponID::SmartMine || ID == (int8)WeaponID::LevelMine;
                    else
                        return HasFlag(mask, ObjectMask::Weapon);

                default:
                    return false;
            }
        }

        bool IsInFaction(Inferno::Faction faction) const {
            if (faction == Faction::None) return false;
            return HasFlag(Faction, faction);
        }

        bool IsPowerup(PowerupID id) const {
            if (Type != ObjectType::Powerup) return false;
            return ID == (int)id;
        }

        bool IsPowerup() const { return Type == ObjectType::Powerup; }
        bool IsPlayer() const { return Type == ObjectType::Player; }
        bool IsCoop() const { return Type == ObjectType::Coop; }
        bool IsRobot() const { return Type == ObjectType::Robot; }
        bool IsWeapon() const { return Type == ObjectType::Weapon; }
        bool IsReactor() const { return Type == ObjectType::Reactor; }
    };

    // Point used for AI navigation
    struct NavPoint {
        SegID Segment = SegID::None;
        Vector3 Position;

        NavPoint() = default;
        NavPoint(SegID seg, const Vector3& pos) : Segment(seg), Position(pos) {}
        NavPoint(const Object& obj) : Segment(obj.Segment), Position(obj.Position) {}
    };
}
