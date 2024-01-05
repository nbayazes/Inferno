#pragma once

#include "Object.h"
#include "Player.h"
#include "SoundTypes.h"
#include "Utility.h"
#include "Wall.h"
#include "Weapon.h"

namespace Inferno {
    enum class FireState { None, Press, Hold, Release };

    constexpr float MAX_ENERGY = 200;
    constexpr float MAX_SHIELDS = 200;
    constexpr float OMEGA_CHARGE_COST = 1.0f / 8; // charge cost to fire one shot of omega
    constexpr float OMEGA_RECHARGE_TIME = 3; // time to fully recharge omega (original: 4)
    constexpr float OMEGA_RECHARGE_ENERGY = 4; // energy to fully recharge omega
    constexpr float OMEGA_RECHARGE_DELAY = 1.0f / 4; // how long before recharging starts

    // Extracted player state that was scattered across methods or globals as static variables
    class Player : public PlayerData {
        SoundUID _afterburnerSoundSig = SoundUID::None;
        SoundUID _fusionChargeSound = SoundUID::None;
        float _prevAfterburnerCharge = 0;
        double _nextFlareFireTime = 0;

    public:
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
        uint8 FiringIndex = 0, MissileFiringIndex = 0;

        FireState PrimaryState{}, SecondaryState{};
        double RefuelSoundTime = 0; // Next time an energy center can make noise
        bool AfterburnerActive = false;
        int BombIndex = 0; // 0 is proxy, 1 is smart mine
        double LastPrimaryFireTime = 0;
        ShipInfo Ship = PyroGX;
        Color DirectLight;

        bool IsDead = false;
        float TimeDead = 0;
        bool Exploded = false; // Indicates if the player exploded after dying

        SegID SpawnSegment = SegID::None;
        Vector3 SpawnPosition; // Can be moved based on checkpoints
        Matrix3x3 SpawnRotation;

        void GiveWeapon(PrimaryWeaponIndex weapon) {
            PrimaryWeapons |= 1 << (uint16)weapon;
            if (weapon == PrimaryWeaponIndex::Vulcan || weapon == PrimaryWeaponIndex::Gauss)
                PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan] += 2500;
        }

        void RemoveWeapon(PrimaryWeaponIndex weapon) {
            ClearFlag(PrimaryWeapons, (uint16)weapon);
        }

        //void GiveWeapon(SecondaryWeaponIndex weapon) {
        //    SecondaryWeapons |= 1 << (uint16)weapon;
        //    SecondaryAmmo[(uint16)weapon]++;
        //}

        bool HasWeapon(PrimaryWeaponIndex weapon) const {
            return PrimaryWeapons & (1 << (uint16)weapon);
        }

        SecondaryWeaponIndex GetActiveBomb() const;

        void AddEnergy(float energy) {
            Energy += energy;
            Energy = std::clamp(Energy, 0.0f, MAX_ENERGY);
        }

        void CyclePrimary();
        void CycleSecondary();
        void CycleBombs();

        void DropBomb();

        bool HasWeapon(SecondaryWeaponIndex weapon) const {
            return SecondaryAmmo[(int)weapon] > 0;
            //return SecondaryWeapons & (1 << (uint16)weapon);
        }

        void GivePowerup(PowerupFlag powerup) {
            SetFlag(Powerups, powerup);
        }

        bool HasPowerup(PowerupFlag powerup) const {
            return HasFlag(Powerups, powerup);
        }

        void RemovePowerup(PowerupFlag powerup) {
            ClearFlag(Powerups, powerup);
        }

        void SetPowerup(PowerupFlag powerup, bool state) {
            if (state) GivePowerup(powerup);
            else RemovePowerup(powerup);
        }

        void TouchPowerup(Object& obj);
        void TouchObject(Object& obj);

        // Gives energy and returns true if able to pick up a powerup
        bool PickUpEnergy();
        bool PickUpPrimary(PrimaryWeaponIndex);
        bool PickUpSecondary(SecondaryWeaponIndex, uint16 count = 1);
        void DoDeathSequence(float dt);
        // Drops all items after dying
        void DropAllItems();

        // Returns the amount of ammo picked up
        int PickUpAmmo(PrimaryWeaponIndex, uint16 amount);

        bool CanFirePrimary(PrimaryWeaponIndex index) const;

        bool CanFireSecondary(SecondaryWeaponIndex index) const;

        // returns the forward thrust multiplier
        float UpdateAfterburner(float dt, bool active);

        void SelectPrimary(PrimaryWeaponIndex index);
        void SelectSecondary(SecondaryWeaponIndex index);

        void Update(float dt);
        void UpdateFireState();

        void FireFlare();
        void FirePrimary();
        void HoldPrimary();
        void ReleasePrimary();

        void FireSecondary();

        bool CanOpenDoor(const Wall& wall) const;

        void AutoselectPrimary();
        void AutoselectSecondary();
        void GiveExtraLife(uint8 lives = 1);

        void ApplyDamage(float damage, bool playSound);
        void Respawn(bool died);
        void StartNewLevel(bool secret);
        float GetShipVisibility() const;

    private:
        float GetWeaponEnergyCost(const Weapon& weapon) const;

        WeaponID GetPrimaryWeaponID(PrimaryWeaponIndex index) const {
            if (index == PrimaryWeaponIndex::Laser) {
                if (LaserLevel < 4) return WeaponID{ (int)WeaponID::Laser1 + LaserLevel };
                if (LaserLevel == 4) return WeaponID::Laser5;
                if (LaserLevel == 5) return WeaponID::Laser6;
            }

            return PrimaryToWeaponID[(int)index];
        }

        static WeaponID GetSecondaryWeaponID(SecondaryWeaponIndex index) {
            return SecondaryToWeaponID[(int)index];
        }
    };
}
