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
            auto dontHave = Resources::GetString(StringTableEntry::DontHave);
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
            auto haveNo = Resources::GetString(StringTableEntry::HaveNo);
            auto sx = Resources::GetString(StringTableEntry::Sx);
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

    Vector2 GetHelixOffset(int index) {
        switch (index) {
            default:
            case 0: return { 1 / 16.0f, 0 };
            case 1: return { 1 / 17.0f, 1 / 42.0f };
            case 2: return { 1 / 22.0f, 1 / 22.0f };
            case 3: return { 1 / 42.0f, 1 / 17.0f };
            case 4: return { 0, 1 / 16.0f };
            case 5: return { -1 / 42.0f, 1 / 17.0f };
            case 6: return { -1 / 22.0f, 1 / 22.0f };
            case 7: return { -1 / 17.0f, 1 / 42.0f };
        }
    }

    void Player::FirePrimary() {
        if (!CanFirePrimary()) {
            // Arm different weapon
            return;
        }

        auto id = GetPrimaryWeaponID();
        auto& weapon = Resources::GameData.Weapons[(int)id];
        PrimaryDelay = weapon.FireDelay;
        switch (Primary) {
            //case PrimaryWeaponIndex::Laser:
            //case PrimaryWeaponIndex::SuperLaser:
            //case PrimaryWeaponIndex::Plasma:
            //    break;

            case PrimaryWeaponIndex::Vulcan:
            case PrimaryWeaponIndex::Gauss:
                if (Primary == PrimaryWeaponIndex::Vulcan) {
                    // -0.03125 to 0.03125 spread
                    Vector2 spread = { RandomN11() / 32, RandomN11() / 32 };
                    Game::FireWeapon(ID, 7, id, true, spread);
                }
                else {
                    Game::FireWeapon(ID, 7, id);
                }
                PrimaryAmmo[1] -= weapon.AmmoUsage * 13; // Not exact usage compared to original
                break;

            case PrimaryWeaponIndex::Spreadfire:
            {
                constexpr float SPREAD_ANGLE = 1 / 16.0f;
                if (SpreadfireToggle) {
                    Game::FireWeapon(ID, 6, id);
                    Game::FireWeapon(ID, 6, id, false, { 0, -SPREAD_ANGLE });
                    Game::FireWeapon(ID, 6, id, false, { 0, SPREAD_ANGLE });
                }
                else {
                    Game::FireWeapon(ID, 6, id);
                    Game::FireWeapon(ID, 6, id, false, { -SPREAD_ANGLE, 0 });
                    Game::FireWeapon(ID, 6, id, false, { SPREAD_ANGLE, 0 });
                }
                SpreadfireToggle = !SpreadfireToggle;
                break;
            }
            case PrimaryWeaponIndex::Helix:
            {
                HelixOrientation = (HelixOrientation + 1) % 8;
                auto offset = GetHelixOffset(HelixOrientation);
                Game::FireWeapon(ID, 6, id);
                Game::FireWeapon(ID, 6, id, false, offset);
                Game::FireWeapon(ID, 6, id, false, offset * 2);
                Game::FireWeapon(ID, 6, id, false, -offset);
                Game::FireWeapon(ID, 6, id, false, -offset * 2);
                break;
            }

            //case PrimaryWeaponIndex::Fusion:
                //break;
            //case PrimaryWeaponIndex::Phoenix:
                //break;
            //case PrimaryWeaponIndex::Omega:
                //break;
            default:
                Game::FireWeapon(ID, 0, id);
                Game::FireWeapon(ID, 1, id);

                if (HasPowerup(PowerupFlag::QuadLasers) && Primary == PrimaryWeaponIndex::Laser) {
                    Game::FireWeapon(ID, 2, id);
                    Game::FireWeapon(ID, 3, id);
                }
                break;
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
        Game::FireWeapon(ID, MissileGunpoint, id);
        // Swap to different weapon if ammo == 0
    }
}