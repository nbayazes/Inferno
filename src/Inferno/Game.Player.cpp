#include "pch.h"
#include "Game.Player.h"
#include <utility>
#include "VisualEffects.h"
#include "Game.AI.h"
#include "Game.Bindings.h"
#include "Game.h"
#include "Game.Weapon.h"
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
    constexpr float AFTERBURNER_VOLUME = 1.1f;

    float GetWeaponAlertRadius(const Weapon& weapon) {
        // Robots use half-linear falloff instead of inverse square because it doesn't require traversing nearly as far.
        //float mult = 0.5f + std::min(2, (int)Game::Difficulty) / 4.0f; // hotshot, ace, insane = 1, trainee = 0.5, rookie = 0.75
        return weapon.Extended.SoundRadius /** mult */ * 0.6f; // Reduce radius because weapons have a default sound radius of 300
    }

    float Player::UpdateAfterburner(float dt, bool active) {
        //if (!HasPowerup(PowerupFlag::Afterburner)) return 0;

        float thrust = 0;

        // AB keeps draining energy if button is held when empty even though it doesn't do anything.
        // This is the original behavior.
        if (active) {
            constexpr float AFTERBURNER_USE_SECS = 3;
            constexpr float USE_SPEED = 1 / 15.0f / AFTERBURNER_USE_SECS;

            float oldCount = AfterburnerCharge / USE_SPEED;
            AfterburnerCharge -= dt / AFTERBURNER_USE_SECS;

            if (AfterburnerCharge > 0 && _nextAfterburnerAlertTime < Game::Time) {
                AlertRobotsOfNoise(Game::GetPlayerObject(), Game::PLAYER_AFTERBURNER_SOUND_RADIUS, 0.45f);
                _nextAfterburnerAlertTime = Game::Time + 0.25f; // Alert every 0.25 seconds
            }

            if (AfterburnerCharge < 0) AfterburnerCharge = 0;
            float count = AfterburnerCharge / USE_SPEED;

            if (oldCount != count) {} // drop blobs
            thrust = 1 + std::min(0.5f, AfterburnerCharge) * 2; // Falloff from 2 under 50% charge
        }
        else if (AfterburnerCharge < 1) {
            const float rechargeTime = HasPowerup(PowerupFlag::Afterburner) ? 4.0f : 8.0f; // halve recharge time with afterburner cooler
            float chargeUp = std::min(dt / rechargeTime, 1 - AfterburnerCharge);
            float energy = std::max(Energy - 10, 0.0f); // don't drop below 10 energy
            chargeUp = std::min(chargeUp, energy / 10); // limit charge if <= 10 energy
            AfterburnerCharge += chargeUp;
            if (AfterburnerCharge > 1) AfterburnerCharge = 1;

            AddEnergy(-chargeUp * 100 / 10); // full charge uses 10% energy
        }

        if (AfterburnerCharge <= 0 && active)
            active = false; // ran out of charge

        // AB button held
        if (active && !AfterburnerActive) {
            SoundResource resource{ AFTERBURNER_SOUND };
            Sound3D sound(resource);
            sound.Radius = 125;
            sound.Volume = AFTERBURNER_VOLUME;
            _afterburnerSoundSig = Sound::PlayFrom(sound, Game::GetPlayerObject());

            //if (auto soundId = Seq::tryItem(Resources::Descent2.Sounds, (int)SoundID::AfterburnerIgnite)) {
            //    SoundResource resource{ AFTERBURNER_SOUND };
            //    //resource.D2 = *soundId;
            //    Sound3D sound(resource);
            //    sound.Radius = 125;
            //    sound.LoopStart = 32027;
            //    sound.LoopEnd = 48452;
            //    sound.Looped = true;
            //    _afterburnerSoundSig = Sound::PlayFrom(sound, Game::GetPlayerObject());
            //    //Render::Camera.Shake(2.0f);
            //}

            if (auto info = Resources::GetLightInfo("Afterburner")) {
                LightEffectInfo light;
                light.Radius = info->Radius;
                light.LightColor = info->Color;
                light.Mode = DynamicLightMode::StrongFlicker;
                _afterburnerEffect = AttachLight(light, Reference, { .id = 0, .offset = { 0, 4.0f, 0 } });
            }
        }

        // AB button released
        if (!active && AfterburnerActive) {
            Sound::FadeOut(_afterburnerSoundSig, 0.25f);
            SoundResource resource{ AFTERBURNER_OFF_SOUND };
            Sound3D sound(resource);
            sound.Radius = 125;
            sound.Volume = AFTERBURNER_VOLUME;
            Sound::PlayFrom(sound, Game::GetPlayerObject());

            StopEffect(_afterburnerEffect);

            /*if (auto soundId = Seq::tryItem(Resources::Descent2.Sounds, (int)SoundID::AfterburnerStop)) {
                SoundResource resource;
                resource.D2 = *soundId;
                Sound3D sound(resource);
                sound.Radius = 125;
                Sound::PlayFrom(sound, Game::GetPlayerObject());
            }*/
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

        auto& weaponInfo = Resources::GetWeapon(GetPrimaryWeaponID((PrimaryWeaponIndex)weapon));

        if (!HasWeapon((PrimaryWeaponIndex)weapon)) {
            auto msg = fmt::format("you don't have the {}!", weaponInfo.Extended.Name);
            PrintHudMessage(msg);
            Sound::Play2D({ SoundID::SelectFail });
            return;
        }

        // Shareware doesn't have the selection sounds, use the selection beep
        if (Game::Level.IsShareware)
            Sound::Play2D(SoundID::AlreadySelected, 0.75f, 0, 0.15f);
        else
            Sound::Play2D(SoundID::SelectPrimary);

        ReleaseFusionCharge(); // Release fusion in case it's being charged while switching weapons

        PrimaryDelay = RearmTime;
        Primary = (PrimaryWeaponIndex)weapon;
        PrimaryWasSuper[weapon % SUPER_WEAPON] = weapon >= SUPER_WEAPON;
        PrintHudMessage(fmt::format("{} selected!", weaponInfo.Extended.Name));
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

        auto& weaponInfo = Resources::GetWeapon(GetSecondaryWeaponID((SecondaryWeaponIndex)weapon));

        if (!CanFireSecondary((SecondaryWeaponIndex)weapon)) {
            auto msg = fmt::format("you have no {}s!", weaponInfo.Extended.Name);
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
        PrintHudMessage(fmt::format("{} selected!", weaponInfo.Extended.Name));
    }


    void Player::UpdateFireState(bool firePrimary, bool fireSecondary) {
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
        toggleState(PrimaryState, firePrimary);
        toggleState(SecondaryState, fireSecondary);
    }

    void Player::Update(float dt) {
        PrimaryDelay -= dt;
        SecondaryDelay -= dt;
        TertiaryDelay -= dt;

        if (Game::Level.Objects.empty()) return;

        auto& weapon = Resources::GetWeapon(GetPrimaryWeaponID(Primary));

        if (IsDead) {
            // Fire the fusion cannon if the ship is destroyed while charging it
            ReleaseFusionCharge();
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
                    Sound::Play2D({ SoundID::Refuel }, ENERGY_CENTER_VOLUME);
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
                    auto& battery = Ship.Weapons[(int)Primary];
                    AddEnergy(-GetWeaponEnergyCost(battery.EnergyUsage));
                }
            }
            else if (PrimaryState == FireState::Hold && Energy > 0 && WeaponCharge > 0) {
                AddEnergy(-dt); // 1 energy cost per second
                WeaponCharge += dt;

                FusionNextSoundDelay -= dt;
                Game::FusionTint.SetTarget(Color(0.6, 0, 1.0f, std::min(WeaponCharge * 0.6f, 1.5f)), Game::Time, 0);

                float physicsMult = std::min(1 + WeaponCharge * 0.5f, 4.0f);

                if (Settings::Inferno.SlowmoFusion && WeaponCharge > SLOWMO_MIN_CHARGE) {
                    Game::SetTimeScale(0.5f, SLOWMO_DOWN_RATE); // Halve speed
                }

                if (FusionNextSoundDelay < 0) {
                    if (WeaponCharge > weapon.Extended.MaxCharge) {
                        // Self damage
                        Sound::PlayFrom({ SoundID::Explosion }, player);
                        constexpr float OVERCHARGE_DAMAGE = 3.0f;
                        ApplyDamage(Random() * OVERCHARGE_DAMAGE, false);
                    }
                    else {
                        Sound3D sound(SoundID::FusionWarmup);
                        sound.Volume = .8f;
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
                ReleaseFusionCharge();
            }
        }
        else if (PrimaryState == FireState::Hold) {
            FirePrimary();
        }

        if (SecondaryState == FireState::Hold || SecondaryState == FireState::Press) {
            FireSecondary();
        }

        // Recharge omega
        if (HasWeapon(PrimaryWeaponIndex::Omega) && Energy > 0) {
            auto& battery = Ship.Weapons[(int)PrimaryWeaponIndex::Omega];

            if (OmegaCharge < battery.Ammo && LastPrimaryFireTime + OMEGA_RECHARGE_DELAY < Game::Time) {
                float chargeUp = std::min(battery.Ammo * dt / OMEGA_RECHARGE_TIME, battery.Ammo - OmegaCharge);
                OmegaCharge += chargeUp;
                AddEnergy(-chargeUp * battery.EnergyUsage);
            }
        }
    }

    constexpr float FLARE_FIRE_DELAY = 0.25f;

    void Player::FireFlare() {
        if (_nextFlareFireTime > Game::Time) return;
        Game::FireWeaponInfo info = { .id = WeaponID::Flare, .gun = FLARE_GUN_ID };
        Game::FireWeapon(Game::GetPlayerObject(), info);
        auto& weapon = Resources::GetWeapon(WeaponID::Flare);
        _nextFlareFireTime = Game::Time + weapon.FireDelay;
        auto& player = Game::GetPlayerObject();
        Game::PlayWeaponSound(WeaponID::Flare, weapon.Extended.FireVolume, player, FLARE_GUN_ID);
        AlertRobotsOfNoise(player, GetWeaponAlertRadius(weapon), weapon.Extended.Noise);
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
        if (TertiaryDelay > 0) return;

        auto bomb = GetActiveBomb();
        auto& ammo = SecondaryAmmo[(int)bomb];
        if (ammo == 0) {
            Sound::Play2D({ SoundID::SelectFail });
            PrintHudMessage("you have no bombs!");
            return;
        }

        auto& player = Game::GetPlayerObject();
        auto id = GetSecondaryWeaponID(bomb);
        auto& weapon = Resources::GameData.Weapons[(int)id];

        Game::PlayWeaponSound(id, weapon.Extended.FireVolume, player, BOMB_GUN_ID);
        Game::FireWeapon(player, { .id = id, .gun = BOMB_GUN_ID });
        ammo -= (uint16)weapon.AmmoUsage;

        // Use the weapon delay instead of the ship battery for bombs.
        // Eventually bombs should be removed from weapon switching
        TertiaryDelay = weapon.FireDelay;

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

    void Player::GivePowerup(PowerupFlag powerup) {
        bool newQuadLasers = HasFlag(powerup, PowerupFlag::QuadFire) && !HasFlag(Powerups, PowerupFlag::QuadFire);
        SetFlag(Powerups, powerup);

        if (newQuadLasers) {
            // Select to lasers if they have a higher priority after picking up quad fire powerup
            auto laserIndex =
                Game::Level.IsDescent2() && HasWeapon(PrimaryWeaponIndex::SuperLaser)
                ? PrimaryWeaponIndex::SuperLaser
                : PrimaryWeaponIndex::Laser;

            PrimaryPickupAutoselect(laserIndex);
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
            AutoselectPrimary(AutoselectCondition::AmmoDepletion);
            return;
        }


        auto id = GetPrimaryWeaponID(Primary);
        auto& weapon = Resources::GetWeapon(id);
        auto& battery = Ship.Weapons[(int)Primary];

        // must do a different check for omega so running out of charge doesn't cause an autoswap
        if (Primary == PrimaryWeaponIndex::Omega && OmegaCharge < battery.EnergyUsage)
            return;

        // Charged weapons drain energy on button down instead of here
        if (!weapon.Extended.Chargable) {
            AddEnergy(-GetWeaponEnergyCost(battery.EnergyUsage));
            if (Seq::inRange(PrimaryAmmo, battery.AmmoType))
                PrimaryAmmo[battery.AmmoType] -= battery.AmmoUsage; // all ammo is treated as vulcan ammo for now
        }

        auto& sequence = battery.Firing;
        if (FiringIndex >= battery.FiringCount) FiringIndex = 0;

        PrimaryDelay = GetPrimaryFireDelay();
        auto& player = Game::GetPlayerObject();

        // count the active gunpoints to reduce the volume of the fired shots so after they are averaged it's not too loud
        int activeGunpoints = 0;
        Vector3 averageGunPosition;

        for (uint8 i = 0; i < MAX_GUNPOINTS; i++) {
            bool quadFire = HasPowerup(PowerupFlag::QuadFire) && Ship.Weapons[(int)Primary].QuadGunpoints[i];
            if (sequence[FiringIndex].Gunpoints[i] || quadFire) {
                activeGunpoints++;
                averageGunPosition += GetGunpointSubmodelOffset(player, i).offset;
            }
        }

        if (activeGunpoints > 0)
            averageGunPosition /= (float)activeGunpoints;

        float volume = weapon.Extended.FireVolume;

        // Make quad lasers slightly louder
        //if (activeGunpoints >= 4) volume *= 1.1f;

        // Directly play sounds on the player, otherwise mixing gets too complicated to keep a consistent volume
        auto sound = Game::InitWeaponSound(id, volume);
        sound.AttachOffset = averageGunPosition;
        Sound::PlayFrom(sound, player);

        for (uint8 i = 0; i < MAX_GUNPOINTS; i++) {
            bool quadFire = HasPowerup(PowerupFlag::QuadFire) && Ship.Weapons[(int)Primary].QuadGunpoints[i];
            if (sequence[FiringIndex].Gunpoints[i] || quadFire) {
                auto& behavior = Game::GetWeaponBehavior(weapon.Extended.Behavior);

                // Check quad laser gunpoints for wall clipping. They can end up outside the game world.
                if ((i == 2 || i == 3) && GunpointIntersectsWall(player, i)) {
                    //SPDLOG_WARN("Player gun clips wall! Not firing.");
                    continue;
                }
                behavior(*this, i, id);
            }
        }

        FiringIndex = (FiringIndex + 1) % battery.FiringCount;
        WeaponCharge = 0;
        LastPrimaryFireTime = Game::Time;

        AlertRobotsOfNoise(player, GetWeaponAlertRadius(weapon), weapon.Extended.Noise * 2.0f);

        if (!CanFirePrimary(Primary) && Primary != PrimaryWeaponIndex::Omega)
            AutoselectPrimary(AutoselectCondition::AmmoDepletion);
    }

    void Player::HoldPrimary() {}

    void Player::ReleasePrimary() {
        //auto id = GetPrimaryWeaponID();
        //auto& weapon = Resources::GameData.Weapons[(int)id];
    }

    void Player::FireSecondary() {
        if (Secondary == SecondaryWeaponIndex::ProximityMine || Secondary == SecondaryWeaponIndex::SmartMine) {
            DropBomb(); // Defer bombs to the specialized bomb function
            return;
        }

        if (SecondaryDelay > 0) return;
        if (!CanFireSecondary(Secondary)) {
            AutoselectSecondary();
            return;
        }

        auto id = GetSecondaryWeaponID(Secondary);
        auto& weapon = Resources::GameData.Weapons[(int)id];
        SecondaryDelay = GetSecondaryFireDelay();

        auto& battery = Ship.Weapons[10 + (int)Secondary];
        auto& sequence = battery.Firing;
        if (SecondaryFiringIndex >= battery.FiringCount) SecondaryFiringIndex = 0;
        auto& player = Game::GetPlayerObject();

        for (uint8 gun = 0; gun < MAX_GUNPOINTS; gun++) {
            if (sequence[SecondaryFiringIndex].Gunpoints[gun]) {
                Game::FireWeapon(Game::GetPlayerObject(), { .id = id, .gun = gun });

                auto sound = Game::InitWeaponSound(id, weapon.Extended.FireVolume);
                auto gunSubmodel = GetGunpointSubmodelOffset(player, gun);
                //sound.AttachOffset = GetSubmodelOffset(player, gunSubmodel);
                sound.AttachOffset = gunSubmodel.offset;
                Sound::PlayFrom(sound, player);
            }
        }

        SecondaryFiringIndex = (SecondaryFiringIndex + 1) % battery.FiringCount;
        SecondaryAmmo[(int)Secondary] -= battery.AmmoUsage;
        LastSecondaryFireTime = Game::Time;

        AlertRobotsOfNoise(Game::GetPlayerObject(), GetWeaponAlertRadius(weapon), weapon.Extended.Noise * 2.0f);

        if (!CanFireSecondary(Secondary))
            AutoselectSecondary(); // Swap to different weapon if out of ammo
    }

    void Player::ReleaseFusionCharge() {
        auto& weapon = Resources::GetWeapon(GetPrimaryWeaponID(Primary));

        if (weapon.Extended.Chargable && WeaponCharge > 0) {
            if (Settings::Inferno.SlowmoFusion && WeaponCharge > SLOWMO_MIN_CHARGE) {
                Game::SetTimeScale(1.0f, SLOWMO_UP_RATE); // Return to normal speed
            }

            Game::FusionTint.SetTarget(Color(0, 0, 0, 0), Game::Time, 0.4f);
            Sound::Stop(_fusionChargeSound);
            FirePrimary();
        }
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

    void Player::PrimaryPickupAutoselect(PrimaryWeaponIndex weapon) {
        if (Settings::Inferno.OnlyAutoselectWhenEmpty && CanFirePrimary(Primary))
            return;
        if (Settings::Inferno.NoAutoselectWhileFiring && (PrimaryState == FireState::Press || PrimaryState == FireState::Hold) && CanFirePrimary(Primary))
            return;

        if (GetPrimaryWeaponPriority(weapon) < GetPrimaryWeaponPriority(Primary) && CanFirePrimary(weapon))
            SelectPrimary(weapon);
    }

    void Player::SecondaryPickupAutoselect(SecondaryWeaponIndex weapon) {
        if (Settings::Inferno.OnlyAutoselectWhenEmpty && CanFireSecondary(Secondary))
            return;
        if (Settings::Inferno.NoAutoselectWhileFiring && (SecondaryState == FireState::Press || SecondaryState == FireState::Hold) && CanFireSecondary(Secondary))
            return;
        if (GetSecondaryWeaponPriority(weapon) < GetSecondaryWeaponPriority(Secondary) && CanFireSecondary(weapon))
            SelectSecondary(weapon);
    }

    void Player::AutoselectPrimary(AutoselectCondition condition, int16 ammoType) {
        int priority = -1;
        int index = -1;
        const int numWeapons = Game::Level.IsDescent1() ? 5 : 10;

        auto equippedPrio = (int)GetPrimaryWeaponPriority(Primary);
        bool primaryUnusable = (condition == AutoselectCondition::AmmoDepletion) || !CanFirePrimary(Primary);

        if (Settings::Inferno.OnlyAutoselectWhenEmpty && !primaryUnusable)
            return;

        if (Settings::Inferno.NoAutoselectWhileFiring && (PrimaryState == FireState::Press || PrimaryState == FireState::Hold) && !primaryUnusable)
            return;

        for (int i = 0; i < numWeapons; i++) {
            auto idx = (PrimaryWeaponIndex)i;
            auto& battery = Ship.Weapons[i];

            if (condition == AutoselectCondition::AmmoPickup) {
                if (!battery.AmmoUsage || ammoType != battery.AmmoType)
                    continue; // skip weapons that don't use the ammo type picked up
            }

            if (condition == AutoselectCondition::EnergyPickup) {
                if (!battery.EnergyUsage)
                    continue; // skip weapons that don't use energy
            }

            if (battery.EnergyUsage > 0 && Energy < 1)
                continue; // don't switch to energy weapons at low energy

            if (!CanFirePrimary(idx)) continue;

            auto p = GetPrimaryWeaponPriority(idx);
            if (p == NO_AUTOSELECT) continue;

            if (!primaryUnusable)
                if (std::cmp_less(equippedPrio, p))
                    continue; // only switch to lower priority weapon when the current weapon is depleted

            if (std::cmp_less(p, priority) || priority == -1) {
                priority = p;
                index = i;
            }
        }

        if (index == -1) {
            if (primaryUnusable)
                PrintHudMessage("no primary weapons available!");
            // play sound first time this happens?
            return;
        }

        if (index == (int)Primary && Game::Level.IsDescent1())
            return; // Weapon already selected

        SelectPrimary(PrimaryWeaponIndex(index));
    }

    void Player::AutoselectSecondary() {
        int priority = -1;
        int index = -1;
        const int numWeapons = Game::Level.IsDescent1() ? 5 : 10;

        if (Settings::Inferno.NoAutoselectWhileFiring && (SecondaryState == FireState::Press || SecondaryState == FireState::Hold) && CanFireSecondary(Secondary))
            return;

        for (int i = 0; i < numWeapons; i++) {
            auto idx = (SecondaryWeaponIndex)i;
            if (!CanFireSecondary(idx)) continue;

            auto p = GetSecondaryWeaponPriority(idx);
            if (p == NO_AUTOSELECT) continue;

            if (std::cmp_less(p, priority) || priority == -1) {
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

    void Player::LoseLife() {
        stats.deaths++;

        if (Lives > 0)
            Lives--;
    }

    void Player::ApplyDamage(float damage, bool playSound, bool showTint) {
        if (Game::GetState() == GameState::EscapeSequence)
            return; // Can't take damage during cutscene

        // Keep player shields in sync with the object that represents it
        if (auto player = Game::Level.TryGetObject(Reference)) {
            constexpr float SCALE = 40;
            auto flash = std::max(damage / SCALE, 0.0f);

            if (!IsDead) {
                if (player->IsInvulnerable() || Settings::Cheats.DisableWeaponDamage) {
                    if (showTint) Game::AddDamageTint({ 0, 0, flash });
                }
                else {
                    Shields -= damage;
                    if (showTint) Game::AddDamageTint({ flash, -flash, -flash });
                }
            }

            if (Shields < 0) {
                IsDead = true;
                Input::ResetState(); // Reset state so fusion charging releases
                Game::Player.StopAfterburner(); // stop afterburner when dead so sounds and effects stop
                Game::ResetTints();
                Shields = 0;
                Energy = 0;
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
        FiringIndex = SecondaryFiringIndex = 0;
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
        Game::ResetTints();

        if (Settings::Cheats.LowShields) {
            Shields = 10;
            Energy = 10;
        }

        auto& player = Game::GetPlayerObject();
        stats.hostagesOnboard = 0;
        _prevAfterburnerCharge = AfterburnerCharge = 1;
        _nextAfterburnerAlertTime = 0;
        AfterburnerActive = false;
        WeaponCharge = 0;
        HomingObjectDist = -1;
        IsDead = false;
        TimeDead = 0;
        Exploded = false;
        PrimaryDelay = SecondaryDelay = 0;
        _nextFlareFireTime = 0;
        RefuelSoundTime = 0;
        LastPrimaryFireTime = LastSecondaryFireTime = 0;
        _headlight = HeadlightState::Off;

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
        //PyroGX.Weapons[(int)PrimaryWeaponIndex::Vulcan].MaxAmmo = Game::Level.IsDescent1() ? 10000 : 20000;
        // Always use a max ammo of 10000 for balance reasons
        Ship.Weapons[(int)PrimaryWeaponIndex::Vulcan].Ammo = 10000;

        if (died) {
            ResetInventory();
        }

        {
            // Play spawn effect
            // Old sprite
            //ParticleInfo p{};
            //p.Clip = VClipID::PlayerSpawn;
            //p.Radius = player.Radius;
            //p.RandomRotation = false;
            //Vector3 position = player.Position + player.Rotation.Forward() * 3;
            //AddParticle(p, player.Segment, position);

            //if (auto beam = EffectLibrary.GetBeamInfo("player spawn")) {
            //    for (int i = 0; i < 5; i++) {
            //        //Vector3 position = player.Position + player.Rotation.Forward() * 3;
            //        //Vector3 position = player.Position + player.Rotation.Down() * 6 + player.Rotation.Forward() * 3;
            //        Vector3 position = player.Position + player.Rotation.Forward() * 5;
            //        AddBeam(*beam, player.Segment, position);
            //    }

            //    //for (int i = 0; i < 8; i++) {
            //    //    //Vector3 position = player.Position + player.Rotation.Forward() * 3;
            //    //    Vector3 position = player.Position + player.Rotation.Up() * 6 + player.Rotation.Forward() * 3;
            //    //    AddBeam(*beam, player.Segment, position);
            //    //}
            //}

            //if (auto beam = EffectLibrary.GetBeamInfo("player spawn 2")) {
            //    for (int i = 0; i < 6; i++) {
            //        //Vector3 position = player.Position + player.Rotation.Forward() * 3;
            //        //Vector3 position = player.Position + player.Rotation.Down() * 6 + player.Rotation.Forward() * 3;
            //        Vector3 position = player.Position + player.Rotation.Forward() * 5;
            //        AddBeam(*beam, player.Segment, position);
            //    }

            //    //for (int i = 0; i < 8; i++) {
            //    //    //Vector3 position = player.Position + player.Rotation.Forward() * 3;
            //    //    Vector3 position = player.Position + player.Rotation.Up() * 6 + player.Rotation.Forward() * 3;
            //    //    AddBeam(*beam, player.Segment, position);
            //    //}
            //}

            auto start = player.Position + player.Rotation.Forward() * 4;
            constexpr float size = 5.0f;

            if (auto beam = EffectLibrary.GetBeamInfo("player spawn vertical")) {
                //for (int i = 0; i < 2; i++) {
                Vector3 end = start + player.Rotation.Up() * size;
                AddBeam(*beam, player.Segment, start, end);

                end = start + player.Rotation.Left() * size;
                AddBeam(*beam, player.Segment, start, end);

                end = start + player.Rotation.Right() * size;
                AddBeam(*beam, player.Segment, start, end);

                end = start + player.Rotation.Down() * size;
                AddBeam(*beam, player.Segment, start, end);
                //}
            }

            if (auto beam = EffectLibrary.GetBeamInfo("player spawn vertical 2")) {
                for (int i = 0; i < 2; i++) {
                    //Vector3 position = player.Position + player.Rotation.Forward() * 3;
                    //Vector3 position = player.Position + player.Rotation.Down() * 6 + player.Rotation.Forward() * 3;
                    Vector3 end = start + player.Rotation.Up() * size;
                    AddBeam(*beam, player.Segment, start, end);

                    end = start + player.Rotation.Left() * size;
                    AddBeam(*beam, player.Segment, start, end);

                    end = start + player.Rotation.Right() * size;
                    AddBeam(*beam, player.Segment, start, end);

                    end = start + player.Rotation.Down() * size;
                    AddBeam(*beam, player.Segment, start, end);
                }
            }

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
            GivePowerup(PowerupFlag::Headlight);
            LaserLevel = Game::Level.IsDescent2() ? 5 : 3;

            if (Game::Level.IsDescent2()) {
                GivePowerup(PowerupFlag::AmmoRack);
                //GivePowerup(PowerupFlag::FullMap);
            }

            PrimaryWeapons = 0xffff;
            SecondaryWeapons = 0xffff;
            int weaponCount = Game::Level.IsDescent2() ? 10 : 5;

            for (int i = 0; i < weaponCount; i++) {
                if (i == (int)SecondaryWeaponIndex::ProximityMine || i == (int)SecondaryWeaponIndex::SmartMine)
                    SecondaryAmmo[i] = 99;
                else
                    SecondaryAmmo[i] = 200;
            }

            PrimaryAmmo[1] = 10000;
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
        if (_headlight != HeadlightState::Off || AfterburnerActive)
            return 1.0f; // fully visible when headlight is on or using afterburner!

        if (auto player = Game::Level.TryGetObject(Reference)) {
            auto lum = Luminance(player->Ambient.GetValue().ToVector3());
            lum = Saturate(lum - 0.1f); // Make slightly darker so dim segments are stealthy
            if (LastPrimaryFireTime + .5f > Game::Time) lum = std::max(lum, 2.0f);
            return Saturate(lum);
        }

        return 1.0f;
    }

    void Player::TurnOffHeadlight(bool playSound) {
        switch (_headlight) {
            case HeadlightState::Normal:
            case HeadlightState::Bright:
                if (playSound)
                    Sound::Play2D({ HEADLIGHT_OFF_SOUND });

                _headlight = HeadlightState::Off;
                StopEffect(_headlightEffect);
                break;
        }
    }

    void Player::StopAfterburner() {
        UpdateAfterburner(Game::TICK_RATE, false);
    }

    void Player::ToggleHeadlight() {
        switch (_headlight) {
            case HeadlightState::Off: {
                if (auto info = Resources::GetLightInfo("Headlight")) {
                    LightEffectInfo light;
                    light.Radius = info->Radius;
                    light.LightColor = info->Color;
                    light.Normal = -Vector3::UnitZ;
                    light.Angle0 = info->Angle0;
                    light.Angle1 = info->Angle1;
                    light.ConeSpill = info->ConeSpill;

                    _headlightEffect = AttachLight(light, Reference, { .id = 0, .offset = { 0, -1.75f, 0 } });
                    Sound::Play2D({ HEADLIGHT_ON_SOUND });
                    //PrintHudMessage("Headlight On");
                    _headlight = HeadlightState::Normal;
                }
            }
            break;
            case HeadlightState::Normal:
                if (!HasPowerup(PowerupFlag::Headlight)) {
                    TurnOffHeadlight();
                    _headlight = HeadlightState::Off;
                }
                else if (auto info = Resources::GetLightInfo("Boosted Headlight")) {
                    // Remove the existing effect if there is one
                    if (_headlightEffect != EffectID::None) {
                        StopEffect(_headlightEffect);
                        _headlightEffect = EffectID::None;
                    }

                    LightEffectInfo light;
                    light.Radius = info->Radius;
                    light.LightColor = info->Color;
                    light.Normal = -Vector3::UnitZ;
                    light.Angle0 = info->Angle0;
                    light.Angle1 = info->Angle1;
                    light.ConeSpill = info->ConeSpill;

                    _headlightEffect = AttachLight(light, Reference);
                    Sound::Play2D({ HEADLIGHT_ON_SOUND });
                    _headlight = HeadlightState::Bright;
                }
                break;
            case HeadlightState::Bright:
                TurnOffHeadlight();
                break;
        }
    }

    float Player::GetWeaponEnergyCost(float baseCost) const {
        bool quadFire = false;
        if (HasPowerup(PowerupFlag::QuadFire)) {
            for (uint8 i = 0; i < MAX_GUNPOINTS; i++) {
                if (Ship.Weapons[(int)Primary].QuadGunpoints[i]) {
                    quadFire = true;
                    break;
                }
            }
        }

        float energyUsage = baseCost * Ship.EnergyMultiplier;

        // Double the cost of quad fire weapons. Note this expects the base cost to be lowered.
        return quadFire ? energyUsage * 2 : energyUsage;
    }

    uint8 Player::GetPrimaryWeaponPriority(PrimaryWeaponIndex primary) const {
        for (uint8 i = 0; i < Settings::Inferno.PrimaryPriority.size(); i++) {
            auto priority = Settings::Inferno.PrimaryPriority[i];

            if (priority == NO_AUTOSELECT)
                return NO_AUTOSELECT; // skip all weapons after autoselect

            if (HasPowerup(PowerupFlag::QuadFire)) {
                if (Game::Level.IsDescent2() && priority == QUAD_SUPER_LASER_PRIORITY && primary == PrimaryWeaponIndex::SuperLaser)
                    return i;

                if (priority == QUAD_LASER_PRIORITY && primary == PrimaryWeaponIndex::Laser)
                    return i;
            }

            if (priority == (int)primary)
                return i;
        }

        return 0;
    }

    uint8 Player::GetSecondaryWeaponPriority(SecondaryWeaponIndex secondary) const {
        for (uint8 i = 0; i < Settings::Inferno.SecondaryPriority.size(); i++) {
            auto priority = Settings::Inferno.SecondaryPriority[i];

            if (priority == NO_AUTOSELECT)
                return NO_AUTOSELECT; // skip all weapons after autoselect

            if (priority == (int)secondary)
                return i;
        }

        return 0;
    }

    bool Player::PickUpEnergy() {
        if (Energy < MAX_ENERGY) {
            AddEnergy(float(3 + 3 * (5 - (int)Game::Difficulty)));

            AddScreenFlash(FLASH_GOLD);
            auto msg = fmt::format("{} {} {}", Resources::GetString(GameString::Energy), Resources::GetString(GameString::BoostedTo), int(Energy));
            PrintHudMessage(msg);

            if (!CanFirePrimary(Primary))
                AutoselectPrimary(AutoselectCondition::EnergyPickup); // maybe picking up energy lets us fire a weapon

            return true;
        }
        else {
            PrintHudMessage("your energy is maxed out!");
            return false;
        }
    }

    int Player::PickUpAmmo(PrimaryWeaponIndex index, uint16 amount) {
        if (amount == 0) return amount;

        auto max = Ship.Weapons[(int)index].Ammo;
        if (HasPowerup(PowerupFlag::AmmoRack))
            max *= 2;

        auto& ammo = PrimaryAmmo[(int)index];
        bool wasEmpty = ammo == 0;

        if (ammo >= max)
            return 0;

        ammo += amount;

        if (ammo > max) {
            amount += max - ammo;
            ammo = max;
        }

        // If picking up ammo allows player to fire a higher priority weapon, or if the current weapon is empty, switch to it
        if (wasEmpty)
            AutoselectPrimary(AutoselectCondition::AmmoPickup, (int)index);

        return amount;
    }

    bool Player::CanFirePrimary(PrimaryWeaponIndex index) const {
        if (!HasWeapon(index)) return false;
        if (Game::Level.IsShareware && (index == PrimaryWeaponIndex::Fusion || index == PrimaryWeaponIndex::Plasma))
            return false;

        bool canFire = true;
        auto& battery = Ship.Weapons[(int)index];

        if ((index == PrimaryWeaponIndex::Vulcan ||
             index == PrimaryWeaponIndex::Gauss) &&
            Seq::inRange(PrimaryAmmo, battery.AmmoType)) {
            canFire &= battery.AmmoUsage <= PrimaryAmmo[battery.AmmoType];
        }

        if (index == PrimaryWeaponIndex::Omega)
            canFire &= Energy > 1 || OmegaCharge > battery.EnergyUsage; // it's annoying to switch to omega with no energy

        canFire &= GetWeaponEnergyCost(battery.EnergyUsage) <= Energy;
        return canFire;
    }

    bool Player::CanFireSecondary(SecondaryWeaponIndex index) const {
        if (Game::Level.IsShareware && index == SecondaryWeaponIndex::Mega)
            return false;

        auto& battery = Ship.Weapons[(int)Secondary + 10];

        return
            battery.AmmoUsage <= SecondaryAmmo[(int)index] &&
            battery.EnergyUsage <= Energy;
    }

    float Player::GetPrimaryFireDelay() {
        auto& weapon = Ship.Weapons[(int)Primary];

        if (FiringIndex >= weapon.Firing.size())
            FiringIndex = 0;

        // Reset the firing sequence if the weapon hasn't fired recently
        if (weapon.SequenceResetTime > 0 &&
            LastPrimaryFireTime + weapon.SequenceResetTime < Game::Time)
            FiringIndex = 0;

        return weapon.Firing[FiringIndex].Delay;
    }

    float Player::GetSecondaryFireDelay() {
        auto& weapon = Ship.Weapons[10 + (int)Secondary];

        if (SecondaryFiringIndex >= weapon.Firing.size())
            SecondaryFiringIndex = 0;

        // Reset the firing sequence if the weapon hasn't fired recently
        // but only if the reset time is less than the current delay
        if (weapon.SequenceResetTime > 0 &&
            weapon.Firing[SecondaryFiringIndex].Delay < weapon.SequenceResetTime &&
            LastSecondaryFireTime + weapon.SequenceResetTime < Game::Time)
            FiringIndex = 0;

        return weapon.Firing[SecondaryFiringIndex].Delay;
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

            case PowerupID::ShieldBoost: {
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

            case PowerupID::KeyBlue: {
                if (HasPowerup(PowerupFlag::BlueKey))
                    break;

                GivePowerup(PowerupFlag::BlueKey);
                AddScreenFlash(FLASH_BLUE);

                auto msg = fmt::format("{} {}", Resources::GetString(GameString::Blue), Resources::GetString(GameString::AccessGranted));
                PrintHudMessage(msg);
                used = true;
                break;
            }

            case PowerupID::KeyRed: {
                if (HasPowerup(PowerupFlag::RedKey))
                    break;

                GivePowerup(PowerupFlag::RedKey);
                AddScreenFlash(FLASH_RED);

                auto msg = fmt::format("{} {}", Resources::GetString(GameString::Red), Resources::GetString(GameString::AccessGranted));
                PrintHudMessage(msg);
                used = true;
                break;
            }

            case PowerupID::KeyGold: {
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
            case PowerupID::Gauss: {
                auto& ammo = obj.Control.Powerup.Count; // remaining ammo on the weapon
                auto weaponIndex = obj.ID == (int)PowerupID::Vulcan ? PrimaryWeaponIndex::Vulcan : PrimaryWeaponIndex::Gauss;

                // Give ammo first so autoselect works properly
                if (ammo > 0) {
                    auto amount = PickUpAmmo(PrimaryWeaponIndex::Vulcan, (uint16)ammo);
                    ammo -= amount;
                    if (!used && amount > 0) {
                        AddScreenFlash(FLASH_PRIMARY);
                        //PrintHudMessage(fmt::format("{} vulcan rounds!", amount / 10));
                        PrintHudMessage("vulcan ammo!");
                        ammoPickedUp = true;
                        if (ammo == 0)
                            used = true; // remove object if all ammo was taken
                    }
                }

                // Always remove the object if we didn't have the weapon
                if (!HasWeapon(weaponIndex))
                    used = PickUpPrimary(weaponIndex);

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

            case PowerupID::SuperLaser: {
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
                        else
                            PrimaryPickupAutoselect(PrimaryWeaponIndex::Laser);
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

            case PowerupID::VulcanAmmo: {
                auto amount = PickUpAmmo(PrimaryWeaponIndex::Vulcan, Game::VULCAN_AMMO_PICKUP);

                if (amount > 0) {
                    AddScreenFlash(FLASH_PRIMARY * 0.66f);
                    auto msg = fmt::format("{} vulcan rounds!", amount / 10);
                    PrintHudMessage(msg);
                    used = true;
                }
                else {
                    PrintHudMessage(fmt::format("you already have {} vulcan rounds!", PrimaryAmmo[1] / 10));
                }
                break;
            }

            case PowerupID::Cloak: {
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
                used = pickUpAccesory(PowerupFlag::Afterburner, "afterburner cooler");
                break;

            case PowerupID::Headlight:
                used = pickUpAccesory(PowerupFlag::Headlight, "headlight");
                break;
        }

        if (used)
            obj.Lifespan = -1;

        if ((used || ammoPickedUp) && playSound)
            Sound::Play2D({ powerup.HitSound });
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
            stats.hostagesOnboard++;
            stats.hostagesRescued++;
            PrintHudMessage("hostage rescued!");
            AddScreenFlash({ 0, 0, MAX_FLASH });
            Sound::Play2D({ SoundID::RescueHostage });
        }
    }

    void Player::GiveWeapon(PrimaryWeaponIndex weapon) {
        PrimaryWeapons |= 1 << (uint16)weapon;
    }

    bool Player::PickUpPrimary(PrimaryWeaponIndex index) {
        uint16 flag = 1 << (uint16)index;
        auto& weaponInfo = Resources::GetWeapon(GetPrimaryWeaponID(index));

        if (index != PrimaryWeaponIndex::Laser && PrimaryWeapons & flag) {
            PrintHudMessage(fmt::format("you already have the {}", weaponInfo.Extended.Name));
            return false;
        }

        if (index != PrimaryWeaponIndex::Laser)
            PrintHudMessage(fmt::format("{}!", weaponInfo.Extended.Name));

        GiveWeapon(index);
        AddScreenFlash(FLASH_PRIMARY);

        // Select the weapon we just picked up if it has a higher priority
        PrimaryPickupAutoselect(index);
        return true;
    }

    bool Player::PickUpSecondary(SecondaryWeaponIndex index, uint16 count) {
        auto max = Ship.Weapons[10 + (int)index].Ammo;
        if (HasPowerup(PowerupFlag::AmmoRack))
            max *= 2;

        auto& ammo = SecondaryAmmo[(int)index];
        auto startAmmo = ammo;
        auto& weaponInfo = Resources::GetWeapon(GetSecondaryWeaponID(index));
        auto& name = weaponInfo.Extended.Name;

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

        if (!CanFireSecondary(Secondary) || startAmmo == 0)
            SecondaryPickupAutoselect(index);

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
                    Game::FireWeaponInfo info = {
                        .id = WeaponID::ProxMine,
                        .gun = BOMB_GUN_ID,
                        .showFlash = false
                    };
                    auto mineRef = Game::FireWeapon(player, info);
                    if (auto mine = Game::GetObject(mineRef))
                        mine->Physics.Velocity += RandomVector(64) + player.Physics.Velocity;
                }
            }
        };

        maybeArmMine(SecondaryWeaponIndex::ProximityMine);
        maybeArmMine(SecondaryWeaponIndex::SmartMine);

        if (LaserLevel > 3) {
            for (uint i = 3; i < LaserLevel; i++)
                Game::DropPowerup(PowerupID::SuperLaser, player.Position, player.Segment);
        }
        else if (LaserLevel > 0) {
            for (uint i = 0; i < LaserLevel; i++)
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
        TurnOffHeadlight(false);

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
