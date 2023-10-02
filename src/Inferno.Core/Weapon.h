#pragma once

#include "Types.h"

namespace Inferno {
    enum class WeaponFlag : sbyte {
        Placable = 1 // can be placed by level designer
    };

    enum class WeaponRenderType : sbyte {
        None = -1,
        Laser = 0,
        Blob = 1,
        Model = 2,
        VClip = 3
    };

    enum class PrimaryWeaponIndex : uint8 {
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
        Flash = 5,
        Guided = 6,
        SmartMine = 7,
        Mercury = 8,
        Shaker = 9
    };

    enum class LaserLevel : uint8 {
        Level1,
        Level2,
        Level3,
        Level4,
        Level5,
        Level6,
    };

    constexpr int MAX_LASER_LEVEL = (int)LaserLevel::Level4;
    constexpr int MAX_SUPER_LASER_LEVEL = (int)LaserLevel::Level6;

    // HAM IDs for each weapon
    enum class WeaponID : int8 {
        None = -1,
        Laser1 = 0,
        Laser2 = 1,
        Laser3 = 2,
        Laser4 = 3,
        ReactorBlob = 6,
        Concussion = 8,
        Flare = 9,
        Vulcan = 11,
        Spreadfire = 12,
        Plasma = 13,
        Fusion = 14,
        Homing = 15,
        ProxMine = 16,
        Smart = 17,
        Mega = 18,

        PlayerSmartBlob = 19,
        RobotSmartBlob = 29,

        Laser5 = 30,
        Laser6 = 31,
        Gauss = 32,
        Helix = 33,
        Phoenix = 34,
        Omega = 35,

        Flash = 36,
        Guided = 37,
        SmartMine = 38,
        Mercury = 39,
        Shaker = 40,

        LevelMine = 51, // Placeable level mine
    };

    constexpr bool WeaponIsMine(WeaponID id) {
        return id == WeaponID::LevelMine || id == WeaponID::ProxMine || id == WeaponID::SmartMine;
    }

    static inline WeaponID PrimaryToWeaponID[10] = { WeaponID::Laser1, WeaponID::Vulcan, WeaponID::Spreadfire, WeaponID::Plasma, WeaponID::Fusion, WeaponID::Laser5, WeaponID::Gauss, WeaponID::Helix, WeaponID::Phoenix, WeaponID::Omega };
    static inline WeaponID SecondaryToWeaponID[10] = { WeaponID::Concussion, WeaponID::Homing, WeaponID::ProxMine, WeaponID::Smart, WeaponID::Mega, WeaponID::Flash, WeaponID::Guided, WeaponID::SmartMine, WeaponID::Mercury, WeaponID::Shaker };

    enum class PowerupID : uint8;

    struct WeaponExtended {
        WeaponID ID; // Associate with this existing weapon ID in the HAM
        string Name; // Name in fullscreen HUD
        string ShortName; // Name in cockpit window
        string Behavior; // Function to call when firing this weapon. Fusion, Omega, Spreadfire, Helix, Mass Driver (zoom)
        string Decal = "scorchA"; // Texture to apply to walls when hit
        float DecalRadius = 1; // Radius of decals. 0 uses a ratio of impact size.
        string ModelName; // Name of a model file to load (D3 OOF)
        float ModelScale = 1;

        string ExplosionTexture; // Texture to use when exploding, overrides vclip. Renders as a plane aligned to the hit normal or camera.
        float ExplosionSize = 1.5f; // Initial size of explosion texture, scales up and out
        float ExplosionTime = 0.4f; // How long the explosion takes to fade out
        string ExplosionSound; // Sound to play when exploding. Overrides base sound.
        float ExplosionSoundRadius = 250; // Sound radius when exploding

        PowerupID PowerupType; // Powerup when dropped
        int WeaponID; // Icon shown in cockpit, the time between shots and energy usage. Mainly for lasers.
        int AmmoType; // Vulcan and gauss share ammo types
        bool Zooms; // Zooms in when fire is held
        bool Chargable = false; // Fusion, Mass Driver
        float MaxCharge = 2; // Max charge time for full power
        int Crosshair = 0; // Crosshair shown when selected but not ready to fire
        List<int> Levels; // Weapon ID fired at each upgrade level (for lasers)

        bool SilentSelectFail = false; // Hide HUD errors when selecting
        //Vector2 SpreadMax, SpreadMin; // Random spread on X/Y
        Color Glow; // Color for additive weapons
        Color LightColor; // color for projectile environment lighting
        float LightRadius = -1; // size of environment lighting
        DynamicLightMode LightMode = DynamicLightMode::Constant; // Effect to use for lighting
        float LightFadeTime = 0.25f; // Time to fade out light when expiring or hitting something
        Color ExplosionColor = LIGHT_UNSET; // color for contact explosion. size scales based on explosion size

        string Sparks; // Sparks to create while alive
        string DeathSparks; // Sparks to create when expiring
        int Bounces = 0;
        bool Sticky = false; // Sticks to surfaces once Bounces = 0
        bool InheritParentVelocity = false; // Adds the parent velocity to weapon when firing
        Vector3 RotationalVelocity; // Initial rotational velocity
        float Size = -1; // Overrides Blob Size and Model Size

        float Spread = 0; // Amount of spread in units
        Color FlashColor = { 1, 1, 1 }; // Color for muzzle flash sprites
        float Noise = 1; // How much noise (awareness) weapon creates when firing
        float SoundRadius = 240; // Sound radius when firing
        float StunMult = 1; // how effective this weapon is at stunning robots. 0.5 would halve stun duration.
        Array<float, 5> InitialSpeed; // Speed to spawn with
        bool PointCollideWalls = true; // Use raycasting against level geometry. Otherwise use spheres.
        float Recoil = 0; // How much backwards force to apply when firing
        float HomingFov = 0; // Homing FOV in degrees
        float HomingDistance = 0; // Distance to look for new targets
        //float HomingTurnRate = 0; // Amount of rotational force to apply each second for homing weapons

        //struct FiringPattern {
        //    string Crosshair;

        //    // Fires a projectile from an object using:
        //    // Object.FVec + Direction +- Spread
        //    struct Projectile {
        //        int Gun = 0;
        //        Vector3 Direction = { 0, 0, 1 }; // Fixed direction firing vector (Z-forward)
        //        bool QuadOnly = false;
        //        float DamageMultiplier = 1;
        //    };
        //};

        //List<FiringPattern> Pattern;
        //ubyte State[32]; // Buffer to serialize arbitrary weapon state. Helix / Spreadfire rotation. Omega charge?
    };

    struct Weapon {
        WeaponRenderType RenderType;
        bool Piercing; // Passes through enemies (fusion)
        ModelID Model = ModelID::None;
        ModelID ModelInner = ModelID::None;

        VClipID FlashVClip = VClipID::None; // Muzzle flash
        SoundID FlashSound = SoundID::None; // Sound to play when fired

        // Number of times to 'fire' this weapon per pull of the trigger. 
        // For missiles it will alternate gunpoints.
        // For most lasers it will stack the projectiles.
        int FireCount = 1;

        VClipID RobotHitVClip = VClipID::None;
        SoundID RobotHitSound = SoundID::None;

        int AmmoUsage = 0;
        VClipID WeaponVClip = VClipID::None;

        VClipID WallHitVClip = VClipID::None;
        SoundID WallHitSound = SoundID::None;

        bool IsDestroyable = false;     // If true this weapon can be destroyed by another weapon
        bool IsMatter = false;          // Is a matter weapon if true, energy if false
        sbyte Bounce = 0;           // 1 always bounces, 2 bounces twice
        bool IsHoming = false;

        float SpeedVariance = 1;  // Randomized speed multiplier. 0.5 is 50-100%, 1.5 is 150-100%

        WeaponFlag Flags{};

        sbyte FlashStrength = 0; // Blinding flash effect strength
        sbyte TrailSize; // Size of blobs in 1/16 units. Player afterburner size = 2.5.

        WeaponID Spawn = WeaponID::None;  // Weapon to spawn when destroyed
        uint SpawnCount = 0; // NEW: number of children to spawn

        float EnergyUsage;
        float FireDelay;

        float PlayerDamageScale = 1; // Scale damage by this amount when hitting a player

        TexID BlobBitmap;
        float BlobSize;  // Size of blob if blob type, used for collision

        float FlashSize; // Muzzle flash radius
        float ImpactSize; // Radius of effect when hitting something
        Array<float, 5> Damage;
        Array<float, 5> Speed;
        float Mass;
        float Drag;
        float Thrust;
        float ModelSizeRatio;  // Ratio of length / width for models
        float Light;
        float Lifetime;
        float SplashRadius;
        TexID Icon = TexID::None, HiresIcon = TexID::None;  // Texture to use in the cockpit or UI

        WeaponExtended Extended{};

        float GetDecalSize() const { return Extended.DecalRadius ? Extended.DecalRadius : ImpactSize / 3; }
        bool IsExplosive() const { return SplashRadius > 0; }
    };

    struct ShipInfo {
        float DamageMultiplier = 1.0f; // Multiplier on damage taken
        float EnergyMultiplier = 1.0f; // Multiplier for weapon energy costs

        //struct PrimaryAmmo {
        //    int Max = 10000;
        //    float DisplayMultiplier = 1;
        //};

        // Ammo used by primary weapons. Vulcan and Gauss share. Could add Napalm fuel.
        //List<PrimaryAmmo> PrimaryAmmoTypes;

        struct WeaponBattery {
            //float EnergyUsage = 0; // Energy per shot
            //float AmmoUsage = 0; // Ammo per shot
            //int AmmoType = -1;
            //int GunpointWeapons[8]{}; // Weapon IDs to use for each gunpoint
            WeaponID Weapon;
            //bool Gunpoints[8]{};

            // Crosshair should be determined by gunpoints
            struct FiringInfo {
                bool Gunpoints[8]{};
                float Delay = 0.25f; // Delay between shots
            };

            List<FiringInfo> Firing; // Cycles through each entry after firing.

            bool QuadGunpoints[8]{}; // Gunpoints to use with quad upgrade
            uint16 MaxAmmo = 0;
        };

        WeaponBattery Weapons[20]; // 10 primaries, 10 secondaries
    };

    inline ShipInfo PyroGX = {
        .Weapons = {
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Laser1,
                    .Firing = { {.Gunpoints = { 1, 1 } } },
                    .QuadGunpoints = { 1, 1, 1, 1 }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Vulcan,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint 
                    },
                    .MaxAmmo = 20000 // 10000 in D1
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Spreadfire,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint 
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Plasma,
                    .Firing = { {.Gunpoints = { 1, 1 } } }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Fusion,
                    .Firing = { {.Gunpoints = { 1, 1 } } }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Laser5,
                    .Firing = { {.Gunpoints = { 1, 1 } } },
                    .QuadGunpoints = { 1, 1, 1, 1 }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Gauss,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint 
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Helix,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint 
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Phoenix,
                    .Firing = { {.Gunpoints = { 1, 1 } } }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Omega,
                    .Firing = { {.Gunpoints = { 0, 1 } } }
                }
            },
            {
                // Secondaries (Gun 4 and 5 for alternating)
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Concussion,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    },
                    .MaxAmmo = 20
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Homing,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    },
                    .MaxAmmo = 10
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::ProxMine,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 0, 1 } } // 7 is rear gunpoint
                    },
                    .MaxAmmo = 20
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Smart,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint
                    },
                    .MaxAmmo = 5
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Mega,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint
                    },
                    .MaxAmmo = 5
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Flash,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 }  }
                    },
                    .MaxAmmo = 20
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Guided,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    },
                    .MaxAmmo = 20
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::SmartMine,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 0, 1 } } // 7 is rear gunpoint
                    },
                    .MaxAmmo = 15
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Mercury,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    },
                    .MaxAmmo = 10
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Shaker,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint
                    },
                    .MaxAmmo = 10 // Really?
                }
            },
        }
    };
}