#include "pch.h"
#define NOMINMAX
#include "Physics.h"

#include "Game.AI.h"
#include "Physics.Math.h"
#include "Resources.h"
#include "Game.h"
#include "Game.Object.h"
#include "Graphics/Render.h"
#include "Input.h"
#include "Graphics/Render.Debug.h"
#include "SoundSystem.h"
#include "Editor/Events.h"
#include "Graphics/Render.Particles.h"
#include "Game.Wall.h"
#include "Intersect.h"
#include "Editor/Editor.Segment.h"

//#define DEBUG_OBJ_OUTLINE
// #define DEBUG_LEVEL_OUTLINE

using namespace DirectX;

namespace Inferno {
    // todo: move to extended object props
    constexpr auto PlayerTurnRollScale = FixToFloat(0x4ec4 / 2) * XM_2PI;
    constexpr auto PlayerTurnRollRate = FixToFloat(0x2000) * XM_2PI;

    namespace {
        IntersectContext Intersect(Game::Level);
    }

    // returns true if overlay was destroyed
    bool CheckDestroyableOverlay(Level& level, const Vector3& point, Tag tag, int tri, bool isPlayer) {
        tri = std::clamp(tri, 0, 1);

        auto seg = level.TryGetSegment(tag);
        if (!seg) return false;

        auto& side = seg->GetSide(tag.Side);
        if (side.TMap2 <= LevelTexID::Unset) return false;

        auto& tmi = Resources::GetLevelTextureInfo(side.TMap2);
        if (tmi.EffectClip == EClipID::None && tmi.DestroyedTexture == LevelTexID::None)
            return false;

        auto& eclip = Resources::GetEffectClip(tmi.EffectClip);
        if (eclip.OneShotTag) return false; // don't trigger from one-shot effects

        bool hasEClip = eclip.DestroyedTexture != LevelTexID::None || eclip.DestroyedEClip != EClipID::None;
        if (!hasEClip && tmi.DestroyedTexture == LevelTexID::None)
            return false;

        // Don't allow non-players to destroy triggers
        if (!isPlayer) {
            if (auto wall = level.TryGetWall(tag)) {
                if (wall->Trigger != TriggerID::None)
                    return false;
            }
        }

        auto face = Face2::FromSide(level, *seg, tag.Side);
        auto uv = IntersectFaceUVs(point, face, tri);

        auto& bitmap = Resources::GetBitmap(Resources::LookupTexID(side.TMap2));
        auto& info = bitmap.Info;
        auto x = uint(uv.x * info.Width) % info.Width;
        auto y = uint(uv.y * info.Height) % info.Height;
        FixOverlayRotation(x, y, info.Width, info.Height, side.OverlayRotation);

        if (!bitmap.Mask.empty() && bitmap.Mask[y * info.Width + x] == Palette::SUPER_MASK)
            return false; // portion hit was supertransparent

        if (bitmap.Data[y * info.Width + x].a == 0)
            return false; // portion hit was transparent

        // Hit opaque overlay!
        //Inferno::SubtractLight(level, tag, *seg);

        bool usedEClip = false;

        if (eclip.DestroyedEClip != EClipID::None) {
            // Hack storing exploding side state into the global effect.
            // The original game did this, but should be replaced with a more robust system.
            // If more than one monitor breaks with different times the animation wouldn't play properly.
            if (Seq::inRange(Resources::GameData.Effects, (int)eclip.DestroyedEClip)) {
                auto& destroyed = Resources::GameData.Effects[(int)eclip.DestroyedEClip];
                if (!destroyed.OneShotTag) {
                    side.TMap2 = Resources::LookupLevelTexID(destroyed.VClip.Frames[0]);
                    destroyed.TimeLeft = destroyed.VClip.PlayTime;
                    destroyed.OneShotTag = tag;
                    destroyed.DestroyedTexture = eclip.DestroyedTexture;
                    usedEClip = true;
                    Render::LoadTextureDynamic(eclip.DestroyedTexture);
                    Render::LoadTextureDynamic(side.TMap2);
                }
            }
        }

        if (!usedEClip) {
            side.TMap2 = hasEClip ? eclip.DestroyedTexture : tmi.DestroyedTexture;
            Render::LoadTextureDynamic(side.TMap2);
        }

        Editor::Events::LevelChanged();

        //Render::ExplosionInfo ei;
        //ei.Clip = hasEClip ? eclip.DestroyedVClip : VClipID::LightExplosion;
        //ei.MinRadius = ei.MaxRadius = hasEClip ? eclip.ExplosionSize : 20.0f;
        //ei.FadeTime = 0.25f;
        //ei.Position = point;
        //ei.Segment = tag.Segment;
        //Render::CreateExplosion(ei);
        if (auto e = Render::EffectLibrary.GetSparks("overlay_destroyed")) {
            e->Direction = side.AverageNormal;
            e->Up = side.Tangents[0];
            auto position = point + side.AverageNormal * 0.1f;
            Render::AddSparkEmitter(*e, tag.Segment, position);
        }

        auto& vclip = Resources::GetVideoClip(eclip.DestroyedVClip);
        auto soundId = vclip.Sound != SoundID::None ? vclip.Sound : SoundID::LightDestroyed;
        Sound3D sound({ soundId }, point, tag.Segment);
        Sound::Play(sound);

        if (auto trigger = level.TryGetTrigger(side.Wall)) {
            SPDLOG_INFO("Activating switch {}:{}\n", tag.Segment, tag.Side);
            //fmt::print("Activating switch {}:{}\n", tag.Segment, tag.Side);
            ActivateTrigger(level, *trigger, tag);
        }

        return true; // was destroyed!
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
            TurnRoll(obj.Physics, PlayerTurnRollScale, PlayerTurnRollRate, dt);

            // re-rotate object for bank caused by turn
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(-pd.TurnRoll) * obj.Rotation);
        }

        obj.Rotation.Normalize();
    }

    void LinearPhysics(Object& obj, float dt) {
        auto& pd = obj.Physics;
        const auto stepScale = dt / Game::TICK_RATE;

        Weapon* weapon = obj.IsWeapon() ? &Resources::GetWeapon(obj) : nullptr;

        if (HasFlag(obj.Physics.Flags, PhysicsFlag::Gravity))
            pd.Velocity += Game::Gravity * dt;

        // Apply weapon thrust
        if (weapon && weapon->Thrust != 0)
            pd.Thrust = obj.Rotation.Forward() * weapon->Thrust * dt;

        if (pd.Velocity == Vector3::Zero && pd.Thrust == Vector3::Zero)
            return;

        if (pd.Thrust != Vector3::Zero && pd.Mass > 0)
            pd.Velocity += pd.Thrust / pd.Mass * stepScale; // acceleration

        if (pd.Drag > 0)
            pd.Velocity *= 1 - pd.Drag * stepScale;

        // Cap the max speed of weapons with thrust
        if (weapon && weapon->Thrust != 0) {
            auto maxSpeed = weapon->Speed[Game::Difficulty];
            if (pd.Velocity.Length() > maxSpeed) {
                Vector3 dir;
                pd.Velocity.Normalize(dir);
                pd.Velocity = dir * maxSpeed;
            }
        }

        obj.Position += pd.Velocity * dt;

        if (obj.Physics.Wiggle > 0) {
            //auto offset = (float)obj.Signature * 0.8191f; // random offset to keep objects from wiggling at same time
            //WiggleObject(obj, t + offset, dt, obj.Physics.Wiggle, obj.Physics.WiggleRate);
        }
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

    // Applies wiggle to an object
    void WiggleObject(Object& obj, double t, float dt, float amplitude, float rate) {
        auto angle = std::sinf((float)t * XM_2PI * rate) * 20; // multiplier tweaked to cause 0.5 units of movement at a 1/64 tick rate
        auto wiggle = obj.Rotation.Up() * angle * amplitude * dt;
        obj.Physics.Velocity += wiggle;
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

    List<SegID>& GetPotentialSegments(Level& level, SegID start, const Vector3& point, float radius, const Vector3& velocity, float /*dt*/) {
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

            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);

                // Don't hit the other side of walls. Note that projectiles will still pass through transparent pixels.
                if (level.TryGetWall(side.Wall))
                    continue;

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
                    if (conn != SegID::None && !Seq::contains(g_VisitedStack, conn)) {
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

        if (src.Type == ObjectType::Player && target.Type == ObjectType::Weapon) {
            // Player can't hit mines until they arm
            if (WeaponIsMine((WeaponID)target.ID) && target.Control.Weapon.AliveTime < Game::MINE_ARM_TIME)
                return CollisionType::None;

            //return target.ID == (int)WeaponID::LevelMine
        }

        if ((src.IsPlayer() && target.IsRobot() && HasFlag(target.Physics.Flags, PhysicsFlag::SphereCollidePlayer)) ||
            (src.IsRobot() && target.IsPlayer() && HasFlag(src.Physics.Flags, PhysicsFlag::SphereCollidePlayer)))
            return CollisionType::SphereSphere;

        if (src.Type == ObjectType::Weapon) {
            if (Seq::contains(src.Control.Weapon.RecentHits, target.Signature))
                return CollisionType::None; // Don't hit objects recently hit by this weapon (for piercing)

            switch (target.Type) {
                case ObjectType::Robot:
                {
                    auto targetId = Game::GetObjectRef(target);
                    if (src.Parent == targetId) return CollisionType::None; // Don't hit robot with their own shots

                    auto& ri = Resources::GetRobotInfo(target.ID);
                    if (ri.IsCompanion)
                        return CollisionType::None; // weapons can't directly hit guidebots
                    break;
                }
                case ObjectType::Player:
                {
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

    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent) {
        auto dir = b.Position - a.Position;
        auto dist = dir.Length();
        dir.Normalize();
        Ray ray(a.Position, dir);
        LevelHit hit;
        RayQuery query{ .MaxDistance = dist, .Start = a.Segment, .PassTransparent = passTransparent, .TestTextures = true };
        return Intersect.RayLevel(ray, query, hit);
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

    void ApplyRotationalForce(Object& object, const Vector3& hitPoint, Vector3 force, float targetMass) {
        Matrix basis(object.Rotation);
        basis = basis.Invert();
        force = Vector3::Transform(force, basis); // transform force to basis of object

        auto arm = Vector3::Transform(hitPoint - object.Position, basis);
        const auto torque = force.Cross(arm);
        // moment of inertia. solid sphere I = 2/5 MR^2. Thin shell: 2/3 MR^2
        const auto inertia = 1.0f / 6.0f * targetMass * object.Radius * object.Radius;
        auto accel = torque / inertia;
        object.Physics.AngularAcceleration += accel;
    }

    // Creates an explosion that can cause damage or knockback
    void CreateExplosion(Level& level, const Object* source, const GameExplosion& explosion) {
        ASSERT(explosion.Room != RoomID::None);
        ASSERT(explosion.Segment != SegID::None);

        auto roomAction = [&](const Room& room) {
            for (auto& segId : room.Segments) {
                auto& seg = level.GetSegment(segId);
                for (auto& objId : seg.Objects) {
                    auto obj = level.TryGetObject(objId);
                    if (!obj) continue;
                    auto& target = *obj;

                    if (&target == source) continue;
                    if (!target.IsAlive()) continue;

                    if (target.Type == ObjectType::Weapon && (target.ID != (int)WeaponID::ProxMine && target.ID != (int)WeaponID::SmartMine && target.ID != (int)WeaponID::LevelMine))
                        continue; // only allow explosions to affect weapons that are mines

                    // ((obj0p->type==OBJ_ROBOT) && ((Objects[parent].type != OBJ_ROBOT) || (Objects[parent].id != obj0p->id)))
                    //if (&level.GetObject(obj.Parent) == &source) continue; // don't hit your parent

                    if (target.Type != ObjectType::Player && target.Type != ObjectType::Robot && target.Type != ObjectType::Weapon && target.Type != ObjectType::Reactor)
                        continue;

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
                    RayQuery query{ .MaxDistance = dist, .Start = explosion.Segment, .PassTransparent = true, .TestTextures = true };
                    if (Intersect.RayLevel(ray, query, hit))
                        continue;

                    // linear damage falloff
                    float damage = explosion.Damage - (dist * explosion.Damage) / explosion.Radius;
                    float force = explosion.Force - (dist * explosion.Force) / explosion.Radius;

                    dir += RandomVector(0.25f);
                    dir.Normalize();

                    Vector3 forceVec = dir * force;
                    //auto hitPos = (source.Position - obj.Position) * obj.Radius / (obj.Radius + dist);

                    // Find where the point of impact is... ( pos_hit )
                    //vm_vec_scale(vm_vec_sub(&pos_hit, &obj->pos, &obj0p->pos), fixdiv(obj0p->size, obj0p->size + dist));

                    switch (target.Type) {
                        case ObjectType::Weapon:
                        {
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

                        case ObjectType::Robot:
                        {
                            float stunMult = 1;
                            if (source && source->IsWeapon()) {
                                auto& weapon = Resources::GetWeapon(WeaponID(source->ID));
                                stunMult = weapon.Extended.StunMult;
                            }

                            ApplyForce(target, forceVec);
                            auto parent = source ? Game::Level.TryGetObject(source->Parent) : nullptr;
                            bool srcIsPlayer = parent ? parent->IsPlayer() : false;
                            DamageRobot(explosion.Position, srcIsPlayer, target, damage, stunMult);

                            target.LastHitForce = forceVec;
                            //fmt::print("applied {} splash damage at dist {}\n", damage, dist);

                            // todo: guidebot ouchies

                            //Vector3 negForce = forceVec * 2.0f * float(7 - Game::Difficulty) / 8.0f;
                            // Don't apply rotation if source directly hit this object, so that it doesn't rotate oddly
                            if (!source || source->LastHitObject != target.Signature) {
                                auto pt = RandomPointOnCircle(target.Radius);
                                auto edgePt = Vector3::Transform(pt, target.GetTransform());
                                auto edgeDir = edgePt - hit.Point;
                                edgeDir.Normalize();
                                auto& robot = Resources::GetRobotInfo(target);
                                ApplyRotationalForce(target, edgePt, forceVec, robot.Mass);
                            }
                            break;
                        }

                        case ObjectType::Reactor:
                        {
                            // apply damage if source is player
                            if (!Settings::Cheats.DisableWeaponDamage && source && source->Parent.Id == ObjID(0))
                                target.ApplyDamage(damage);

                            break;
                        }

                        case ObjectType::Player:
                        {
                            ApplyForce(target, forceVec);
                            if (!source || source->LastHitObject != target.Signature)
                                ApplyRotation(target, forceVec);

                            // Quarter damage explosions on trainee
                            if (Game::Difficulty == 0) damage /= 4;
                            Game::Player.ApplyDamage(damage, false);
                            break;
                        }

                        default:
                            throw Exception("Invalid object type in CreateExplosion()");
                    }
                }
            }

            return false;
        };

        Game::TraverseRoomsByDistance(level, explosion.Room, explosion.Position,
                                      explosion.Radius * 2, false, roomAction);
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

        //SPDLOG_INFO("{}-{} impact speed: {}", a.Signature, b.Signature, hit.Speed);

        if (target.Type == ObjectType::Powerup || target.Type == ObjectType::Marker)
            return;

        if (obj.Type != ObjectType::Weapon && target.Type != ObjectType::Weapon) { }

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
                speed += weapon.Damage[Game::Difficulty] * 4; // Damage equals force

            // Use projectile velocity as hit normal so torque is applied reliably
            obj.Physics.Velocity.Normalize(normal);
        }

        auto force = normal * speed * m1 / m2;

        //const float resitution = obj.Type == ObjectType::Player ? 0.2f : 0.2f;
        constexpr float resitution = 0.4f;
        target.Physics.Velocity += force * resitution;
        target.LastHitForce = force * resitution;

        // Only apply rotational velocity when something hits a robot. Feels bad if a player being hit loses aim.
        if (/*a.Type == ObjectType::Weapon &&*/ target.Type == ObjectType::Robot) {
            //if (obj.Type == ObjectType::Weapon) force *= 2; // make weapon hits apply more rotation force
            //if (obj.Type == ObjectType::Player) force *= 0.25f; // Less rotation from players
            ApplyRotationalForce(target, hit.Point, force, m2);
        }
    }


    // Performs intersection checks between an object's sphere and another object's model mesh.
    // Target is repositioned based on the intersections.
    HitInfo IntersectSpherePoly(const Object& sphereSource, const Object& meshSource, Object& target, float dt) {
        if (meshSource.Render.Type != RenderType::Model) return {};
        auto& model = Resources::GetModel(meshSource.Render.Model.ID);

        const float speed = sphereSource.Physics.Velocity.Length();
        const float travelDist = speed * dt;
        const bool needsRaycast = travelDist > sphereSource.Radius /** 1.5f*/;
        Vector3 direction;
        sphereSource.Physics.Velocity.Normalize(direction);

        const auto objDistance = Vector3::Distance(sphereSource.Position, meshSource.Position);
        const auto radii = sphereSource.Radius + meshSource.Radius;

        if (needsRaycast) {
            // Add both radii together to ensure the ray doesn't miss the bounds
            BoundingSphere sphere(meshSource.Position, radii);
            Ray pathRay(sphereSource.Position, direction);
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
        const auto localPos = Vector3::Transform(sphereSource.Position, invTransform);
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

                    bool triFacesObj = localDir.Dot(normal) <= 0;
                    auto offset = normal * sphereSource.Radius; // offset triangle by radius to account for object size
                    Vector3 faceLocalPos = localPos;

                    if (needsRaycast && triFacesObj) {
                        float dist{};
                        Plane basePlane(p0, p1, p2);

                        if (ray.Intersects(p0, p1, p2, dist) && dist < travelDist) {
                            // Move object to intersection of triangle and proceed
                            faceLocalPos += localDir * (dist - sphereSource.Radius);
                        }
                        else if (ray.Intersects(basePlane, dist) && dist < travelDist) {
                            // Move object to intersection of plane and proceed
                            // This allows the radius of raycasted projectiles to have effect
                            faceLocalPos += localDir * dist;
                        }
                    }

                    Plane plane(p0 + offset, p1 + offset, p2 + offset);
                    auto planeDist = -plane.DotCoordinate(faceLocalPos); // flipped winding
                    if (planeDist > 0 || planeDist < -sphereSource.Radius)
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
        averageHitDistance /= hits;

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

        if (sphereSource.Type == ObjectType::Weapon) {
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

        for (auto& sideId : SideIDs) {
            if (!seg.SideIsSolid(sideId, level)) continue;
            if (Settings::Cheats.DisableWallCollision && seg.GetSide(sideId).HasWall()) continue;
            auto& side = seg.GetSide(sideId);
            auto face = Face2::FromSide(level, seg, sideId);
            auto& indices = side.GetRenderIndices();
            float edgeDistance = 0; // 0 for edge tests

            // Check the position against each triangle
            for (int tri = 0; tri < 2; tri++) {
                Vector3 tangent = face.Side->Tangents[tri];
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

    void IntersectLevelMesh(Level& level, Object& obj, span<SegID> pvs, LevelHit& hit, float dt) {
        Vector3 averagePosition;
        int hits = 0;
        auto speed = obj.Physics.Velocity.Length();
        float travelDistance = speed * dt;

        Vector3 direction;
        obj.Physics.Velocity.Normalize(direction);
        if (IsZero(direction)) direction = Vector3::UnitY;
        Ray pathRay(obj.Position, direction);

        for (auto& segId : pvs) {
            Debug::SegmentsChecked++;
            auto& seg = level.Segments[(int)segId];

            for (auto& sideId : SideIDs) {
                if (!seg.SideIsSolid(sideId, level)) continue;
                auto& side = seg.GetSide(sideId);
                if (Settings::Cheats.DisableWallCollision && side.HasWall()) continue;
                auto face = Face2::FromSide(level, seg, sideId);
                auto& indices = side.GetRenderIndices();
                float edgeDistance = 0; // 0 for edge tests

                // Check the position against each triangle
                for (int tri = 0; tri < 2; tri++) {
                    Vector3 tangent = face.Side->Tangents[tri];
                    // Offset the triangle by the object radius and then do a point-triangle intersection.
                    // This leaves space at the edges to do capsule intersection checks.
                    const auto offset = side.Normals[tri] * obj.Radius;
                    const Vector3 p0 = face[indices[tri * 3 + 0]];
                    const Vector3 p1 = face[indices[tri * 3 + 1]];
                    const Vector3 p2 = face[indices[tri * 3 + 2]];

                    auto objDir = obj.PrevPosition - side.Centers[tri];
                    objDir.Normalize();

                    bool triFacesObj = objDir.Dot(side.Normals[tri]);

                    float hitDistance = FLT_MAX;
                    Vector3 hitPoint, hitNormal;

#ifdef DEBUG_LEVEL_OUTLINE
                    Render::Debug::DrawLine(p0, p1, { 0, 1, 0 });
                    Render::Debug::DrawLine(p1, p2, { 0, 1, 0 });
                    Render::Debug::DrawLine(p2, p0, { 0, 1, 0 });
#endif
                    // a size 4 object would need a velocity > 250 to clip through walls
                    if (obj.Type == ObjectType::Weapon && HasFlag(obj.Physics.Flags, PhysicsFlag::PointCollideWalls)) {
                        // Use raycasting for weapons because they are typically small and have high velocities
                        float dist;
                        if (triFacesObj &&
                            pathRay.Intersects(p0, p1, p2, dist) &&
                            dist < travelDistance) {
                            hitPoint = obj.PrevPosition + direction * dist;
                            if (WallPointIsTransparent(hitPoint, face, tri))
                                continue; // skip projectiles that hit transparent part of a wall

                            // move the object to the surface and proceed as normal
                            obj.Position = hitPoint - direction * obj.Radius;
                            hitNormal = side.Normals[tri];
                            hitDistance = dist;
                            edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
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
                            }
                        }
                    }

                    float hitSpeed = 0;
                    if (hitDistance < -.5f && hitDistance > -obj.Radius) {
                        //if (obj.Type != ObjectType::Debris)
                        //    SPDLOG_WARN("Moved object {} to wall surface", Game::GetObjectRef(obj).Id);
                        obj.Position = hit.Point + hit.Normal * obj.Radius;
                    }

                    if (hitDistance < obj.Radius + 0.001f) {
                        // Check if hit is transparent (duplicate check due to triangle edges)
                        if (obj.Type == ObjectType::Weapon && WallPointIsTransparent(hitPoint, face, tri))
                            continue; // skip projectiles that hit transparent part of a wall

                        hitSpeed = abs(hitNormal.Dot(obj.Physics.Velocity));
                        auto& ti = Resources::GetLevelTextureInfo(side.TMap);

                        if (obj.Physics.CanBounce() || ti.HasFlag(TextureFlag::ForceField)) {
                            hit.Bounced = true;
                            // bounce velocity is handled after all hits are resolved so that overlapping
                            // triangle edges don't double the effect
                        }
                        else if (!HasFlag(obj.Physics.Flags, PhysicsFlag::Piercing) && !HasFlag(obj.Physics.Flags, PhysicsFlag::Stick)) {
                            obj.Physics.Velocity += hitNormal * hitSpeed; // slide along wall
                            //SPDLOG_INFO("Sliding along wall, speed: {}", hitSpeed);
                        }

                        averagePosition += hitPoint + hitNormal * obj.Radius;
                        hits++;

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

        bool sticky = false;

        if (obj.IsWeapon()) {
            // Sticky weapons shouldn't be repositioned to geometry surface
            auto& weapon = Resources::GetWeapon(obj);
            sticky = weapon.Extended.Sticky;
        }

        if (hits > 0 && !sticky)
            obj.Position = averagePosition / (float)hits;
    }

    bool IntersectLevel(Level& level, Object& obj, ObjID id, LevelHit& hit, float dt) {
        // Don't hit test objects that haven't moved unless they are the player
        // This is so moving powerups are tested against the player
        if (obj.Physics.Velocity.LengthSquared() <= MIN_TRAVEL_DISTANCE /*&& obj.Type != ObjectType::Player*/) return false;
        //Vector3 direction;
        //obj.Physics.Velocity.Normalize(direction);
        //Ray pathRay(obj.PrevPosition, direction);

        // Use a larger radius for the object so the large objects in adjacent segments are found.
        // Needs testing against boss robots
        auto& pvs = GetPotentialSegments(level, obj.Segment, obj.Position, obj.Radius * 2, obj.Physics.Velocity, dt);

        // Did we hit any objects?
        for (auto& segId : pvs) {
            auto& seg = level.GetSegment(segId);

            for (int i = 0; i < seg.Objects.size(); i++) {
                if (id == seg.Objects[i]) continue; // don't hit yourself!
                auto other = level.TryGetObject(seg.Objects[i]);
                if (!other) continue;

                auto pSrc = &obj;
                if (obj.Type == ObjectType::Player && other->Type == ObjectType::Weapon)
                    std::swap(pSrc, other);

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

                    case CollisionType::SphereSphere:
                    {
                        // for robots their spheres are too large... apply multiplier. Having some overlap is okay.
                        auto radiusMult = obj.Type == ObjectType::Robot && other->Type == ObjectType::Robot ? 0.66f : 1.0f;
                        BoundingSphere sphereA(obj.Position, obj.Radius * radiusMult);
                        BoundingSphere sphereB(other->Position, other->Radius * radiusMult);

                        if (auto info = IntersectSphereSphere(sphereA, sphereB)) {
                            hit.Update(info, other);

                            // Move players and robots when they collide with something
                            if ((obj.Type == ObjectType::Robot || obj.Type == ObjectType::Player) &&
                                (other->Type == ObjectType::Robot || other->Type == ObjectType::Player)) {
                                auto nDotVel = info.Normal.Dot(obj.Physics.Velocity);
                                obj.Physics.Velocity -= info.Normal * nDotVel; // slide along normal
                                hit.Speed = obj.Physics.Velocity.Length();
                                obj.Position = info.Point + info.Normal * obj.Radius * radiusMult;
                                //obj.Physics.Velocity += info.Normal * hitSpeed;
                            }


                            // Shove player when hit by weapons
                            if (obj.Type == ObjectType::Weapon && other->Type == ObjectType::Player)
                                hit.Speed = (obj.Physics.Velocity + other->Physics.Velocity).Length();

                            CollideObjects(hit, obj, *other, dt);
                        }
                        break;
                    }
                }
            }
        }

        IntersectLevelMesh(level, obj, pvs, hit, dt);
        return (bool)hit;
    }


    // Finds the nearest sphere-level intersection for debris
    // Debris only collide with robots, players and walls
    bool IntersectLevelDebris(Level& level, const BoundingSphere& debris, SegID segId, LevelHit& hit) {
        auto& pvs = GetPotentialSegments(level, segId, debris.Center, debris.Radius * 2, Vector3::Zero, Game::TICK_RATE);

        // Did we hit any objects?
        for (auto& segment : pvs) {
            auto& seg = level.GetSegment(segment);

            for (int i = 0; i < seg.Objects.size(); i++) {
                auto other = level.TryGetObject(seg.Objects[i]);

                if (!other->IsAlive() || other->Segment != segment) continue;
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
        dummyObj.Radius = debris.Radius;
        dummyObj.Type = ObjectType::Debris;
        IntersectLevelMesh(level, dummyObj, pvs, hit, Game::TICK_RATE);
        return (bool)hit;
    }


    void BumpObject(Object& obj, Vector3 hitDir, float damage) {
        hitDir *= damage;
        ApplyForce(obj, hitDir);
    }

    void ScrapeWall(Object& obj, const LevelHit& hit, const LevelTexture& ti, float dt) {
        if (ti.HasFlag(TextureFlag::Volatile) || ti.HasFlag(TextureFlag::Water)) {
            if (ti.HasFlag(TextureFlag::Volatile)) {
                // todo: ignite the object if D3 enhanced
                auto damage = ti.Damage * dt;
                if (obj.IsPlayer()) {
                    if (Game::Difficulty == 0) damage *= 0.5f; // half damage on trainee
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
                Sound3D sound({ soundId }, hit.Point, hit.Tag.Segment);
                Sound::Play(sound);
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
    void CheckForImpact(Object& obj, const LevelHit& hit, const LevelTexture* ti) {
        constexpr float DAMAGE_SCALE = 128;
        constexpr float DAMAGE_THRESHOLD = 1 / 3.0f;
        auto speed = obj.Physics.Velocity.Length() - obj.Physics.PrevVelocity.Length();
        bool isForceField = ti && ti->IsForceField();
        if (speed > 0 && !isForceField) return; // Object sped up
        speed = abs(speed);

        auto damage = speed / DAMAGE_SCALE;

        if (isForceField) {
            damage *= 8;
            if (obj.IsPlayer())
                Game::AddScreenFlash({ 0, 0, 1 });

            Sound3D sound({ SoundID::PlayerHitForcefield }, hit.Point, obj.Segment);
            Sound::Play(sound);

            auto force = Vector3(RandomN11(), RandomN11(), RandomN11()) * 20;
            ApplyRotation(obj, force);
        }
        else if (damage > DAMAGE_THRESHOLD) {
            auto volume = isForceField ? 1 : std::clamp((speed - DAMAGE_SCALE * DAMAGE_THRESHOLD) / 20, 0.0f, 1.0f);

            if (volume > 0) {
                if (obj.IsPlayer())
                    AlertEnemiesOfNoise(Game::GetPlayerObject(), Game::PLAYER_HIT_WALL_RADIUS, Game::PLAYER_HIT_WALL_NOISE);

                Sound3D sound({ SoundID::PlayerHitWall }, hit.Point, obj.Segment);
                Sound::Play(sound);
            }
        }

        //SPDLOG_INFO("{} wall hit damage: {}", obj.Signature, damage);

        if (damage > DAMAGE_THRESHOLD) {
            if (obj.Type == ObjectType::Player) {
                if (obj.HitPoints > 10 || isForceField)
                    Game::Player.ApplyDamage(damage, false);
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
        constexpr int STEPS = 2;
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

            if (obj.Physics.Velocity.Length() * dt > MIN_TRAVEL_DISTANCE)
                MoveObject(level, obj);

            if (HasFlag(obj.Flags, ObjectFlag::Attached))
                continue; // don't test collision of attached objects

            //if (id != 0) continue; // player only testing
            LevelHit hit{ .Source = &obj };

            if (IntersectLevel(level, obj, objId, hit, dt)) {
                if (obj.Type == ObjectType::Weapon) {
                    if (hit.HitObj) {
                        Game::WeaponHitObject(hit, obj);
                    }
                    else {
                        Game::WeaponHitWall(hit, obj, level, objId);
                    }
                }

                if (auto wall = level.TryGetWall(hit.Tag)) {
                    HitWall(level, hit.Point, obj, *wall);
                }

                if (obj.Type == ObjectType::Player && hit.HitObj) {
                    Game::Player.TouchObject(*hit.HitObj);
                }

                const LevelTexture* ti = nullptr;
                if (auto side = level.TryGetSide(hit.Tag))
                    ti = &Resources::GetLevelTextureInfo(side->TMap);

                if (hit.Bounced) {
                    obj.Physics.Velocity = Vector3::Reflect(obj.Physics.PrevVelocity, hit.Normal);
                    if (ti && ti->IsForceField())
                        obj.Physics.Velocity *= 1.5f;

                    // flip weapon to face the new direction
                    if (obj.Type == ObjectType::Weapon)
                        obj.Rotation = Matrix3x3(obj.Physics.Velocity, obj.Rotation.Up());

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
        }

        if (objId == (ObjID)0) {
            Debug::ShipVelocity = obj.Physics.Velocity;
            Debug::ShipPosition = obj.Position;
            Debug::ShipThrust = obj.Physics.Thrust;
            PlotPhysics(Clock.GetTotalTimeSeconds(), obj.Physics);
        }

        assert(IsNormalized(obj.Rotation.Forward()));
    }
}
