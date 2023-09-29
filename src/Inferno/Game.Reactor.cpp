#include "pch.h"
#include "Game.Reactor.h"
#include "Game.h"
#include "Game.Wall.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Game {
    void PlaySelfDestructSounds(float delay) {
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

    void DestroyReactor(Object& obj) {
        assert(obj.Type == ObjectType::Reactor);

        obj.Render.Model.ID = Resources::GameData.DeadModels[(int)obj.Render.Model.ID];
        Render::LoadModelDynamic(obj.Render.Model.ID);

        AddPointsToScore(REACTOR_SCORE);

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
                Render::AddBeam(*beam, CountdownTimer + 5, startObj);
            }
        }

        //if (auto beam = Render::EffectLibrary.GetBeamInfo("reactor_internal_arcs")) {
        //    for (int i = 0; i < 4; i++) {
        //        auto startObj = ObjID(&obj - Level.Objects.data());
        //        beam->StartDelay = i * 0.4f + Random() * 0.125f;
        //        Render::AddBeam(*beam, CountdownTimer + 5, startObj);
        //    }
        //}

        // Load critical clips
        Set<TexID> ids;
        for (auto& eclip : Resources::GameData.Effects) {
            auto& crit = Resources::GetEffectClip(eclip.CritClip);
            Seq::insert(ids, crit.VClip.GetFrames());
        }

        Render::Materials->LoadMaterials(Seq::ofSet(ids), false);
        PlaySelfDestructSounds(3);
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

    int GetBestGun(const Object& reactor, const Vector3& target) {
        if (reactor.Type != ObjectType::Reactor)
            return -1;

        auto& info = Resources::GameData.Reactors[reactor.ID];
        float bestDot = -2;
        int bestGun = -1;

        for (int i = 0; i < info.Guns; i++) {
            auto gunPoint = Vector3::Transform(info.GunPoints[i], reactor.Rotation) + reactor.Position;
            auto targetDir = target - gunPoint;
            targetDir.Normalize();

            auto gunDir = Vector3::Transform(info.GunDirs[i], reactor.Rotation);
            auto dot = targetDir.Dot(gunDir);

            if (dot > bestDot) {
                bestDot = dot;
                bestGun = i;
            }
        }

        ASSERT(bestGun != -1);

        return bestDot < 0 ? -1 : bestGun;

        //fix best_dot = -F1_0 * 2;
        //int best_gun = -1;

        //for (int i = 0; i < num_guns; i++) {
        //	fix			dot;
        //	vms_vector	gun_vec;

        //	vm_vec_sub(&gun_vec, objpos, &gun_pos[i]);
        //	vm_vec_normalize_quick(&gun_vec);
        //	dot = vm_vec_dot(&gun_dir[i], &gun_vec);

        //	if (dot > best_dot) {
        //		best_dot = dot;
        //		best_gun = i;
        //	}
        //}

        //Assert(best_gun != -1);		// Contact Mike.  This is impossible.  Or maybe you're getting an unnormalized vector somewhere.

        //if (best_dot < 0)
        //	return -1;
        //else
        //	return best_gun;
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

    void UpdateReactorAI(const Inferno::Object& reactor, float dt) {
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
            auto gun = GetBestGun(reactor, Reactor.KnownPlayerPosition);
            if (gun >= 0) {
                auto& info = Resources::GameData.Reactors[reactor.ID];
                auto gunPoint = Vector3::Transform(info.GunPoints[gun], reactor.Rotation) + reactor.Position;
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
}
