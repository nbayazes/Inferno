#pragma once
#include "Polymodel.h"
#include "Types.h"
#include "Object.h"

namespace Inferno {
    constexpr auto MAX_ROBOT_JOINTS = 1600;
    constexpr auto MAX_GUNS = 8;
    constexpr auto N_ANIM_STATES = 5;

    // describes a list of joint positions
    struct JointList {
        short Count;
        short Offset;
    };

    struct RobotDifficultyInfo {
        float FieldOfView;
        float FireDelay, FireDelay2;
        float TurnTime; // time in seconds to rotate 360 degrees in a dimension
        float MaxSpeed;
        float CircleDistance;
        sbyte RapidfireCount;
        sbyte EvadeSpeed;   // rate at which robot can evade shots, 0=none, 4=very fast
    };
    
    enum class CloakType : sbyte { None, Always, WhenNotFiring };
    enum class AttackType : sbyte { Ranged, Melee };

    struct RobotInfo {
        ModelID Model;
        Vector3 GunPoints[MAX_GUNS]; // where each gun model is
        ubyte GunSubmodels[MAX_GUNS];   // which submodel is each gun in?

        VClipID ExplosionClip1, ExplosionClip2;

        WeaponID WeaponType; // Primary weapon
        WeaponID WeaponType2 = WeaponID::None;    // Secondary weapon. D2 only
        uint8 Guns;            // how many different gun positions

        ContainsData Contains;
        sbyte ContainsChance;   // Probability that this instance will contain something in N/16

        sbyte Kamikaze;        // Strength of suicide explosion and knockback

        short Score;
        int8 Badass;         // Dies with badass explosion. > 0 specifies strength (damage?)
        int8 EnergyDrain;    // Energy drained when touched

        float Lighting;
        float HitPoints;

        float Mass, Drag;

        Array<RobotDifficultyInfo, 5> Difficulty{};
        
        CloakType Cloaking = CloakType::None;
        AttackType Attack = AttackType::Ranged;

        SoundID ExplosionSound1 = SoundID::None;
        SoundID ExplosionSound2 = SoundID::None;
        SoundID SeeSound = SoundID::None;
        SoundID AttackSound = SoundID::None;
        SoundID ClawSound = SoundID::None;
        SoundID TauntSound = SoundID::None;
        SoundID DeathrollSound = SoundID::None;

        bool IsBoss;
        bool IsCompanion;    // Companion robot, leads you to things.

        sbyte smart_blobs;  // Blobs on death, not implemented
        sbyte energy_blobs; // Blobs when hit by an energy weapon

        bool IsThief;
        bool Pursues;       // Chases player after going around a corner.  4 = 4/2 pursue up to 4/2 seconds after becoming invisible if up to 4 segments away
        sbyte LightCast;    // Amount of light cast. 1 is default.  10 is very large.
        sbyte DeathRoll;   // 0 = dies without death roll. !0 means does death roll, larger = faster and louder

        ubyte Flags;   // misc properties

        ubyte Glow;        // apply this light to robot itself. stored as 4:4 fixed-point
        ubyte Behavior;    // Default behavior
        ubyte Aim = 255;   // 255 is perfect aim. 0 is very inaccurate.

        //animation info
        JointList anim_states[MAX_GUNS + 1][N_ANIM_STATES];
    };

}