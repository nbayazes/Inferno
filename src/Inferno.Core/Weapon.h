#pragma once

#include "Types.h"

namespace Inferno {
    enum class WeaponFlag : sbyte {
        Placable = 1 // can be placed by level designer
    };

    enum class WeaponRenderType : sbyte {
        Laser = 0,
        Blob = 1,
        Model = 2
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