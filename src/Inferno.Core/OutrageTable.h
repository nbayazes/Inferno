#pragma once

#include "Types.h"
#include "Streams.h"
#include "Object.h"

namespace Inferno::Outrage {
    constexpr int MAX_OBJ_SOUNDS = 2;
    constexpr int MAX_AI_SOUNDS = 5;
    constexpr int NUM_MOVEMENT_CLASSES = 5;
    constexpr int NUM_ANIMS_PER_CLASS = 24;
    constexpr int MAX_WBS_PER_OBJ = 21;
    constexpr int MAX_WB_FIRING_MASKS = 8;
    constexpr int MAX_WB_GUNPOINTS = 8;
    constexpr int MAX_WB_UPGRADES = 5;
    constexpr int MAX_DSPEW_TYPES = 2;

    enum class TextureFlag {
        None = 0,
        Volatile = 1,
        Water = (1 << 1),
        Metal = (1 << 2), // Editor sorting
        Marble = (1 << 3), // Editor sorting
        Plastic = (1 << 4), // Editor sorting
        Forcefield = (1 << 5),
        Animated = (1 << 6),
        Destroyable = (1 << 7),
        Effect = (1 << 8),
        HudCockpit = (1 << 9),
        Mine = (1 << 10),
        Terrain = (1 << 11),
        Object = (1 << 12),
        Texture64 = (1 << 13),
        Tmap2 = (1 << 14),
        Texture32 = (1 << 15),
        FlyThru = (1 << 16),
        PassThru = (1 << 17),
        PingPong = (1 << 18),
        Light = (1 << 19), // Full bright
        Breakable = (1 << 20),
        Saturate = (1 << 21), // Additive
        Alpha = (1 << 22), // Use the alpha value in the tablefile
        Dontuse = (1 << 23), // Not intended for levels? Hidden in texture browser?
        Procedural = (1 << 24),
        WaterProcedural = (1 << 25),
        ForceLightmap = (1 << 26),
        SaturateLightmap = (1 << 27),
        Texture256 = (1 << 28),
        Lava = (1 << 29),
        Rubble = (1 << 30),
        SmoothSpecular = (1 << 31)
    };

    enum class ProceduralType : uint8 {
        None,
        LineLightning,
        SphereLightning,
        Straight,
        RisingEmbers,
        RandomEmbers,
        Spinners,
        Roamers,
        Fountain,
        Cone,
        FallRight,
        FallLeft
    };

    enum class WaterProceduralType : uint8 {
        None,
        HeightBlob,
        SineBlob,
        RandomRaindrops,
        RandomBlobdrops
    };

    struct TextureInfo {
        string Name; // Entry in tablefile
        string FileName; // File name in hog or on disk
        Color Color;
        Vector2 Slide;
        float Speed; // Total time of animation
        float Reflectivity; // For radiosity calcs 
        TextureFlag Flags;
        int8 Corona;
        int Damage;

        //struct {
        //    int Palette[255]{};
        //    int8 Heat, Light, Thickness, EvalTime, OscTime, OscValue;
        //    short Elements;
        //    int8 Type, Frequency, Speed, Size, X1, Y1, X2, Y2;
        //} Procedural;

        string Sound;

        constexpr bool Saturate() const { return bool(Flags & TextureFlag::Saturate); }
        constexpr bool Alpha() const { return bool(Flags & TextureFlag::Alpha); }
        constexpr bool Animated() const { return bool(Flags & TextureFlag::Animated); }
        constexpr bool Procedural() const { return bool(Flags & TextureFlag::Procedural); }
    };

    struct SoundInfo {
        string Name; // Entry in tablefile
        string FileName; // File name in hog or on disk
        int Flags;
        int LoopStart, LoopEnd;
        float OuterConeVolume;
        int InnerConeAngle, OuterConeAngle;
        float MinDistance, MaxDistance;
        float ImportVolume;
    };

    struct AnimElem {
        int16 From;
        int16 To;
        float Speed;
    };

    struct AnimClasses {
        Array<AnimElem,NUM_ANIMS_PER_CLASS> Elems;
    };

    struct PhysicsInfo {
        Vector3 Velocity;
        Vector3 RotVel;
        int NumBounces;
        float CoeffRestitution;
        float Mass;
        float Drag;
        float RotDrag;
        float FullThrust;
        float FullRotThrust;
        float MaxTurnrollRate;
        float TurnrollRatio;
        float WiggleAmplitude;
        float WigglesPerSec;
        float HitDieDot;
        uint Flags;
    };

    struct LightInfo {
        int Flags;
        float LightDistance;
        Color Color1;
        Color Color2;
        float TimeInterval;
        float FlickerDistance;
        float DirectionalDot;
        int TimeBits;
        ubyte Angle;
        ubyte LightingRenderType;
    };

    enum class AINotifyFlag : uint32 {
        NewMovement = 1 << 1,
        ObjKilled = 1 << 2,
        WhitByObj = 1 << 3,
        SeeTarget = 1 << 4,
        PlayerSeesYou = 1 << 5,
        WhitObject = 1 << 6,
        TargetDied = 1 << 7,
        ObjFired = 1 << 8,
        GoalComplete = 1 << 9,
        GoalFail = 1 << 10,
        GoalError = 1 << 11,
        HearNoise = 1 << 12,
        NearTarget = 1 << 13,
        HitByWeapon = 1 << 14,
        NearWall = 1 << 15,
        UserDefined = 1 << 16,
        TargetInvalid = 1 << 17,
        GoalInvalid = 1 << 18,
        ScriptedGoal = 1 << 19,
        ScriptedEnabler = 1 << 20,
        AnimComplete = 1 << 21,
        BumpedObj = 1 << 22,
        MeleeHit = 1 << 23,
        MeleeAttackFrame = 1 << 24,
        ScriptedInfluence = 1 << 25,
        ScriptedOrient = 1 << 26,
        MovieStart = 1 << 27,
        MovieEnd = 1 << 28,
        FiredWeapon = 1 << 29,

        AlwaysOn = AnimComplete | NewMovement | PlayerSeesYou | GoalComplete | GoalFail | GoalError |
            UserDefined | TargetDied | TargetInvalid | BumpedObj | MeleeHit | MeleeAttackFrame
    };

    enum class AIFlag : uint32 {
        Weapon1 = 1 << 0,
        Weapon2 = 1 << 1,
        Melee1 = 1 << 2,
        Melee2 = 1 << 3,
        StaysInout = 1 << 4,
        ActAsNeutralUntilShot = 1 << 5,
        Persistant = 1 << 6,
        Dodge = 1 << 7,
        Fire = 1 << 8,
        Flinch = 1 << 9,
        DetermineTarget = 1 << 10,
        Aim = 1 << 11,
        OnlyTauntAtDeath = 1 << 12,
        AvoidWalls = 1 << 13,
        Disabled = 1 << 14,
        FluctuateSpeedProperties = 1 << 15,
        TeamMask1 = 1 << 16,
        TeamMask2 = 1 << 17,
        OrderedWBFiring = 1 << 18,
        OrientToVel = 1 << 19,
        XZDist = 1 << 20,
        ReportNewOrient = 1 << 21,
        TargetByDist = 1 << 22,
        DisableFiring = 1 << 23,
        DisableMelee = 1 << 24,
        AutoAvoidFriends = 1 << 25,
        TrackClosest2Friends = 1 << 26,
        TrackClosest2Enemies = 1 << 27,
        BiasedFlightHeight = 1 << 28,
        ForceAwareness = 1 << 29,
        UVecFov = 1 << 30,
        AimPntFov = 1u << 31,

        TeamMask = TeamMask1 | TeamMask2,
    };

    struct AIInfo {
        ubyte AIClass;
        ubyte AIType;

        float MaxVelocity;
        float MaxDeltaVelocity;
        float MaxTurnRate;
        float MaxDeltaTurnRate;

        float AttackVelPercent;
        float FleeVelPercent;
        float DodgeVelPercent;

        float CircleDistance;
        float DodgePercent;

        Array<float,2> MeleeDamage;
        Array<float,2> MeleeLatency;

        Array<int,MAX_AI_SOUNDS> Sound;

        ubyte MovementType;
        ubyte MovementSubtype;

        AIFlag Flags;
        AINotifyFlag NotifyFlags;

        float FOV;

        float AvoidFriendsDistance;

        float Frustration;
        float Curiousity;
        float LifePreservation;
        float Agression;

        float FireSpread;
        float NightVision;
        float FogVision;
        float LeadAccuracy;
        float LeadVarience;
        float FightTeam;
        float FightSame;
        float Hearing;
        float Roaming;

        float BiasedFlightImportance;
        float BiasedFlightMin;
        float BiasedFlightMax;

        constexpr bool HasFlag(AIFlag flag) const { return (bool)(Flags & flag); }
    };

    struct AnimInfo {
        Array<AnimClasses,NUM_MOVEMENT_CLASSES> Classes;
    };

    struct WeaponBatteryInfo {
        Array<uint16,MAX_WB_GUNPOINTS> GPWeaponIndex;
        Array<uint16,MAX_WB_FIRING_MASKS> FMFireSoundIndex;
        uint16 AimingGPIndex;

        ubyte NumMasks;
        Array<ubyte,MAX_WB_FIRING_MASKS> GPFireMasks;
        Array<float,MAX_WB_FIRING_MASKS> GPFireWait;

        ubyte GPQuadFireMask;

        ubyte NumLevels;
        Array<uint16,MAX_WB_UPGRADES> GPLevelWeaponIndex;
        Array<uint16,MAX_WB_UPGRADES> GPLevelFireSoundIndex;

        ubyte AimingFlags;
        float Aiming3DDot; // These Can be Reused.
        float Aiming3DDist;
        float AimingXZDot;

        Array<float,MAX_WB_FIRING_MASKS> AnimStartFrame;
        Array<float,MAX_WB_FIRING_MASKS> AnimFireFrame;
        Array<float,MAX_WB_FIRING_MASKS> AnimEndFrame;
        Array<float,MAX_WB_FIRING_MASKS> AnimTime;

        uint16 Flags;

        float EnergyUsage;
        float AmmoUsage;
    };

    struct DeathInfo {
        int Flags;
        float DelayMin;
        float DelayMax;
        ubyte Probabilities;
    };

    enum class GenericFlag {
        ControlAI = 1 << 0,
        UsesPhysics = 1 << 1,
        Destroyable = 1 << 2,
        InvenSelectable = 1 << 3,
        InvenNonuseable = 1 << 4,
        InvenTypeMission = 1 << 5,
        InvenNoremove = 1 << 6,
        InvenViswhenused = 1 << 7,
        AIScriptedDeath = 1 << 8,
        DoCeilingCheck = 1 << 9, // Check terrain 'ceiling' collision
        IgnoreForcefieldsAndGlass = 1 << 10,
        NoDiffScaleDamage = 1 << 11,
        NoDiffScaleMove = 1 << 12,
        AmbientObject = 1 << 13
    };

    struct GenericInfo {
        ObjectType Type;
        string Name;
        string ModelName;
        string MedModelName;
        string LoModelName;
        float ImpactSize;
        float ImpactTime;
        float Damage;
        int Score;
        int AmmoCount;
        string ModuleName;
        string ScriptNameOverride;
        string Description;
        string IconName;
        float MedLodDistance;
        float LoLodDistance;
        PhysicsInfo Physics;
        float Size;
        LightInfo Light;
        int HitPoints;
        GenericFlag Flags;
        AIInfo AI;
        ubyte DSpewFlags;
        Array<float,MAX_DSPEW_TYPES> DSpewPercent;
        Array<int16,MAX_DSPEW_TYPES> DSpewNumber;
        Array<string,MAX_DSPEW_TYPES> DSpewGenericNames;
        AnimInfo Anim;
        Array<WeaponBatteryInfo,MAX_WBS_PER_OBJ> WeaponBatteries;
        Array<Array<string,MAX_WB_GUNPOINTS>,MAX_WBS_PER_OBJ> WBWeaponNames;
        Array<string,MAX_OBJ_SOUNDS> SoundNames;
        Array<string,MAX_AI_SOUNDS> AISoundNames;
        Array<Array<string,MAX_WB_GUNPOINTS>,MAX_WBS_PER_OBJ> WBSoundNames;
        Array<Array<string,NUM_ANIMS_PER_CLASS>,NUM_MOVEMENT_CLASSES> AnimSoundNames;
        float RespawnScalar;
        List<DeathInfo> DeathTypes;

        constexpr bool HasFlag(GenericFlag flag) { return (bool)(Flags & flag); }
    };

    // Descent 3 Game Table (GAM). Contains metadata for game assets.
    struct GameTable {
        enum {
            TABLE_FILE_BASE = 0,
            TABLE_FILE_MISSION = 1,
            TABLE_FILE_MODULE = 2
        } Type{};
        
        string Name;

        List<TextureInfo> Textures;
        List<SoundInfo> Sounds;
        List<GenericInfo> Generics;
        static GameTable Read(StreamReader&);
    };
}