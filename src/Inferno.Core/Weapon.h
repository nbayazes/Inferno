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

    // HAM IDs for each weapon
    enum class WeaponID : sbyte {
        None = -1,
        Laser1 = 0,
        Laser2 = 1,
        Laser3 = 2,
        Laser4 = 3,
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

    static inline WeaponID PrimaryToWeaponID[10] = { WeaponID::Laser1, WeaponID::Vulcan, WeaponID::Spreadfire, WeaponID::Plasma, WeaponID::Fusion, WeaponID::Laser5, WeaponID::Gauss, WeaponID::Helix, WeaponID::Phoenix, WeaponID::Omega };
    static inline WeaponID SecondaryToWeaponID[10] = { WeaponID::Concussion, WeaponID::Homing, WeaponID::ProxMine, WeaponID::Smart, WeaponID::Mega, WeaponID::Flash, WeaponID::Guided, WeaponID::SmartMine, WeaponID::Mercury, WeaponID::Shaker };

    struct WeaponExtended {
        WeaponID ID; // Associate with this existing weapon ID in the HAM
        string Name; // Name in fullscreen HUD
        string ShortName; // Name in cockpit window
        string Behavior; // Function to call when firing this weapon. Fusion, Omega, Spreadfire, Helix, Mass Driver (zoom)

        int PowerupType; // Powerup when dropped
        int WeaponID; // Icon shown in cockpit, the time between shots and energy usage. Mainly for lasers.
        int AmmoType; // Vulcan and gauss share ammo types
        bool Zooms; // Zooms in when fire is held
        bool Chargable = false; // Fusion, Mass Driver
        float MaxCharge = 2; // Max charge time for full power
        int Crosshair = 0; // Crosshair shown when selected but not ready to fire
        float HomingTurnRate = 0; // Amount of rotational force to apply each second for homing weapons
        List<int> Levels; // Weapon ID fired at each upgrade level (for lasers)

        bool SilentSelectFail = false; // Hide HUD errors when selecting
        Vector2 SpreadMax, SpreadMin; // Random spread on X/Y

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
        sbyte FireCount = 1;

        VClipID RobotHitVClip = VClipID::None;
        SoundID RobotHitSound = SoundID::None;

        sbyte AmmoUsage = 0;
        VClipID WeaponVClip = VClipID::None;

        VClipID WallHitVClip = VClipID::None;
        SoundID WallHitSound = SoundID::None;

        bool IsDestroyable = false;     // If true this weapon can be destroyed by another weapon
        bool IsMatter = false;          // Is a matter weapon if true, energy if false
        sbyte Bounce = 0;           // 1 always bounces, 2 bounces twice
        bool IsHoming = false;

        float SpeedVariance = 1;  // Randomized speed multiplier. 0.5 is 50-100%

        WeaponFlag Flags{};

        sbyte FlashStrength = 0; // Blinding flash effect strength
        sbyte TrailSize; // Size of blobs in 1/16 units. Player afterburner size = 2.5.

        WeaponID Children = WeaponID::None;  // Weapon to spawn when destroyed

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
        TexID Icon = TexID::None, HiresIcon = TexID::None;  // Texture to use in the cockpit or UI

        WeaponExtended Extended{};
    };

    struct ShipInfo {
        float DamageMultiplier = 1.0f; // Multiplier on damage taken

        struct PrimaryAmmo {
            int Max = 10000;
            float DisplayMultiplier = 1;
        };

        // Ammo used by primary weapons. Vulcan and Gauss share. Could add Napalm fuel.
        List<PrimaryAmmo> PrimaryAmmoTypes;

        struct WeaponBattery {
            //float EnergyUsage = 0; // Energy per shot
            //float AmmoUsage = 0; // Ammo per shot
            int AmmoType = -1;
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
                    }
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
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Homing,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::ProxMine,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 0, 1 } } // 7 is rear gunpoint
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Smart,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Mega,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint
                    }
                }
            },
                {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Flash,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 }  }
                    }
                }
                },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Guided,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::SmartMine,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 0, 1 } } // 7 is rear gunpoint
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Mercury,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 1 } },
                        {.Gunpoints = { 0, 0, 0, 0, 0, 1 } }
                    }
                }
            },
            {
                ShipInfo::WeaponBattery {
                    .Weapon = WeaponID::Shaker,
                    .Firing = {
                        {.Gunpoints = { 0, 0, 0, 0, 0, 0, 1 } } // 6 is center gunpoint
                    }
                }
            },
        }
    };
}