#include "pch.h"
#include "Game.Player.h"
#include "Game.h"

namespace Inferno {
    constexpr uint8 SUPER_WEAPON = 5;

    void Player::ArmPrimary(PrimaryWeaponIndex index) {
        const uint8 requestedWeapon = (uint8)index;
        uint8 weapon = (uint8)index;

        if (index == Primary && Game::Level.IsDescent1()) {
            Sound::Play(Resources::GetSoundResource(SoundID::AlreadySelected));
            return;
        }

        if (Primary == index || Primary == PrimaryWeaponIndex((uint8)index + SUPER_WEAPON)) {
            // Weapon already selected, toggle super version
            weapon = 2 * weapon + SUPER_WEAPON - (uint8)Primary;
        }
        else {
            if (PrimaryWasSuper[(int)index])
                weapon += SUPER_WEAPON;

            // Try other version if we don't have it anymore
            if (!HasWeapon(index)) {
                weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
                if (!HasWeapon((PrimaryWeaponIndex)weapon))
                    weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
            }
        }

        if (!HasWeapon((PrimaryWeaponIndex)weapon)) {
            auto dontHave = Resources::GetStringTableEntry(StringTableEntry::DontHave);
            auto msg = fmt::format("{} {}!", dontHave, Resources::GetPrimaryName(index));
            PrintHudMessage(msg);
            Sound::Play(Resources::GetSoundResource(SoundID::SelectFail));
            return;
        }

        Sound::Play(Resources::GetSoundResource(SoundID::SelectPrimary));
        PrimaryDelay = RearmTime;
        Primary = (PrimaryWeaponIndex)weapon;
        PrimaryWasSuper[weapon % SUPER_WEAPON] = weapon >= SUPER_WEAPON;
    }

    void Player::ArmSecondary(SecondaryWeaponIndex index) {
        const uint8 requestedWeapon = (uint8)index;
        uint8 weapon = (uint8)index;

        if (index == Secondary && Game::Level.IsDescent1()) {
            Sound::Play(Resources::GetSoundResource(SoundID::AlreadySelected));
            return;
        }

        if (Secondary == index || Secondary == SecondaryWeaponIndex((uint8)index + SUPER_WEAPON)) {
            // Weapon already selected, toggle super version
            weapon = 2 * weapon + SUPER_WEAPON - (uint8)Secondary;
        }
        else {
            if (SecondaryWasSuper[(int)index])
                weapon += SUPER_WEAPON;

            // Try other version if we don't have it anymore
            if (!HasWeapon(index)) {
                weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
                if (!HasWeapon((SecondaryWeaponIndex)weapon))
                    weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
            }
        }

        if (!HasWeapon((SecondaryWeaponIndex)weapon)) {
            auto haveNo = Resources::GetStringTableEntry(StringTableEntry::HaveNo);
            auto sx = Resources::GetStringTableEntry(StringTableEntry::Sx);
            auto msg = fmt::format("{} {}{}!", haveNo, Resources::GetSecondaryName(index), sx);
            PrintHudMessage(msg);
            Sound::Play(Resources::GetSoundResource(SoundID::SelectFail));
            return;
        }

        Sound::Play(Resources::GetSoundResource(SoundID::SelectSecondary));
        SecondaryDelay = RearmTime;
        Secondary = (SecondaryWeaponIndex)weapon;
        SecondaryWasSuper[weapon % SUPER_WEAPON] = weapon >= SUPER_WEAPON;
    }

    void Player::FirePrimary() {
        if (!CanFirePrimary()) {
            // Arm different weapon
            return;
        }

        auto id = GetPrimaryWeaponID();
        auto& weapon = Resources::GameData.Weapons[(int)id];
        PrimaryDelay = weapon.FireDelay;
        if (Primary == PrimaryWeaponIndex::Vulcan ||
            Primary == PrimaryWeaponIndex::Gauss) {
            Game::FirePlayerWeapon(Game::Level, ObjID(0), 7, id);
            PrimaryAmmo[1] -= weapon.AmmoUsage;
        }
        else {
            Game::FirePlayerWeapon(Game::Level, ObjID(0), 0, id);
            Game::FirePlayerWeapon(Game::Level, ObjID(0), 1, id);

            if (HasPowerup(PowerupFlag::QuadLasers) && Primary == PrimaryWeaponIndex::Laser) {
                Game::FirePlayerWeapon(Game::Level, ObjID(0), 2, id);
                Game::FirePlayerWeapon(Game::Level, ObjID(0), 3, id);
            }
        }

        // Swap to different weapon if ammo or energy == 0
    }

    void Player::FireSecondary() {
        if (!CanFireSecondary()) return;

        auto id = GetSecondaryWeaponID();
        auto& weapon = Resources::GameData.Weapons[(int)id];
        SecondaryDelay = weapon.FireDelay;
        MissileGunpoint = (MissileGunpoint + 1) % 2;
        SecondaryAmmo[(int)Secondary] -= weapon.AmmoUsage;
        Game::FirePlayerWeapon(Game::Level, ObjID(0), MissileGunpoint, id);
        // Swap to different weapon if ammo == 0
    }
}