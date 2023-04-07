#include "pch.h"
#include "Object.h"
#include "Game.h"
#include "Game.Wall.h"
#include "Physics.h"
#include "Editor/Editor.Segment.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Game {
    // Gets the gunpoint offset relative to the object center
    Vector3 GetGunpointOffset(const Object& obj, int gun) {
        gun = std::clamp(gun, 0, 8);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            return robot.GunPoints[gun] * Vector3(1, 1, -1);
        }

        if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            return Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);;
            //offset = Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);
        }

        if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return Vector3::Zero;
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return reactor.GunPoints[gun];
        }

        return Vector3::Zero;
    }

    void ExplodeWeapon(const Object& obj) {
        if (obj.Type != ObjectType::Weapon) return;
        const Weapon& weapon = Resources::GetWeapon((WeaponID)obj.ID);
        if (weapon.SplashRadius <= 0) return; // don't explode weapons without a splash radius

        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;
        float damage = weapon.Damage[Game::Difficulty];

        Render::ExplosionInfo e;
        e.Radius = { weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f };
        e.Clip = vclip;
        e.Sound = soundId;
        e.Position = obj.Position;
        //e.Color = Color{ 1, 1, 1 };
        e.FadeTime = 0.1f;
        e.Segment = obj.Segment;
        Render::CreateExplosion(e);

        if (weapon.SplashRadius > 0) {
            GameExplosion ge{};
            ge.Damage = damage;
            ge.Force = damage;
            ge.Radius = weapon.SplashRadius;
            ge.Segment = obj.Segment;
            ge.Position = obj.Position;
            CreateExplosion(Game::Level, &obj, ge);
        }
    }

    void ProxMineBehavior(Object& obj) {
        constexpr auto PROX_WAKE_RANGE = 60;
        constexpr auto PROX_ACTIVATE_RANGE = 30;

        auto& cw = obj.Control.Weapon;

        if (TimeHasElapsed(obj.NextThinkTime)) {
            obj.NextThinkTime = (float)Game::Time + 0.25f;

            // Try to find a nearby target
            if (cw.TrackingTarget == ObjID::None) {
                // todo: filter targets based on if mine owner is a player
                auto [id, dist] = Game::FindNearestObject(obj.Position, PROX_WAKE_RANGE, ObjectMask::Enemy);
                if (id != ObjID::None && dist <= PROX_WAKE_RANGE)
                    cw.TrackingTarget = id; // New target!
            }
        }

        if (cw.TrackingTarget == ObjID::None)
            return; // Still no target

        auto target = Game::Level.TryGetObject(cw.TrackingTarget);
        auto dist = target ? obj.Distance(*target) : FLT_MAX;

        if (dist > PROX_WAKE_RANGE) {
            cw.TrackingTarget = ObjID::None; // Went out of range
        }
        else {
            //auto lerp = std::lerp(1.00f, 2.00f, (dist - PROX_ACTIVATE_RANGE) / (PROX_WAKE_RANGE - PROX_ACTIVATE_RANGE));
            //lerp = std::clamp(lerp, 1.0f, 2.0f);

            //if (TimeHasElapsed(cw.SoundDelay)) {
            //    Sound3D sound(obj.Position, obj.Segment);
            //    //sound.Pitch = 0.25f - (lerp - 1.25);
            //    sound.Resource = Resources::GetSoundResource(SoundID::HomingWarning);
            //    Sound::Play(sound);
            //    cw.SoundDelay = (float)Game::Time + lerp;
            //}

            if (dist <= PROX_ACTIVATE_RANGE && !cw.DetonateMine) {
                // Commit to the target
                cw.DetonateMine = true;
                obj.Lifespan = 2;
                ClearFlag(obj.Physics.Flags, PhysicsFlag::Bounce); // explode on contacting walls

                if (target) {
                    auto delta = target->Position - obj.Position;
                    delta.Normalize();
                    obj.Physics.Thrust = delta * 0.9; // fire and forget thrust
                }
            }
        }
    }

    void CreateSmartBlobs(const Object& obj) {
        auto parentType = obj.Control.Weapon.ParentType;

        List<ObjID> targets;
        targets.reserve(30);

        for (int i = 0; i < Game::Level.Objects.size(); i++) {
            auto& other = Game::Level.Objects[i];
            auto validTarget = other.Type == ObjectType::Robot && !other.Control.AI.IsCloaked() || other.Type == ObjectType::Player;

            if (!validTarget || (ObjID)i == other.Control.Weapon.Parent)
                continue; // Don't target cloaked robots (todo: or players?) or the owner

            if (other.Type == ObjectType::Player && parentType == ObjectType::Player)
                continue; // don't track the owning player

            if (other.Type == ObjectType::Robot && parentType == ObjectType::Robot)
                continue; // robot blobs can't track other robots

            auto dist = Vector3::Distance(other.Position, obj.Position);
            constexpr auto MAX_SMART_DISTANCE = 150.0f;
            if (dist <= MAX_SMART_DISTANCE) {
                if (ObjectToObjectVisibility(obj, other, true))
                    targets.push_back((ObjID)i);
            }
        }

        //const int children = 6; // todo: add to extended weapon data
        //auto wid = parentType == ObjectType::Player ? WeaponID::PlayerSmartBlob : WeaponID::RobotSmartBlob;

        //if (targets.empty()) {
        //    for (int i = 0; i < children; i++) {
        //        //CreateHomingWeapon(-1); // random target
        //    }
        //}
        //else {
        //    for (int i = 0; i < children; i++) {
        //        //CreateHomingWeapon(); // use random target in list
        //    }
        //}
    }

    void FireWeapon(ObjID objId, int gun, WeaponID id, bool showFlash, const Vector2& spread) {
        auto& level = Game::Level;
        auto& obj = level.Objects[(int)objId];
        auto gunOffset = GetGunpointOffset(obj, gun);
        auto point = Vector3::Transform(gunOffset, obj.GetTransform());
        auto& weapon = Resources::GameData.Weapons[(int)id];

        Object bullet{};
        bullet.Position = bullet.LastPosition = point;
        bullet.Rotation = bullet.LastRotation = obj.Rotation;
        auto direction = obj.Rotation.Forward();

        if (spread != Vector2::Zero) {
            direction += obj.Rotation.Right() * spread.x;
            direction += obj.Rotation.Up() * spread.y;
        }

        bullet.Movement = MovementType::Physics;
        bullet.Physics.Velocity = direction * weapon.Speed[Game::Difficulty]/* * 0.01f*/;
        if (weapon.Extended.InheritParentVelocity)
            bullet.Physics.Velocity += obj.Physics.Velocity;

        bullet.Physics.Flags |= weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
        bullet.Physics.AngularVelocity = weapon.Extended.RotationalVelocity;
        bullet.Physics.Flags |= PhysicsFlag::FixedAngVel; // HACK

        if (weapon.Extended.Sticky) bullet.Physics.Flags |= PhysicsFlag::Stick;
        bullet.Physics.Drag = weapon.Drag;
        bullet.Physics.Mass = weapon.Mass;
        bullet.Physics.Bounces = weapon.Extended.Bounces;
        if (bullet.Physics.Bounces > 0)
            ClearFlag(bullet.Physics.Flags, PhysicsFlag::Bounce); // remove the bounce flag as physics will stop when bounces = 0

        bullet.Control.Type = ControlType::Weapon;
        bullet.Control.Weapon = {};
        bullet.Control.Weapon.ParentType = obj.Type;
        bullet.LightColor = weapon.Extended.LightColor;
        bullet.LightRadius = weapon.Extended.LightRadius;

        if (weapon.RenderType == WeaponRenderType::Blob) {
            bullet.Render.Type = RenderType::Laser; // Blobs overload the laser render path
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.BlobBitmap);
        }
        else if (weapon.RenderType == WeaponRenderType::VClip) {
            bullet.Render.Type = RenderType::WeaponVClip;
            bullet.Render.VClip.ID = weapon.WeaponVClip;
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.WeaponVClip);
        }
        else if (weapon.RenderType == WeaponRenderType::Model) {
            bullet.Render.Type = RenderType::Model;

            auto& model = Resources::GetModel(weapon.Model);
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : model.Radius / weapon.ModelSizeRatio;
            if (bullet.Radius < 0) bullet.Radius = 1;

            if (!weapon.Extended.Model.empty()) {
                bullet.Render.Model.ID = Render::LoadOutrageModel(weapon.Extended.Model);
                bullet.Render.Model.Outrage = true;
                bullet.Scale = weapon.Extended.ModelScale;
            }
            else {
                bullet.Radius = model.Radius / weapon.ModelSizeRatio;
                bullet.Render.Model.ID = weapon.Model;
            }

            // Randomize the rotation of models
            auto rotation = Matrix::CreateFromAxisAngle(obj.Rotation.Forward(), Random() * DirectX::XM_2PI);
            bullet.Rotation *= rotation;
            bullet.LastRotation = bullet.Rotation;

            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
            Render::LoadModelDynamic(weapon.ModelInner);
        }
        else if (weapon.RenderType == WeaponRenderType::None) {
            bullet.Radius = 0.1f; // original game used a value of 1
        }

        bullet.Render.Rotation = Random() * DirectX::XM_2PI;

        bullet.Lifespan = weapon.Lifetime;
        //bullet.Lifespan = 3; // for testing fade-out
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = objId;
        bullet.Segment = obj.Segment;

        bullet.Render.Emissive = weapon.Extended.Glow;

        if (id == WeaponID::ProxMine || id == WeaponID::SmartMine) {
            bullet.NextThinkTime = (float)Game::Time + MINE_ARM_TIME;
        }

        if (showFlash) {
            Sound3D sound(objId);
            sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
            sound.Volume = 0.55f;
            sound.AttachToSource = true;
            sound.AttachOffset = gunOffset;
            sound.FromPlayer = true;
            sound.Source = objId;
            Sound::Play(sound);

            // Hide flashes in first person for gunpoints under the ship
            if (gun < 6) {
                Render::Particle p{};
                p.Clip = weapon.FlashVClip;
                p.Position = point;
                p.Radius = weapon.FlashSize;
                p.Parent = objId;
                p.ParentOffset = gunOffset;
                p.FadeTime = 0.175f;
                Render::AddParticle(p, obj.Segment);
            }
        }

        AddObject(bullet);
    }

    void SpreadfireBehavior(Inferno::Player& player, int gun, WeaponID wid) {
        constexpr float SPREAD_ANGLE = 1 / 16.0f;
        if (player.SpreadfireToggle) {
            // Vertical
            FireWeapon(player.ID, gun, wid);
            FireWeapon(player.ID, gun, wid, false, { 0, -SPREAD_ANGLE });
            FireWeapon(player.ID, gun, wid, false, { 0, SPREAD_ANGLE });
        }
        else {
            // Horizontal
            FireWeapon(player.ID, gun, wid);
            FireWeapon(player.ID, gun, wid, false, { -SPREAD_ANGLE, 0 });
            FireWeapon(player.ID, gun, wid, false, { SPREAD_ANGLE, 0 });
        }

        player.SpreadfireToggle = !player.SpreadfireToggle;
    }

    constexpr Vector2 GetHelixOffset(int index) {
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

    void HelixBehavior(Inferno::Player& player, int gun, WeaponID wid) {
        player.HelixOrientation = (player.HelixOrientation + 1) % 8;
        auto offset = GetHelixOffset(player.HelixOrientation);
        FireWeapon(player.ID, gun, wid);
        FireWeapon(player.ID, gun, wid, false, offset);
        FireWeapon(player.ID, gun, wid, false, offset * 2);
        FireWeapon(player.ID, gun, wid, false, -offset);
        FireWeapon(player.ID, gun, wid, false, -offset * 2);
    }

    void VulcanBehavior(const Inferno::Player& player, int gun, WeaponID wid) {
        constexpr float SPREAD_ANGLE = 1 / 32.0f; // -0.03125 to 0.03125 spread
        //Vector2 spread = { RandomN11() * SPREAD_ANGLE, RandomN11() * SPREAD_ANGLE };
        auto spread = RandomPointInCircle(SPREAD_ANGLE);
        FireWeapon(player.ID, gun, wid, true, { spread.x, spread.y });
    }

    // FOV in 0 to PI
    bool ObjectIsInFOV(const Ray& ray, const Object& other, float fov) {
        auto vec = other.Position - ray.position;
        vec.Normalize();
        auto angle = AngleBetweenVectors(ray.direction, vec);
        return angle <= fov;
    }

    // Used for omega and homing weapons
    ObjID GetClosestObjectInFOV(const Object& src, float fov, float dist, ObjectMask mask) {
        auto result = ObjID::None;
        float minDist = FLT_MAX;

        // todo: don't scan all objects, only nearby ones
        for (int i = 0; i < Game::Level.Objects.size(); i++) {
            auto& obj = Game::Level.Objects[i];
            if (!obj.IsAlive()) continue;
            if (!obj.PassesMask(mask)) continue;

            auto odist = obj.Distance(src);
            if (odist > dist || odist >= minDist) continue;

            auto vec = obj.Position - src.Position;
            vec.Normalize();
            Ray targetRay(src.Position, vec);
            LevelHit hit;

            if (ObjectIsInFOV(Ray(src.Position, src.Rotation.Forward()), obj, fov) &&
                !IntersectLevel(Game::Level, targetRay, src.Segment, odist, false, true, hit)) {
                minDist = odist;
                result = (ObjID)i;
            }
        }

        return result;
    }

    void OmegaBehavior(Inferno::Player& player, int gun, WeaponID wid) {
        constexpr auto FOV = 12.5f * DegToRad;
        constexpr auto MAX_DIST = 60;
        constexpr auto MAX_TARGETS = 3;
        constexpr auto MAX_CHAIN_DIST = 30;

        player.OmegaCharge -= OMEGA_CHARGE_COST;
        player.OmegaCharge = std::max(0.0f, player.OmegaCharge);

        const auto& weapon = Resources::GetWeapon(wid);

        auto pObj = Game::Level.TryGetObject(player.ID);
        if (!pObj) return;
        auto& playerObj = *pObj;
        auto gunOffset = GetGunpointOffset(playerObj, gun);
        auto start = Vector3::Transform(gunOffset, playerObj.GetTransform());
        auto initialTarget = GetClosestObjectInFOV(playerObj, FOV, MAX_DIST, ObjectMask::Enemy);

        // thicker beam used when hitting targets
        Render::BeamInfo beam{
            .Start = start,
            .StartObj = player.ID,
            .StartObjGunpoint = gun,
            .Width = 1.25f + 0.25f * Random(),
            .Life = weapon.FireDelay,
            .Color = { 3.00f + Random() * 0.5f, 1.0f, 3.0f + Random() * 0.5f },
            .Texture = "HellionBeam",
            .Frequency = 1 / 30.0f,
            .Amplitude = 1.00f,
        };

        // tracers are thinner 'feelers' for the main beam
        Render::BeamInfo tracer{
            .Start = start,
            .StartObj = player.ID,
            .StartObjGunpoint = gun,
            .Radius = MAX_CHAIN_DIST,
            .Width = 0.65f,
            .Life = weapon.FireDelay,
            .Color = { 1.30f, 1.0f, 1.45f },
            .Texture = "Lightning4",
            .Frequency = 1 / 30.0f,
            .Amplitude = 0.55f,
        };

        Render::SparkEmitter spark;
        spark.DurationRange = { 0.35f, 0.85f };
        spark.Restitution = 0.6f;
        spark.Velocity = { 30, 40 };
        spark.Count = { 13, 20 };
        spark.Color = Color{ 3, 3, 3 };
        spark.Texture = "Bright Blue Energy1";
        spark.Width = 0.15f;

        if (initialTarget != ObjID::None) {
            // found a target! try chaining to others
            std::array<ObjID, MAX_TARGETS> targets{};
            targets.fill(ObjID::None);
            targets[0] = initialTarget;

            for (int i = 0; i < MAX_TARGETS - 1; i++) {
                if (targets[i] == ObjID::None) break;

                if (auto src = Game::Level.TryGetObject(targets[i])) {
                    auto [id, dist] = Game::FindNearestVisibleObject(src->Position, src->Segment, MAX_CHAIN_DIST, ObjectMask::Enemy, targets);
                    if (id != ObjID::None)
                        targets[i + 1] = id;
                }
            }

            auto prevPosition = start;
            ObjID prevObj = player.ID;
            int objGunpoint = gun;

            // Apply damage and visuals to each target
            for (auto& targetObj : targets) {
                if (targetObj == ObjID::None) continue;
                auto target = Game::Level.TryGetObject(targetObj);
                if (!target) continue;
                target->HitPoints -= weapon.Damage[Difficulty];

                // Beams between previous and next target
                tracer.Start = beam.Start = prevPosition;
                tracer.End = beam.End = target->Position;
                tracer.StartObj = beam.StartObj = prevObj;
                tracer.EndObj = beam.EndObj = targetObj;
                tracer.StartObjGunpoint = beam.StartObjGunpoint = objGunpoint;
                prevObj = targetObj;
                objGunpoint = -1;

                prevPosition = beam.End;
                Render::AddBeam(beam);
                Render::AddBeam(tracer);
                Render::AddBeam(tracer);

                tracer.Start = target->Position;
                //beam.Width -= 0.33f;

                // Random endpoint tendrils
                tracer.RandomEnd = true;
                auto ampl = tracer.Amplitude;
                tracer.Amplitude = 1.25;
                tracer.StartObj = beam.StartObj = targetObj;
                tracer.EndObj = beam.EndObj = ObjID::None;
                tracer.FadeEnd = true;
                Render::AddBeam(tracer);
                Render::AddBeam(tracer);
                tracer.RandomEnd = false;
                tracer.Amplitude = ampl;
                tracer.FadeEnd = false;

                // Sparks and explosion
                spark.Position = target->Position;
                spark.Segment = target->Segment;
                Render::AddSparkEmitter(spark);

                Render::ExplosionInfo expl;
                //expl.Sound = weapon.RobotHitSound;
                expl.Segment = target->Segment;
                expl.Position = target->Position;
                expl.Clip = VClipID::SmallExplosion;
                expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
                expl.Variance = target->Radius * 0.45f;
                //expl.Color = Color{ 1.15f, 1.15f, 1.15f };
                expl.FadeTime = 0.1f;
                Render::CreateExplosion(expl);
            }

            // Hit sound
            constexpr std::array hitSounds = { "EnvElectricA", "EnvElectricB", "EnvElectricC", "EnvElectricD", "EnvElectricE", "EnvElectricF" };
            if (auto initialTargetObj = Game::Level.TryGetObject(initialTarget)) {
                Sound3D hitSound(initialTargetObj->Position, initialTargetObj->Segment);
                hitSound.Resource = { .D3 = hitSounds[RandomInt((int)hitSounds.size() - 1)] };
                hitSound.Volume = 2.00f;
                hitSound.Radius = 200;
                Sound::Play(hitSound);
            }
        }
        else {
            // no target: pick a random point within FOV
            auto offset = RandomPointInCircle(FOV * 0.75f);
            auto dir = playerObj.Rotation.Forward();
            dir += playerObj.Rotation.Right() * offset.x;
            dir += playerObj.Rotation.Up() * offset.y;
            dir.Normalize();

            LevelHit hit;
            if (IntersectLevel(Level, { playerObj.Position, dir }, playerObj.Segment, MAX_DIST, false, true, hit)) {
                tracer.End = beam.End = hit.Point;
                spark.Position = beam.End;
                spark.Segment = hit.Tag.Segment;
                Render::AddSparkEmitter(spark);

                // Do wall hit stuff
                Object dummy{};
                dummy.Position = hit.Point;
                dummy.Parent = player.ID;
                dummy.ID = (int)WeaponID::Omega;
                dummy.Type = ObjectType::Weapon;
                dummy.Control.Weapon.ParentType = ObjectType::Player;
                WeaponHitWall(hit, dummy, Level, ObjID::None);

                if (auto wall = Level.TryGetWall(hit.Tag))
                    HitWall(Level, hit.Point, dummy, *wall);
            }
            else {
                tracer.FadeEnd = true;
                // not sure why scaling the length down is necessary, but it feels more accurate
                tracer.End = beam.End = start + dir * MAX_DIST * 0.3f;
            }

            Render::AddBeam(tracer);
        }

        // Fire sound
        Sound3D sound(player.ID);
        sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
        sound.Volume = 0.40f;
        sound.AttachToSource = true;
        sound.AttachOffset = gunOffset;
        sound.FromPlayer = true;
        Sound::Play(sound);
    }

    // default weapon firing behavior
    void DefaultBehavior(const Inferno::Player& player, int gun, WeaponID wid) {
        FireWeapon(player.ID, gun, wid);
    }

    Dictionary<string, WeaponBehavior> WeaponFireBehaviors = {
        { "default", DefaultBehavior },
        { "vulcan", VulcanBehavior },
        { "helix", HelixBehavior },
        { "spreadfire", SpreadfireBehavior },
        { "omega", OmegaBehavior }
    };

    WeaponBehavior& GetWeaponBehavior(const string& name) {
        for (auto& [key, value] : WeaponFireBehaviors) {
            if (name == key) return value;
        }

        return WeaponFireBehaviors["default"];
    }

    void UpdateWeapon(Object& obj, float dt) {
        obj.Control.Weapon.AliveTime += dt;
        if (obj.ID == (int)WeaponID::ProxMine)
            ProxMineBehavior(obj);
    }
}
