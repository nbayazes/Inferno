#include "pch.h"
#include "Game.Player.h"
#include "VisualEffects.h"
#include "Game.AI.h"
#include "Game.Bindings.h"
#include "Game.h"
#include "HUD.h"
#include "Input.h"
#include "Physics.h"
#include "Resources.h"
#include "Settings.h"
#include "SoundSystem.h"
#include "Graphics/Render.h"

namespace Inferno {
    constexpr uint8 SUPER_WEAPON = 5;
    using Game::MAX_FLASH;
    using Game::AddScreenFlash;
    constexpr auto FLASH = MAX_FLASH / 2;
    constexpr Color FLASH_PRIMARY = { FLASH / 3, FLASH / 2, FLASH };
    constexpr Color FLASH_WHITE = { FLASH, FLASH, FLASH };
    constexpr Color FLASH_LASER_POWERUP = { FLASH * 0.66f, 0, FLASH * 0.66f };
    constexpr Color FLASH_BLUE = { 0, 0, FLASH };
    constexpr Color FLASH_RED = { FLASH, 0, 0 };
    constexpr Color FLASH_GOLD = { FLASH * 0.9f, FLASH * 0.9f, FLASH * 0.4f };
    constexpr Color FLASH_POWERUP = { FLASH, 0, FLASH };
    constexpr Color FLASH_FUSION_CHARGE = { MAX_FLASH * Game::TICK_RATE * 2.0f, 0, MAX_FLASH * Game::TICK_RATE * 2.0f };

    // Returns a value indicating the weapon's priority. Lower values are higher priority. 255 is disabled.
    int GetWeaponPriority(PrimaryWeaponIndex primary) {
        for (int i = 0; i < Game::PrimaryPriority.size(); i++) {
            if (i == 255) return 255;
            if (Game::PrimaryPriority[i] == (int)primary) {
                return i;
            }
        }

        return 0;
    }

    float GetWeaponSoundRadius(const Weapon& weapon) {
        // Robots use half-linear falloff instead of inverse square because it doesn't require traversing nearly as far.
        float mult = 0.5f + std::min(2, (int)Game::Difficulty) * 0.25f; // hotshot, ace, insane = 1
        return weapon.Extended.SoundRadius * mult * 0.75f;
    }

    float Player::UpdateAfterburner(float dt, bool active) {
        if (!HasPowerup(PowerupFlag::Afterburner)) return 0;

        float thrust = 0;

        // AB keeps draining energy if button is held when empty even though it doesn't do anything.
        // This is the original behavior.
        if (active) {
            constexpr float AFTERBURNER_USE_SECS = 3;
            constexpr float USE_SPEED = 1 / 15.0f / AFTERBURNER_USE_SECS;

            float oldCount = AfterburnerCharge / USE_SPEED;
            AfterburnerCharge -= dt / AFTERBURNER_USE_SECS;

            if (AfterburnerCharge < 0) AfterburnerCharge = 0;
            float count = AfterburnerCharge / USE_SPEED;

            if (oldCount != count) {} // drop blobs
            thrust = 1 + std::min(0.5f, AfterburnerCharge) * 2; // Falloff from 2 under 50% charge
        }
        else if (AfterburnerCharge < 1) {
            float chargeUp = std::min(dt / 8, 1 - AfterburnerCharge); // 8 second recharge
            float energy = std::max(Energy - 10, 0.0f); // don't drop below 10 energy
            chargeUp = std::min(chargeUp, energy / 10); // limit charge if <= 10 energy
            AfterburnerCharge += chargeUp;
            if (AfterburnerCharge > 1) AfterburnerCharge = 1;

            SubtractEnergy(chargeUp * 100 / 10); // full charge uses 10% energy
        }

        if (AfterburnerCharge <= 0 && active)
            active = false; // ran out of charge

        // AB button held
        if (active && !AfterburnerActive) {
            Sound3D sound(SoundID::AfterburnerIgnite);
            sound.Radius = 125;
            sound.LoopStart = 32027;
            sound.LoopEnd = 48452;
            sound.Looped = true;
            _afterburnerSoundSig = Sound::PlayFrom(sound, Game::GetPlayerObject());
            //Render::Camera.Shake(2.0f);
        }

        // AB button released
        if (!active && AfterburnerActive) {
            Sound::Stop(_afterburnerSoundSig);
            Sound3D sound(SoundID::AfterburnerStop);
            sound.Radius = 125;
            Sound::PlayFrom(sound, Game::GetPlayerObject());
        }

        AfterburnerActive = active;
        _prevAfterburnerCharge = AfterburnerCharge;
        return thrust;
    }

    void Player::SelectPrimary(PrimaryWeaponIndex index) {
        const auto requestedWeapon = (uint8)index;
        auto weapon = (uint8)index;

        if (index == Primary && Game::Level.IsDescent1()) {
            Sound::Play2D({ SoundID::AlreadySelected });
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
            if (!HasWeapon((PrimaryWeaponIndex)weapon)) {
                weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
                if (!HasWeapon((PrimaryWeaponIndex)weapon))
                    weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
            }
        }

        if (!HasWeapon((PrimaryWeaponIndex)weapon)) {
            auto msg = fmt::format("you don't have the {}!", Resources::GetPrimaryName(index));
            PrintHudMessage(msg);
            Sound::Play2D({ SoundID::SelectFail });
            return;
        }

        // Shareware doesn't have the selection sounds, use the selection beep
        if (Game::Level.IsShareware)
            Sound::Play2D(SoundID::AlreadySelected, 0.75f, 0, 0.15f);
        else
            Sound::Play2D(SoundID::SelectPrimary);

        PrimaryDelay = RearmTime;
        Primary = (PrimaryWeaponIndex)weapon;
        PrimaryWasSuper[weapon % SUPER_WEAPON] = weapon >= SUPER_WEAPON;
        PrintHudMessage(fmt::format("{} selected!", Resources::GetPrimaryName(Primary)));

        WeaponCharge = 0; // failsafe
    }

    void Player::SelectSecondary(SecondaryWeaponIndex index) {
        const auto requestedWeapon = (uint8)index;
        auto weapon = (uint8)index;

        if (index == Secondary && Game::Level.IsDescent1()) {
            Sound::Play2D({ SoundID::AlreadySelected });
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
            if (!CanFireSecondary((SecondaryWeaponIndex)weapon)) {
                weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
                if (!CanFireSecondary((SecondaryWeaponIndex)weapon))
                    weapon = 2 * requestedWeapon + SUPER_WEAPON - weapon;
            }
        }

        if (!CanFireSecondary((SecondaryWeaponIndex)weapon)) {
            auto msg = fmt::format("you have no {}s!", Resources::GetSecondaryName(index));
            PrintHudMessage(msg);
            Sound::Play2D({ SoundID::SelectFail });
            return;
        }

        // Shareware doesn't have the selection sounds, use the selection beep
        if (Game::Level.IsShareware)
            Sound::Play2D(SoundID::AlreadySelected, 0.75f, 0, 0.15f);
        else
            Sound::Play2D(SoundID::SelectSecondary);

        SecondaryDelay = RearmTime;
        Secondary = (SecondaryWeaponIndex)weapon;
        SecondaryWasSuper[weapon % SUPER_WEAPON] = weapon >= SUPER_WEAPON;

        PrintHudMessage(fmt::format("{} selected!", Resources::GetSecondaryName(Secondary)));
    }


    void Player::UpdateFireState() {
        auto toggleState = [](FireState& state, bool buttonDown) {
            if (buttonDown) {
                if (state == FireState::None || state == FireState::Release)
                    state = FireState::Press;
                else if (state == FireState::Press)
                    state = FireState::Hold;
            }
            else {
                if (state == FireState::Release)
                    state = FireState::None;
                else if (state != FireState::None)
                    state = FireState::Release;
            }
        };

        // must check held keys inside of fixed updates so events aren't missed
        // due to the state changing on a frame that doesn't have a game tick
        toggleState(PrimaryState, Game::Bindings.Pressed(GameAction::FirePrimary));
        toggleState(SecondaryState, Game::Bindings.Pressed(GameAction::FireSecondary));
    }

    void Player::Update(float dt) {
        PrimaryDelay -= dt;
        SecondaryDelay -= dt;

        UpdateFireState();
        if (Game::Level.Objects.empty()) return;

        auto& weapon = Resources::GetWeapon(GetPrimaryWeaponID(Primary));

        if (IsDead) {
            // Fire the fusion cannon if the ship is destroyed while charging it
            if (weapon.Extended.Chargable && WeaponCharge > 0) {
                Sound::Stop(_fusionChargeSound);
                FirePrimary();
                WeaponCharge = 0;
            }
            return;
        }

        auto& player = Game::GetPlayerObject();

        if (HasPowerup(PowerupFlag::Cloak) && player.Effects.CloakTimer >= player.Effects.CloakDuration)
            RemovePowerup(PowerupFlag::Cloak); // Cloak sound is handled by effect updates

        if (auto seg = Game::Level.TryGetSegment(player.Segment)) {
            if (seg->Type == SegmentType::Energy && Energy < 100) {
                constexpr float ENERGY_PER_SECOND = 25.0f;
                AddEnergy(ENERGY_PER_SECOND * dt);

                if (RefuelSoundTime <= Game::Time) {
                    Sound::Play2D({ SoundID::Refuel }, 0.5f);
                    constexpr float REFUEL_SOUND_DELAY = 0.25f;
                    RefuelSoundTime = Game::Time + REFUEL_SOUND_DELAY;
                }
            }
        }

        if (weapon.Extended.Chargable) {
            if (PrimaryState == FireState::Hold && WeaponCharge <= 0) {
                if (CanFirePrimary(Primary) && PrimaryDelay <= 0) {
                    WeaponCharge = 0.001f;
                    FusionNextSoundDelay = 0.25f;
                    SubtractEnergy(GetWeaponEnergyCost(weapon));
                }
            }
            else if (PrimaryState == FireState::Hold && Energy > 0 && WeaponCharge > 0) {
                SubtractEnergy(dt); // 1 energy cost per second
                WeaponCharge += dt;
                if (Energy <= 0) {
                    Energy = 0;
                }

                AddScreenFlash(FLASH_FUSION_CHARGE);
                FusionNextSoundDelay -= dt;

                float physicsMult = std::min(1 + WeaponCharge * 0.5f, 4.0f);

                if (FusionNextSoundDelay < 0) {
                    if (WeaponCharge > weapon.Extended.MaxCharge) {
                        // Self damage
                        Sound::PlayFrom({ SoundID::Explosion }, player);
                        constexpr float OVERCHARGE_DAMAGE = 3.0f;
                        ApplyDamage(Random() * OVERCHARGE_DAMAGE, false);
                    }
                    else {
                        Sound3D sound(SoundID::FusionWarmup);
                        sound.Volume = .6f;
                        Sound::PlayFrom(sound, player);
                        AlertRobotsOfNoise(player, Game::PLAYER_FUSION_SOUND_RADIUS, 0.5f);
                    }

                    if (auto fx = EffectLibrary.GetSparks("fusion_charge")) {
                        AttachSparkEmitter(*fx, Reference, GetGunpointOffset(player, 0));
                        AttachSparkEmitter(*fx, Reference, GetGunpointOffset(player, 1));
                        LightEffectInfo lightInfo{ .FadeTime = 0.1f, .Radius = 50.0f, .LightColor = fx->Color };
                        lightInfo.LightColor.w = 3;
                        AddLight(lightInfo, player.GetPosition(Game::LerpAmount), 0.25f, player.Segment);
                    }

                    FusionNextSoundDelay = 0.125f + Random() / 8;

                    // Shake the player while charging
                    //player.Physics.AngularVelocity.x = RandomN11() * .02f * physicsMult;
                    //player.Physics.AngularVelocity.z = RandomN11() * .02f * physicsMult;
                }

                auto dir = RandomVector(Game::FUSION_SHAKE_STRENGTH * physicsMult);
                ApplyForce(player, dir);
            }
            else if (PrimaryState == FireState::Release || Energy <= 0) {
                if (WeaponCharge > 0) {
                    Sound::Stop(_fusionChargeSound);
                    FirePrimary();
                }
            }
        }
        else if (PrimaryState == FireState::Hold) {
            FirePrimary();
        }

        if (SecondaryState == FireState::Hold || SecondaryState == FireState::Press) {
            FireSecondary();
        }

        if (Energy > 0 && OmegaCharge < 1 &&
            LastPrimaryFireTime + OMEGA_RECHARGE_DELAY < Game::Time) {
            // Recharge omega
            float chargeUp = std::min(dt / OMEGA_RECHARGE_TIME, 1 - OmegaCharge);
            OmegaCharge += chargeUp;
            AddEnergy(-chargeUp * OMEGA_RECHARGE_ENERGY);
        }
    }

    constexpr float FLARE_FIRE_DELAY = 0.25f;

    void Player::FireFlare() {
        if (_nextFlareFireTime > Game::Time) return;
        Game::FireWeapon(Game::GetPlayerObject(), WeaponID::Flare, 6);
        auto& weapon = Resources::GetWeapon(WeaponID::Flare);
        _nextFlareFireTime = Game::Time + weapon.FireDelay;
        AlertRobotsOfNoise(Game::GetPlayerObject(), GetWeaponSoundRadius(weapon), weapon.Extended.Noise);
    }

    void Player::GiveWeapon(PrimaryWeaponIndex weapon) {
        PrimaryWeapons |= 1 << (uint16)weapon;
        if (weapon == PrimaryWeaponIndex::Vulcan || weapon == PrimaryWeaponIndex::Gauss)
            PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan] += Game::VULCAN_AMMO_PICKUP;
    }

    SecondaryWeaponIndex Player::GetActiveBomb() const {
        return BombIndex == 0 || Game::Level.IsDescent1() ? SecondaryWeaponIndex::ProximityMine : SecondaryWeaponIndex::SmartMine;
    }

    void Player::CyclePrimary() {
        auto newIndex = Primary;
        const auto count = Game::Level.IsDescent1() ? 5 : 10;

        for (int i = 1; i < count; i++) {
            auto offset = (int)Primary + i;
            offset = offset % count;
            auto index = PrimaryWeaponIndex(offset);

            if (HasWeapon(index)) {
                newIndex = index;
                break;
            }
        }

        if (Primary == newIndex) {
            Sound::Play2D({ SoundID::SelectFail });
            return;
        }

        SelectPrimary(newIndex);
    }

    void Player::CycleSecondary() {
        auto newIndex = Secondary;
        const auto count = Game::Level.IsDescent1() ? 5 : 10;

        for (int i = 1; i < count; i++) {
            auto offset = (int)Secondary + i;
            offset = offset % count;
            auto index = SecondaryWeaponIndex(offset);
            if (index == SecondaryWeaponIndex::ProximityMine)
                continue; // Don't autoselect prox mines

            if (HasWeapon(index)) {
                newIndex = index;
                break;
            }
        }

        if (Secondary == newIndex) {
            Sound::Play2D({ SoundID::SelectFail });
            return;
        }

        SelectSecondary(newIndex);
    }

    void Player::CycleBombs() {
        if (Game::Level.IsDescent1()) {
            BombIndex = 0;
            Sound::Play2D({ SoundID::SelectFail });
            return;
        }

        auto proxAmmo = SecondaryAmmo[(int)SecondaryWeaponIndex::ProximityMine];
        auto smartAmmo = SecondaryAmmo[(int)SecondaryWeaponIndex::SmartMine];

        if (BombIndex == 0 && smartAmmo > 0) {
            BombIndex = 1;
            Sound::Play2D({ SoundID::SelectSecondary });
        }
        else if (BombIndex == 1 && proxAmmo > 0) {
            BombIndex = 0;
            Sound::Play2D({ SoundID::SelectSecondary });
        }
        else {
            Sound::Play2D({ SoundID::SelectFail });
        }
    }

    void Player::DropBomb() {
        auto bomb = GetActiveBomb();
        auto& ammo = SecondaryAmmo[(int)bomb];
        if (ammo == 0) {
            Sound::Play2D({ SoundID::SelectFail });
            PrintHudMessage("you have no bombs!");
            return;
        }

        auto id = GetSecondaryWeaponID(bomb);
        auto& weapon = Resources::GameData.Weapons[(int)id];
        Game::FireWeapon(Game::GetPlayerObject(), id, 7);
        ammo -= (uint16)weapon.AmmoUsage;

        // Switch active bomb type if ran out of ammo
        if (ammo == 0 && !Game::Level.IsDescent1()) {
            if (BombIndex == 0 && SecondaryAmmo[(int)SecondaryWeaponIndex::SmartMine]) {
                BombIndex = 1;
                Sound::Play2D({ SoundID::SelectSecondary });
            }
            else if (BombIndex == 1 && SecondaryAmmo[(int)SecondaryWeaponIndex::ProximityMine]) {
                BombIndex = 0;
                Sound::Play2D({ SoundID::SelectSecondary });
            }
        }
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
        if (PrimaryDelay > 0)
            return;

        if (!CanFirePrimary(Primary) && WeaponCharge <= 0) {
            AutoselectPrimary();
            return;
        }

        // must do a different check for omega so running out of charge doesn't cause an autoswap
        if (Primary == PrimaryWeaponIndex::Omega && OmegaCharge < OMEGA_CHARGE_COST)
            return;

        auto id = GetPrimaryWeaponID(Primary);
        auto& weapon = Resources::GetWeapon(id);
        PrimaryDelay = weapon.FireDelay;

        // Charged weapons drain energy on button down instead of here
        if (!weapon.Extended.Chargable) {
            SubtractEnergy(GetWeaponEnergyCost(weapon));
            PrimaryAmmo[1] -= (uint16)weapon.AmmoUsage; // only vulcan ammo
        }

        auto& sequence = Ship.Weapons[(int)Primary].Firing;
        if (FiringIndex >= sequence.size()) FiringIndex = 0;

        for (uint8 i = 0; i < 8; i++) {
            bool quadFire = HasPowerup(PowerupFlag::QuadFire) && Ship.Weapons[(int)Primary].QuadGunpoints[i];
            if (sequence[FiringIndex].Gunpoints[i] || quadFire) {
                auto& behavior = Game::GetWeaponBehavior(weapon.Extended.Behavior);
                behavior(*this, i, id);
            }
        }

        FiringIndex = (FiringIndex + 1) % sequence.size();
        WeaponCharge = 0;
        LastPrimaryFireTime = Game::Time;

        AlertRobotsOfNoise(Game::GetPlayerObject(), GetWeaponSoundRadius(weapon), weapon.Extended.Noise);

        if (!CanFirePrimary(Primary) && Primary != PrimaryWeaponIndex::Omega)
            AutoselectPrimary();
    }

    void Player::HoldPrimary() {}

    void Player::ReleasePrimary() {
        //auto id = GetPrimaryWeaponID();
        //auto& weapon = Resources::GameData.Weapons[(int)id];
    }

    void Player::FireSecondary() {
        if (SecondaryDelay > 0) return;
        if (!CanFireSecondary(Secondary)) {
            AutoselectSecondary();
            return;
        }

        auto id = GetSecondaryWeaponID(Secondary);
        auto& weapon = Resources::GameData.Weapons[(int)id];
        SecondaryDelay = weapon.FireDelay;
        auto& ship = PyroGX;

        auto& sequence = ship.Weapons[10 + (int)Secondary].Firing;
        if (MissileFiringIndex >= sequence.size()) MissileFiringIndex = 0;

        for (uint8 i = 0; i < 8; i++) {
            if (sequence[MissileFiringIndex].Gunpoints[i])
                Game::FireWeapon(Game::GetPlayerObject(), id, i);
        }

        MissileFiringIndex = (MissileFiringIndex + 1) % 2;
        SecondaryAmmo[(int)Secondary] -= (uint16)weapon.AmmoUsage;
        AlertRobotsOfNoise(Game::GetPlayerObject(), GetWeaponSoundRadius(weapon), weapon.Extended.Noise);

        if (!CanFireSecondary(Secondary))
            AutoselectSecondary(); // Swap to different weapon if out of ammo
    }

    bool Player::CanOpenDoor(const Wall& wall) const {
        if (wall.Type != WallType::Door || wall.HasFlag(WallFlag::DoorLocked))
            return false;

        if (HasFlag(wall.Keys, WallKey::Red) && !HasPowerup(PowerupFlag::RedKey))
            return false;

        if (HasFlag(wall.Keys, WallKey::Blue) && !HasPowerup(PowerupFlag::BlueKey))
            return false;

        if (HasFlag(wall.Keys, WallKey::Gold) && !HasPowerup(PowerupFlag::GoldKey))
            return false;

        return true;
    }

    void Player::AutoselectPrimary() {
        int priority = -1;
        int index = -1;
        const int numWeapons = Game::Level.IsDescent1() ? 5 : 10;

        for (int i = 0; i < numWeapons; i++) {
            auto idx = (PrimaryWeaponIndex)i;
            auto& weapon = Resources::GetWeapon(PrimaryToWeaponID[i]);
            if (weapon.EnergyUsage > 0 && Energy < 1)
                continue; // don't switch to energy weapons at low energy

            if (!CanFirePrimary(idx)) continue;

            auto p = GetWeaponPriority(idx);
            if (p == 255) continue;

            if (p < priority || priority == -1) {
                priority = p;
                index = i;
            }
        }

        if (index == -1) {
            PrintHudMessage("no primary weapons available!");
            // play sound first time this happens?
            return;
        }

        if (index == (int)Primary && Game::Level.IsDescent1())
            return; // Weapon already selected

        SelectPrimary(PrimaryWeaponIndex(index));
    }

    void Player::AutoselectSecondary() {
        auto getPriority = [](SecondaryWeaponIndex secondary) {
            for (int i = 0; i < Game::SecondaryPriority.size(); i++) {
                auto prio = Game::SecondaryPriority[i];
                if (prio == 255) return 255;
                if (prio == (int)secondary) return i;
            }
            return 0;
        };

        int priority = -1;
        int index = -1;
        const int numWeapons = Game::Level.IsDescent1() ? 5 : 10;

        for (int i = 0; i < numWeapons; i++) {
            auto idx = (SecondaryWeaponIndex)i;
            if (!CanFireSecondary(idx)) continue;

            auto p = getPriority(idx);
            if (p == 255) continue;

            if (p < priority || priority == -1) {
                priority = p;
                index = i;
            }
        }

        if (index == -1) {
            PrintHudMessage("no secondary weapons available!");
            return;
        }

        if (index == (int)Secondary && Game::Level.IsDescent1())
            return; // Weapon already selected

        SelectSecondary(SecondaryWeaponIndex(index));
    }

    void Player::GiveExtraLife(uint8 lives) {
        Lives += lives;
        PrintHudMessage("extra life!");
        AddScreenFlash(FLASH_WHITE);
    }

    void Player::ApplyDamage(float damage, bool playSound) {
        //if (Endlevel_sequence)
        //    return;

        // Keep player shields in sync with the object that represents it
        if (auto player = Game::Level.TryGetObject(Reference)) {
            constexpr float SCALE = 20;
            auto flash = std::max(damage / SCALE, 0.1f);

            if (!IsDead) {
                if (player->IsInvulnerable() || Settings::Cheats.DisableWeaponDamage) {
                    AddScreenFlash({ 0, 0, flash });
                }
                else {
                    Shields -= damage;
                    AddScreenFlash({ flash, -flash, -flash });
                }
            }

            if (Shields < 0) {
                IsDead = true;
                Input::ResetState(); // Reset state so fusion charging releases
            }

            player->HitPoints = Shields;

            if (playSound) {
                auto soundId = player->IsInvulnerable() ? SoundID::HitInvulnerable : SoundID::HitPlayer;
                Sound3D sound(soundId);
                Sound::Play(sound, player->Position, player->Segment);
            }
        }
    }

    void Player::ResetInventory() {
        LaserLevel = 0;
        PrimaryWeapons = 0;
        SecondaryWeapons = 0;
        Primary = PrimaryWeaponIndex::Laser;
        Secondary = SecondaryWeaponIndex::Concussion;
        PrimarySwapTime = PrimaryDelay = 0;
        SecondarySwapTime = SecondaryDelay = 0;
        Shields = 100;
        Energy = 100;

        for (int i = 0; i < 10; i++) {
            SecondaryAmmo[i] = 0;
            PrimaryAmmo[i] = 0;
        }
    }

    // Respawns the player at the current start location. Call with true to fully reset inventory.
    void Player::Respawn(bool died) {
        Input::ResetState(); // Clear input events so firing doesn't cause a shot on spawn

        Game::FreeObject(Game::DeathCamera.Id);
        Game::DeathCamera = {};

        // Reset shields and energy to at least 100
        Shields = std::max(Shields, 100.0f);
        Energy = std::max(Energy, 100.0f);

        if (Settings::Cheats.LowShields) {
            Shields = 10;
            Energy = 10;
        }

        auto& player = Game::GetPlayerObject();
        HostagesOnShip = 0;
        _prevAfterburnerCharge = AfterburnerCharge = 1;
        AfterburnerActive = false;
        WeaponCharge = 0;
        KilledBy = {};
        HomingObjectDist = -1;
        IsDead = false;
        TimeDead = 0;
        Exploded = false;
        PrimaryDelay = SecondaryDelay = 0;
        _nextFlareFireTime = 0;
        RefuelSoundTime = 0;

        player.Effects = {};
        player.Physics.Wiggle = Resources::GameData.PlayerShip.Wiggle;

        player.Position = SpawnPosition;
        player.Rotation = SpawnRotation;
        player.Physics.AngularVelocity = Vector3::Zero;
        player.Physics.AngularThrust = Vector3::Zero;
        player.Physics.AngularAcceleration = Vector3::Zero;
        player.Physics.BankState = PhysicsData().BankState;
        player.Physics.TurnRoll = 0;
        player.Type = ObjectType::Player;
        player.Render.Type = RenderType::None; // Hide the player model

        RelinkObject(Game::Level, player, SpawnSegment == SegID::None ? player.Segment : SpawnSegment);

        // Max vulcan ammo changes between D1 and D2
        PyroGX.Weapons[(int)PrimaryWeaponIndex::Vulcan].MaxAmmo = Game::Level.IsDescent1() ? 10000 : 20000;

        if (died) {
            ResetInventory();

            // Play respawn effect
            ParticleInfo p{};
            p.Clip = VClipID::PlayerSpawn;
            p.Radius = player.Radius;
            p.RandomRotation = false;
            Vector3 position = player.Position + player.Rotation.Forward() * 3;
            AddParticle(p, player.Segment, position);

            auto& vclip = Resources::GetVideoClip(VClipID::PlayerSpawn);
            Sound3D sound(vclip.Sound);
            Sound::Play(sound, player.Position, player.Segment);
        }

        GiveWeapon(PrimaryWeaponIndex::Laser);

        // Give the player some free missiles
        auto& concussions = SecondaryAmmo[(int)SecondaryWeaponIndex::Concussion];
        concussions = std::max(concussions, uint16(2 + (int)DifficultyLevel::Count - (int)Game::Difficulty));

        if (Settings::Cheats.Invulnerable)
            Game::MakeInvulnerable(player, -1, false);

        if (Settings::Cheats.Cloaked)
            Game::CloakObject(player, -1, false);

        if (Settings::Cheats.FullyLoaded) {
            GivePowerup(PowerupFlag::Afterburner);
            GivePowerup(PowerupFlag::QuadFire);
            LaserLevel = Game::Level.IsDescent2() ? 5 : 3;

            if (Game::Level.IsDescent2()) {
                GivePowerup(PowerupFlag::AmmoRack);
                GivePowerup(PowerupFlag::Headlight);
                GivePowerup(PowerupFlag::FullMap);
            }

            PrimaryWeapons = 0xffff;
            SecondaryWeapons = 0xffff;
            int weaponCount = Game::Level.IsDescent2() ? 10 : 5;

            for (int i = 0; i < weaponCount; i++) {
                if (i == (int)SecondaryWeaponIndex::ProximityMine || i == (int)SecondaryWeaponIndex::SmartMine)
                    SecondaryAmmo[i] = 99;
                else
                    SecondaryAmmo[i] = 200;

                PrimaryAmmo[i] = 10000;
            }
        }

        ResetHUD();
        SPDLOG_INFO("Respawning player");
    }

    void Player::StartNewLevel(bool secret) {
        if (!secret) {
            // Clear keys on level load
            RemovePowerup(PowerupFlag::BlueKey);
            RemovePowerup(PowerupFlag::GoldKey);
            RemovePowerup(PowerupFlag::RedKey);

            RemovePowerup(PowerupFlag::FullMap);
            if (auto player = Game::Level.TryGetObject(Reference)) {
                Game::UncloakObject(*player, false);
                Game::MakeVulnerable(*player, false);
            }
        }
    }

    float Player::GetShipVisibility() const {
        if (auto player = Game::Level.TryGetObject(Reference)) {
            auto lum = Luminance(player->Ambient.GetColor().ToVector3());
            lum = Saturate(lum - 0.1f); // Make slightly darker so dim segments are stealthy
            if (LastPrimaryFireTime + .5f > Game::Time) lum = std::max(lum, 2.0f);
            return lum;
        }

        return 1.0f;
    }

    float Player::GetWeaponEnergyCost(const Weapon& weapon) const {
        bool quadFire = false;
        if (HasPowerup(PowerupFlag::QuadFire)) {
            for (auto& gp : Ship.Weapons[(int)Primary].QuadGunpoints) {
                if (gp) {
                    quadFire = true;
                    break;
                }
            }
        }

        float energyUsage = weapon.EnergyUsage * Ship.EnergyMultiplier;

        // Double the cost of quad fire weapons. Note this expects the base cost to be lowered.
        return quadFire ? energyUsage * 2 : energyUsage;
    }

    bool Player::PickUpEnergy() {
        if (Energy < MAX_ENERGY) {
            AddEnergy(float(3 + 3 * (5 - (int)Game::Difficulty)));

            AddScreenFlash(FLASH_GOLD);
            auto msg = fmt::format("{} {} {}", Resources::GetString(GameString::Energy), Resources::GetString(GameString::BoostedTo), int(Energy));
            PrintHudMessage(msg);

            if (!CanFirePrimary(Primary))
                AutoselectPrimary(); // maybe picking up energy lets us fire a weapon

            return true;
        }
        else {
            PrintHudMessage("your energy is maxed out!");
            return false;
        }
    }

    int Player::PickUpAmmo(PrimaryWeaponIndex index, uint16 amount) {
        if (amount == 0) return amount;

        auto max = PyroGX.Weapons[(int)index].MaxAmmo;
        if (HasPowerup(PowerupFlag::AmmoRack))
            max *= 2;

        auto& ammo = PrimaryAmmo[(int)index];
        if (ammo >= max)
            return 0;

        ammo += amount;

        if (ammo > max) {
            amount += max - ammo;
            ammo = max;
        }

        return amount;
    }

    bool Player::CanFirePrimary(PrimaryWeaponIndex index) const {
        if (!HasWeapon(index)) return false;
        if (Game::Level.IsShareware && (index == PrimaryWeaponIndex::Fusion || index == PrimaryWeaponIndex::Plasma))
            return false;

        auto& weapon = Resources::GetWeapon(GetPrimaryWeaponID(index));
        bool canFire = true;

        if (index == PrimaryWeaponIndex::Vulcan ||
            index == PrimaryWeaponIndex::Gauss)
            canFire &= weapon.AmmoUsage <= PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan];

        if (index == PrimaryWeaponIndex::Omega)
            canFire &= Energy > 1 || OmegaCharge > OMEGA_CHARGE_COST; // it's annoying to switch to omega with no energy

        canFire &= GetWeaponEnergyCost(weapon) <= Energy;
        return canFire;
    }

    bool Player::CanFireSecondary(SecondaryWeaponIndex index) const {
        auto& weapon = Resources::GetWeapon(GetSecondaryWeaponID(index));
        if (Game::Level.IsShareware && index == SecondaryWeaponIndex::Mega)
            return false;

        return
            weapon.AmmoUsage <= SecondaryAmmo[(int)index] &&
            weapon.EnergyUsage <= Energy;
    }

    void Player::TouchPowerup(Object& obj) {
        if (obj.Lifespan <= 0) return; // Already picked up
        if (IsDead) return; // Player is dead!

        assert(obj.Type == ObjectType::Powerup);

        auto pickUpAccesory = [this](PowerupFlag powerup, string_view name) {
            if (HasPowerup(powerup)) {
                auto msg = fmt::format("{} the {}!", Resources::GetString(GameString::AlreadyHave), name);
                PrintHudMessage(msg);
                return PickUpEnergy();
            }
            else {
                GivePowerup(powerup);
                AddScreenFlash(FLASH_POWERUP);
                PrintHudMessage(fmt::format("{}!", name));
                return true;
            }
        };

        auto tryPickUpPrimary = [this](PrimaryWeaponIndex weapon) {
            auto pickedUp = PickUpPrimary(weapon);
            if (!pickedUp)
                pickedUp = PickUpEnergy();
            return pickedUp;
        };

        auto id = PowerupID(obj.ID);
        auto& powerup = Resources::GameData.Powerups[obj.ID];
        bool used = false, ammoPickedUp = false;
        bool playSound = true;

        switch (id) {
            case PowerupID::ExtraLife:
                GiveExtraLife(1);
                used = true;
                break;

            case PowerupID::Energy:
                used = PickUpEnergy();
                break;

            case PowerupID::ShieldBoost:
            {
                if (Shields < MAX_SHIELDS) {
                    auto amount = 3 + 3 * (5 - (int)Game::Difficulty); // 18, 15, 12, 9, 6

                    if (Game::Level.IsDescent2() && Game::Difficulty == DifficultyLevel::Trainee) 
                        amount = 27; // D2 gives 27 shields on trainee

                    Shields += amount;
                    if (Shields > MAX_SHIELDS) Shields = MAX_SHIELDS;

                    AddScreenFlash(FLASH_BLUE);
                    auto msg = fmt::format("{} {} {}", Resources::GetString(GameString::Shield), Resources::GetString(GameString::BoostedTo), int(Shields));
                    PrintHudMessage(msg);
                    used = true;
                }
                else {
                    PrintHudMessage("your shield is maxed out!");
                }

                break;
            }

            case PowerupID::Laser:
                if (LaserLevel >= MAX_LASER_LEVEL) {
                    PrintHudMessage("your laser cannon is maxed out!");
                    used = PickUpEnergy();
                }
                else {
                    LaserLevel++;
                    AddScreenFlash(FLASH_LASER_POWERUP);
                    auto msg = fmt::format("laser cannon boosted to {}", LaserLevel + 1);
                    PrintHudMessage(msg);
                    PickUpPrimary(PrimaryWeaponIndex::Laser);
                    used = true;
                }
                break;

            case PowerupID::KeyBlue:
            {
                if (HasPowerup(PowerupFlag::BlueKey))
                    break;

                GivePowerup(PowerupFlag::BlueKey);
                AddScreenFlash(FLASH_BLUE);

                auto msg = fmt::format("{} {}", Resources::GetString(GameString::Blue), Resources::GetString(GameString::AccessGranted));
                PrintHudMessage(msg);
                used = true;
                break;
            }

            case PowerupID::KeyRed:
            {
                if (HasPowerup(PowerupFlag::RedKey))
                    break;

                GivePowerup(PowerupFlag::RedKey);
                AddScreenFlash(FLASH_RED);

                auto msg = fmt::format("{} {}", Resources::GetString(GameString::Red), Resources::GetString(GameString::AccessGranted));
                PrintHudMessage(msg);
                used = true;
                break;
            }

            case PowerupID::KeyGold:
            {
                if (HasPowerup(PowerupFlag::GoldKey))
                    break;

                GivePowerup(PowerupFlag::GoldKey);
                AddScreenFlash(FLASH_GOLD);

                auto msg = fmt::format("{} {}", Resources::GetString(GameString::Yellow), Resources::GetString(GameString::AccessGranted));
                PrintHudMessage(msg);
                used = true;
                break;
            }

            case PowerupID::Vulcan:
            case PowerupID::Gauss:
            {
                // Give ammo first so autoselect works properly
                auto& ammo = obj.Control.Powerup.Count; // remaining ammo on the weapon

                if (ammo > 0) {
                    auto amount = PickUpAmmo(PrimaryWeaponIndex::Vulcan, (uint16)ammo);
                    ammo -= amount;
                    if (!used && amount > 0) {
                        AddScreenFlash(FLASH_PRIMARY);
                        PrintHudMessage(fmt::format("{} vulcan rounds!", amount));
                        ammoPickedUp = true;
                        if (ammo == 0)
                            used = true; // remove object if all ammo was taken
                    }
                }

                used = PickUpPrimary(obj.ID == (int)PowerupID::Vulcan ? PrimaryWeaponIndex::Vulcan : PrimaryWeaponIndex::Gauss);
                break;
            }

            case PowerupID::Spreadfire:
                used = tryPickUpPrimary(PrimaryWeaponIndex::Spreadfire);
                break;

            case PowerupID::Plasma:
                used = tryPickUpPrimary(PrimaryWeaponIndex::Plasma);
                break;

            case PowerupID::Fusion:
                used = tryPickUpPrimary(PrimaryWeaponIndex::Fusion);
                break;

            case PowerupID::SuperLaser:
            {
                if (LaserLevel >= MAX_SUPER_LASER_LEVEL) {
                    LaserLevel = MAX_SUPER_LASER_LEVEL;
                    PrintHudMessage("super laser maxed out!");
                    used = PickUpEnergy();
                }
                else {
                    if (LaserLevel <= MAX_LASER_LEVEL) {
                        LaserLevel = MAX_LASER_LEVEL;

                        if (Primary == PrimaryWeaponIndex::Laser) {
                            // Fake a weapon swap if the laser is already selected and super laser is picked up
                            Sound::Play2D({ SoundID::SelectPrimary });
                            PrimaryDelay = RearmTime;
                        }
                        else if (GetWeaponPriority(PrimaryWeaponIndex::SuperLaser) < GetWeaponPriority(Primary)) {
                            // Do a real weapon swap check
                            SelectPrimary(PrimaryWeaponIndex::Laser);
                        }
                    }

                    LaserLevel++;
                    AddScreenFlash(FLASH_LASER_POWERUP);
                    PrintHudMessage(fmt::format("super boost to laser level {}", LaserLevel + 1));
                    used = true;
                }
                break;
            }

            case PowerupID::Phoenix:
                used = tryPickUpPrimary(PrimaryWeaponIndex::Phoenix);
                break;

            case PowerupID::Omega:
                used = tryPickUpPrimary(PrimaryWeaponIndex::Omega);
                break;

            case PowerupID::Concussion1:
                used = PickUpSecondary(SecondaryWeaponIndex::Concussion);
                break;

            case PowerupID::Concussion4:
                used = PickUpSecondary(SecondaryWeaponIndex::Concussion, 4);
                break;

            case PowerupID::Homing1:
                used = PickUpSecondary(SecondaryWeaponIndex::Homing);
                break;

            case PowerupID::Homing4:
                used = PickUpSecondary(SecondaryWeaponIndex::Homing, 4);
                break;

            case PowerupID::ProximityMine:
                used = PickUpSecondary(SecondaryWeaponIndex::ProximityMine, 4);
                break;

            case PowerupID::SmartMissile:
                used = PickUpSecondary(SecondaryWeaponIndex::Smart);
                break;

            case PowerupID::Mega:
                used = PickUpSecondary(SecondaryWeaponIndex::Mega);
                break;

            case PowerupID::FlashMissile1:
                used = PickUpSecondary(SecondaryWeaponIndex::Flash);
                break;

            case PowerupID::FlashMissile4:
                used = PickUpSecondary(SecondaryWeaponIndex::Flash, 4);
                break;

            case PowerupID::GuidedMissile1:
                used = PickUpSecondary(SecondaryWeaponIndex::Guided);
                break;

            case PowerupID::GuidedMissile4:
                used = PickUpSecondary(SecondaryWeaponIndex::Guided, 4);
                break;

            case PowerupID::SmartMine:
                used = PickUpSecondary(SecondaryWeaponIndex::SmartMine, 4);
                break;

            case PowerupID::MercuryMissile1:
                used = PickUpSecondary(SecondaryWeaponIndex::Mercury);
                break;

            case PowerupID::MercuryMissile4:
                used = PickUpSecondary(SecondaryWeaponIndex::Mercury, 4);
                break;

            case PowerupID::EarthshakerMissile:
                used = PickUpSecondary(SecondaryWeaponIndex::Earthshaker);
                break;

            case PowerupID::VulcanAmmo:
            {
                auto amount = PickUpAmmo(PrimaryWeaponIndex::Vulcan, Game::VULCAN_AMMO_PICKUP);

                if (amount > 0) {
                    AddScreenFlash(FLASH_PRIMARY * 0.66f);
                    auto msg = fmt::format("{} vulcan rounds!", amount);
                    PrintHudMessage(msg);
                    used = true;

                    // Picking up ammo lets us fire a weapon!
                    if (!CanFirePrimary(Primary))
                        AutoselectPrimary();
                }
                else {
                    PrintHudMessage(fmt::format("you already have {} vulcan rounds!", PrimaryAmmo[1]));
                }
                break;
            }

            case PowerupID::Cloak:
            {
                if (Game::GetPlayerObject().IsCloaked()) {
                    auto msg = fmt::format("{} {}!", Resources::GetString(GameString::AlreadyAre), Resources::GetString(GameString::Cloaked));
                    PrintHudMessage(msg);
                }
                else {
                    GivePowerup(PowerupFlag::Cloak);
                    PrintHudMessage(fmt::format("{}!", Resources::GetString(GameString::CloakingDevice)));
                    Game::CloakObject(Game::GetPlayerObject(), Game::CLOAK_TIME);
                    used = true;
                    playSound = false;
                }
                break;
            };

            case PowerupID::Invulnerability:
                if (Game::GetPlayerObject().IsInvulnerable()) {
                    auto msg = fmt::format("{} {}!", Resources::GetString(GameString::AlreadyAre), Resources::GetString(GameString::Invulnerable));
                    PrintHudMessage(msg);
                }
                else {
                    Game::MakeInvulnerable(Game::GetPlayerObject(), Game::INVULNERABLE_TIME);
                    PrintHudMessage(fmt::format("{}!", Resources::GetString(GameString::Invulnerability)));
                    used = true;
                    playSound = false;
                }
                break;

            case PowerupID::QuadFire:
                used = pickUpAccesory(PowerupFlag::QuadFire, Resources::GetString(GameString::QuadLasers));
                break;

            case PowerupID::FullMap:
                used = pickUpAccesory(PowerupFlag::FullMap, "full map");
                break;

            case PowerupID::Converter:
                used = pickUpAccesory(PowerupFlag::Converter, "energy to shield converter");
                break;

            case PowerupID::AmmoRack:
                used = pickUpAccesory(PowerupFlag::AmmoRack, "ammo rack");
                break;

            case PowerupID::Afterburner:
                used = pickUpAccesory(PowerupFlag::Afterburner, "afterburner");
                break;

            case PowerupID::Headlight:
                used = pickUpAccesory(PowerupFlag::Headlight, "headlight");
                break;
        }

        if (used || ammoPickedUp) {
            obj.Lifespan = -1;
            if (playSound)
                Sound::Play2D({ powerup.HitSound });
        }
    }

    void Player::TouchObject(Object& obj) {
        if (obj.Lifespan <= 0) return; // Already picked up
        if (IsDead) return; // Player is dead!

        if (obj.Type == ObjectType::Powerup) {
            TouchPowerup(obj);
        }

        if (obj.Type == ObjectType::Hostage) {
            obj.Lifespan = -1;
            Game::AddPointsToScore(Game::HOSTAGE_SCORE);
            HostagesOnShip++;
            HostagesRescued++;
            PrintHudMessage("hostage rescued!");
            AddScreenFlash({ 0, 0, MAX_FLASH });
            Sound::Play2D({ SoundID::RescueHostage });
        }
    }

    bool Player::PickUpPrimary(PrimaryWeaponIndex index) {
        uint16 flag = 1 << (int)index;
        auto name = Resources::GetPrimaryName(index);

        if (index != PrimaryWeaponIndex::Laser && PrimaryWeapons & flag) {
            PrintHudMessage(fmt::format("you already have the {}", name));
            return false;
        }

        if (index != PrimaryWeaponIndex::Laser)
            PrintHudMessage(fmt::format("{}!", name));

        PrimaryWeapons |= flag;
        AddScreenFlash(FLASH_PRIMARY);

        // Select the weapon we just picked up if it has a higher priority
        // Also check if the weapon we just picked up has ammo before selecting it
        if (GetWeaponPriority(index) < GetWeaponPriority(Primary) && CanFirePrimary(index))
            SelectPrimary(index);

        return true;
    }

    bool Player::PickUpSecondary(SecondaryWeaponIndex index, uint16 count) {
        auto max = PyroGX.Weapons[10 + (int)index].MaxAmmo;
        if (HasPowerup(PowerupFlag::AmmoRack))
            max *= 2;

        auto& ammo = SecondaryAmmo[(int)index];
        auto startAmmo = ammo;
        auto name = Resources::GetSecondaryName(index);

        if (ammo >= max) {
            auto msg = fmt::format("{} {} {}s!", Resources::GetString(GameString::AlreadyHave), ammo, name);
            PrintHudMessage(msg);
            return false;
        }

        int pickedUp = count;
        ammo += count;

        if (ammo > max) {
            pickedUp = count - (ammo - max);
            ammo = max;
        }

        if (pickedUp > 1) {
            AddScreenFlash(FLASH_WHITE * 0.9f);
            auto msg = fmt::format("{} {}s!", pickedUp, name);
            PrintHudMessage(msg);
        }
        else {
            AddScreenFlash(FLASH_WHITE * 0.66f);
            PrintHudMessage(fmt::format("{}!", name));
        }

        // todo: autoselect priority
        if (!CanFireSecondary(Secondary) || (startAmmo == 0 && Secondary < index && index != SecondaryWeaponIndex::ProximityMine))
            SelectSecondary(index);

        // todo: spawn individual missiles if count > 1 and full
        return true;
    }

    constexpr PowerupID PrimaryWeaponToPowerup(PrimaryWeaponIndex index) {
        constexpr PowerupID powerups[] = {
            PowerupID::Laser, PowerupID::Vulcan, PowerupID::Spreadfire, PowerupID::Plasma, PowerupID::Fusion, PowerupID::SuperLaser, PowerupID::Helix, PowerupID::Phoenix, PowerupID::Omega
        };

        return powerups[(int)index];
    }

    constexpr PowerupID SecondaryWeaponToPowerup(SecondaryWeaponIndex index) {
        constexpr PowerupID powerups[] = {
            PowerupID::Concussion1, PowerupID::Homing1, PowerupID::ProximityMine, PowerupID::SmartMissile, PowerupID::Mega,
            PowerupID::FlashMissile1, PowerupID::GuidedMissile1, PowerupID::SmartMine, PowerupID::EarthshakerMissile
        };

        return powerups[(int)index];
    }

    void Player::DropAllItems() {
        auto& player = Game::GetPlayerObject();

        // Tries to arm mines that don't fit into packs of 4
        auto maybeArmMine = [this, &player](SecondaryWeaponIndex index) {
            float armChance = .9f;
            while (SecondaryAmmo[(int)index] % 4 != 0) {
                SecondaryAmmo[(int)index]--;
                if (Random() < armChance) {
                    armChance *= 0.5f;
                    auto mineRef = Game::FireWeapon(player, WeaponID::ProxMine, 7, nullptr, 1, false, 0);
                    if (auto mine = Game::GetObject(mineRef))
                        mine->Physics.Velocity += RandomVector(64) + player.Physics.Velocity;
                }
            }
        };

        maybeArmMine(SecondaryWeaponIndex::ProximityMine);
        maybeArmMine(SecondaryWeaponIndex::SmartMine);

        if (LaserLevel > 3) {
            for (int i = 3; i < LaserLevel; i++)
                Game::DropPowerup(PowerupID::SuperLaser, player.Position, player.Segment);
        }
        else if (LaserLevel > 0) {
            for (int i = 0; i < LaserLevel; i++)
                Game::DropPowerup(PowerupID::Laser, player.Position, player.Segment);
        }

        LaserLevel = 0;

        auto dropPowerup = [this, &player](PowerupFlag flag, PowerupID id) {
            if (HasPowerup(flag)) {
                RemovePowerup(flag);
                Game::DropPowerup(id, player.Position, player.Segment);
            }
        };

        dropPowerup(PowerupFlag::QuadFire, PowerupID::QuadFire);
        dropPowerup(PowerupFlag::Cloak, PowerupID::Cloak);
        dropPowerup(PowerupFlag::FullMap, PowerupID::FullMap);
        dropPowerup(PowerupFlag::Afterburner, PowerupID::Afterburner);
        dropPowerup(PowerupFlag::AmmoRack, PowerupID::AmmoRack);
        dropPowerup(PowerupFlag::Converter, PowerupID::Converter);
        dropPowerup(PowerupFlag::Headlight, PowerupID::Headlight);

        auto maybeDropWeapon = [this, &player](PrimaryWeaponIndex weapon, int ammo = 0) {
            if (!HasWeapon(weapon)) return;
            auto powerup = PrimaryWeaponToPowerup(weapon);
            auto ref = Game::DropPowerup(powerup, player.Position, player.Segment);
            if (auto obj = Game::GetObject(ref))
                obj->Control.Powerup.Count = ammo;

            RemoveWeapon(weapon);
        };

        auto vulcanAmmo = PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan];
        if (HasWeapon(PrimaryWeaponIndex::Gauss) && HasWeapon(PrimaryWeaponIndex::Vulcan))
            vulcanAmmo /= 2; // split ammo between both guns

        maybeDropWeapon(PrimaryWeaponIndex::Vulcan, vulcanAmmo);
        maybeDropWeapon(PrimaryWeaponIndex::Gauss, vulcanAmmo);
        maybeDropWeapon(PrimaryWeaponIndex::Spreadfire);
        maybeDropWeapon(PrimaryWeaponIndex::Plasma);
        maybeDropWeapon(PrimaryWeaponIndex::Fusion);
        maybeDropWeapon(PrimaryWeaponIndex::Helix);
        maybeDropWeapon(PrimaryWeaponIndex::Phoenix);
        maybeDropWeapon(PrimaryWeaponIndex::Omega);

        if (!HasWeapon(PrimaryWeaponIndex::Gauss) && !HasWeapon(PrimaryWeaponIndex::Vulcan) && vulcanAmmo > 0) {
            // Has vulcan ammo but neither weapon, drop the ammo
            auto ammoRef = Game::DropPowerup(PowerupID::VulcanAmmo, player.Position, player.Segment);
            if (auto ammoPickup = Game::GetObject(ammoRef)) {
                ammoPickup->Control.Powerup.Count = vulcanAmmo;
            }
        }

        auto maybeDropSecondary = [this, &player](SecondaryWeaponIndex weapon, uint16 max, uint16 packSize = 1) {
            auto ammo = SecondaryAmmo[(int)weapon];
            if (ammo == 0) return;
            auto count = std::min(max, uint16(ammo / packSize));
            auto powerup = SecondaryWeaponToPowerup(weapon);
            for (int i = 0; i < count; i++)
                Game::DropPowerup(powerup, player.Position, player.Segment);

            SecondaryAmmo[(int)weapon] = 0;
        };

        maybeDropSecondary(SecondaryWeaponIndex::Mega, 3);
        maybeDropSecondary(SecondaryWeaponIndex::Earthshaker, 3);
        maybeDropSecondary(SecondaryWeaponIndex::Smart, 3);

        // Mines come in packs of 4, so divide by 4
        maybeDropSecondary(SecondaryWeaponIndex::ProximityMine, 3, 4);
        maybeDropSecondary(SecondaryWeaponIndex::SmartMine, 3, 4);

        // Drops missiles that can be in packs of 1 or 4, up to a maximum
        auto dropMissilePacks = [this, &player](SecondaryWeaponIndex weapon, uint16 max) {
            auto ammo = SecondaryAmmo[(int)weapon];
            if (ammo == 0) return;

            auto count = std::min(ammo, max);
            auto powerup = SecondaryWeaponToPowerup(weapon);

            auto packs = count / 4;
            auto singles = count % 4;

            for (int i = 0; i < packs; i++)
                Game::DropPowerup(PowerupID((int)powerup + 1), player.Position, player.Segment);

            for (int i = 0; i < singles; i++)
                Game::DropPowerup(powerup, player.Position, player.Segment);

            SecondaryAmmo[(int)weapon] = 0;
        };

        dropMissilePacks(SecondaryWeaponIndex::Concussion, 10);
        dropMissilePacks(SecondaryWeaponIndex::Homing, 10);
        dropMissilePacks(SecondaryWeaponIndex::Flash, 10);
        dropMissilePacks(SecondaryWeaponIndex::Guided, 10);
        dropMissilePacks(SecondaryWeaponIndex::Mercury, 10);
    }
}
