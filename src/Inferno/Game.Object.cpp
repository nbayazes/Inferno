#include <pch.h>

#include "Level.h"
#include "Game.Object.h"
#include "Game.AI.h"
#include "Game.AI.Pathing.h"
#include "Game.h"
#include "Game.Segment.h"
#include "Game.Wall.h"
#include "Physics.h"
#include "SoundSystem.h"
#include "Editor/Editor.Object.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"

namespace Inferno {
    namespace {
        uint16 ObjSigIndex = 1;
        // Objects to be added at the end of this tick.
        // Exists to prevent modifying object list size mid-update.
        List<Object> PendingNewObjects;
    }

    uint8 GetGunSubmodel(const Object& obj, uint8 gun) {
        gun = std::clamp(gun, (uint8)0, MAX_GUNS);

        if (obj.Type == ObjectType::Robot) {
            return Resources::GetRobotInfo(obj.ID).GunSubmodels[gun];
        }

        if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            return 0;
        }

        if (obj.Type == ObjectType::Reactor) {
            return 0;
        }

        return 0;
    }

    Tuple<Vector3, Vector3> GetSubmodelOffsetAndRotation(const Object& object, const Model& model, int submodel) {
        if (!Seq::inRange(model.Submodels, submodel)) return { Vector3::Zero, Vector3::Zero };

        // accumulate the offsets for each submodel
        auto submodelOffset = Vector3::Zero;
        Vector3 submodelAngle = object.Render.Model.Angles[submodel];
        auto* smc = &model.Submodels[submodel];
        while (smc->Parent != ROOT_SUBMODEL) {
            auto& parentAngle = object.Render.Model.Angles[smc->Parent];
            auto parentRotation = Matrix::CreateFromYawPitchRoll(parentAngle);
            submodelOffset += Vector3::Transform(smc->Offset, parentRotation);
            submodelAngle += object.Render.Model.Angles[smc->Parent];
            smc = &model.Submodels[smc->Parent];
        }

        return { submodelOffset, submodelAngle };
    }

    Matrix GetSubmodelTransform(const Object& object, const Model& model, int submodel) {
        if (!Seq::inRange(model.Submodels, submodel)) return Matrix::Identity;

        // accumulate the offsets for each submodel
        auto submodelOffset = Vector3::Zero;
        Vector3 submodelAngle = object.Render.Model.Angles[submodel];
        auto* smc = &model.Submodels[submodel];
        while (smc->Parent != ROOT_SUBMODEL) {
            auto& parentAngle = object.Render.Model.Angles[smc->Parent];
            auto parentRotation = Matrix::CreateFromYawPitchRoll(parentAngle);
            submodelOffset += Vector3::Transform(smc->Offset, parentRotation);
            submodelAngle += object.Render.Model.Angles[smc->Parent];
            smc = &model.Submodels[smc->Parent];
        }

        auto transform = Matrix::CreateFromYawPitchRoll(submodelAngle);
        transform.Translation(submodelOffset);
        return transform;
    }

    Matrix GetSubmodelTranslation(const Model& model, int submodel) {
        if (!Seq::inRange(model.Submodels, submodel)) return Matrix::Identity;

        // accumulate the offsets for each submodel
        auto submodelOffset = Vector3::Zero;
        auto* smc = &model.Submodels[submodel];
        while (smc->Parent != ROOT_SUBMODEL) {
            submodelOffset += smc->Offset;
            smc = &model.Submodels[smc->Parent];
        }

        //auto transform = Matrix::CreateFromYawPitchRoll(submodelAngle);
        Matrix transform;
        transform.Translation(submodelOffset);
        return transform;
    }

    Vector3 GetSubmodelOffset(const Object& obj, SubmodelRef submodel) {
        auto& model = Resources::GetModel(obj.Render.Model.ID);

        if (submodel.ID < 0 || submodel.ID >= model.Submodels.size())
            return Vector3::Zero; // Unset

        auto sm = submodel.ID;
        while (sm != ROOT_SUBMODEL) {
            auto rotation = Matrix::CreateFromYawPitchRoll(obj.Render.Model.Angles[sm]);
            submodel.Offset = Vector3::Transform(submodel.Offset, rotation) + model.Submodels[sm].Offset;
            sm = model.Submodels[sm].Parent;
        }

        return submodel.Offset;
    }

    SubmodelRef GetGunpointSubmodelOffset(const Object& obj, uint8 gun) {
        gun = std::clamp(gun, (uint8)0, MAX_GUNS);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto gunpoint = robot.GunPoints[gun];

#ifdef _DEBUG
            auto& model = Resources::GetModel(obj);
            if (robot.GunSubmodels[gun] >= model.Submodels.size())
                __debugbreak(); // gunpoint submodel out of range
#endif
            return { robot.GunSubmodels[gun], gunpoint };
        }

        if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            auto& gunpoint = Resources::GameData.PlayerShip.GunPoints[gun];
            return { 0, gunpoint };
        }

        if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return { 0, Vector3::Zero };
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return { 0, reactor.GunPoints[gun] };
        }

        return { 0, Vector3::Zero };
    }

    Vector3 GetGunpointOffset(const Object& obj, uint8 gun) {
        gun = std::clamp(gun, (uint8)0, MAX_GUNS);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto& model = Resources::GetModel(robot.Model);
            auto gunpoint = robot.GunPoints[gun];
            auto submodel = robot.GunSubmodels[gun];

            while (submodel != ROOT_SUBMODEL) {
                auto rotation = Matrix::CreateFromYawPitchRoll(obj.Render.Model.Angles[submodel]);
                gunpoint = Vector3::Transform(gunpoint, rotation) + model.Submodels[submodel].Offset;
                submodel = model.Submodels[submodel].Parent;
            }

            return gunpoint;
        }

        if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            return Resources::GameData.PlayerShip.GunPoints[gun];
        }

        if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return Vector3::Zero;
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return reactor.GunPoints[gun];
        }

        return Vector3::Zero;
    }

    Vector3 GetGunpointWorldPosition(const Object& obj, uint8 gun) {
        auto gunSubmodel = GetGunpointSubmodelOffset(obj, gun);
        auto objOffset = GetSubmodelOffset(obj, gunSubmodel);
        return Vector3::Transform(objOffset, obj.GetTransform());
    }

    bool UpdateObjectSegment(Level& level, Object& obj) {
        if (PointInSegment(level, obj.Segment, obj.Position))
            return false; // Already in the right segment

        auto id = FindContainingSegment(level, obj.Position);
        // Leave the last good ID if nothing contains the object
        if (id != SegID::None) obj.Segment = id;

        auto& seg = level.GetSegment(obj.Segment);
        auto transitionTime = Game::GetState() == GameState::Game ? 0.5f : 0;
        obj.Ambient.SetTarget(seg.VolumeLight, Game::Time, transitionTime);
        return true;
    }

    void RelinkObject(Level& level, Object& obj, SegID newSegment) {
        auto id = Game::GetObjectRef(obj).Id;
        auto& prevSeg = level.GetSegment(obj.Segment);
        prevSeg.RemoveObject(id);
        auto& seg = level.GetSegment(newSegment);
        seg.AddObject(id);
        obj.Ambient.SetTarget(seg.VolumeLight, Game::Time, 0.25f);
        obj.Segment = newSegment;
    }

    void MoveObject(Level& level, ObjID objId) {
        auto pObj = level.TryGetObject(objId);
        if (!pObj) return;
        auto& obj = *pObj;
        auto prevSegId = obj.Segment;

        if (!UpdateObjectSegment(level, obj))
            return; // already in the right segment

        if (obj.Segment == SegID::None)
            return; // Object was outside of world

        Tag connection{};

        // Check if the new position is in a touching segment, because fast moving objects can cross
        // multiple segments in one update. This affects gauss the most.
        auto& prevSeg = level.GetSegment(prevSegId);
        for (auto& side : SideIDs) {
            auto cid = prevSeg.GetConnection(side);
            if (PointInSegment(level, cid, obj.Position)) {
                connection = { prevSegId, side };
                break;
            }
        }

        if (connection && obj.IsPlayer()) {
            // Activate triggers
            if (auto trigger = level.TryGetTrigger(connection)) {
                fmt::print("Activating fly through trigger {}:{}\n", connection.Segment, connection.Side);
                ActivateTrigger(level, *trigger, connection);
            }
        }
        else if (!connection) {
            // object crossed multiple segments in a single update.
            // usually caused by fast moving projectiles, but can also happen if object is outside world.
            // Rarely occurs when flying across the corner of four segments
            if (obj.Type == ObjectType::Player && prevSegId != obj.Segment)
                SPDLOG_WARN("Player {} warped from segment {} to {}. Any fly-through triggers did not activate!", objId, prevSegId, obj.Segment);
        }

        // Update object pointers
        prevSeg.RemoveObject(objId);
        auto& seg = level.GetSegment(obj.Segment);
        seg.AddObject(objId);
        obj.Ambient.SetTarget(seg.VolumeLight, Game::Time, 0.25f);
    }

    const std::set BOSS_IDS = { 17, 23, 31, 45, 46, 52, 62, 64, 75, 76 };

    bool IsBossRobot(const Object& obj) {
        return obj.Type == ObjectType::Robot && BOSS_IDS.contains(obj.ID);
    }

    void CreateRobot(SegID segment, const Vector3& position, int8 type, MatcenID srcMatcen) {
        Object obj{};
        Editor::InitObject(Game::Level, obj, ObjectType::Robot, type);
        obj.Position = position ;
        obj.Segment = segment;
        obj.SourceMatcen = srcMatcen;
        Game::AddObject(obj);
    }

    void ExplodeObject(Object& obj, float delay) {
        if (HasFlag(obj.Flags, ObjectFlag::Exploding)) return;

        obj.Lifespan = delay;
        SetFlag(obj.Flags, ObjectFlag::Exploding);
    }

    Object& AllocObject(Inferno::Level& level) {
        for (auto& obj : level.Objects) {
            if (!obj.IsAlive()) {
                obj = {};
                return obj;
            }
        }

        return level.Objects.emplace_back(Object{});
    }

    void SpawnContained(Level& level, const ContainsData& contains, const Vector3& position, SegID segment, const Vector3& force) {
        switch (contains.Type) {
            case ObjectType::Powerup:
            {
                auto pid = (PowerupID)contains.ID;
                // Replace Vulcan/Gauss with ammo drops if player already has it
                if ((pid == PowerupID::Vulcan && Game::Player.HasWeapon(PrimaryWeaponIndex::Vulcan)) ||
                    (pid == PowerupID::Gauss && Game::Player.HasWeapon(PrimaryWeaponIndex::Gauss)))
                    pid = PowerupID::VulcanAmmo;
                else if (
                    (pid == PowerupID::Laser && Game::Player.HasWeapon(PrimaryWeaponIndex::Laser)) ||
                    (pid == PowerupID::Spreadfire && Game::Player.HasWeapon(PrimaryWeaponIndex::Spreadfire)) ||
                    (pid == PowerupID::Helix && Game::Player.HasWeapon(PrimaryWeaponIndex::Helix)) ||
                    (pid == PowerupID::Plasma && Game::Player.HasWeapon(PrimaryWeaponIndex::Plasma)) ||
                    (pid == PowerupID::Phoenix && Game::Player.HasWeapon(PrimaryWeaponIndex::Phoenix)) ||
                    (pid == PowerupID::Fusion && Game::Player.HasWeapon(PrimaryWeaponIndex::Fusion)) ||
                    (pid == PowerupID::Omega && Game::Player.HasWeapon(PrimaryWeaponIndex::Omega)))
                    pid = PowerupID::Energy;

                auto& pinfo = Resources::GetPowerup(pid);
                if (pinfo.VClip == VClipID::None) {
                    SPDLOG_WARN("Tried to drop an invalid powerup!");
                    return;
                }

                for (int i = 0; i < contains.Count; i++) {
                    Object powerup{};
                    Editor::InitObject(level, powerup, ObjectType::Powerup, (int)pid);
                    powerup.Position = position;
                    powerup.Segment = segment;

                    powerup.Movement = MovementType::Physics;
                    powerup.Physics.Velocity = RandomVector(32) + force;
                    powerup.Physics.Mass = 1;
                    powerup.Physics.Drag = 0.01f;
                    powerup.Physics.Flags = PhysicsFlag::Bounce;

                    Render::LoadTextureDynamic(pinfo.VClip);
                    Game::AddObject(powerup);
                }
                break;
            }
            case ObjectType::Robot:
            {
                for (int i = 0; i < contains.Count; i++) {
                    Object spawn{};
                    Editor::InitObject(level, spawn, ObjectType::Robot, contains.ID);
                    spawn.Position = position;
                    spawn.Segment = segment;
                    spawn.Type = ObjectType::Robot;
                    spawn.Physics.Velocity = RandomVector(32) + force;
                    spawn.NextThinkTime = 0;
                    Game::AddObject(spawn);
                }

                break;
            }
        }
    }

    void DropContents(const Object& obj) {
        assert(obj.Type == ObjectType::Robot);

        if (obj.Contains.Type != ObjectType::None) {
            // Editor specified contents override robot contents
            SpawnContained(Game::Level, obj.Contains, obj.Position, obj.Segment, obj.LastHitForce);
        }
        else {
            // Robot defined contents
            auto& ri = Resources::GetRobotInfo(obj.ID);
            if (ri.Contains.Count > 0) {
                if (Random() < (float)ri.ContainsChance / 16.0f) {
                    auto contains = ri.Contains;
                    contains.Count = ri.Contains.Count == 1 ? 1 : (uint8)RandomInt(1, ri.Contains.Count);
                    SpawnContained(Game::Level, contains, obj.Position, obj.Segment, obj.LastHitForce);
                }
            }
        }
    }

    void DestroyObject(Object& obj) {
        obj.Flags |= ObjectFlag::Destroyed;

        switch (obj.Type) {
            case ObjectType::Reactor:
            {
                //DestroyReactor(obj);
                break;
            }

            case ObjectType::Robot:
            {
                constexpr float EXPLOSION_DELAY = 0.2f;

                auto& robot = Resources::GetRobotInfo(obj.ID);

                Render::ExplosionInfo expl;
                expl.Sound = robot.ExplosionSound2;
                expl.Clip = robot.ExplosionClip2;
                expl.Radius = { obj.Radius * 1.75f, obj.Radius * 1.9f };
                Render::CreateExplosion(expl, obj.Segment, obj.GetPosition(Game::LerpAmount));

                expl.Sound = SoundID::None;
                expl.InitialDelay = EXPLOSION_DELAY;
                expl.Radius = { obj.Radius * 1.15f, obj.Radius * 1.55f };
                expl.Variance = obj.Radius * 0.5f;
                Render::CreateExplosion(expl, obj.Segment, obj.GetPosition(Game::LerpAmount));

                if (robot.ExplosionStrength > 0) {
                    GameExplosion ge{};
                    ge.Damage = robot.ExplosionStrength;
                    ge.Radius = robot.ExplosionStrength * 4.0f;
                    ge.Force = robot.ExplosionStrength * 35.0f;
                    ge.Segment = obj.Segment;
                    ge.Position = obj.Position;
                    ge.Room = Game::Level.GetRoomID(obj);
                    CreateExplosion(Game::Level, &obj, ge);
                }

                // Don't give score from robots created by bosses to prevent score farming
                if (obj.SourceMatcen != MatcenID::Boss)
                    Game::AddPointsToScore(robot.Score);

                auto& model = Resources::GetModel(robot.Model);
                for (int sm = 0; sm < model.Submodels.size(); sm++) {
                    Matrix transform = Matrix::Lerp(obj.GetPrevTransform(), obj.GetTransform(), Game::LerpAmount);
                    auto world = GetSubmodelTransform(obj, model, sm) * transform;

                    auto explosionDir = world.Translation() - obj.Position; // explode outwards
                    explosionDir.Normalize();
                    auto hitForce = obj.LastHitForce * (1.0f + Random() * 0.5f);

                    Render::Debris debris;
                    //Vector3 vec(Random() + 0.5, Random() + 0.5, Random() + 0.5);
                    //auto vec = RandomVector(obj.Radius * 5);
                    //debris.Velocity = vec + obj.LastHitVelocity / (4 + obj.Movement.Physics.Mass);
                    //debris.Velocity =  RandomVector(obj.Radius * 5);
                    debris.Velocity = sm == 0 ? hitForce : explosionDir * 20 + RandomVector(5) + hitForce;
                    debris.Velocity += obj.Physics.Velocity;
                    debris.AngularVelocity.x = RandomN11();
                    debris.AngularVelocity.y = RandomN11();
                    debris.AngularVelocity.z = RandomN11();
                    //debris.AngularVelocity = RandomVector(std::min(obj.LastHitForce.Length(), 3.14f));
                    debris.Transform = world;
                    //debris.Transform.Translation(debris.Transform.Translation() + RandomVector(obj.Radius / 2));
                    debris.PrevTransform = world;
                    debris.Mass = 1; // obj.Movement.Physics.Mass;
                    debris.Drag = 0.0075f; // obj.Movement.Physics.Drag;
                    // It looks weird if the main body (sm 0) sticks around, so destroy it quick
                    debris.Duration = sm == 0 ? 0 : 0.75f + Random() * 1.5f;
                    debris.Radius = model.Submodels[sm].Radius;
                    //debris.Model = (ModelID)Resources::GameData.DeadModels[(int)robot.Model];
                    debris.Model = robot.Model;
                    debris.Submodel = sm;
                    debris.TexOverride = Resources::LookupTexID(obj.Render.Model.TextureOverride);
                    AddDebris(debris, obj.Segment);
                }

                // todo: spawn particles

                DropContents(obj);
                obj.Flags |= ObjectFlag::Dead;
                break;
            }

            case ObjectType::Player:
            {
                // Player_ship->expl_vclip_num
                break;
            }

            case ObjectType::Weapon:
            {
                // weapons are destroyed in WeaponHitWall, WeaponHitObject and ExplodeWeapon
                break;
            }
        }
    }

    Tuple<ObjRef, float> Game::FindNearestObject(const Vector3& position, float maxDist, ObjectMask mask) {
        ObjRef ref;
        //auto& srcObj = Level.GetObject(src);
        float dist = FLT_MAX;

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            if (!obj.PassesMask(mask) || !obj.IsAlive()) continue;
            auto d = Vector3::Distance(obj.Position, position);
            if (d <= maxDist && d < dist) {
                ref = { (ObjID)i, obj.Signature };
                dist = d;
            }
        }

        return { ref, dist };
    }

    Tuple<ObjRef, float> Game::FindNearestVisibleObject(const Vector3& position, SegID seg, float maxDist, ObjectMask mask, span<ObjRef> objFilter) {
        ObjRef id;
        float minDist = FLT_MAX;

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            if (!obj.PassesMask(mask) || !obj.IsAlive()) continue;
            ObjRef ref = { (ObjID)i, obj.Signature };
            if (Seq::contains(objFilter, ref)) continue;
            auto dir = obj.Position - position;
            auto d = dir.Length();
            dir.Normalize();
            Ray ray(position, dir);
            LevelHit hit;
            RayQuery query{ .MaxDistance = d, .Start = seg, .TestTextures = true };
            if (d <= maxDist && d < minDist && !Game::Intersect.RayLevel(ray, query, hit)) {
                id = ref;
                minDist = d;
            }
        }

        return { id, minDist };
    }

    // Attaches a light to an object based on its settings
    void Game::AttachLight(const Object& obj, ObjRef ref) {
        if (!obj.IsAlive()) return;
        Render::DynamicLight light;

        switch (obj.Type) {
            case ObjectType::None:
                break;
            case ObjectType::Fireball:
                break;
            case ObjectType::Robot:
                break;
            case ObjectType::Hostage:
                break;
            case ObjectType::Player:
                break;
            case ObjectType::Weapon:
            {
                auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
                light.LightColor = weapon.Extended.LightColor;
                light.Radius = weapon.Extended.LightRadius;
                light.Mode = weapon.Extended.LightMode;
                light.FadeTime = weapon.Extended.LightFadeTime;
                break;
            }
            case ObjectType::Powerup:
            {
                auto& info = Resources::GetPowerup((PowerupID)obj.ID);
                light.LightColor = info.LightColor;
                light.Radius = info.LightRadius;
                light.Mode = info.LightMode;
                break;
            }
            case ObjectType::Debris:
                break;
            case ObjectType::Reactor:
            {
                //obj.Light.Color = Color(1, 0, 0);
                //obj.Light.Radius = 50;
                //obj.Light.Mode = DynamicLightMode::BigPulse;
                light.LightColor = Color(3, 0, 0);
                light.Radius = 30;
                light.Mode = DynamicLightMode::BigPulse;
                break;
            }
            case ObjectType::Clutter:
                break;
            case ObjectType::Light:
                light.LightColor = obj.Light.Color;
                light.Radius = obj.Light.Radius;
                light.Mode = obj.Light.Mode;
                if (obj.SourceMatcen != MatcenID::None) {
                    light.FadeOnParentDeath = true;
                    light.FadeTime = 0.5f;
                }
                break;
            case ObjectType::Coop:
                break;
            case ObjectType::Marker:
                break;
            default: break;
        }

        if (light.Radius > 0 && light.LightColor != Color()) {
            light.Parent = ref;
            light.Duration = MAX_OBJECT_LIFE; // lights will be removed when their parent is destroyed
            light.Segment = obj.Segment;
            Render::AddDynamicLight(light);
        }
    }

    ObjSig GetObjectSig() {
        ObjSigIndex++;
        if (ObjSigIndex == (uint16)ObjSig::None)
            ObjSigIndex++; // Skip none after wrapping. Note that wrapping is actually a failure case.

        if (std::numeric_limits<unsigned short>::max() == ObjSigIndex) {
            __debugbreak();
            SPDLOG_ERROR("Maximum number of object signatures generated! Behavior is undefined.");
        }

        return ObjSig(ObjSigIndex);
    }

    void Game::InitObjects(Inferno::Level& level) {
        for (auto& seg : level.Segments)
            seg.Objects.clear();

        ObjSigIndex = 1;

        // Re-init each object to ensure a valid state.
        // Note this won't update weapons
        for (int id = 0; id < level.Objects.size(); id++) {
            auto& obj = level.Objects[id];
            Editor::InitObject(level, obj, obj.Type, obj.ID, false);
            if (auto seg = level.TryGetSegment(obj.Segment)) {
                obj.Ambient.SetTarget(seg->VolumeLight, Game::Time, 0);
            }

            obj.Rotation.Normalize();
            obj.PrevPosition = obj.Position;
            obj.PrevRotation = obj.Rotation;
            obj.Signature = GetObjectSig();

            if (auto seg = level.TryGetSegment(obj.Segment))
                seg->AddObject((ObjID)id);

            AttachLight(obj, { (ObjID)id, obj.Signature });
            //UpdateDirectLight(obj, 0);
        }

        ResizeAI(level.Objects.size());
        ResetAI();
    }

    void Game::AddPendingObjects(Inferno::Level& level) {
        ResizeAI(level.Objects.size() + PendingNewObjects.size());

        for (auto& pending : PendingNewObjects) {
            auto id = ObjID::None;
            pending.Signature = GetObjectSig();

            {
                // Find or create a slot for the new object
                bool foundExisting = false;

                for (int i = 0; i < level.Objects.size(); i++) {
                    auto& o = level.Objects[i];
                    if (!o.IsAlive()) {
                        if (auto seg = level.TryGetSegment(o.Segment)) {
                            Seq::remove(seg->Objects, (ObjID)i); // ensure dead object is removed from segment
                            //ASSERT(!Seq::contains(seg->Objects, (ObjID)i));
                        }

                        o = pending;
                        foundExisting = true;
                        id = ObjID(i);
                        break;
                    }
                }

                if (!foundExisting) {
                    id = ObjID(level.Objects.size());
                    level.Objects.push_back(pending);
                }

                ASSERT(id != ObjID::None);
            }

            auto& obj = level.Objects[(int)id];
            obj.PrevPosition = obj.Position;
            obj.PrevRotation = obj.Rotation;

            level.GetSegment(pending.Segment).AddObject(id);
            ObjRef objRef = { id, pending.Signature };

            // Hack to attach tracers due to not having the object ID in firing code
            if (obj.IsWeapon()) {
                WeaponID weaponID{ obj.ID };
                auto& weapon = Resources::GetWeapon(weaponID);

                if (weaponID == WeaponID::Vulcan) {
                    if (auto tracer = Render::EffectLibrary.GetTracer("vulcan_tracer"))
                        Render::AddTracer(*tracer, obj.Segment, objRef);
                }

                if (weaponID == WeaponID::Gauss) {
                    if (auto tracer = Render::EffectLibrary.GetTracer("gauss_tracer"))
                        Render::AddTracer(*tracer, obj.Segment, objRef);
                }

                if (auto sparks = Render::EffectLibrary.GetSparks(weapon.Extended.Sparks)) {
                    sparks->Parent = objRef;
                    sparks->Duration = (float)obj.Lifespan;
                    Render::AddSparkEmitter(*sparks, obj.Segment, obj.Position);
                }
            }

            if (obj.IsRobot()) {
                // Path newly created robots to their matcen triggers
                if (auto matcen = level.TryGetMatcen(obj.SourceMatcen)) {
                    AI::SetPath(obj, matcen->TriggerPath);
                }
            }

            AttachLight(obj, { id, obj.Signature });
        }

        PendingNewObjects.clear();
    }

    void Game::AddObject(const Object& obj) {
        PendingNewObjects.push_back(obj);
    }

    void UpdateDirectLight(Object& /*obj*/, float /*duration*/) {
        //Color directLight;

        //for (auto& light : Graphics::Lights.GetLights()) {
        //    if (light.radiusSq <= 0) continue;
        //    // todo: only scan nearby lights
        //    auto lightDist = Vector3::Distance(obj.Position, light.pos);
        //    auto radius = sqrt(light.radiusSq);
        //    if (lightDist > radius) continue;
        //    auto falloff = 1 - std::clamp(lightDist / radius, 0.0f, 1.0f);
        //    directLight += light.color * falloff;

        //    //auto lightDistSq = Vector3::DistanceSquared(obj.Position, other.Position);
        //    //auto lightRadiusSq = other.LightRadius * other.LightRadius;
        //    //if (lightDistSq > lightRadiusSq) continue;

        //    //float factor = lightDistSq / lightRadiusSq;                   
        //    //float smoothFactor = std::max(1.0f - pow(factor, 0.5f), 0.0f); // 0 to 1
        //    //float falloff = smoothFactor * smoothFactor / std::max(sqrt(lightDistSq), 1e-4f);
        //    //directLight += other.LightColor * falloff * 50;
        //}

        //obj.DirectLight.SetTarget(directLight, Game::Time, duration);
    }

    // Creates random arcs on damaged objects
    void AddDamagedEffects(const Object& obj, float dt) {
        if (!obj.IsAlive()) return;
        if (obj.Type != ObjectType::Robot && obj.Type != ObjectType::Reactor) return;

        auto chance = std::lerp(2.5f, 0.0f, obj.HitPoints / (obj.MaxHitPoints * 0.7f));
        if (chance < 0) return;

        // Create sparks randomly
        if (Random() < chance * dt) {
            auto effect = obj.IsRobot() && Resources::GetRobotInfo(obj).IsBoss
                ? "damaged boss arcs"
                : "damaged object arcs";

            if (auto beam = Render::EffectLibrary.GetBeamInfo(effect)) {
                auto startObj = Game::GetObjectRef(obj);
                Render::AddBeam(*beam, beam->Life, startObj);
            }
        }
    }

    void FixedUpdateObject(float dt, ObjID id, Object& obj) {
        if (HasFlag(obj.Flags, ObjectFlag::Updated)) return;
        SetFlag(obj.Flags, ObjectFlag::Updated);
        ObjRef objRef{ id, obj.Signature };

        UpdatePhysics(Game::Level, id, dt);
        obj.Ambient.Update(Game::Time);

        if (!HasFlag(obj.Flags, ObjectFlag::Destroyed) && (obj.Lifespan <= 0 && HasFlag(obj.Flags, ObjectFlag::Exploding))) {
            // Check if a live object has been destroyed.
            // This can happen by running out of hit points or by being set to explode
            DestroyObject(obj);
            // Keep playing effects from a dead reactor
            if (obj.Type != ObjectType::Reactor) {
                Render::RemoveEffects(objRef);
                Sound::Stop(objRef); // stop any sounds playing from this object
            }
        }
        else if (obj.Lifespan <= 0 && !HasFlag(obj.Flags, ObjectFlag::Dead)) {
            Game::ExplodeWeapon(Game::Level, obj); // explode expired weapons
            obj.Flags |= ObjectFlag::Dead;

            if (auto seg = Game::Level.TryGetSegment(obj.Segment))
                seg->RemoveObject(id);
        }

        if (!HasFlag(obj.Flags, ObjectFlag::Dead)) {
            if (obj.Type == ObjectType::Weapon)
                Game::UpdateWeapon(obj, dt);

            UpdateDirectLight(obj, 0.10f);
            AddDamagedEffects(obj, dt);
            UpdateAI(obj, dt);
        }
    }


    void TurnTowardsVector(Object& obj, Vector3 towards, float rate) {
        if (towards == Vector3::Zero) return;
        // transform towards to local coordinates
        Matrix basis(obj.Rotation);
        basis = basis.Invert();
        towards = Vector3::Transform(towards, basis); // transform towards to basis of object
        towards.z *= -1; // hack: correct for LH object matrix

        auto rotation = Quaternion::FromToRotation(Vector3::UnitZ, towards); // rotation to the target vector
        auto euler = rotation.ToEuler() / rate / DirectX::XM_2PI; // Physics update multiplies by XM_2PI so divide it here
        euler.z = 0; // remove roll
        obj.Physics.AngularVelocity = euler;
    }

    void RotateTowards(Object& obj, Vector3 point, float angularThrust) {
        auto dir = point - obj.Position;
        dir.Normalize();

        // transform towards to local coordinates
        Matrix basis(obj.Rotation);
        basis = basis.Invert();
        dir = Vector3::Transform(dir, basis); // transform towards to basis of object
        dir.z *= -1; // hack: correct for LH object matrix

        auto rotation = Quaternion::FromToRotation(Vector3::UnitZ, dir); // rotation to the target vector
        auto euler = rotation.ToEuler() * angularThrust;
        euler.z = 0; // remove roll
        //obj.Physics.AngularVelocity = euler;
        obj.Physics.AngularThrust += euler;
    }

    void ApplyForce(Object& obj, const Vector3& force) {
        if (obj.Movement != MovementType::Physics) return;
        if (obj.Physics.Mass == 0) return;
        obj.Physics.Velocity += force / obj.Physics.Mass;
    }

    void ApplyRotation(Object& obj, const Vector3& force) {
        if (obj.Movement != MovementType::Physics || obj.Physics.Mass <= 0) return;
        auto vecmag = force.Length();
        if (vecmag == 0) return;
        vecmag /= 8.0f;

        // rate should go down as vecmag or mass goes up
        float rate = obj.Physics.Mass / vecmag;
        if (obj.Type == ObjectType::Robot) {
            if (rate < 0.25f) rate = 0.25f;
        }
        else {
            if (rate < 0.5f) rate = 0.5f;
        }
        //}

        TurnTowardsVector(obj, force, rate);
    }

}
