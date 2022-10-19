#include "pch.h"
#include "Object.h"
#include "Game.h"
#include "Physics.h"

namespace Inferno::Game {
    Vector3 GetGunpointOffset(const Object& obj, int gun) {
        //Vector3 offset = Vector3::Zero;
        gun = std::clamp(gun, 0, 8);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            return robot.GunPoints[gun] * Vector3(1, 1, -1);
        }
        else if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            return Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);;
            //offset = Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);
        }
        else if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return Vector3::Zero;
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return reactor.GunPoints[gun];
            //if (!Seq::inRange(reactor.GunPoints, gun));
        }

        return Vector3::Zero;
    }

    void ExplodeBomb(const Weapon& weapon, Object& bomb) {
        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;
        float damage = weapon.Damage[Game::Difficulty];

        Render::ExplosionInfo e;
        e.MinRadius = weapon.ImpactSize * 0.9f;
        e.MaxRadius = weapon.ImpactSize * 1.1f;
        e.Clip = vclip;
        e.Sound = soundId;
        e.Position = bomb.Position; // move explosion out of wall
        e.Color = { 1, 1, 1 };
        e.FadeTime = 0.1f;
        Render::CreateExplosion(e);

        if (weapon.SplashRadius > 0) {
            GameExplosion ge{};
            ge.Damage = damage;
            ge.Force = damage; // force = damage, really?
            ge.Radius = weapon.SplashRadius;
            ge.Segment = bomb.Segment;
            ge.Position = bomb.Position;
            CreateExplosion(Game::Level, bomb, ge);
        }

        bomb.HitPoints = 0;
    }

    void ProxMineBehavior(float dt, Object& obj) {
        constexpr auto PROX_WAKE_RANGE = 60;
        constexpr auto PROX_ACTIVATE_RANGE = 30;

        auto& cw = obj.Control.Weapon;

        if (TimeHasElapsed(obj.NextThinkTime)) {
            obj.NextThinkTime = Game::Time + 0.25f;

            // Try to find a nearby target
            if (cw.TrackingTarget == ObjID::None) {
                auto [id, dist] = Game::FindNearestObject(obj);
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
            auto lerp = std::lerp(1.00f, 2.00f, (dist - PROX_ACTIVATE_RANGE) / (PROX_WAKE_RANGE - PROX_ACTIVATE_RANGE));
            //lerp = std::clamp(lerp, 1.0f, 2.0f);

            if (TimeHasElapsed(cw.SoundDelay)) {
                Sound3D sound(obj.Position, obj.Segment);
                //sound.Pitch = 0.25f - (lerp - 1.25);
                sound.Resource = Resources::GetSoundResource(SoundID::HomingWarning);
                Sound::Play(sound);
                cw.SoundDelay = Game::Time + lerp;
            }

            if (dist <= PROX_ACTIVATE_RANGE && !cw.DetonateMine) {
                // Commit to the target
                cw.DetonateMine = true;
                obj.Lifespan = 2;
                obj.Physics.ClearFlag(PhysicsFlag::Wiggle); // explode on contacting walls

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

        if (obj.ID != (int)WeaponID::Smart) return; // todo: don't hard code this

        List<ObjID> targets;
        targets.reserve(30);

        for (int i = 0; i < Game::Level.Objects.size(); i++) {
            auto& other = Game::Level.Objects[i];
            auto validTarget = (other.Type == ObjectType::Robot && !other.Control.AI.IsCloaked()) || other.Type == ObjectType::Player;

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

        const int children = 6; // todo: add to extended weapon data
        auto wid = parentType == ObjectType::Player ? WeaponID::PlayerSmartBlob : WeaponID::RobotSmartBlob;

        if (targets.empty()) {
            for (int i = 0; i < children; i++) {
                //CreateHomingWeapon(-1); // random target
            }
        }
        else {
            for (int i = 0; i < children; i++) {
                //CreateHomingWeapon(); // use random target in list
            }
        }
    }


    ObjID FireWeapon(ObjID objId, int gun, WeaponID id, bool showFlash, const Vector2& spread) {
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
        bullet.Physics.Velocity = direction * weapon.Speed[Game::Difficulty];
        bullet.Physics.Flags = weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
        bullet.Physics.Drag = weapon.Drag;
        bullet.Physics.Mass = weapon.Mass;

        bullet.Control.Type = ControlType::Weapon;
        bullet.Control.Weapon = {};

        if (weapon.RenderType == WeaponRenderType::Blob) {
            bullet.Render.Type = RenderType::Laser; // Blobs overload the laser render path
            bullet.Radius = weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.BlobBitmap);
        }
        else if (weapon.RenderType == WeaponRenderType::VClip) {
            bullet.Render.Type = RenderType::WeaponVClip;
            bullet.Render.VClip.ID = weapon.WeaponVClip;
            bullet.Radius = weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.WeaponVClip);
        }
        else if (weapon.RenderType == WeaponRenderType::Model) {
            bullet.Render.Type = RenderType::Model;
            bullet.Render.Model.ID = weapon.Model;
            auto& model = Resources::GetModel(weapon.Model);
            bullet.Radius = model.Radius / weapon.ModelSizeRatio;
            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
            Render::LoadModelDynamic(weapon.ModelInner);
        }

        bullet.Render.Rotation = Random() * DirectX::XM_2PI;

        bullet.Lifespan = weapon.Lifetime;
        //bullet.Lifespan = 3; // for testing fade-out
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = ObjID(0);

        if (id == WeaponID::Laser5)
            bullet.Render.Emissive = { 0.8f, 0.4f, 0.1f };
        else
            bullet.Render.Emissive = { 0.1f, 0.1f, 0.1f };

        if (id == WeaponID::ProxMine || id == WeaponID::SmartMine) {
            constexpr float MINE_ARM_TIME = 2.0f;
            bullet.NextThinkTime = Game::Time + MINE_ARM_TIME;
        }

        if (showFlash) {
            Sound3D sound(ObjID(0));
            sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
            sound.Volume = 0.55f;
            sound.AttachToSource = true;
            sound.AttachOffset = gunOffset;
            sound.FromPlayer = true;
            Sound::Play(sound);

            Render::Particle p{};
            p.Clip = weapon.FlashVClip;
            p.Position = point;
            p.Radius = weapon.FlashSize;
            p.Parent = ObjID(0);
            p.ParentOffset = gunOffset;
            p.FadeTime = 0.175f;
            Render::AddParticle(p);
        }

        for (int i = 0; i < level.Objects.size(); i++) {
            auto& o = level.Objects[i];
            if (o.Lifespan <= 0) {
                o = bullet;
                return (ObjID)i; // found a dead object to reuse!
            }
        }

        level.Objects.emplace_back(bullet); // insert a new object
        return (ObjID)(level.Objects.size() - 1);
    }

    void UpdateWeapon(Object& obj, float dt) {
        obj.Control.Weapon.AliveTime += dt;
        if (obj.ID == (int)WeaponID::ProxMine)
            ProxMineBehavior(dt, obj);
    }
}