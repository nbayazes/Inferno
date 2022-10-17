#pragma once

#include "Player.h"
#include "Resources.h"
#include "HUD.h"

namespace Inferno {
    enum class FireState { None, Press, Hold, Release };

    // Extracted player state that was scattered across methods or globals as static variables
    struct Player : public PlayerInfo {
        float RearmTime = 1.0f; // Time to swap between weapons and being able to fire

        PrimaryWeaponIndex Primary = PrimaryWeaponIndex::Laser;
        SecondaryWeaponIndex Secondary = SecondaryWeaponIndex::Concussion;
        bool PrimaryWasSuper[10]{};
        bool SecondaryWasSuper[10]{};

        float PrimarySwapTime = 0; // Primary weapon is changing. Used to fade monitor contents.
        float SecondarySwapTime = 0; // Secondary weapon is changing. Used to fade monitor contents.
        float WeaponCharge = 0; // How long weapon has been charging (held down)
        float OmegaCharge = 1; // How much charge the omega has stored
        float OmegaRechargeDelay = 0; // Delay before Omega starts recharging after firing
        float FlareDelay = 0;
        float PrimaryDelay = 0;
        float SecondaryDelay = 0;
        float AfterburnerCharge = 1; // 0 to 1
        bool HasSpew = false; // has dropped items on death
        bool SpawnInvuln = false; // temporary invuln when spawning
        bool LavafallHissPlaying = false; // checks if a lavafall (or waterfall) sound is already playing
        bool SpreadfireToggle = false; // horizontal / vertical
        uint8 HelixOrientation = 0; // increments in 22.5 degrees
        float FusionNextSoundDelay = 0;
        uint8 FiringIndex = 0, MissileFiringIndex;

        FireState PrimaryState, SecondaryState;

        bool Gunpoints[20][8] = {
            { true, true }, // Laser
            { false, false, false, false, false, false, true }, // Center fire
            { false, false, false, false, false, false, true }, // Center fire
            { true, true }, // Plasma
            { true, true }, // Fusion
            { true, true }, // Laser
            { false, false, false, false, false, false, true }, // Center fire
            { false, false, false, false, false, false, true }, // Center fire
            { true, true }, // Phoenix
            { true }, // Omega
        };

        void GiveWeapon(PrimaryWeaponIndex weapon) {
            PrimaryWeapons |= (1 << (uint16)weapon);
            if ((weapon == PrimaryWeaponIndex::Vulcan || weapon == PrimaryWeaponIndex::Gauss))
                PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan] += 2500;

            // todo: autoswap based on priority
        }

        void GiveWeapon(SecondaryWeaponIndex weapon) {
            SecondaryWeapons |= (1 << (uint16)weapon);
            SecondaryAmmo[(uint16)weapon]++;
            // todo: autoswap based on priority
        }

        bool HasWeapon(PrimaryWeaponIndex weapon) const {
            return PrimaryWeapons & (1 << (uint16)weapon);
        }

        bool HasWeapon(SecondaryWeaponIndex weapon) const {
            return SecondaryWeapons & (1 << (uint16)weapon);
        }

        void GivePowerup(PowerupFlag powerup) {
            Powerups = (PowerupFlag)((uint32)Powerups | (uint32)powerup);
        }

        bool HasPowerup(PowerupFlag powerup) {
            return (bool)((uint32)Powerups & (uint32)powerup);
        }

        WeaponID GetPrimaryWeaponID() {
            if (Primary == PrimaryWeaponIndex::Laser) {
                if (LaserLevel < 4) return WeaponID{ (int)WeaponID::Laser1 + LaserLevel };
                if (LaserLevel == 4) return WeaponID::Laser5;
                if (LaserLevel == 5) return WeaponID::Laser6;
            }

            return PrimaryToWeaponID[(int)Primary];
        }

        WeaponID GetSecondaryWeaponID() {
            return SecondaryToWeaponID[(int)Secondary];
        }

        bool CanFirePrimary() {
            auto& weapon = Resources::GetWeapon(GetPrimaryWeaponID());
            auto index = Primary;
            if (!HasWeapon(index)) return false;
            if (PrimaryDelay > 0) return false;

            bool canFire = true;

            if (index == PrimaryWeaponIndex::Vulcan ||
                index == PrimaryWeaponIndex::Gauss)
                canFire &= weapon.AmmoUsage <= PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan];

            if (index == PrimaryWeaponIndex::Omega)
                canFire &= Energy > 0 || OmegaCharge > 0;

            canFire &= weapon.EnergyUsage <= Energy;
            return canFire;
        }

        bool CanFireSecondary() {
            auto& weapon = Resources::GetWeapon(GetSecondaryWeaponID());
            auto index = Secondary;
            if (!HasWeapon(index)) return false;
            if (SecondaryDelay > 0) return false;

            return
                weapon.AmmoUsage <= SecondaryAmmo[(int)index] &&
                weapon.EnergyUsage <= Energy;
        }

        void ArmPrimary(PrimaryWeaponIndex index);

        void ArmSecondary(SecondaryWeaponIndex index);

        void Update(float dt) {
            PrimaryDelay -= dt;
            SecondaryDelay -= dt;
            if (CloakTime > 0) CloakTime -= dt;
            if (InvulnerableTime > 0) InvulnerableTime -= dt;

            auto& weapon = Resources::GetWeapon(GetPrimaryWeaponID());

            if (weapon.Extended.Chargable) {
                if (PrimaryState == FireState::Press) {
                    WeaponCharge = 0;
                    FusionNextSoundDelay = 1.0f / 6 + Random() / 4;
                }
                else if (PrimaryState == FireState::Hold && CanFirePrimary()) {
                    Energy -= dt;
                    WeaponCharge += dt;
                    if (Energy <= 0) {
                        Energy = 0;
                        //ForceFire = true;
                    }

                    FusionNextSoundDelay -= dt;
                    if (FusionNextSoundDelay < 0) {
                        if (WeaponCharge > weapon.Extended.MaxCharge) {
                            // Self damage
                            Sound3D sound(ID);
                            sound.Resource = Resources::GetSoundResource(SoundID::Explosion);
                            sound.FromPlayer = true;
                            Sound::Play(sound);
                            constexpr float OVERCHARGE_DAMAGE = 3.0f;
                            Shields -= Random() * OVERCHARGE_DAMAGE;
                        }
                        else {
                            // increase robot awareness
                            Sound3D sound(ID);
                            sound.Resource = Resources::GetSoundResource(SoundID::FusionWarmup);
                            sound.FromPlayer = true;
                            Sound::Play(sound);
                        }

                        FusionNextSoundDelay = 1.0f / 6 + Random() / 4;
                    }
                }
                else if (PrimaryState == FireState::Release) {
                    FirePrimary();
                    //WeaponCharge = 0;
                    //FusionNextSoundDelay = 0;
                }
            }
            else if (PrimaryState == FireState::Hold) {
                FirePrimary();
            }

            if (SecondaryState == FireState::Hold || SecondaryState == FireState::Press) {
                FireSecondary();
            }
        }

        void FirePrimary();
        void HoldPrimary();
        void ReleasePrimary();

        void FireSecondary();
    };
}