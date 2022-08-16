#pragma once

#include "Types.h"

namespace Inferno {
    enum class WeaponFlag : sbyte {
        Placable = 1 // can be placed by level designer
    };

    enum class WeaponRenderType : sbyte {
        Laser = 0,
        Blob = 1,
        Model = 2,
        VClip = 3
    };

    enum PrimaryWeaponIndex : uint8 {
        Laser = 0,
        Vulcan = 1,
        Spreadfire = 2,
        Plasma = 3,
        Fusion = 4,
        SuperLaser = 5,
        Gauss = 6,
        Helix = 7,
        Phoenix = 8,
        Omega = 9,
    };

    enum class SecondaryWeaponIndex : uint8 {
        Concussion = 0,
        Homing = 1,
        Proximity = 2,
        Smart = 3,
        Mega = 4,
        Smissile1 = 5,
        Guided = 6,
        SmartMine = 7,
        Smissile4 = 8,
        Smissile5 = 9
    };

    enum class LaserLevel : uint8 {
        Level1,
        Level2,
        Level3,
        Level4,
        Level5,
        Level6,
    };

    namespace WeaponID {
        constexpr int Laser = 0;
        constexpr int Concussion = 8;
        constexpr int Flare = 9;
        constexpr int Vulcan = 11;
        constexpr int Spreadfire = 12;
        constexpr int Plasma = 13;
        constexpr int Fusion = 14;
        constexpr int Homing = 15;
        constexpr int ProxMine = 16;
        constexpr int Smart = 17;
        constexpr int Mega = 18;

        constexpr int Laser5 = 30;
        constexpr int Laser6 = 31;
        constexpr int Gauss = 32;
        constexpr int Helix = 33;
        constexpr int Phoenix = 34;
        constexpr int Omega = 35;

        constexpr int Flash = 36;
        constexpr int Guided = 37;
        constexpr int SmartMine = 38;
        constexpr int Mercury = 39;
        constexpr int Shaker = 40;

        constexpr int LevelMine = 51; // Placeable level mine
    }

    struct Weapon {
        WeaponRenderType RenderType;
        bool Piercing;
        ModelID Model;
        ModelID ModelInner;
        
        VClipID FlashVClip; // Muzzle flash
        SoundID FlashSound; // Sound to play when fired

        sbyte FireCount;        // Bursts fired from each gun point?

        VClipID RobotHitVClip;
        SoundID RobotHitSound;

        sbyte AmmoUsage;
        VClipID WeaponVClip;

        VClipID WallHitVClip;
        SoundID WallHitSound;

        bool IsDestroyable;       // If true this weapon can be destroyed by another weapon
        bool IsMatter;            // Is a matter weapon if true, energy if false
        sbyte Bounce;            // 1 always bounces, 2 bounces twice
        bool IsHoming;

        ubyte SpeedVariance;  // allowed variance in speed below average, /128: 64 = 50% meaning if speed = 100, can be 50..100

        WeaponFlag Flags;

        sbyte HasFlashEffect;
        sbyte TrailSize; // Size of blobs in 1/16 units. Player afterburner size = 2.5.

        sbyte Children;  // ID of weapon to drop if this contains children. -1 means no children.

        float EnergyUsage; 
        float FireDelay;

        float PlayerDamageScale = 1; // Scale damage by this amount when hitting a player

        TexID BlobBitmap;
        float BlobSize;  // Size of blob if blob type, used for collision

        float FlashSize; // Muzzle flash radius
        float ImpactSize;
        float Damage[5];
        float Speed[5];
        float Mass;
        float Drag;
        float Thrust;
        float ModelSizeRatio;  // Ratio of length / width for models
        float Light;
        float Lifetime;
        float SplashRadius;
        TexID Icon, HiresIcon;  // Texture to use in the cockpit or UI
    };

}