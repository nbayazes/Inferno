#include "pch.h"
#include "Game.Player.h"
#include "Game.h"
#include "HUD.h"

namespace Inferno {
    constexpr uint8 SUPER_WEAPON = 5;

    float Player::UpdateAfterburner(float dt, bool active) {
        if (!HasPowerup(PowerupFlag::Afterburner)) return 0;

        if (active != AfterburnerActive) {
            if (AfterburnerCharge > 0 && active) {
                // play looping sound
                //constexpr int loopStart = 32027;
                //constexpr int loopEnd = 48452;
                Sound3D sound(ID);
                sound.Resource = Resources::GetSoundResource(SoundID::AfterburnerIgnite);
                sound.FromPlayer = true;
                sound.Radius = 125;
                Sound::Play(sound);
            }
            else {
                Sound3D sound(ID);
                sound.Resource = Resources::GetSoundResource(SoundID::AfterburnerStop);
                sound.FromPlayer = true;
                sound.Radius = 125;
                Sound::Play(sound);
            }
        }

        AfterburnerActive = active;

        if (active) {
            constexpr float AFTERBURNER_USE_SECS = 3;
            constexpr float USE_SPEED = 1 / 15.0f / AFTERBURNER_USE_SECS;

            float oldCount = AfterburnerCharge / USE_SPEED;
            AfterburnerCharge -= dt / AFTERBURNER_USE_SECS;

            if (AfterburnerCharge < 0) AfterburnerCharge = 0;
            float count = AfterburnerCharge / USE_SPEED;

            if (oldCount != count) {} // drop blobs
            return 1 + std::min(0.5f, AfterburnerCharge) * 2; // Falloff from 2 under 50% charge
        }
        else {
            float chargeUp = std::min(dt / 8, 1 - AfterburnerCharge); // 8 second recharge
            float energy = std::max(Energy - 10, 0.0f); // don't drop below 10 energy
            chargeUp = std::min(chargeUp, energy / 10); // limit charge if <= 10 energy
            AfterburnerCharge += chargeUp;
            Energy -= chargeUp * 100 / 10; // full charge uses 10% energy
        }

        return 0;
    }

    void Player::ArmPrimary(PrimaryWeaponIndex index) {
        const auto requestedWeapon = (uint8)index;
        auto weapon = (uint8)index;

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
            auto msg = fmt::format("{} {}!", Resources::GetString(GameString::DontHave), Resources::GetPrimaryName(index));
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
            auto msg = fmt::format("{} {}{}!",
                                   Resources::GetString(GameString::HaveNo),
                                   Resources::GetSecondaryName(index),
                                   Resources::GetString(GameString::Sx));
            PrintHudMessage(msg);
            Sound::Play(Resources::GetSoundResource(SoundID::SelectFail));
            return;
        }

        Sound::Play(Resources::GetSoundResource(SoundID::SelectSecondary));
        SecondaryDelay = RearmTime;
        Secondary = (SecondaryWeaponIndex)weapon;
        SecondaryWasSuper[weapon % SUPER_WEAPON] = weapon >= SUPER_WEAPON;
    }

    void Player::Update(float dt) {
        PrimaryDelay -= dt;
        SecondaryDelay -= dt;

        if (HasPowerup(PowerupFlag::Cloak) && Game::Time > CloakTime + CLOAK_TIME) {
            Sound::Play(Resources::GetSoundResource(SoundID::CloakOff));
            RemovePowerup(PowerupFlag::Cloak);
        }

        if (HasPowerup(PowerupFlag::Invulnerable) && Game::Time > InvulnerableTime + INVULN_TIME) {
            Sound::Play(Resources::GetSoundResource(SoundID::InvulnOff));
            RemovePowerup(PowerupFlag::Invulnerable);
        }

        if (auto player = Game::Level.TryGetObject(ID)) {
            if (auto seg = Game::Level.TryGetSegment(player->Segment)) {
                if (seg->Type == SegmentType::Energy && Energy < 100) {
                    constexpr float ENERGY_PER_SECOND = 25.0f;
                    Energy += ENERGY_PER_SECOND * dt;

                    if (RefuelSoundTime <= Game::Time) {
                        Sound::Play(Resources::GetSoundResource(SoundID::Refuel), 0.5f);
                        constexpr float REFUEL_SOUND_DELAY = 0.25f;
                        RefuelSoundTime = (float)Game::Time + REFUEL_SOUND_DELAY;
                    }
                }
            }
        }

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
        auto& weapon = Resources::GetWeapon(id);
        PrimaryDelay = weapon.FireDelay;

        Energy -= weapon.EnergyUsage;
        PrimaryAmmo[1] -= weapon.AmmoUsage; // only vulcan ammo

        auto& ship = PyroGX;
        auto& sequence = ship.Weapons[(int)Primary].Firing;
        if (FiringIndex >= sequence.size()) FiringIndex = 0;

        for (int i = 0; i < 8; i++) {
            if (sequence[FiringIndex].Gunpoints[i]) {
                auto& behavior = Game::GetWeaponBehavior(weapon.Extended.Behavior);
                behavior(*this, i, id);
            }
        }

        if (HasPowerup(PowerupFlag::QuadLasers)) {
            for (int i = 0; i < 8; i++) {
                if (ship.Weapons[(int)Primary].QuadGunpoints[i]) {
                    auto& behavior = Game::GetWeaponBehavior(weapon.Extended.Behavior);
                    behavior(*this, i, id);
                }
            }
        }

        FiringIndex = (FiringIndex + 1) % sequence.size();

        // todo: Swap to different weapon if ammo or energy == 0
    }

    void Player::HoldPrimary() {

    }

    void Player::ReleasePrimary() {
        //auto id = GetPrimaryWeaponID();
        //auto& weapon = Resources::GameData.Weapons[(int)id];

        //if (weapon.Extended.Chargable && WeaponCharge > 0) {
        //    Game::FireWeapon(ID, 0, id);
        //    Game::FireWeapon(ID, 1, id);
        //}
    }

    void Player::FireSecondary() {
        if (!CanFireSecondary()) return;

        auto id = GetSecondaryWeaponID();
        auto& weapon = Resources::GameData.Weapons[(int)id];
        SecondaryDelay = weapon.FireDelay;
        auto& ship = PyroGX;

        auto& sequence = ship.Weapons[10 + (int)Secondary].Firing;
        if (MissileFiringIndex >= sequence.size()) MissileFiringIndex = 0;

        for (int i = 0; i < 8; i++) {
            if (sequence[MissileFiringIndex].Gunpoints[i])
                Game::FireWeapon(ID, i, id);
        }

        MissileFiringIndex = (MissileFiringIndex + 1) % 2;
        SecondaryAmmo[(int)Secondary] -= weapon.AmmoUsage;
        // Swap to different weapon if ammo == 0
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

    void ScreenFlash(const Color& color) {
        // Tint the screen
    }

    bool Player::PickUpEnergy() {
        constexpr float MAX_ENERGY = 200;
        if (Energy < MAX_ENERGY) {
            Energy += 3 + 3 * (5 - Game::Difficulty);
            if (Energy > MAX_ENERGY) Energy = MAX_ENERGY;

            ScreenFlash({ 15, 15, 7 });
            auto msg = fmt::format("{} {} {}", Resources::GetString(GameString::Energy), Resources::GetString(GameString::BoostedTo), int(Energy));
            PrintHudMessage(msg);
            return true;
        }
        else {
            PrintHudMessage("your energy is maxed out!");
            return false;
        }
    }

    void AutoselectPrimary() {}

    int Player::PickUpAmmo(PrimaryWeaponIndex index, uint16 amount) {
        if (amount == 0) return amount;

        auto max = PyroGX.Weapons[(int)index].MaxAmmo;
        if (HasPowerup(PowerupFlag::AmmoRack))
            max *= 2;

        auto& ammo = PrimaryAmmo[(int)index];
        if (ammo >= max)
            return 0;

        auto prevAmmo = ammo;
        ammo += amount;

        if (ammo > max) {
            amount += max - ammo;
            ammo = max;
        }

        if (prevAmmo == 0)
            AutoselectPrimary();

        return amount;
    }

    void Player::TouchPowerup(Object& obj) {
        if (Shields < 0) return; // Player is dead!

        assert(obj.Type == ObjectType::Powerup);

        auto id = PowerupID(obj.ID);
        auto& powerup = Resources::GameData.Powerups[obj.ID];

        bool used = false, ammoPickedUp = false;

        auto PickUpAccesory = [this](PowerupFlag powerup, string_view name) {
            if (HasPowerup(powerup)) {
                auto msg = fmt::format("{} the {}!", Resources::GetString(GameString::AlreadyHave), name);
                PrintHudMessage(msg);
                return PickUpEnergy();
            }
            else {
                GivePowerup(powerup);
                ScreenFlash({ 15, 0, 15 });
                PrintHudMessage(fmt::format("{}!", name));
                return true;
            }
        };

        auto TryPickUpPrimary = [this](PrimaryWeaponIndex weapon) {
            auto pickedUp = PickUpPrimary(weapon);
            if (!pickedUp)
                pickedUp = PickUpEnergy();
            return pickedUp;
        };

        switch (id) {
            case PowerupID::ExtraLife:
                Lives++;
                ScreenFlash({ 15, 15, 15 });
                used = true;
                break;

            case PowerupID::Energy:
                used = PickUpEnergy();
                break;

            case PowerupID::ShieldBoost:
            {
                constexpr float MAX_SHIELDS = 200;
                if (Shields < MAX_SHIELDS) {
                    Shields += 3 + 3 * (5 - Game::Difficulty);
                    if (Shields > MAX_SHIELDS) Shields = MAX_SHIELDS;

                    ScreenFlash({ 0, 0, 15 });
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
                    ScreenFlash({ 10, 0, 10 });
                    auto msg = fmt::format("{} {} {}", Resources::GetString(GameString::Laser), Resources::GetString(GameString::BoostedTo), LaserLevel + 1);
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
                ScreenFlash({ 0, 0, 15 });

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
                ScreenFlash({ 15, 0, 0 });

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
                ScreenFlash({ 15, 15, 7 });

                auto msg = fmt::format("{} {}", Resources::GetString(GameString::Yellow), Resources::GetString(GameString::AccessGranted));
                PrintHudMessage(msg);
                used = true;
                break;
            }

            case PowerupID::Vulcan:
            case PowerupID::Gauss:
            {
                used = PickUpPrimary(obj.ID == (int)PowerupID::Vulcan ? PrimaryWeaponIndex::Vulcan : PrimaryWeaponIndex::Gauss);

                auto& ammo = obj.Control.Powerup.Count; // remaining ammo on the weapon

                if (ammo > 0) {
                    auto amount = PickUpAmmo(PrimaryWeaponIndex::Vulcan, (uint16)ammo);
                    ammo -= amount;
                    if (!used && amount > 0) {
                        ScreenFlash({ 7, 14, 21 });
                        PrintHudMessage(fmt::format("{}!", Resources::GetString(GameString::VulcanAmmo)));
                        ammoPickedUp = true;
                        if (ammo == 0)
                            used = true; // remove object if all ammo was taken
                    }
                }

                break;
            }

            case PowerupID::Spreadfire:
                used = TryPickUpPrimary(PrimaryWeaponIndex::Spreadfire);
                break;

            case PowerupID::Plasma:
                used = TryPickUpPrimary(PrimaryWeaponIndex::Plasma);
                break;

            case PowerupID::Fusion:
                used = TryPickUpPrimary(PrimaryWeaponIndex::Fusion);
                break;

            case PowerupID::SuperLaser:
            {
                if (LaserLevel >= MAX_SUPER_LASER_LEVEL) {
                    LaserLevel = MAX_SUPER_LASER_LEVEL;
                    PrintHudMessage("super laser maxed out!");
                    used = PickUpEnergy();
                }
                else {
                    //auto oldLevel = LaserLevel;
                    if (LaserLevel <= MAX_LASER_LEVEL) {
                        LaserLevel = MAX_LASER_LEVEL;

                        if (Primary == PrimaryWeaponIndex::Laser) {
                            Sound::Play(Resources::GetSoundResource(SoundID::SelectPrimary));
                            PrimaryDelay = RearmTime;
                            //UpgradingSuperLaser = true;
                        }
                    }

                    LaserLevel++;
                    ScreenFlash({ 10, 0, 10 });
                    PrintHudMessage(fmt::format("super boost to laser level {}", LaserLevel + 1));
                    used = true;
                    // todo: autoswap
                }
                break;
            }

            case PowerupID::Phoenix:
                used = TryPickUpPrimary(PrimaryWeaponIndex::Phoenix);
                break;

            case PowerupID::Omega:
                used = TryPickUpPrimary(PrimaryWeaponIndex::Omega);
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

            case PowerupID::ProximityBomb:
                used = PickUpSecondary(SecondaryWeaponIndex::Proximity, 4);
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

            case PowerupID::SmartBomb:
                used = PickUpSecondary(SecondaryWeaponIndex::SmartMine, 4);
                break;

            case PowerupID::MercuryMissile1:
                used = PickUpSecondary(SecondaryWeaponIndex::Mercury);
                break;

            case PowerupID::MercuryMissile4:
                used = PickUpSecondary(SecondaryWeaponIndex::Mercury, 4);
                break;

            case PowerupID::EarthshakerMissile:
                used = PickUpSecondary(SecondaryWeaponIndex::Shaker);
                break;

            case PowerupID::VulcanAmmo:
                if (PickUpAmmo(PrimaryWeaponIndex::Vulcan, 2500)) {
                    ScreenFlash({ 7, 14, 21 });
                    PrintHudMessage("vulcan ammo!");
                    used = true;
                }
                else {
                    PrintHudMessage(fmt::format("you already have {} vulcan rounds!", PrimaryAmmo[1]));
                }
                break;

            case PowerupID::Cloak:
            {
                if (HasPowerup(PowerupFlag::Cloak)) {
                    auto msg = fmt::format("{} {}!", Resources::GetString(GameString::AlreadyAre), Resources::GetString(GameString::Cloaked));
                    PrintHudMessage(msg);
                }
                else {
                    GivePowerup(PowerupFlag::Cloak);
                    CloakTime = (float)Game::Time;
                    PrintHudMessage(fmt::format("{}!", Resources::GetString(GameString::CloakingDevice)));
                    used = true;
                }
                break;
            };

            case PowerupID::Invulnerability:
                if (HasPowerup(PowerupFlag::Invulnerable)) {
                    auto msg = fmt::format("{} {}!", Resources::GetString(GameString::AlreadyAre), Resources::GetString(GameString::Invulnerable));
                    PrintHudMessage(msg);
                }
                else {
                    GivePowerup(PowerupFlag::Invulnerable);
                    InvulnerableTime = (float)Game::Time;
                    PrintHudMessage(fmt::format("{}!", Resources::GetString(GameString::Invulnerability)));
                    used = true;
                }
                break;

            case PowerupID::QuadFire:
                used = PickUpAccesory(PowerupFlag::QuadLasers, Resources::GetString(GameString::QuadLasers));
                break;

            case PowerupID::FullMap:
                used = PickUpAccesory(PowerupFlag::FullMap, "full map");
                break;

            case PowerupID::Converter:
                used = PickUpAccesory(PowerupFlag::Converter, "energy to shield converter");
                break;

            case PowerupID::AmmoRack:
                used = PickUpAccesory(PowerupFlag::AmmoRack, "ammo rack");
                break;

            case PowerupID::Afterburner:
                used = PickUpAccesory(PowerupFlag::Afterburner, "afterburner");
                break;

            case PowerupID::Headlight:
                used = PickUpAccesory(PowerupFlag::Headlight, "headlight");
                break;
        }

        if (used || ammoPickedUp) {
            obj.Lifespan = -1;

            Sound3D sound(ObjID(0));
            sound.Resource = Resources::GetSoundResource(powerup.HitSound);
            sound.FromPlayer = true;
            Sound::Play(sound);
        }
    }

    void Player::TouchObject(Object& obj) {
        if (obj.Type == ObjectType::Powerup) {
            TouchPowerup(obj);
        }

        if (obj.Type == ObjectType::Hostage) {
            obj.Lifespan = -1;
            Score += 1000;
            HostagesOnShip++;
            PrintHudMessage("hostage rescued!");
            ScreenFlash({ 0, 0, 25 });

            Sound3D sound(ObjID(0));
            sound.Resource = Resources::GetSoundResource(SoundID::RescueHostage);
            sound.FromPlayer = true;
            Sound::Play(sound);
        }
    }

    bool Player::PickUpPrimary(PrimaryWeaponIndex index) {
        uint16 flag = 1 << (int)index;
        auto name = Resources::GetString(GameString(104 + (int)index)); // this changes in d1

        if (index != PrimaryWeaponIndex::Laser && PrimaryWeapons & flag) {
            PrintHudMessage(fmt::format("you already have the {}", name));
            return false;
        }

        PrimaryWeapons |= flag;
        AutoselectPrimary();
        ScreenFlash({ 7, 14, 21 });

        if (index != PrimaryWeaponIndex::Laser)
            PrintHudMessage(fmt::format("{}!", name));

        return true;
    }

    bool Player::PickUpSecondary(SecondaryWeaponIndex index, uint16 count) {
        auto max = PyroGX.Weapons[10 + (int)index].MaxAmmo;
        if (HasPowerup(PowerupFlag::AmmoRack))
            max *= 2;

        auto& ammo = SecondaryAmmo[(int)index];
        auto name = Resources::GetString(GameString(114 + (int)index)); // this changes in d1

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

        // todo: autoselect stuff, set ownership flag

        if (pickedUp > 1) {
            ScreenFlash({ 15, 15, 15 });
            auto msg = fmt::format("{} {}{}", pickedUp, name, Resources::GetString(GameString::Sx));
            PrintHudMessage(msg);
        }
        else {
            ScreenFlash({ 10, 10, 10 });
            PrintHudMessage(fmt::format("{}!", name));
        }

        // todo: spawn individual missiles if count > 1 and full
        return true;
    }
}