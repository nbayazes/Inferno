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
            CreateExplosion(Game::Level, &bomb, ge);
        }

        bomb.HitPoints = 0;
    }

    void ProxMineBehavior(Object& obj) {
        constexpr auto PROX_WAKE_RANGE = 60;
        constexpr auto PROX_ACTIVATE_RANGE = 30;

        auto& cw = obj.Control.Weapon;

        if (TimeHasElapsed(obj.NextThinkTime)) {
            obj.NextThinkTime = (float)Game::Time + 0.25f;

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
                cw.SoundDelay = (float)Game::Time + lerp;
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

    // Returns a random point inside of a circle
    Vector2 RandomPointInCircle(float radius) {
        auto t = Random() * DirectX::XM_2PI;
        auto x = std::cos(t) * radius * RandomN11();
        auto y = std::sin(t) * radius * RandomN11();
        return { x, y };
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
        bullet.Physics.Velocity = direction * weapon.Speed[Game::Difficulty];
        //bullet.Physics.Velocity = direction * 10;
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
            
            // Randomize the rotation of models
            auto rotation = Matrix::CreateFromAxisAngle(obj.Rotation.Forward(), Random() * DirectX::XM_2PI);
            bullet.Rotation *= rotation;
            bullet.LastRotation = bullet.Rotation;

            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
            Render::LoadModelDynamic(weapon.ModelInner);
        }

        bullet.Render.Rotation = Random() * DirectX::XM_2PI;

        bullet.Lifespan = weapon.Lifetime;
        //bullet.Lifespan = 3; // for testing fade-out
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = objId;
        bullet.Segment = obj.Segment;

        if (id == WeaponID::Laser5)
            bullet.Render.Emissive = { 0.8f, 0.4f, 0.1f };
        else
            bullet.Render.Emissive = { 0.1f, 0.1f, 0.1f };

        if (id == WeaponID::ProxMine || id == WeaponID::SmartMine) {
            constexpr float MINE_ARM_TIME = 2.0f;
            bullet.NextThinkTime = (float)Game::Time + MINE_ARM_TIME;
        }

        if (showFlash) {
            Sound3D sound(objId);
            sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
            sound.Volume = 0.55f;
            sound.AttachToSource = true;
            sound.AttachOffset = gunOffset;
            sound.FromPlayer = true;
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
                Render::AddParticle(p);
            }
        }

        AddObject(bullet);
    }

    void SpreadfireBehavior(Inferno::Player& player, int gun, WeaponID wid) {
        constexpr float SPREAD_ANGLE = 1 / 16.0f;
        if (player.SpreadfireToggle) { // Vertical
            FireWeapon(player.ID, gun, wid);
            FireWeapon(player.ID, gun, wid, false, { 0, -SPREAD_ANGLE });
            FireWeapon(player.ID, gun, wid, false, { 0, SPREAD_ANGLE });
        }
        else { // Horizontal
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
        Vector2 spread = RandomPointInCircle(SPREAD_ANGLE);
        FireWeapon(player.ID, gun, wid, true, spread);
    }

    // FOV in 0 to PI
    bool ObjectIsInFOV(const Ray& ray, const Object& other, float fov) {
        auto vec = other.Position - ray.position;
        vec.Normalize();
        auto angle = AngleBetweenVectors(ray.direction, vec);
        return angle <= fov;
    }

    // Used for omega and homing weapons
    Object* GetClosestObjectInFOV(Object& src, float fov, float dist, int mask) {
        Object* result = nullptr;
        float minDist = FLT_MAX;

        // todo: don't scan all objects, only nearby ones
        for (auto& obj : Game::Level.Objects) {
            // todo: filter object types based on mask
            auto odist = obj.Distance(src);
            if (odist > dist || odist >= minDist) continue;

            if (ObjectIsInFOV(Ray(src.Position, src.Rotation.Forward()), obj, fov)) {
                minDist = odist;
                result = &obj;
            }
        }

        return result;
    }

    // Returns the object closest object within a distance to a point
    Object* GetClosestObject(const Vector3& pos, float dist) {
        Object* result = nullptr;
        float minDist = FLT_MAX;

        for (auto& obj : Game::Level.Objects) {
            auto d = Vector3::Distance(obj.Position, pos);
            if (d <= dist && d < minDist) {
                minDist = d;
                result = &obj;
            }
        }

        return result;
    }

    void OmegaBehavior(const Inferno::Player& player, int gun, WeaponID wid) {
        constexpr auto FOV = 12.5f * DegToRad;
        constexpr auto MAX_DIST = 100;
        constexpr auto MAX_TARGETS = 3;
        constexpr auto MAX_CHAIN_DIST = 50;
        constexpr auto NO_TARGET_RADIUS = 0.25f;

        Object* targets[MAX_TARGETS]{};
        const auto& weapon = Resources::GetWeapon(wid);

        auto& playerObj = Game::Level.GetObject(player.ID);
        auto gunOffset = GetGunpointOffset(playerObj, gun);
        auto start = Vector3::Transform(gunOffset, playerObj.GetTransform());

        auto initialTarget = GetClosestObjectInFOV(playerObj, FOV, MAX_DIST, 0);
        if (initialTarget) {
            targets[0] = initialTarget;

            for (int i = 0; i < MAX_TARGETS - 1; i++) {
                auto src = targets[i];
                if (!src) break;

                if (auto next = GetClosestObject(src->Position, MAX_CHAIN_DIST)) {
                    targets[i + 1] = next;
                }
            }

            // Apply damage to each target
            for (auto& target : targets) {
                if (target)
                    target->HitPoints -= weapon.Damage[Difficulty];
            }
        }
        else {
            // no target: pick a random point within FOV
            auto offset = RandomPointInCircle(NO_TARGET_RADIUS);
            auto dir = playerObj.Rotation.Forward();
            dir += playerObj.Rotation.Right() * offset.x;
            dir += playerObj.Rotation.Up() * offset.y;
            dir.Normalize();

            Vector3 end;
            LevelHit hit;
            if (IntersectLevel(Level, { playerObj.Position, dir }, playerObj.Segment, MAX_DIST, true, hit)) {
                end = hit.Point;
                // create explosion / sound
            }
            else {
                end = start + dir * MAX_DIST;
            }

            Render::BeamInfo beam{
                .Start = start,
                .End = end,
                //.Radius = 20,
                .Width = 0.35f,
                .Life = 1.5f,
                .Color = { 3.00f, 1.0f, 2.0f },
                .Texture = "Lightning1",
                //.Scale = 0.25f,
                //.SineNoise = true,
                .Frequency = 1000,
                .Amplitude = 2.25f,
            };
            Render::AddBeam(beam);
        }

        // Create vfx between each object, along with random arcs at each


        // Find a target within a certain range and FOV, otherwise project straight ahead randomly

        // If found a valid target, chain to up to x more within range (or randomly arc)
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

    WeaponBehavior& GetWeaponBehavior(string name) {
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