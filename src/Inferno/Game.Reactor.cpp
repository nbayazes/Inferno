#include "pch.h"
#include "Game.Reactor.h"
#include "Game.h"
#include "Game.Wall.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"

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

    void SelfDestructMine() {
        for (auto& tag : Level.ReactorTriggers) {
            if (auto wall = Level.TryGetWall(tag)) {
                if (wall->Type == WallType::Door && wall->State == WallState::Closed)
                    OpenDoor(Level, tag);

                if (wall->Type == WallType::Destroyable)
                    DestroyWall(Level, tag);
            }
        }

        if (Level.BaseReactorCountdown != DEFAULT_REACTOR_COUNTDOWN) {
            TotalCountdown = Level.BaseReactorCountdown + Level.BaseReactorCountdown * (5 - Difficulty - 1) / 2;
        }
        else {
            constexpr std::array DefaultCountdownTimes = { 90, 60, 45, 35, 30 };
            TotalCountdown = DefaultCountdownTimes[Difficulty];
        }

        //TotalCountdown = 30; // debug
        CountdownTimer = (float)TotalCountdown;
        ControlCenterDestroyed = true;
        PlaySelfDestructSounds(3);
    }

    void DestroyReactor(Object& obj) {
        assert(obj.Type == ObjectType::Reactor);
        if (HasFlag(obj.Flags, ObjectFlag::Destroyed)) return;
        SetFlag(obj.Flags, ObjectFlag::Destroyed);

        if (Seq::inRange(Resources::GameData.DeadModels, (int)obj.Render.Model.ID)) {
            obj.Render.Model.ID = Resources::GameData.DeadModels[(int)obj.Render.Model.ID];
            Render::LoadModelDynamic(obj.Render.Model.ID);
        }

        AddPointsToScore(REACTOR_SCORE);
        SelfDestructMine();

        if (auto e = Render::EffectLibrary.GetSparks("reactor_destroyed"))
            Render::AddSparkEmitter(*e, obj.Segment, obj.Position);

        if (auto e = Render::EffectLibrary.GetExplosion("reactor_initial_explosion")) {
            e->Radius = { obj.Radius * 0.5f, obj.Radius * 0.7f };
            e->Variance = obj.Radius * 0.9f;
            Render::CreateExplosion(*e, obj.Segment, obj.Position);
        }

        if (auto e = Render::EffectLibrary.GetExplosion("reactor_large_explosions")) {
            // Larger periodic explosions with sound
            e->Variance = obj.Radius * 0.45f;
            e->Instances = TotalCountdown;
            Render::CreateExplosion(*e, obj.Segment, obj.Position);
        }

        if (auto e = Render::EffectLibrary.GetExplosion("reactor_small_explosions")) {
            e->Variance = obj.Radius * 0.55f;
            e->Instances = TotalCountdown * 10;
            Render::CreateExplosion(*e, obj.Segment, obj.Position);
        }

        if (auto beam = Render::EffectLibrary.GetBeamInfo("reactor_arcs")) {
            Render::DynamicLight light;
            light.LightColor = beam->Color;
            light.Radius = 15;
            light.Mode = DynamicLightMode::FastFlicker;
            light.Duration = MAX_OBJECT_LIFE;
            light.Segment = obj.Segment;
            light.Position = obj.Position;
            Render::AddDynamicLight(light);

            for (int i = 0; i < 4; i++) {
                auto startObj = Game::GetObjectRef(obj);
                beam->StartDelay = i * 0.4f + Random() * 0.125f;
                Render::AddBeam(*beam, (float)TotalCountdown + 5, startObj);
            }
        }

        //if (auto beam = Render::EffectLibrary.GetBeamInfo("reactor_internal_arcs")) {
        //    for (int i = 0; i < 4; i++) {
        //        auto startObj = ObjID(&obj - Level.Objects.data());
        //        beam->StartDelay = i * 0.4f + Random() * 0.125f;
        //        Render::AddBeam(*beam, CountdownTimer + 5, startObj);
        //    }
        //}
    }

    void UpdateReactorCountdown(float dt) {
        auto fc = std::min(CountdownSeconds, 16);
        auto scale = Difficulty == 0 ? 0.25f : 1; // reduce shaking on trainee

        // Shake the player ship
        auto& player = Game::GetPlayerObject();
        player.Physics.AngularVelocity.z += RandomN11() * 0.25f * (3.0f / 16 + (16 - fc) / 32.0f) * scale;
        player.Physics.AngularVelocity.x += RandomN11() * 0.25f * (3.0f / 16 + (16 - fc) / 32.0f) * scale;

        auto time = CountdownTimer;
        CountdownTimer -= dt;
        CountdownSeconds = int(CountdownTimer + 7.0f / 8);

        constexpr float COUNTDOWN_VOICE_TIME = 12.75f;
        if (time > COUNTDOWN_VOICE_TIME && CountdownTimer <= COUNTDOWN_VOICE_TIME) {
            Sound::Play({ SoundID::Countdown13 });
        }

        if (int(time + 7.0f / 8) != CountdownSeconds) {
            if (CountdownSeconds >= 0 && CountdownSeconds < 10)
                Sound::Play({ SoundID((int)SoundID::Countdown0 + CountdownSeconds) });
            if (CountdownSeconds == TotalCountdown - 1)
                Sound::Play({ SoundID::SelfDestructActivated });
        }

        if (CountdownTimer > 0) {
            // play siren every 2 seconds
            constexpr float SIREN_DELAY = 3.4f; // Seconds after the reactor is destroyed to start playing siren. Exists due to self destruct message.
            auto size = (float)TotalCountdown - CountdownTimer / 0.65f;
            auto oldSize = (float)TotalCountdown - time / 0.65f;
            if (std::floor(size) != std::floor(oldSize) && CountdownSeconds < TotalCountdown - SIREN_DELAY)
                Sound::Play({ SoundID::Siren });
        }
        else {
            if (time > 0)
                Sound::Play({ SoundID::MineBlewUp });

            auto flash = -CountdownTimer / 4.0f; // 4 seconds to fade out
            ScreenFlash = Color{ flash, flash, flash };

            if (CountdownTimer < -4) {
                // todo: kill player, show "you have died in the mine" message
                SetState(GameState::Editor);
            }
        }
    }

    Tuple<int, Vector3> GetBestGun(const Object& reactor, const Vector3& target) {
        if (reactor.Type != ObjectType::Reactor)
            return { -1, {} };

        auto& info = Resources::GameData.Reactors[reactor.ID];
        float bestDot = -2;
        int bestGun = -1;
        Vector3 gunPoint;

        for (int gun = 0; gun < info.Guns; gun++) {
            gunPoint = Vector3::Transform(info.GunPoints[gun], reactor.GetTransform());
            auto targetDir = target - gunPoint;
            targetDir.Normalize();

            auto& dir = info.GunDirs[gun];
            auto gunDir = Vector3::Transform(dir, reactor.Rotation);
            auto dot = targetDir.Dot(gunDir);

            if (dot > bestDot) {
                bestDot = dot;
                bestGun = gun;
            }
        }

        ASSERT(bestGun != -1);
        return bestDot < 0 ? Tuple<int, Vector3>{ -1, {} } : Tuple{ bestGun, gunPoint };
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
        if (reactor.HitPoints <= 0)
            DestroyReactor(reactor);
    }

    void UpdateReactorAI(const Inferno::Object& reactor, float dt) {
        if (Settings::Cheats.DisableAI) return;
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
            if (gun >= 0) {
                auto dir = Reactor.KnownPlayerPosition - gunPoint;
                dir.Normalize();

                FireWeapon(Game::GetObjectRef(reactor), WeaponID::ReactorBlob, (uint8)gun, &dir);

                // Randomly fire more blobs based on level number and difficulty
                auto chance = 1.0f / ((float)Game::LevelNumber / 4 + 2);
                int count = 0;
                while (count++ < Game::Difficulty && Random() > chance) {
                    dir += RandomVector(1 / 6.0f);
                    dir.Normalize();
                    FireWeapon(Game::GetObjectRef(reactor), WeaponID::ReactorBlob, (uint8)gun, &dir);
                }
            }

            Reactor.FireDelay = ((int)DifficultyLevel::Count - Game::Difficulty) / 4.0f;
        }
    }

    void InitReactor(const Inferno::Level& level, Object& reactor) {
        if (Seq::findIndex(Game::Level.Objects, IsBossRobot)) {
            reactor.Lifespan = -1; // Remove reactor on levels with a boss robot
            return;
        }

        if (level.ReactorStrength > 0) {
            reactor.HitPoints = (float)level.ReactorStrength;
        }
        else {
            // Scale reactor health scales with number
            if (Game::LevelNumber >= 0) {
                reactor.HitPoints = 200.0f + 200.0f / 4 * Game::LevelNumber;
            }
            else {
                // Secret levels
                reactor.HitPoints = 200.0f - Game::LevelNumber * 100.0f;
            }
        }

        SPDLOG_INFO("Reactor has {} hit points", reactor.HitPoints);

        // M is very bass heavy "AmbDroneReactor"
        Sound3D reactorHum({ "AmbDroneM" }, Game::GetObjectRef(reactor));
        reactorHum.Radius = 300;
        reactorHum.Looped = true;
        reactorHum.Volume = 0.3f;
        reactorHum.Occlusion = false;
        reactorHum.Position = reactor.Position;
        reactorHum.Segment = reactor.Segment;
        Sound::Play(reactorHum);

        reactorHum.Resource = { "Indoor Ambient 5" };
        reactorHum.Radius = 160;
        reactorHum.Looped = true;
        reactorHum.Occlusion = true;
        reactorHum.Volume = 1.1f;
        reactorHum.Position = reactor.Position;
        Sound::Play(reactorHum);
    }
}
