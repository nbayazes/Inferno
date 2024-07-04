#pragma once
#include "Types.h"
#include "Object.h"

namespace Inferno {
    constexpr auto MAX_ROBOT_JOINTS = 1600;
    constexpr uint8 MAX_GUNS = 8;
    constexpr uint8 N_ANIM_STATES = 5;

    // joint lookup in the robot joints game data
    struct JointLookup {
        short Count;
        short Offset;
    };

    struct RobotDifficultyInfo {
        float FieldOfView; // How well the robot sees around itself in radians
        float FireDelay, FireDelay2;
        float TurnTime; // time in seconds to rotate 360 degrees in a dimension
        float Speed; // How quickly the robot moves
        float CircleDistance; // preferred distance from the player
        uint8 ShotCount; // number of primary shots to fire per delay
        uint8 EvadeSpeed; // rate at which robot can evade shots, 0=none, 4=very fast
        float MeleeDamage; // Damage of a melee swing
    };
    
    enum class CloakType : sbyte { None, Always, WhenNotFiring };
    enum class AttackType : sbyte { Ranged, Melee };

    struct RobotInfo {
        ModelID Model;
        Array<Vector3, MAX_GUNS> GunPoints{}; // where each gun model is
        Array<uint8, MAX_GUNS> GunSubmodels{};   // which submodel each gun is attached to

        VClipID ExplosionClip1, ExplosionClip2;

        WeaponID WeaponType; // Primary weapon
        WeaponID WeaponType2 = WeaponID::None;    // Secondary weapon. D2 only
        uint8 Guns;            // how many different gun positions

        ContainsData Contains;
        sbyte ContainsChance;   // Probability that this instance will contain something in N/16
        sbyte Kamikaze;        // Rushes at player and explodes on contact

        short Score;
        int8 ExplosionStrength; // Radius and force of explosion on death
        int8 EnergyDrain;    // Energy drained when touched

        float Lighting;
        float HitPoints;

        float Mass, Drag;
        float Radius = 0; // Radius override for collision

        Array<RobotDifficultyInfo, 5> Difficulty{};
        
        CloakType Cloaking = CloakType::None;
        AttackType Attack = AttackType::Ranged;

        SoundID ExplosionSound1 = SoundID::None;
        SoundID ExplosionSound2 = SoundID::None;
        SoundID SeeSound = SoundID::None;
        SoundID AttackSound = SoundID::None;
        SoundID ClawSound = SoundID::None;
        SoundID TauntSound = SoundID::None;
        SoundID DeathRollSound = SoundID::None;

        bool IsBoss;
        bool IsCompanion;    // Companion robot, leads you to things.

        sbyte smart_blobs;  // Blobs on death, not implemented
        sbyte energy_blobs; // Blobs when hit by an energy weapon

        bool IsThief;
        bool Pursues;       // Chases player after going around a corner.  4 = 4/2 pursue up to 4/2 seconds after becoming invisible if up to 4 segments away
        sbyte LightCast;    // Amount of light cast. 1 is default.  10 is very large.
        ubyte DeathRoll;   // 0 = dies without death roll. !0 means does death roll, larger = faster and louder

        ubyte Flags;   // misc properties

        ubyte Glow;        // apply this light to robot itself. stored as 4:4 fixed-point
        AIBehavior Behavior;    // Default behavior when materialized (not editor placed)
        ubyte Aim = 255;   // 255 is perfect aim. 0 is very inaccurate.
        ubyte Multishot = 1; // Number of projectiles to fire at once if possible

        // Joint lookup for each gun and animation state
        JointLookup Joints[MAX_GUNS + 1][N_ANIM_STATES]{};

        float AlertRadius = 80; // Increases awareness of robots in this radius while the player is visible
        float AlertAwareness = 0.5f; // Amount of awareness each second to give nearby robots
        List<int8> GatedRobots; // Robots to gate in when hit. For bosses.
        float TeleportInterval; // Interval between boss teleports.
        float FleeThreshold = 0; // Will flee to find another robot when under this amount of life or getting scared

        float ChaseChance = 0.5f; // Chance to chase when target leaves sight
        float SuppressChance = 0.25f; // Chance to fire at out of sight 

        float Curiosity = 0.75f; // Chance to investigate noises while not in combat

        string Script; // Custom behavior script
        bool OpenKeyDoors = false; // Can open key doors
        bool AngerBehavior = false; // Gets angry when alone
        float AimAngle = 30.0f; // Field of view of a robot's guns in degrees
        bool GetBehind = false; // Robot tries to get behind the target by strafing

        float BurstDelay = 1 / 8.0f; // Delay between burst shots (shots fired per FireDelay). D1 and D2 used 0.125.
    };
}