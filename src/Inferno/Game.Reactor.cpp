#include "pch.h"
#include "VisualEffects.h"
#include "Game.AI.h"
#include "Game.h"
#include "Game.Reactor.h"
#include "Game.Wall.h"
#include "Graphics.h"
#include "Resources.h"
#include "Settings.h"
#include "SoundSystem.h"
#include "logging.h"

namespace Inferno::Game {
    void PlaySelfDestructSounds(float delay) {
        if (!Settings::Inferno.Descent3Enhanced) return;

        AmbientSoundEmitter explosions{};
        explosions.Delay = { 0.5f, 3.0f };
        explosions.Sounds = {
            "AmbExplosionFarA", "AmbExplosionFarB", "AmbExplosionFarC", "AmbExplosionFarE",
            "AmbExplosionFarF", /*"AmbExplosionFarG",*/ "AmbExplosionFarI"
        };
        explosions.Volume = { 3.5f, 4.5f };
        explosions.Distance = 500;
        explosions.NextPlayTime = Time + delay;
        Sound::AddEmitter(std::move(explosions));

        AmbientSoundEmitter creaks{};
        creaks.Delay = { 3.0f, 6.0f };
        creaks.Sounds = {
            "AmbPipeKnockB", "AmbPipeKnockC",
            "AmbEnvSlowMetal", "AmbEnvShortMetal",
            "EnvSlowCreakB2", "EnvSlowCreakC", /*"EnvSlowCreakD",*/ "EnvSlowCreakE"
        };
        creaks.Volume = { 1.5f, 2.00f };
        creaks.Distance = 100;
        creaks.NextPlayTime = Time + delay;
        Sound::AddEmitter(std::move(creaks));
    }

    int GetCountdown() {
        auto difficulty = std::clamp((int)Difficulty, 0, (int)DifficultyLevel::Count - 1);

        if (Level.BaseReactorCountdown != DEFAULT_REACTOR_COUNTDOWN) {
            return Level.BaseReactorCountdown + Level.BaseReactorCountdown * ((int)DifficultyLevel::Count - difficulty - 1) / 2;
        }
        else if (Level.IsDescent1()) {
            constexpr std::array DefaultCountdownTimes = { 50, 45, 40, 35, 30 };
            static_assert(DefaultCountdownTimes.size() == (int)DifficultyLevel::Count);
            return DefaultCountdownTimes[difficulty];
        }
        else {
            constexpr std::array DefaultCountdownTimes = { 90, 60, 45, 35, 30 };
            static_assert(DefaultCountdownTimes.size() == (int)DifficultyLevel::Count);
            return DefaultCountdownTimes[difficulty];
        }
    } 

    void BeginSelfDestruct() {
        for (auto& tag : Level.ReactorTriggers) {
            if (auto wall = Level.TryGetWall(tag)) {
                if (wall->Type == WallType::Door && wall->State == WallState::Closed)
                    OpenDoor(Level, tag, Faction::Neutral);

                if (wall->Type == WallType::Destroyable)
                    DestroyWall(Level, tag);
            }
        }

        TotalCountdown = GetCountdown();

        for (auto& obj : Level.Objects) {
            if (obj.IsReactor())
                DestroyReactor(obj);
        }

        //TotalCountdown = 1; // debug
        CountdownTimer = (float)TotalCountdown;
        ControlCenterDestroyed = true;
        PlaySelfDestructSounds(3);

        // Apply a strong force from the initial reactor explosion
        auto& player = Game::GetPlayerObject();
        auto signX = RandomInt(1) ? 1 : -1;
        auto signY = RandomInt(1) ? 1 : -1;
        player.Physics.AngularVelocity.z += signX * .35f;
        player.Physics.AngularVelocity.x += signY * .5f;
    }

    void StopSelfDestruct() {
        ControlCenterDestroyed = false;
    }

    bool DestroyReactor(Object& obj) {
        assert(obj.Type == ObjectType::Reactor);
        if (HasFlag(obj.Flags, ObjectFlag::Destroyed)) return false;
        SetFlag(obj.Flags, ObjectFlag::Destroyed);

        if (Seq::inRange(Resources::GameData.DeadModels, (int)obj.Render.Model.ID)) {
            obj.Render.Model.ID = Resources::GameData.DeadModels[(int)obj.Render.Model.ID];
            Graphics::LoadModel(obj.Render.Model.ID);
        }

        AddPointsToScore(REACTOR_SCORE);

        // Big boom
        Sound3D sound(SoundID::Explosion);
        sound.Merge = false;
        sound.Radius = 400;

        sound.Volume = 1.25f;
        sound.Pitch = -.3f;
        Sound::Play(sound, obj.Position, obj.Segment);

        sound.Volume = 1.5f;
        sound.Pitch = -.8f;
        sound.Delay = 0.14f;
        Sound::Play(sound, obj.Position, obj.Segment);

        int instances = 1000; // Want this to last forever in case multiple reactor levels are ever added

        if (auto e = EffectLibrary.GetSparks("reactor_destroyed"))
            AddSparkEmitter(*e, obj.Segment, obj.Position);

        if (auto e = EffectLibrary.GetExplosion("reactor_initial_explosion")) {
            e->Radius = { obj.Radius * 0.5f, obj.Radius * 0.7f };
            e->Variance = obj.Radius * 0.9f;
            CreateExplosion(*e, obj.Segment, obj.Position);
        }

        if (auto e = EffectLibrary.GetExplosion("reactor large explosions")) {
            // Larger periodic explosions with sound
            e->Variance = obj.Radius * 0.45f;
            e->Instances = instances;
            CreateExplosion(*e, obj.Segment, obj.Position);
        }

        if (auto e = EffectLibrary.GetExplosion("reactor small explosions")) {
            e->Variance = obj.Radius * 0.55f;
            e->Instances = instances * 10;
            CreateExplosion(*e, obj.Segment, obj.Position);
        }

        if (auto beam = EffectLibrary.GetBeamInfo("reactor_arcs")) {
            LightEffectInfo light;
            light.LightColor = beam->Color * 0.25f;
            light.Radius = 25;
            light.Mode = DynamicLightMode::StrongFlicker;
            AddLight(light, obj.Position, MAX_OBJECT_LIFE, obj.Segment);

            for (int i = 0; i < 4; i++) {
                auto startObj = Game::GetObjectRef(obj);
                beam->StartDelay = i * 0.4f + Random() * 0.125f;
                AttachBeam(*beam, (float)instances, startObj);
            }
        }

        //if (auto beam = EffectLibrary.GetBeamInfo("reactor_internal_arcs")) {
        //    for (int i = 0; i < 4; i++) {
        //        auto startObj = ObjID(&obj - Level.Objects.data());
        //        beam->StartDelay = i * 0.4f + Random() * 0.125f;
        //        AddBeam(*beam, CountdownTimer + 5, startObj);
        //    }
        //}
        return true;
    }

    void UpdateReactorCountdown(float dt) {
        // Shake the player ship due to seismic disturbance
        auto& player = Game::GetPlayerObject();
        auto fc = std::min(CountdownSeconds, 16);
        auto scale = Difficulty == DifficultyLevel::Trainee ? 0.25f : 1; // reduce shaking on trainee
        player.Physics.AngularVelocity.z += RandomN11() * 0.25f * (3.0f / 16 + (16 - fc) / 32.0f) * scale;
        player.Physics.AngularVelocity.x += RandomN11() * 0.25f * (3.0f / 16 + (16 - fc) / 32.0f) * scale;

        auto time = CountdownTimer;
        CountdownTimer -= dt;
        CountdownSeconds = int(CountdownTimer + 7.0f / 8);

        constexpr float COUNTDOWN_VOICE_TIME = 12.75f;
        if (time > COUNTDOWN_VOICE_TIME && CountdownTimer <= COUNTDOWN_VOICE_TIME) {
            Sound::Play2D({ SoundID::Countdown13 });
        }

        if (int(time + 7.0f / 8) != CountdownSeconds) {
            if (CountdownSeconds >= 0 && CountdownSeconds < 10)
                Sound::Play2D({ SoundID((int)SoundID::Countdown0 + CountdownSeconds) });
            if (CountdownSeconds == TotalCountdown - 1)
                Sound::Play2D({ SoundID::SelfDestructActivated });
        }

        if (CountdownTimer > 0) {
            // play siren every 2 seconds
            constexpr float SIREN_DELAY = 3.4f; // Seconds after the reactor is destroyed to start playing siren. Exists due to self destruct message.
            auto size = (float)TotalCountdown - CountdownTimer / 0.65f;
            auto oldSize = (float)TotalCountdown - time / 0.65f;
            if (std::floor(size) != std::floor(oldSize) && CountdownSeconds < TotalCountdown - SIREN_DELAY)
                Sound::Play2D({ SoundID::Siren });
        }
        else {
            if (time > 0) {
                Sound::Play2D({ SoundID::MineBlewUp });
                Game::ScreenGlow.SetTarget(Color(1, 1, 1, 60), Game::Time, 4);
                //Game::Exposure.SetTarget(60, 4);
                //Game::BloomStrength.SetTarget(10, 4);
            }

            //auto flash = -CountdownTimer / 4.0f; // 4 seconds to fade out
            //ScreenFlash = Color{ flash, flash, flash };

            //float bloom = std::lerp(0.0f, 10.0f, flash);
            //float exposure = std::lerp(0.0f, 60.0f, flash);

            if (CountdownTimer < -4) {
                // todo: kill player, show "you have died in the mine" message
                Game::Player.ResetInventory();
                SetState(GameState::Editor);
                // todo: show score screen
            }
        }
    }

    // Returns the gun index and the position of the gun in world space
    Tuple<int, Vector3> GetBestGun(const Object& reactor, const Vector3& target) {
        if (reactor.Type != ObjectType::Reactor)
            return { -1, {} };

        float bestDot = -2;
        int bestGun = -1;
        Vector3 bestGunPoint;

        if (auto info = Seq::tryItem(Resources::GameData.Reactors, reactor.ID)) {
            for (uint8 gun = 0; gun < info->Guns; gun++) {
                auto gunSubmodel = GetGunpointSubmodelOffset(reactor, gun);
                auto objOffset = GetSubmodelOffset(reactor, gunSubmodel);
                auto gunPoint = Vector3::Transform(objOffset, reactor.GetTransform());
                auto targetDir = target - gunPoint;
                targetDir.Normalize();

                auto gunDir = Vector3::Transform(info->GunDirs[gun], reactor.Rotation);
                auto dot = targetDir.Dot(gunDir);

                if (dot > bestDot) {
                    bestDot = dot;
                    bestGun = gun;
                    bestGunPoint = gunPoint;
                }
            }
        }

        ASSERT(bestGun != -1);
        return bestDot < 0 ? Tuple{ -1, Vector3::Zero } : Tuple{ bestGun, bestGunPoint };
    }

    constexpr float REACTOR_SIGHT_DISTANCE = 200;
    constexpr float REACTOR_FORGET_TIME = 5; // How long to keep firing after last seeing the player

    struct ReactorState {
        Vector3 KnownPlayerPosition;
        float ThinkDelay = 0;
        float NextFireTime = 0;
        float FireDelay = 0;
        float LastSeenPlayer = MAX_OBJECT_LIFE;
        bool SeenPlayer = false;
        bool HasBeenHit = false;
    };

    namespace {
        ReactorState Reactor{};
    }

    void UpdateReactor(Inferno::Object& reactor) {
        // Update reactor is separate from AI because the player might destroy it with a guided missile
        // outside of the normal AI update range
        if (reactor.HitPoints <= 0) {
            if (DestroyReactor(reactor))
                BeginSelfDestruct();
        }
    }

    void UpdateReactorAI(Inferno::Object& reactor, float dt) {
        if (!Game::EnableAi()) return;
        Reactor.ThinkDelay -= dt;
        Reactor.FireDelay -= dt;

        if (HasFlag(reactor.Flags, ObjectFlag::Destroyed)) return;
        if (Reactor.LastSeenPlayer >= 0) Reactor.LastSeenPlayer += dt;
        if (Reactor.ThinkDelay > 0) return;

        auto& player = Game::GetPlayerObject();
        //if (!Reactor.HasBeenHit && !Reactor.SeenPlayer) {
        //    // Check if player is visible
        //    auto distance = Vector3::Distance(player.Position, reactor.Position);
        //    if (distance < REACTOR_SIGHT_DISTANCE) {
        //        Reactor.SeenPlayer = Game::ObjectCanSeeObject(reactor, player);
        //        Reactor.FireDelay = 0;
        //        Reactor.KnownPlayerPosition = player.Position;
        //    }
        //    return;
        //}

        if (Game::ObjectCanSeeObject(reactor, player)) {
            Reactor.LastSeenPlayer = 0;
            Reactor.KnownPlayerPosition = player.Position;
        }
        else {
            Reactor.ThinkDelay = 0.25f;
        }

        if (Reactor.LastSeenPlayer > REACTOR_FORGET_TIME)
            return;

        if (Reactor.FireDelay < 0) {
            auto [gun, gunPoint] = GetBestGun(reactor, Reactor.KnownPlayerPosition);

            // Lead the player if they are visible on hotshot and higher
            //auto& weapon = Resources::GetWeapon(WeaponID::ReactorBlob);
            //auto targetPosition =
            //    Game::Difficulty < 2 || Reactor.LastSeenPlayer > 0
            //    ? Reactor.KnownPlayerPosition
            //    : LeadTarget(gunPoint, reactor.Segment, player, weapon);

            if (gun >= 0) {
                auto dir = Reactor.KnownPlayerPosition - gunPoint;
                dir.Normalize();

                Game::FireWeaponInfo info = { .id = WeaponID::ReactorBlob, .gun = (uint8)gun, .customDir = &dir };
                FireWeapon(reactor, info);

                // Randomly fire more blobs based on level number and difficulty
                auto chance = 1.0f / ((float)Game::LevelNumber / 4 + 2);
                int count = 0;
                while (count++ < (int)Game::Difficulty && Random() > chance) {
                    dir += RandomVector(1 / 6.0f);
                    dir.Normalize();
                    FireWeapon(reactor, info); // customDir is still pointing at dir
                }
            }

            Reactor.FireDelay = ((int)DifficultyLevel::Count - (int)Game::Difficulty) / 4.0f;
        }
    }

    void InitReactor(const Inferno::Level& level, Object& reactor) {
        Reactor = {}; // Reset state

        if (Seq::findIndex(Game::Level.Objects, IsBossRobot)) {
            reactor.Lifespan = -1; // Remove reactor on levels with a boss robot
            return;
        }

        if (level.ReactorStrength > 0) {
            reactor.HitPoints = (float)level.ReactorStrength;
        }
        else {
            // Scale reactor health with level number
            if (Game::LevelNumber >= 0) {
                reactor.HitPoints = 200.0f + 50.0f * Game::LevelNumber;
            }
            else {
                // Secret levels
                reactor.HitPoints = 200.0f - Game::LevelNumber * 100.0f;
            }
        }

        SPDLOG_INFO("Reactor has {} hit points", reactor.HitPoints);

        // M is very bass heavy "AmbDroneReactor"
        Sound3D reactorHum({ "AmbDroneM" });
        reactorHum.Radius = 300;
        reactorHum.Looped = true;
        reactorHum.Volume = 0.3f;
        reactorHum.Occlusion = false;
        Sound::PlayFrom(reactorHum, reactor);

        reactorHum.Resource = { "Indoor Ambient 5" };
        reactorHum.Radius = 160;
        reactorHum.Looped = true;
        reactorHum.Occlusion = true;
        reactorHum.Volume = 1.1f;
        Sound::PlayFrom(reactorHum, reactor);
    }
}
