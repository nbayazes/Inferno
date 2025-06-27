#include "pch.h"
#define NOMINMAX
#include "Physics.h"

#include "Game.AI.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Wall.h"
#include "Graphics/Render.Debug.h"
#include "Input.h"
#include "Intersect.h"
#include "Resources.h"
#include "SoundSystem.h"

//#define DEBUG_OBJ_OUTLINE
// #define DEBUG_LEVEL_OUTLINE

using namespace DirectX;

namespace Inferno {
    namespace {
        IntersectContext Intersect(Game::Level);
    }

    // Rolls the object when turning
    void TurnRoll(PhysicsData& pd, float rollScale, float rollRate, float dt) {
        const auto desiredBank = pd.AngularVelocity.y * rollScale;
        const auto theta = desiredBank - pd.TurnRoll;

        auto roll = rollRate;

        if (std::abs(theta) < roll) {
            roll = theta; // clamp roll to theta
        }
        else {
            if (theta < 0)
                roll = -roll;
        }

        pd.TurnRoll = pd.BankState.Update(roll, dt);
    }

    // Applies angular physics for an object
    void AngularPhysics(Object& obj, float dt) {
        auto& pd = obj.Physics;

        if (IsZero(pd.AngularVelocity) && IsZero(pd.AngularThrust) && IsZero(pd.AngularAcceleration))
            return;

        auto pdDrag = pd.Drag > 0 ? pd.Drag : 0.001f;
        const auto drag = pdDrag * 5 / 2;
        const auto falloffScale = dt / Game::TICK_RATE; // adjusts falloff of values that expect a normal tick rate

        if (/*HasFlag(pd.Flags, PhysicsFlag::UseThrust) &&*/ pd.Mass > 0) {
            pd.AngularVelocity += pd.AngularThrust / pd.Mass * falloffScale; // acceleration
        }

        if (!HasFlag(pd.Flags, PhysicsFlag::FixedAngVel)) {
            pd.AngularVelocity += pd.AngularAcceleration * dt;
            pd.AngularAcceleration *= 1 - drag * falloffScale;
            pd.AngularVelocity *= 1 - drag * falloffScale;
        }

        Debug::R = pd.AngularVelocity.y;

        // unrotate object for bank caused by turn
        if (HasFlag(pd.Flags, PhysicsFlag::TurnRoll))
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(pd.TurnRoll) * obj.Rotation);

        // negating angles converts from lh to rh
        obj.Rotation = Matrix3x3(Matrix::CreateFromYawPitchRoll(-pd.AngularVelocity * dt * XM_2PI) * obj.Rotation);

        if (HasFlag(pd.Flags, PhysicsFlag::TurnRoll)) {
            auto roll = obj.Physics.TurnRollScale;
            if (obj.IsPlayer()) roll *= Settings::Inferno.ShipRoll == ShipRollMode::Normal ? 1 : 0.5f;
            TurnRoll(obj.Physics, roll, obj.Physics.TurnRollRate, dt);

            // re-rotate object for bank caused by turn
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(-pd.TurnRoll) * obj.Rotation);
        }

        obj.Rotation.Normalize();
        //ASSERT(IsNormalized(obj.Rotation.Forward()));
    }

    // Applies wiggle to an object
    void WiggleObject(Object& obj, double t, float dt, float amplitude, float rate) {
        //const auto stepScale = dt / Game::TICK_RATE; // Rescale for sub-steps
        //auto angle = std::sin(t * XM_2PI * rate) * 20 * stepScale; // multiplier tweaked to cause 0.5 units of movement at a 1/64 tick rate
        auto angle = (float)std::sin(t * XM_2PI * rate) * 20; // multiplier tweaked to cause 0.5 units of movement at a 1/64 tick rate
        auto wiggle = obj.Rotation.Up() * angle * amplitude * dt;
        obj.Physics.Velocity += wiggle;
    }

    void LinearPhysics(Object& obj, float dt) {
        auto& pd = obj.Physics;
        const auto stepScale = dt / Game::TICK_RATE;

        Weapon* weapon = obj.IsWeapon() ? &Resources::GetWeapon(obj) : nullptr;

        if (HasFlag(obj.Physics.Flags, PhysicsFlag::Gravity))
            pd.Velocity += Game::Gravity * dt;

        // Apply weapon thrust
        if (HasFlag(obj.Physics.Flags, PhysicsFlag::UseThrust) && weapon && weapon->Thrust != 0)
            pd.Thrust = obj.Rotation.Forward() * weapon->Thrust * dt;

        if (obj.Physics.Wiggle > 0) {
            float mult = 1;
            auto offset = (float)obj.Signature * 0.8191f; // random offset to keep objects from wiggling at same time

            if (obj.IsPlayer()) {
                if (Settings::Inferno.ShipWiggle == WiggleMode::Reduced)
                    mult = 0.5f;
                else if (Settings::Inferno.ShipWiggle == WiggleMode::Off)
                    mult = 0;

                offset = 0.25; // Align offset so wiggle doesn't shift from start position
            }

            if (mult > 0)
                WiggleObject(obj, obj.Lifespan + offset, dt, obj.Physics.Wiggle * mult, obj.Physics.WiggleRate);
        }

        if (pd.Velocity == Vector3::Zero && pd.Thrust == Vector3::Zero)
            return;

        if (pd.Thrust != Vector3::Zero && pd.Mass > 0)
            pd.Velocity += pd.Thrust / pd.Mass * stepScale; // acceleration

        if (pd.Drag > 0)
            pd.Velocity *= 1 - pd.Drag * stepScale;

        // Cap the max speed of weapons with thrust
        if (HasFlag(obj.Physics.Flags, PhysicsFlag::UseThrust) && weapon && weapon->Thrust != 0) {
            auto maxSpeed = GetSpeed(*weapon);
            if (pd.Velocity.Length() > maxSpeed) {
                Vector3 dir;
                pd.Velocity.Normalize(dir);
                pd.Velocity = dir * maxSpeed;
            }
        }

        obj.Position += pd.Velocity * dt;
    }

    void PlotPhysics(double t, const PhysicsData& pd) {
        static int index = 0;
        static double refresh_time = 0.0;

        if (refresh_time == 0.0)
            refresh_time = t;

        if (Input::IsKeyDown(Input::Keys::Add)) {
            if (index < Debug::ShipVelocities.size() && t >= refresh_time) {
                //while (refresh_time < Game::ElapsedTime) {
                Debug::ShipVelocities[index] = pd.Velocity.Length();
                //std::cout << t << "," << physics.Velocity.Length() << "\n";
                refresh_time = t + 1.0f / 60.0f;
                index++;
            }
        }
        else {
            index = 1;
        }
    }

    // Moves a projectile in a sine pattern
    void SineWeapon(Object& obj, float dt, float speed, float amplitude) {
        if (obj.Control.Type != ControlType::Weapon || !obj.Control.Weapon.SineMovement) return;
        auto offset = std::sin(obj.Control.Weapon.AliveTime * XM_2PI * speed + dt) - std::sin(obj.Control.Weapon.AliveTime * XM_2PI * speed);
        obj.Position += obj.Rotation.Up() * offset * amplitude;
    }

    void PlayerPhysics(const Object& obj, float /*dt*/) {
        if (obj.Type != ObjectType::Player) return;
        auto& physics = obj.Physics;

        Debug::ShipThrust = physics.Thrust;
        Debug::ShipAcceleration = Vector3::Zero;
    }

    //using PotentialSegments = Array<SegID, 10>;
    List<SegID> g_VisitedStack; // global visited segments buffer

    List<SegID>& GetPotentialSegments(Level& level, SegID start, const Vector3& point, float radius, const Vector3& velocity, float /*dt*/, ObjectType objType) {
        g_VisitedStack.clear();
        g_VisitedStack.push_back(start);
        int index = 0;

        Vector3 direction;
        velocity.Normalize(direction);
        //Ray ray(point, direction);
        //const float speed = velocity.Length();
        //const float travelDist = speed * dt * 2;
        //const bool needsRaycast = travelDist > radius;

        while (index < g_VisitedStack.size()) {
            auto segId = g_VisitedStack[index];
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SIDE_IDS) {
                auto& side = seg.GetSide(sideId);

                //if (objType == ObjectType::Weapon) {
                //    if (auto wall = level.TryGetWall(side.Wall)) {
                //        if (wall->Type == WallType::Destroyable)
                //            continue;

                //        // Don't hit the other side of doors with weapons. Note that projectiles will still pass through transparent pixels.
                //        if (wall->Type == WallType::Door && !HasFlag(wall->Flags, WallFlag::DoorOpened))
                //            continue;
                //    }
                //}

                if (objType == ObjectType::Player && seg.SideIsSolid(sideId, level))
                    continue; // Don't hit test segments through solid walls to prevent picking up powerups

                //if (needsRaycast) {
                //    auto raySide = IntersectRaySegmentSide(level, ray, { segId, sideId }, travelDist);
                //    if (raySide != SideID::None) {
                //        if (auto conn = seg.GetConnection(raySide); conn != SegID::None)
                //            g_VisitedStack.push_back(conn);
                //    }
                //}
                //else {
                Plane p(side.Center + side.AverageNormal * radius, side.AverageNormal);
                if (index == 0 || p.DotCoordinate(point) <= 0) {
                    // Point was behind the plane or this was the starting segment
                    auto conn = seg.GetConnection(sideId);

                    if (conn > SegID::None && !Seq::contains(g_VisitedStack, conn)) {
                        g_VisitedStack.push_back(conn);
                    }
                }
                //}
            }

            index++;
        }

        return g_VisitedStack;
    }

    enum class CollisionType {
        None = 0, // Doesn't collide
        SphereRoom, // Same as SpherePoly, except against level meshes
        SpherePoly,
        PolySphere,
        SphereSphere
    };

    using CollisionTable = Array<Array<CollisionType, (int)ObjectType::Door + 1>, (int)ObjectType::Door + 1>;

    constexpr CollisionTable InitCollisionTable() {
        CollisionTable table{};
        auto setEntry = [&table](ObjectType a, ObjectType b, CollisionType type) {
            table[(int)a][(int)b] = type;
        };

        setEntry(ObjectType::Player, ObjectType::Wall, CollisionType::SphereRoom);
        setEntry(ObjectType::Player, ObjectType::Robot, CollisionType::SpherePoly);
        setEntry(ObjectType::Player, ObjectType::Wall, CollisionType::SphereSphere);
        setEntry(ObjectType::Player, ObjectType::Powerup, CollisionType::SphereSphere);
        setEntry(ObjectType::Player, ObjectType::Clutter, CollisionType::SpherePoly);
        setEntry(ObjectType::Player, ObjectType::Building, CollisionType::SpherePoly);
        setEntry(ObjectType::Player, ObjectType::Reactor, CollisionType::SpherePoly);
        setEntry(ObjectType::Player, ObjectType::Hostage, CollisionType::SphereSphere);
        setEntry(ObjectType::Player, ObjectType::Marker, CollisionType::SphereSphere);
        //setEntry(ObjectType::Player, ObjectType::Weapon, CollisionType::SphereSphere); // Weapons can hit players but players can't hit weapons? Simplify logic...
        setEntry(ObjectType::Powerup, ObjectType::Player, CollisionType::SphereSphere);

        setEntry(ObjectType::Robot, ObjectType::Player, CollisionType::PolySphere);
        setEntry(ObjectType::Robot, ObjectType::Robot, CollisionType::SphereSphere);
        setEntry(ObjectType::Robot, ObjectType::Wall, CollisionType::SphereRoom);
        setEntry(ObjectType::Robot, ObjectType::Building, CollisionType::SpherePoly);
        setEntry(ObjectType::Robot, ObjectType::Reactor, CollisionType::SpherePoly);

        setEntry(ObjectType::Weapon, ObjectType::Weapon, CollisionType::SphereSphere);
        setEntry(ObjectType::Weapon, ObjectType::Robot, CollisionType::SpherePoly); // Harder to hit
        //setEntry(ObjectType::Weapon, ObjectType::Player, CollisionType::SpherePoly); // Easier to dodge
        setEntry(ObjectType::Weapon, ObjectType::Player, CollisionType::SphereSphere);
        setEntry(ObjectType::Weapon, ObjectType::Clutter, CollisionType::SpherePoly);
        setEntry(ObjectType::Weapon, ObjectType::Building, CollisionType::SpherePoly);
        setEntry(ObjectType::Weapon, ObjectType::Reactor, CollisionType::SpherePoly);

        return table;
    }

    constexpr CollisionTable COLLISION_TABLE = InitCollisionTable();
    constexpr CollisionType CheckCollision(ObjectType a, ObjectType b) { return COLLISION_TABLE[(int)a][(int)b]; }

    CollisionType ObjectCanHitTarget(const Object& src, const Object& target) {
        if (!target.IsAlive() && target.Type != ObjectType::Reactor) return CollisionType::None;
        if (src.Signature == target.Signature) return CollisionType::None; // don't hit yourself!
        if (target.Type == ObjectType::SecretExitReturn || src.Type == ObjectType::SecretExitReturn) return CollisionType::None;
        //if (src.Parent == target.Parent && src.Parent != ObjID::None) return false; // don't hit your siblings!

        //if ((src.Parent != ObjID::None && target.Parent != ObjID::None) && src.Parent == target.Parent)
        //    return false; // Don't hit your siblings!

        if ((HasFlag(src.Physics.Flags, PhysicsFlag::NoCollideRobots) && target.IsRobot()) ||
            (HasFlag(target.Physics.Flags, PhysicsFlag::NoCollideRobots) && src.IsRobot()))
            return CollisionType::None;

        // Player can't hit mines until they arm
        if ((ObjectIsMine(src) && target.IsPlayer() && src.Control.Weapon.AliveTime < Game::MINE_ARM_TIME) ||
            (ObjectIsMine(target) && src.IsPlayer() && target.Control.Weapon.AliveTime < Game::MINE_ARM_TIME))
            return CollisionType::None;

        // Don't let robots collide with robot-placed mines. Mine laying robots will blow themselves up otherwise.
        if (ObjectIsMine(target) || ObjectIsMine(src)) {
            if (HasFlag(target.Faction, Faction::Robot) && HasFlag(src.Faction, Faction::Robot))
                return CollisionType::None;
        }

        if ((src.IsPlayer() && target.IsRobot() && HasFlag(target.Physics.Flags, PhysicsFlag::SphereCollidePlayer)) ||
            (src.IsRobot() && target.IsPlayer() && HasFlag(src.Physics.Flags, PhysicsFlag::SphereCollidePlayer)))
            return CollisionType::SphereSphere;

        if (src.IsWeapon()) {
            if (Seq::contains(src.Control.Weapon.RecentHits, target.Signature))
                return CollisionType::None; // Don't hit objects recently hit by this weapon (for piercing)

            switch (target.Type) {
                case ObjectType::Robot: {
                    auto targetId = Game::GetObjectRef(target);
                    if (src.Parent == targetId) return CollisionType::None; // Don't hit robot with their own shots

                    auto& ri = Resources::GetRobotInfo(target.ID);
                    if (ri.IsCompanion)
                        return CollisionType::None; // weapons can't directly hit guidebots
                    break;
                }
                case ObjectType::Player: {
                    if (target.ID > 0) return CollisionType::None; // Only hit player 0 in singleplayer
                    if (src.Parent.Id == ObjID(0)) return CollisionType::None; // Don't hit the player with their own shots
                    if (WeaponIsMine((WeaponID)src.ID) && src.Control.Weapon.AliveTime < Game::MINE_ARM_TIME)
                        return CollisionType::None; // Mines can't hit the player until they arm
                    break;
                }

                //case ObjectType::Coop:
                case ObjectType::Weapon:
                    if (WeaponIsMine((WeaponID)src.ID))
                        return CollisionType::None; // mines can't hit other mines

                    if (!WeaponIsMine((WeaponID)target.ID))
                        return CollisionType::None; // Weapons can only other weapons if they are mines
                    break;
            }
        }

        return COLLISION_TABLE[(int)src.Type][(int)target.Type];
    }

    // extract heading and pitch from a vector, assuming bank is 0
    Vector3 ExtractAnglesFromVector(Vector3 v) {
        v.Normalize();
        Vector3 angles = v;

        if (!IsZero(angles)) {
            angles.y = 0; // always zero bank
            angles.x = asin(-v.y);
            if (v.x == 0 && v.z == 0)
                angles.z = 0;
            else
                angles.z = atan2(v.z, v.x);
        }

        return angles;
    }

    // Applies random rotation to an object based on a force, relative to a source position.
    // Is very disorienting and can cause objects to roll and spin.
    void ApplyRotationalForce(Object& object, const Vector3& hitPoint, Vector3 force) {
        Matrix basis(object.Rotation);
        basis = basis.Invert();
        force = Vector3::Transform(force, basis); // transform force to basis of object

        auto arm = Vector3::Transform(hitPoint - object.Position, basis);
        const auto torque = force.Cross(arm);
        auto mass = object.Physics.Mass <= 0 ? 1 : object.Physics.Mass;

        // moment of inertia. solid sphere I = 2/5 MR^2. Thin shell: 2/3 MR^2
        const auto inertia = 1.0f / 6.0f * mass * object.Radius * object.Radius;
        auto accel = torque / inertia;
        object.Physics.AngularAcceleration += accel;
    }

    void ApplyRandomRotationalForce(Object& obj, const Vector3& srcPosition, const Vector3& force) {
        auto pt = RandomPointOnCircle(obj.Radius);
        auto edgePt = Vector3::Transform(pt, obj.GetTransform());
        auto edgeDir = edgePt - srcPosition;
        edgeDir.Normalize();
        ApplyRotationalForce(obj, edgePt, force);
    }

    // Applies rotation to an object based on a force. Does not apply roll.
    // ApplyRotationalForce is more realistic but too disorienting for the player.
    void ApplyRotationForcePlayer(Object& obj, Vector3 force) {
        if (obj.Movement != MovementType::Physics || obj.Physics.Mass <= 0) return;
        auto vecmag = force.Length();
        if (vecmag == 0) return;
        vecmag /= 8.0f;

        if (force == Vector3::Zero) return;

        float rate = obj.Physics.Mass / vecmag;
        if (rate < 0.5f) rate = 0.5f;

        // transform towards to local coordinates
        Matrix basis(obj.Rotation);
        basis = basis.Invert();
        force = Vector3::Transform(force, basis); // transform towards to basis of object
        force.z *= -1; // hack: correct for LH object matrix

        auto rotation = Quaternion::FromToRotation(Vector3::UnitZ, force); // rotation to the target vector
        auto euler = rotation.ToEuler() / rate / DirectX::XM_2PI; // Physics update multiplies by XM_2PI so divide it here
        euler.z = 0; // remove roll
        obj.Physics.AngularVelocity = euler;
    }

    // Creates an explosion that can cause damage or knockback
    void CreateExplosion(Level& level, const Object* source, const GameExplosion& explosion) {
        ASSERT(explosion.Room != RoomID::None);
        ASSERT(explosion.Segment != SegID::None);

        if (explosion.Damage == 0 && explosion.Force == 0)
            return; // No effect

        auto action = [&](const Segment& seg, bool) {
            for (auto& objId : seg.Objects) {
                auto obj = level.TryGetObject(objId);
                if (!obj) continue;
                auto& target = *obj;

                if (source && target.Signature == source->Signature) continue; // Don't hit self
                if (!target.IsAlive()) continue;

                if (target.IsWeapon()) {
                    if (!ObjectIsMine(target))
                        continue; // only allow explosions to affect weapons that are mines
                }

                auto parent = source ? Game::Level.TryGetObject(source->Parent) : nullptr;
                if (parent && parent->IsRobot() && parent->Signature == target.Signature)
                    continue; // Don't let robots damage themselves with explosions. Important for boss robots and robots behind grates.

                if (!target.IsPlayer() && !target.IsRobot() && !target.IsWeapon() && !target.IsReactor())
                    continue; // Filter invalid target types

                auto dist = Vector3::Distance(target.Position, explosion.Position);

                // subtract object radius so large enemies don't take less splash damage, this increases the effectiveness of explosives in general
                // however don't apply it to players due to dramatically increasing the amount of damage taken
                if (target.Type != ObjectType::Player && target.Type != ObjectType::Coop)
                    dist -= target.Radius;

                if (dist >= explosion.Radius) continue;
                dist = std::max(dist, 0.1f);

                Vector3 dir = target.Position - explosion.Position;
                dir.Normalize();
                Ray ray(explosion.Position, dir);
                LevelHit hit;
                RayQuery query{ .MaxDistance = dist, .Start = explosion.Segment, .Mode = RayQueryMode::Visibility };
                if (Intersect.RayLevel(ray, query, hit))
                    continue;

                // linear damage and force falloff
                float damage = explosion.Damage - (dist * explosion.Damage) / explosion.Radius;
                float force = explosion.Force - (dist * explosion.Force) / explosion.Radius;

                dir += RandomVector(0.25f);
                dir.Normalize();

                Vector3 forceVec = dir * force;
                //auto hitPos = (source.Position - obj.Position) * obj.Radius / (obj.Radius + dist);

                // Find where the point of impact is... ( pos_hit )
                //vm_vec_scale(vm_vec_sub(&pos_hit, &obj->pos, &obj0p->pos), fixdiv(obj0p->size, obj0p->size + dist));

                switch (target.Type) {
                    case ObjectType::Weapon: {
                        ApplyForce(target, forceVec);
                        // Mines can blow up under enough force
                        //if (obj.ID == (int)WeaponID::ProxMine || obj.ID == (int)WeaponID::SmartMine) {
                        //    if (dist * force > 0.122f) {
                        //        obj.Lifespan = 0;
                        //        // explode()?
                        //    }
                        //}
                        break;
                    }

                    case ObjectType::Robot: {
                        float stunMult = 1;
                        if (source && source->IsWeapon()) {
                            auto& weapon = Resources::GetWeapon(WeaponID(source->ID));
                            stunMult = weapon.Extended.StunMult;
                        }

                        ApplyForce(target, forceVec);

                        if (source && HasFlag(source->Faction, Faction::Robot) && ObjectIsMine(*source))
                            damage = 0; // Don't apply explosion damage from mines to robots, otherwise mine layers cause too much friendly fire

                        //if (parent && parent->IsRobot() && target.IsRobot())
                        //    damage *= 0.5f; // Halve explosion damage to other robots

                        DamageRobot({ explosion.Segment, explosion.Position }, target, damage, stunMult, parent);

                        target.LastHitForce = forceVec;
                        //fmt::print("applied {} splash damage at dist {}\n", damage, dist);

                        // todo: guidebot ouchies

                        //Vector3 negForce = forceVec * 2.0f * float(7 - Game::Difficulty) / 8.0f;
                        // Don't apply rotation if source directly hit this object, so that it doesn't rotate oddly
                        if (!source || source->LastHitObject != target.Signature)
                            ApplyRandomRotationalForce(target, hit.Point, forceVec);

                        break;
                    }

                    case ObjectType::Reactor: {
                        // apply damage if source is player
                        if (!Settings::Cheats.DisableWeaponDamage && source && source->IsInFaction(Faction::Player))
                            target.ApplyDamage(damage);

                        break;
                    }

                    case ObjectType::Player: {
                        ApplyForce(target, forceVec);
                        if (!source || source->LastHitObject != target.Signature)
                            ApplyRotationForcePlayer(target, forceVec);
                        //ApplyRandomRotationalForce(target, hit.Point, forceVec * 0.25f);

                        if (source && source->IsWeapon()) {
                            auto& weapon = Resources::GetWeapon(WeaponID(source->ID));
                            damage *= weapon.PlayerDamageScale;
                        }

                        // Quarter damage explosions on trainee
                        if (Game::Difficulty == DifficultyLevel::Trainee) damage /= 4;
                        Game::Player.ApplyDamage(damage, false);
                        break;
                    }

                    default:
                        throw Exception("Invalid object type in CreateExplosion()");
                }
            }
        };

        IterateNearbySegments(level, { explosion.Segment, explosion.Position }, explosion.Radius * 2, TraversalFlag::PassTransparent, action);
    }

    void IntersectBoundingBoxes(const Object& obj) {
        auto rotation = obj.Rotation;
        rotation.Forward(-rotation.Forward());
        auto orientation = Quaternion::CreateFromRotationMatrix(rotation);

        if (obj.Render.Type == RenderType::Model) {
            auto& model = Resources::GetModel(obj.Render.Model.ID);
            int smIndex = 0;
            for (auto& sm : model.Submodels) {
                auto offset = model.GetSubmodelOffset(smIndex++);
                auto transform = obj.GetTransform();
                transform.Translation(transform.Translation() + offset);
                transform = obj.Rotation * Matrix::CreateTranslation(obj.Position);

                auto bounds = sm.Bounds;
                bounds.Center.z *= -1;
                bounds.Center = Vector3::Transform(bounds.Center, transform);
                // todo: animation
                bounds.Orientation = orientation;
                Render::Debug::DrawBoundingBox(bounds, Color(0, 1, 0));
            }
        }
    }


    void CollideObjects(const LevelHit& hit, const Object& obj, Object& target, float /*dt*/) {
        if (hit.Speed <= 0.1f) return;

        //SPDLOG_INFO("{}-{} impact speed: {}", obj.Signature, target.Signature, hit.Speed);

        if (target.Type == ObjectType::Powerup || target.Type == ObjectType::Marker)
            return;


        //auto v1 = a.Physics.PrevVelocity.Dot(hit.Normal);
        //auto v2 = b.Physics.PrevVelocity.Dot(hit.Normal);
        //Vector3 v1{}, v2{};
        //v1 = v2 = hit.Normal * hit.Speed;

        // Player ramming a robot should impart less force than a weapon
        //float restitution = obj.Type == ObjectType::Player ? 0.6f : 1.0f;

        auto m1 = obj.Physics.Mass == 0.0f ? 1.0f : obj.Physics.Mass;
        auto m2 = target.Physics.Mass == 0.0f ? 1.0f : target.Physics.Mass;
        // These equations are valid as long as one mass is not zero
        //auto newV1 = (m1 * v1 + m2 * v2 - m2 * (v1 - v2) * restitution) / (m1 + m2);
        //auto newV2 = (m1 * v1 + m2 * v2 - m1 * (v2 - v1) * restitution) / (m1 + m2);

        //auto bDeltaVel = hit.Normal * (newV2 - v2);
        //if (!HasFlag(a.Physics.Flags, PhysicsFlag::Piercing)) // piercing weapons shouldn't bounce
        //    a.Physics.Velocity += hit.Normal * (newV1 - v1);

        //if (b.Movement == MovementType::Physics)
        //    b.Physics.Velocity += hit.Normal * (newV2 - v2);

        float speed = hit.Speed;
        auto normal = -hit.Normal;

        if (obj.Type == ObjectType::Weapon) {
            auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
            if (weapon.SplashRadius > 0)
                speed += GetDamage(weapon) * 4; // Damage equals force

            // Use projectile velocity as hit normal so torque is applied reliably
            obj.Physics.Velocity.Normalize(normal);
        }

        auto force = normal * speed * m1 / m2;

        //const float resitution = obj.Type == ObjectType::Player ? 0.2f : 0.2f;
        constexpr float resitution = 0.4f;
        target.Physics.Velocity += force * resitution;
        target.LastHitForce = force * resitution;

        // Only apply rotational velocity when something hits a robot. Feels bad if a player being hit loses aim.
        if (target.Type == ObjectType::Robot) {
            if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Robot) {
                // Use the source velocity for rotational force between spheres.
                // This is because the normal between spheres always points to the center of the other object
                // which results in no rotation.
                // Use previous velocity because the velocity for this tick has already changed due to the collision.
                obj.Physics.PrevVelocity.Normalize(normal);
                force = normal * speed * m1 / m2;
            }

            //if (obj.Type == ObjectType::Weapon) force *= 2; // make weapon hits apply more rotation force
            //if (obj.Type == ObjectType::Player) force *= 0.25f; // Less rotation from players
            ApplyRotationalForce(target, hit.Point, force);
        }
    }

    // Performs intersection checks between an object's sphere and another object's model mesh.
    // Target is repositioned based on the intersections.
    HitInfo IntersectSpherePoly(const Object& sphereSource, const Object& meshSource, Object& target, float dt) {
        if (meshSource.Render.Type != RenderType::Model) return {};
        auto& model = Resources::GetModel(meshSource.Render.Model.ID);

        const auto& position = sphereSource.PrevPosition;
        const auto& meshPosition = meshSource.PrevPosition;
        //const float speed = sphereSource.Physics.Velocity.Length();
        //const float travelDist = speed * dt;
        auto direction = sphereSource.Position - sphereSource.PrevPosition;
        const float travelDist = direction.Length();
        direction.Normalize();
        const float speed = travelDist / dt;
        //const float travelDist2 = Vector3::Distance(sphereSource.PrevPosition, sphereSource.Position);
        const bool needsRaycast = travelDist > sphereSource.Radius /** 1.5f*/;
        //Vector3 direction;
        //sphereSource.Physics.Velocity.Normalize(direction);

        //const auto objDistance = Vector3::Distance(sphereSource.Position, colliderPosition);
        const auto objDistance = Vector3::Distance(position, meshPosition);
        const auto radii = sphereSource.Radius + meshSource.Radius;

        if (needsRaycast) {
            // Add both radii together to ensure the ray doesn't miss the bounds
            //BoundingSphere sphere(colliderPosition, radii);
            //Ray pathRay(position, direction);
            BoundingSphere sphere(meshPosition, radii);
            Ray pathRay(position, direction);

            float dist;
            if (!pathRay.Intersects(sphere, dist))
                return {}; // Ray doesn't intersect

            if (dist > travelDist && objDistance > radii)
                return {}; // Ray too far away and not inside sphere
        }
        else {
            if (objDistance > radii)
                return {}; // Objects too far apart
        }

        // transform ray to model space of the target object
        auto transform = meshSource.GetTransform();
        auto invTransform = transform.Invert();
        auto invRotation = Matrix(meshSource.Rotation).Invert();
        //const auto localPos = Vector3::Transform(sphereSource.Position, invTransform);
        const auto localPos = Vector3::Transform(position, invTransform);
        auto localDir = Vector3::TransformNormal(direction, invRotation);
        localDir.Normalize();
        Ray ray(localPos, localDir); // update the input ray

        HitInfo hit;
        float averageHitDistance = 0;
        Vector3 averageNormal;
        Vector3 averageHitPoint;
        int hits = 0;
        int texNormalIndex = 0, flatNormalIndex = 0;

#ifdef DEBUG_OBJ_OUTLINE
        auto drawTriangleEdge = [&transform](const Vector3& a, const Vector3& b, const Color& color) {
            auto dbgStart = Vector3::Transform(a, transform);
            auto dbgEnd = Vector3::Transform(b, transform);
            Render::Debug::DrawLine(dbgStart, dbgEnd, color);
        };
#endif

        for (int smIndex = 0; smIndex < model.Submodels.size(); smIndex++) {
            auto& submodel = model.Submodels[smIndex];
            auto smTransform = GetSubmodelTransform(meshSource, model, smIndex);

            auto hitTestIndices = [&](span<const uint16> indices, span<const Vector3> normals, int& normalIndex) {
                for (int i = 0; i < indices.size(); i += 3) {
                    Vector3 p0 = Vector3::Transform(model.Vertices[indices[i + 0]], smTransform);
                    Vector3 p1 = Vector3::Transform(model.Vertices[indices[i + 1]], smTransform);
                    Vector3 p2 = Vector3::Transform(model.Vertices[indices[i + 2]], smTransform);
                    Vector3 normal = normals[normalIndex++];

                    // Normal debug
                    //auto center = (p0 + p1 + p2) / 3;
                    //drawTriangleEdge(center, center + normal * 2, { 1, 0, 0 });

                    bool triFacesObj = localDir.Dot(normal) <= 0;
                    Vector3 faceLocalPos = localPos;

                    if (needsRaycast && triFacesObj) {
                        float dist{};
                        Plane basePlane(p0, p1, p2);

                        //auto ri = ray.Intersects(p0, p1, p2, dist);
                        //auto ri2 = ray.Intersects(basePlane, dist);

                        if (ray.Intersects(p0, p1, p2, dist) && dist < travelDist) {
                            // Move object to intersection of triangle and proceed
                            faceLocalPos += localDir * (dist - sphereSource.Radius);
                        }
                        else if (ray.Intersects(basePlane, dist) && dist < travelDist) {
                            // Move object to intersection of plane and proceed
                            // This allows the radius of raycasted projectiles to have effect
                            faceLocalPos += localDir * dist;
                        }
                        else {
                            continue;
                        }
                    }

                    auto offset = normal * sphereSource.Radius; // offset triangle by radius to account for object size
                    Plane plane(p2 + offset, p1 + offset, p0 + offset);
                    auto planeDist = plane.DotCoordinate(faceLocalPos);
                    if (planeDist > 0 || planeDist < -sphereSource.Radius - travelDist)
                        continue; // Object isn't close enough to the triangle plane

                    auto point = ProjectPointOntoPlane(faceLocalPos, plane);
                    float hitDistance = FLT_MAX;
                    Vector3 hitPoint, hitNormal;

#ifdef DEBUG_OBJ_OUTLINE
                    drawTriangleEdge(p0, p1, { 0, 1, 0 });
                    drawTriangleEdge(p1, p2, { 0, 1, 0 });
                    drawTriangleEdge(p2, p0, { 0, 1, 0 });
#endif

                    if (triFacesObj && TriangleContainsPoint(p0 + offset, p1 + offset, p2 + offset, point)) {
                        // point was inside the triangle and behind the plane
                        hitPoint = point - offset;
                        hitNormal = normal;
                        hitDistance = planeDist;
                        //edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                    }
                    else {
                        // Point wasn't inside the triangle, check the edges
                        auto [triPoint, triDist] = ClosestPointOnTriangle2(p0, p1, p2, faceLocalPos);

                        if (triDist <= sphereSource.Radius) {
                            auto edgeNormal = localPos - triPoint;
                            edgeNormal.Normalize();

                            // If this is not present an object can become stuck inside of another one due to
                            // repositioning using an average.
                            //
                            // However, enabling it causes moving objects to not correctly collide.
                            // For example a rotating robot will not collide with a stationary or slow moving player reliably.
                            //
                            // The fix would be to account for the rotational speed and velocity of the source object
                            // in relation to the target.
                            //if (velocity > 1.0f && ray.direction.Dot(edgeNormal) > 0)
                            //    continue; // velocity going away from edge so object doesn't get stuck inside

                            // Object hit a triangle edge
                            hitDistance = triDist;
                            hitNormal = edgeNormal;
                            hitPoint = triPoint;
                        }
                    }

                    if (hitDistance < sphereSource.Radius) {
#ifdef DEBUG_OBJ_OUTLINE
                        drawTriangleEdge(p0, p1, { 1, 0, 0 });
                        drawTriangleEdge(p1, p2, { 1, 0, 0 });
                        drawTriangleEdge(p2, p0, { 1, 0, 0 });

                        //drawTriangleEdge(p0 + offset, p1 + offset);
                        //drawTriangleEdge(p1 + offset, p2 + offset);
                        //drawTriangleEdge(p2 + offset, p0 + offset);
#endif
                        // Transform from local to world space
                        averageNormal += Vector3::TransformNormal(hitNormal, meshSource.Rotation);
                        averageHitPoint += Vector3::Transform(hitPoint, transform);
                        averageHitDistance += hitDistance;
                        hits++;

                        //auto nDotVel = hit.Normal.Dot(sphereSource.Physics.Velocity);
                        hit.Speed = std::max(speed, hit.Speed);
                    }
                }
            };

            hitTestIndices(submodel.Indices, model.Normals, texNormalIndex);
            hitTestIndices(submodel.FlatIndices, model.FlatNormals, flatNormalIndex);
        }

        if (hits == 0)
            return {};

        averageHitPoint /= (float)hits;
        averageNormal /= (float)hits;
        averageHitDistance /= (float)hits;

        hit.Point = averageHitPoint;
        hit.Normal = averageNormal;
        hit.Distance = averageHitDistance;

        if (hits > 0 && sphereSource.Type != ObjectType::Weapon && sphereSource.Type != ObjectType::Reactor) {
            // Don't move weapons or reactors
            // Move objects to the average position of all hits. This fixes jitter against more complex geometry and when nudging between walls.
            if (!HasFlag(sphereSource.Physics.Flags, PhysicsFlag::Piercing))
                target.Position = hit.Point + hit.Normal * sphereSource.Radius;

            auto nDotVel = hit.Normal.Dot(sphereSource.Physics.Velocity);
            target.Physics.Velocity -= hit.Normal * nDotVel; // slide along triangle
        }

        if (sphereSource.Type == ObjectType::Weapon && !needsRaycast) {
            // Use the weapon position as the hit location so the explosion doesn't "snap" to the model
            // Be careful that this doesn't reintroduce the gauss self damage problem...
            hit.Normal = sphereSource.Position - hit.Point;
            hit.Normal.Normalize();
            hit.Point = sphereSource.Position;
        }

        return hit;
    }

    // Performs intersection between an object's model and another object's sphere.
    // Object is repositioned based on the intersections.
    // Used when a robot collides with the player - we want to reposition the player not the robot
    HitInfo IntersectPolySphere(const Object& meshSource, Object& sphereSource, float dt) {
        return IntersectSpherePoly(sphereSource, meshSource, sphereSource, dt);
    }

    constexpr float MIN_TRAVEL_DISTANCE = 0.001f; // Min distance an object must move to test collision

    bool IntersectLevelSegment(Level& level, const Vector3& position, float radius, SegID segId, LevelHit& hit) {
        Debug::SegmentsChecked++;
        auto& seg = level.Segments[(int)segId];

        for (auto& sideId : SIDE_IDS) {
            if (!seg.SideIsSolid(sideId, level)) continue;
            if (Settings::Cheats.DisableWallCollision && seg.GetSide(sideId).HasWall()) continue;
            auto& side = seg.GetSide(sideId);
            auto face = ConstFace::FromSide(level, seg, sideId);
            auto& indices = side.GetRenderIndices();
            float edgeDistance = 0; // 0 for edge tests

            // Check the position against each triangle
            for (int tri = 0; tri < 2; tri++) {
                Vector3 tangent = face.Side.Tangents[tri];
                // Offset the triangle by the object radius and then do a point-triangle intersection.
                // This leaves space at the edges to do capsule intersection checks.
                const auto offset = side.Normals[tri] * radius;
                const Vector3 p0 = face[indices[tri * 3 + 0]];
                const Vector3 p1 = face[indices[tri * 3 + 1]];
                const Vector3 p2 = face[indices[tri * 3 + 2]];

                float hitDistance = FLT_MAX;
                Vector3 hitPoint, hitNormal;

                // Use point-triangle intersections for everything else.
                // Note that fast moving objects could clip through walls!
                Plane plane(p0 + offset, p1 + offset, p2 + offset);
                auto planeDist = plane.DotCoordinate(position);
                if (planeDist > 0 || planeDist < -radius)
                    continue; // Object isn't close enough to the triangle plane

                auto point = ProjectPointOntoPlane(position, plane);

                if (TriangleContainsPoint(p0 + offset, p1 + offset, p2 + offset, point)) {
                    // point was inside the triangle and behind the plane
                    hitPoint = point - offset;
                    hitNormal = side.Normals[tri];
                    hitDistance = planeDist;
                    edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                }
                else {
                    // Point wasn't inside the triangle, check the edges
                    int edgeIndex;
                    auto [triPoint, triDist] = ClosestPointOnTriangle2(p0, p1, p2, position, &edgeIndex);

                    if (triDist <= radius) {
                        auto normal = position - triPoint;
                        normal.Normalize(hitNormal);

                        // Object hit a triangle edge
                        hitDistance = triDist;
                        hitPoint = triPoint;

                        Vector3 tanVec;
                        if (edgeIndex == 0)
                            tanVec = p1 - p0;
                        else if (edgeIndex == 1)
                            tanVec = p2 - p1;
                        else
                            tanVec = p0 - p2;

                        tanVec.Normalize(tangent);
                    }
                }

                if (hitDistance < radius + 0.001f) {
                    // Check if hit is transparent (duplicate check due to triangle edges)
                    //if (obj.Type == ObjectType::Weapon && WallPointIsTransparent(hitPoint, face, tri))
                    //    continue; // skip projectiles that hit transparent part of a wall

                    if (hitDistance < hit.Distance) {
                        // Store the closest overall hit as the final hit
                        hit.Distance = hitDistance;
                        hit.Normal = hitNormal;
                        hit.Point = hitPoint;
                        hit.Tag = { segId, sideId };
                        hit.Tangent = tangent;
                        hit.EdgeDistance = edgeDistance;
                        hit.Tri = tri;
                    }
                }
            }
        }

        return (bool)hit.Tag;
    }

    // todo: the level and object intersections should track the total distance travelled by the object.
    // If the total distance is met (due to sliding or repositioning), stop iteration. Also limit the total number of iterations.
    bool IntersectLevelMesh(Level& level, Object& obj, span<SegID> pvs, LevelHit& hit) {
        Vector3 direction = obj.Position - obj.PrevPosition;
        auto speed = direction.Length();
        if (speed <= 0.001f) return false;
        direction.Normalize();
        if (IsZero(direction)) direction = Vector3::UnitY;
        // The position before moving this tick should be used for projecting mesh intersections,
        // then correcting the new position based on any intersections.
        Ray pathRay(obj.PrevPosition, direction);

        for (auto& segId : pvs) {
            if (segId == SegID::Terrain) return false; // no terrain intersection
            Debug::SegmentsChecked++;
            auto& seg = level.Segments[(int)segId];

            for (auto& sideId : SIDE_IDS) {
                if (!seg.SideIsSolid(sideId, level)) continue;
                auto& side = seg.GetSide(sideId);
                if (Settings::Cheats.DisableWallCollision && side.HasWall()) continue;
                auto face = ConstFace::FromSide(level, seg, sideId);
                auto& indices = side.GetRenderIndices();
                float edgeDistance = 0; // 0 for edge tests

                // Check the position against each triangle
                for (int tri = 0; tri < 2; tri++) {
                    Vector3 tangent = face.Side.Tangents[tri];
                    // Offset the triangle by the object radius and then do a point-triangle intersection.
                    // This leaves space at the edges to do capsule intersection checks.
                    const auto offset = side.Normals[tri] * obj.Radius;
                    const Vector3 p0 = face[indices[tri * 3 + 0]];
                    const Vector3 p1 = face[indices[tri * 3 + 1]];
                    const Vector3 p2 = face[indices[tri * 3 + 2]];

                    auto objDir = obj.PrevPosition - side.Centers[tri];
                    objDir.Normalize();

                    bool triFacesObj = objDir.Dot(side.Normals[tri]) > 0;

                    float hitDistance = FLT_MAX;
                    Vector3 hitPoint, hitNormal;
                    //bool hitEdge = false;

#ifdef DEBUG_LEVEL_OUTLINE
                    Render::Debug::DrawLine(p0, p1, { 0, 1, 0 });
                    Render::Debug::DrawLine(p1, p2, { 0, 1, 0 });
                    Render::Debug::DrawLine(p2, p0, { 0, 1, 0 });
#endif
                    // a size 4 object would need a velocity > 250 to clip through walls
                    if (obj.Type == ObjectType::Weapon && HasFlag(obj.Physics.Flags, PhysicsFlag::PointCollideWalls)) {
                        // Use raycasting for weapons because they are typically small and have high velocities
                        float dist{};
                        float travelDistance = Vector3::Distance(obj.Position, obj.PrevPosition);

                        if (triFacesObj &&
                            pathRay.Intersects(p0, p1, p2, dist)) {
                            if (dist < travelDistance) {
                                hitPoint = pathRay.position + direction * dist;
                                if (WallPointIsTransparent(hitPoint, face, tri))
                                    continue; // skip projectiles that hit transparent part of a wall

                                // move the object to the surface and proceed as normal
                                obj.Position = hitPoint - direction * 0.01f;
                                hitNormal = side.Normals[tri];
                                hitDistance = 0.01f; // exact hit
                                edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                            }
                        }
                    }
                    else {
                        // Use point-triangle intersections for everything else.
                        // Note that fast moving objects could clip through walls!
                        Plane plane(p0 + offset, p1 + offset, p2 + offset);
                        auto planeDist = plane.DotCoordinate(obj.Position);
                        if (planeDist > 0 || planeDist < -obj.Radius)
                            continue; // Object isn't close enough to the triangle plane

                        auto point = ProjectPointOntoPlane(obj.Position, plane);

                        if (triFacesObj && TriangleContainsPoint(p0 + offset, p1 + offset, p2 + offset, point)) {
                            // point was inside the triangle and behind the plane
                            hitPoint = point - offset;
                            hitNormal = side.Normals[tri];
                            hitDistance = planeDist;
                            edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                        }
                        else {
                            // Point wasn't inside the triangle, check the edges
                            int edgeIndex;
                            auto [triPoint, triDist] = ClosestPointOnTriangle2(p0, p1, p2, obj.Position, &edgeIndex);

                            if (triDist <= obj.Radius) {
                                auto normal = obj.Position - triPoint;
                                normal.Normalize(hitNormal);

                                if (speed > 0.1f && direction.Dot(hitNormal) > 0)
                                    continue; // velocity going away from surface

                                // Object hit a triangle edge
                                hitDistance = triDist;
                                hitPoint = triPoint;

                                Vector3 tanVec;
                                if (edgeIndex == 0)
                                    tanVec = p1 - p0;
                                else if (edgeIndex == 1)
                                    tanVec = p2 - p1;
                                else
                                    tanVec = p0 - p2;

                                tanVec.Normalize(tangent);
                                //hitEdge = true;
                            }
                        }
                    }

                    float hitSpeed = 0;
                    if (hitDistance < -.5f && hitDistance > -obj.Radius) {
                        // Reposition objects stuck in a wall to the surface.
                        // Offset is necessary so bombs don't slide around.
                        //if (obj.Type != ObjectType::Debris)
                        //    SPDLOG_WARN("Moved object {} to wall surface", Game::GetObjectRef(obj).Id);
                        obj.Position = hitPoint + hitNormal * (obj.Radius + 0.1f);
                    }

                    if (hitDistance < obj.Radius + 0.001f) {
                        // Check if hit is transparent (duplicate check due to triangle edges)
                        if (obj.Type == ObjectType::Weapon && !ObjectIsMine(obj) && WallPointIsTransparent(hitPoint, face, tri))
                            continue; // skip projectiles that hit transparent part of a wall

                        hitSpeed = abs(hitNormal.Dot(obj.Physics.Velocity));
                        auto& ti = Resources::GetLevelTextureInfo(side.TMap);

                        // bounce velocity is handled after all hits are resolved so that overlapping
                        // triangle edges don't double the effect
                        if (ti.HasFlag(TextureFlag::ForceField))
                            hit.Bounce = BounceType::Standard;
                        else if (obj.Physics.CanBounce()) {
                            if (HasFlag(obj.Physics.Flags, PhysicsFlag::Ricochet)) {
                                auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
                                auto texInfo = GetTextureFromIntersect(hitPoint, face, tri);
                                auto& matInfo = Resources::GetMaterial(texInfo.tex);
                                float metalMult = 1 + weapon.Extended.RicochetMetalMultiplier * matInfo.Metalness;

                                float ang = AngleBetweenVectors(direction, hitNormal) * RadToDeg - 90.0f;
                                float minimumAngle = weapon.Extended.RicochetAngle * metalMult;
                                if (ang <= minimumAngle) {
                                    float angleMult = 1;
                                    float maximumChanceAngle = minimumAngle / 3.0f;
                                    if (ang > maximumChanceAngle)
                                        angleMult = 1 - (ang - maximumChanceAngle) / (minimumAngle - maximumChanceAngle);
                                    float chance = weapon.Extended.RicochetChance * metalMult * angleMult;

                                    if (Random() < chance) {
                                        hit.TexHit = texInfo;
                                        hit.Bounce = BounceType::Ricochet;
                                        hit.BounceAngle = ang;
                                    }
                                }
                            }
                            else {
                                hit.Bounce = BounceType::Standard;
                            }
                        }
                        else if (!HasFlag(obj.Physics.Flags, PhysicsFlag::Stick)) {
                            // Note that wall sliding is disabled when the object is touching the edge of a triangle.
                            // Edge sliding would cause objects to randomly bounce off at high speeds.
                            //SPDLOG_INFO("Sliding along wall, speed: {} vel: {}", hitSpeed, obj.Physics.Velocity.Length());
                            obj.Physics.Velocity += hitNormal * hitSpeed; // slide along wall
                            obj.Position = hitPoint + hitNormal * obj.Radius;
                        }                            

                        // apply friction so robots pinned against the wall don't spin in place
                        //if (obj.Type == ObjectType::Robot) {
                        //    obj.Physics.AngularAcceleration *= 0.5f;
                        //    //obj.Physics.Velocity *= 0.125f;
                        //}
                        //Debug::ClosestPoints.push_back(hitPoint);
                        //Render::Debug::DrawLine(hitPoint, hitPoint + hitNormal, { 1, 0, 0 });

                        if (hitDistance < hit.Distance) {
                            // Store the closest overall hit as the final hit
                            hit.Distance = hitDistance;
                            hit.Normal = hitNormal;
                            hit.Point = hitPoint;
                            hit.Tag = { segId, sideId };
                            hit.Tangent = tangent;
                            hit.EdgeDistance = edgeDistance;
                            hit.Tri = tri;
                            hit.Speed = hitSpeed;
                        }
                    }
                }
            }
        }

        //bool sticky = false;

        //if (obj.IsWeapon()) {
        //    // Sticky weapons shouldn't be repositioned to geometry surface
        //    auto& weapon = Resources::GetWeapon(obj);
        //    sticky = weapon.Extended.Sticky;
        //}

        //if (hits > 0 && !sticky) {
        //obj.Physics.Velocity += faceVel != Vector3::Zero ? faceVel / (float)hits : edgeVel / (float)hits;
        //obj.Physics.Velocity += slidingVel / (float)hits;
        //obj.Position = averagePosition / (float)hits;
        //}

        return hit && hit.Tag;
    }

    bool IntersectObjects(Level& level, Object& obj, ObjID id, span<SegID> pvs, LevelHit& hit, float dt) {
        // Did we hit any objects?
        for (auto& segId : pvs) {
            auto& seg = level.GetSegment(segId);

            for (int i = 0; i < seg.Objects.size(); i++) {
                auto other = level.TryGetObject(seg.Objects[i]);
                if (!other) continue;
                if (other->Signature == obj.Signature) continue; // don't hit yourself!

                if (id == other->Parent.Id) continue; // Don't hit your children!
                if (obj.Parent.Signature == other->Signature) continue; // Don't hit your parent!

                switch (ObjectCanHitTarget(obj, *other)) {
                    default:
                    case CollisionType::None: break;
                    case CollisionType::SphereRoom: break;
                    case CollisionType::SpherePoly:
                        if (auto info = IntersectSpherePoly(obj, *other, obj, dt)) {
                            hit.Update(info, other);
                            CollideObjects(hit, obj, *other, dt);
                        }
                        break;
                    case CollisionType::PolySphere:
                        // Reposition the other object, not this one while using the mesh from this object.
                        if (auto info = IntersectPolySphere(obj, *other, dt)) {
                            hit.Update(info, other);
                            CollideObjects(hit, *other, obj, dt);
                        }
                        break;

                    case CollisionType::SphereSphere: {
                        auto r1 = obj.Radius;
                        auto r2 = other->Radius;

                        // for robots their spheres are too large... apply multiplier. Having some overlap is okay.
                        if (obj.IsRobot() && other->IsRobot()) {
                            r1 *= 0.66f;
                            r2 *= 0.66f;
                        }

                        // Make powerups a consistent size regardless of their render size
                        if (obj.IsPowerup()) r1 = Game::POWERUP_RADIUS;
                        if (other->IsPowerup()) r2 = Game::POWERUP_RADIUS;

                        if (auto info = IntersectSphereSphere({ obj.Position, r1 }, { other->Position, r2 })) {
                            if (Game::GetState() == GameState::EscapeSequence) {
                                // Player destroys any robots that are in the way during escape!
                                if (obj.IsPlayer() && other->IsRobot()) DestroyObject(*other);
                                if (obj.IsRobot() && other->IsPlayer()) DestroyObject(obj);
                                break; // don't actually collide
                            }

                            hit.Update(info, other);

                            // Move players and robots when they collide with something
                            if ((obj.IsRobot() || obj.IsPlayer()) &&
                                (other->IsRobot() || other->IsPlayer())) {
                                auto nDotVel = info.Normal.Dot(obj.Physics.Velocity);
                                hit.Speed = abs(nDotVel);
                                obj.Physics.Velocity -= info.Normal * nDotVel; // slide along normal

                                obj.Position = info.Point + info.Normal * r1;
                            }

                            // Shove player when hit by weapons
                            if (obj.IsWeapon() && other->IsPlayer())
                                hit.Speed = (obj.Physics.Velocity - other->Physics.Velocity).Length();

                            CollideObjects(hit, obj, *other, dt);
                        }

                        break;
                    }
                }
            }
        }

        return hit.HitObj != nullptr;
    }

    // Finds the nearest sphere-level intersection for debris
    // Debris only collide with robots, players and walls
    bool IntersectLevelDebris(Level& level, const BoundingSphere& debris, const Vector3& prevPosition, SegID segId, LevelHit& hit) {
        auto& pvs = GetPotentialSegments(level, segId, debris.Center, debris.Radius * 2, Vector3::Zero, Game::TICK_RATE, ObjectType::None);

        // Did we hit any objects?
        for (auto& segment : pvs) {
            auto& seg = level.GetSegment(segment);

            for (int i = 0; i < seg.Objects.size(); i++) {
                auto other = level.TryGetObject(seg.Objects[i]);
                if (!other || !other->IsAlive() || other->Segment != segment) continue;
                if (other->Type != ObjectType::Player && other->Type != ObjectType::Robot && other->Type != ObjectType::Reactor)
                    continue;

                BoundingSphere sphere(other->Position, other->Radius);

                if (auto sphereHit = IntersectSphereSphere(debris, sphere)) {
                    hit.Distance = sphereHit.Distance;
                    hit.Normal = sphereHit.Normal;
                    hit.Point = sphereHit.Point;
                    return true;
                }
            }
        }

        Object dummyObj{};
        dummyObj.Position = debris.Center;
        dummyObj.PrevPosition = prevPosition;
        dummyObj.Radius = debris.Radius;
        dummyObj.Type = ObjectType::Debris;
        dummyObj.Physics.Velocity = Vector3(1, 1, 1);
        IntersectLevelMesh(level, dummyObj, pvs, hit);
        return (bool)hit;
    }

    void ScrapeWall(Object& obj, const LevelHit& hit, const LevelTexture& ti, float dt) {
        if (ti.HasFlag(TextureFlag::Volatile) || ti.HasFlag(TextureFlag::Water)) {
            if (ti.HasFlag(TextureFlag::Volatile)) {
                // todo: ignite the object if D3 enhanced
                auto damage = ti.Damage * dt;
                if (obj.IsPlayer()) {
                    if (Game::Difficulty == DifficultyLevel::Trainee)
                        damage *= 0.5f; // half damage on trainee

                    Game::Player.ApplyDamage(damage, false);
                }
                else {
                    obj.ApplyDamage(damage);
                }
            }

            static double lastScrapeTime = 0;

            if (Game::Time > lastScrapeTime + 0.25 || Game::Time < lastScrapeTime) {
                lastScrapeTime = Game::Time;

                auto soundId = ti.HasFlag(TextureFlag::Volatile) ? SoundID::TouchLava : SoundID::TouchWater;
                Sound::Play({ soundId }, hit.Point, hit.Tag.Segment);
            }

            obj.Physics.AngularVelocity.x = RandomN11() / 8; // -0.125 to 0.125
            obj.Physics.AngularVelocity.z = RandomN11() / 8;
            auto dir = hit.Normal;
            dir += RandomVector(1 / 8.0f);
            dir.Normalize();

            ApplyForce(obj, dir / 8.0f);
        }
    }

    // Applies damage and play a sound if object velocity changes suddenly
    void CheckForImpact(Object& obj, const LevelHit& hit, const LevelTexture* ti = nullptr) {
        constexpr float DAMAGE_SCALE = 128;
        constexpr float DAMAGE_THRESHOLD = 0.35f;
        auto deltaSpeed = obj.Physics.Velocity.Length() - obj.Physics.PrevVelocity.Length();
        bool isForceField = ti && ti->IsForceField();

        if (obj.IsPlayer() && deltaSpeed >= 10 && !isForceField)
            return; // Player sped up, don't create impact when moving away from object

        auto damage = hit.Speed / DAMAGE_SCALE;

        if (isForceField) {
            damage *= 8;
            if (obj.IsPlayer())
                Game::AddScreenFlash({ 0, 0, 1 });

            Sound::Play({ SoundID::PlayerHitForcefield }, hit.Point, obj.Segment);

            auto force = Vector3(RandomN11(), RandomN11(), RandomN11()) * 20;
            ApplyRotationForcePlayer(obj, force);
        }
        else if (damage > DAMAGE_THRESHOLD) {
            auto volume = isForceField ? 1 : std::clamp((hit.Speed - DAMAGE_SCALE * DAMAGE_THRESHOLD) / 20, 0.0f, 1.0f);

            if (volume > 0) {
                if (hit.PlayerHit())
                    AlertRobotsOfNoise(Game::GetPlayerObject(), Game::PLAYER_HIT_WALL_RADIUS, Game::PLAYER_HIT_WALL_NOISE, &Game::GetPlayerObject());

                Sound::Play({ SoundID::PlayerHitWall }, hit.Point, obj.Segment);
            }
        }

        //SPDLOG_INFO("{} wall hit damage: {}", obj.Signature, damage);

        if (damage > DAMAGE_THRESHOLD) {
            if (hit.PlayerHit()) {
                if (Game::Player.Shields > 10 || isForceField)
                    Game::Player.ApplyDamage(damage, false, false);
            }
            else {
                obj.ApplyDamage(damage);
            }
        }
    }

    void UpdatePhysics(Level& level, ObjID objId, float dt) {
        Debug::Steps = 0;
        Debug::ClosestPoints.clear();
        Debug::SegmentsChecked = 0;

        // At least two steps are necessary to prevent jitter in sharp corners (including against objects)
#ifdef _DEBUG
        constexpr int STEPS = 1;
#else
        constexpr int STEPS = 2;
#endif;

        dt /= STEPS;

        //for (int id = 0; id < level.Objects.size(); id++) {
        auto pobj = level.TryGetObject(objId);
        if (!pobj) return;
        auto& obj = *pobj;

        if (!obj.IsAlive() && obj.Type != ObjectType::Reactor) return;
        if (obj.Type == ObjectType::Player && obj.ID > 0) return; // singleplayer only
        if (obj.Movement != MovementType::Physics) {
            obj.PrevPosition = obj.Position;
            obj.PrevRotation = obj.Rotation;
            return;
        }

        for (int i = 0; i < STEPS; i++) {
            obj.PrevPosition = obj.Position;
            obj.PrevRotation = obj.Rotation;
            obj.Physics.PrevVelocity = obj.Physics.Velocity;
            ASSERT(IsNormalized(obj.Rotation.Forward()));

            PlayerPhysics(obj, dt);
            AngularPhysics(obj, dt);
            LinearPhysics(obj, dt);

            if (HasFlag(obj.Flags, ObjectFlag::Attached))
                continue; // don't test collision of objects attached to walls

            //if (id != 0) continue; // player only testing
            LevelHit hit{ .Source = &obj };
            LevelHit objectHit{ .Source = &obj };

            // Don't hit test objects that haven't moved unless they are weapons (mines don't move).
            // Also always hit-test player so bouncing powerups will get collected.
            if (obj.Physics.Velocity.LengthSquared() <= MIN_TRAVEL_DISTANCE && obj.Type != ObjectType::Weapon && obj.Type != ObjectType::Player)
                continue;

            // Use a larger radius for the object so the large objects in adjacent segments are found.
            // Needs testing against boss robots
            auto& pvs = GetPotentialSegments(level, obj.Segment, obj.Position, obj.Radius * 2, obj.Physics.Velocity, dt, obj.Type);

            auto hitObject = IntersectObjects(level, obj, objId, pvs, objectHit, dt);
            auto hitLevel = IntersectLevelMesh(level, obj, pvs, hit);

            if (hitObject && hitLevel) {
                if (objectHit.HitObj && objectHit.HitObj->Segment != obj.Segment)
                    hitObject = false; // level hit takes priority if hit object is in a different segment
                else
                    hitLevel = false; // hit the object so fast moving projectiles hit it
            }

            if (hitLevel) {
                if (obj.Type == ObjectType::Weapon)
                    WeaponHitWall(hit, obj, level, objId);

                if (auto wall = level.TryGetWall(hit.Tag))
                    HitWall(level, hit.Point, obj, *wall);

                const LevelTexture* ti = nullptr;
                if (auto side = level.TryGetSide(hit.Tag))
                    ti = &Resources::GetLevelTextureInfo(side->TMap);

                if (hit.Bounce != BounceType::None) {
                    obj.Physics.Velocity = Vector3::Reflect(obj.Physics.PrevVelocity, hit.Normal);
                    if (ti && ti->IsForceField())
                        obj.Physics.Velocity *= 1.5f;

                    // flip weapon to face the new direction
                    if (obj.Type == ObjectType::Weapon) {

                        if (hit.Bounce == BounceType::Ricochet) {
                            // Only random bounces receive deviation
                            constexpr float BASE_DEVIATION = 10; // Random ricochet angle. Should come from weapon info.
                            constexpr float ROUGHNESS_DEVIATION = 10; // extra amount by which shot can disperse at max roughness
                            constexpr float MIN_ROUGHNESS = 0.25;
                            constexpr float MAX_ROUGHNESS = 0.75;

                            auto& matInfo = Resources::GetMaterial(hit.TexHit.tex);
                            auto roughness = matInfo.Roughness;
                            float roughnessScale = 0;
                            if (roughness >= MAX_ROUGHNESS)
                                roughnessScale = 1;
                            else if (roughness > MIN_ROUGHNESS)
                                roughnessScale = (roughness - MIN_ROUGHNESS) / (MAX_ROUGHNESS - MIN_ROUGHNESS);

                            // Pick deviation direction - deviation towards the wall is proportionally less likely at shallower angles

                            auto spreadAngle = (BASE_DEVIATION + ROUGHNESS_DEVIATION * roughnessScale) * DegToRad;
                            auto spread = RandomPointInCircle(spreadAngle);
                            auto direction = obj.Physics.Velocity;
                            direction.Normalize();
                            direction += obj.Rotation.Right() * spread.x;
                            direction += obj.Rotation.Up() * spread.y;
                            direction.Normalize();
                            obj.Physics.Velocity = direction * obj.Physics.Velocity.Length();
                            obj.Rotation = Matrix3x3(direction, obj.Rotation.Up());
                        } else {
                            obj.Rotation = Matrix3x3(obj.Physics.Velocity, obj.Rotation.Up());
                        }
                    }

                    obj.Position += hit.Normal * 0.1f; // Move object off of collision surface
                    obj.Physics.Bounces--;
                }

                if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Robot) {
                    if (ti) {
                        if (ti->IsLiquid())
                            ScrapeWall(obj, hit, *ti, dt);
                        else
                            CheckForImpact(obj, hit, ti);
                    }
                    else {
                        CheckForImpact(obj, hit, nullptr);
                    }
                }
            }

            if (hitObject && objectHit.HitObj) {
                if (obj.Type == ObjectType::Weapon) {
                    Game::WeaponHitObject(objectHit, obj);
                }

                if (obj.Type == ObjectType::Player && objectHit.HitObj) {
                    Game::Player.TouchObject(*objectHit.HitObj);
                }

                if (obj.IsRobot() && objectHit.HitObj) {
                    RobotTouchObject(obj, *objectHit.HitObj);

                    if (objectHit.HitObj->IsPlayer() || objectHit.HitObj->IsRobot())
                        CheckForImpact(obj, objectHit);

                    // tumble robots rammed by the player
                    if (objectHit.HitObj->IsPlayer())
                        ApplyRandomRotationalForce(obj, objectHit.Point, objectHit.Normal * objectHit.Speed);
                }

                if (objectHit.HitObj->IsRobot()) {
                    RobotTouchObject(*objectHit.HitObj, obj);

                    if (obj.IsPlayer() || obj.IsRobot())
                        CheckForImpact(*objectHit.HitObj, objectHit);
                }
            }

            // Update object segment after physics is applied
            if (obj.Physics.Velocity.Length() * dt > MIN_TRAVEL_DISTANCE)
                MoveObject(level, obj);
        }

        if (objId == (ObjID)0) {
            Debug::ShipVelocity = obj.Physics.Velocity;
            Debug::ShipPosition = obj.Position;
            Debug::ShipThrust = obj.Physics.Thrust;
            PlotPhysics(Clock.GetTotalTimeSeconds(), obj.Physics);
        }

        ASSERT(IsNormalized(obj.Rotation.Forward()));
    }
}
