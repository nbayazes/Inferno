#include "pch.h"

#include "Types.h"
#include "Game.AI.h"
#include "Game.AI.Pathing.h"
#include "Game.Boss.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Reactor.h"
#include "Resources.h"
#include "Physics.h"
#include "logging.h"
#include "SoundSystem.h"
#include "Editor/Editor.Selection.h"
#include "Graphics.Debug.h"
#include "VisualEffects.h"

namespace Inferno {
    namespace {
        List<AIRuntime> RuntimeState;
        IntersectContext Intersect(Game::Level);

        uint DronesInCombat = 0, DronesInCombatCounter = 0;
        uint FleeingDrones = 0, FleeingDronesCounter = 0;

        constexpr float AI_DODGE_TIME = 0.5f; // Time to dodge a projectile. Should probably scale based on mass.
        constexpr float AI_MAX_DODGE_DISTANCE = 60; // Range at which projectiles are dodged
        constexpr float DEATH_SOUND_DURATION = 2.68f;
        constexpr float AI_SOUND_RADIUS = 300.0f; // Radius for combat sounds

        constexpr float AI_AWARENESS_DECAY = 1 / 5.0f; // Awareness lost per second

        constexpr float MAX_SLOW_TIME = 2.0f; // Max duration of slow
        constexpr float MAX_SLOW_EFFECT = 0.9f; // Max percentage of slow to apply to a robot
        constexpr float MAX_SLOW_THRESHOLD = 0.4f; // Percentage of life dealt to reach max slow

        constexpr float STUN_THRESHOLD = 27.5; // Minimum damage to stun a robot. Concussion is 30 damage.
        constexpr float MAX_STUN_PERCENT = 0.6f; // Percentage of life required in one hit to reach max stun time
        constexpr float MAX_STUN_TIME = 1.5f; // max stun in seconds
        constexpr float MIN_STUN_TIME = 0.25f; // min stun in seconds. Stuns under this duration are discarded.
    }

    template <typename... Args>
    void Chat(const Object& robot, const string_view fmt, Args&... args) {
        string message = fmt::vformat(fmt, fmt::make_format_args(args...));
        fmt::println("{:6.2f}  DRONE {}: {}", Game::Time, robot.Signature, message);
    }

    void ResetAI() {
        for (auto& ai : RuntimeState)
            ai = {};

        Game::InitBoss();
    }

    void ResetAITargets() {
        for (auto& ai : RuntimeState) {
            ai.Target = {};
            ai.TargetPosition = {};
        }
    }

    void ResizeAI(size_t size) {
        if (size + 10 >= RuntimeState.capacity()) {
            size += 50;
            SPDLOG_INFO("Resizing AI state");
        }

        if (size > RuntimeState.capacity())
            RuntimeState.resize(size);
    }

    AIRuntime& GetAI(const Object& obj) {
        ASSERT(obj.IsRobot());
        auto ref = Game::GetObjectRef(obj);
        ASSERT(Seq::inRange(RuntimeState, (int)ref.Id));
        return RuntimeState[(int)ref.Id];
    }

    const RobotDifficultyInfo& DifficultyInfo(const RobotInfo& info) {
        return info.Difficulty[(int)Game::Difficulty];
    }

    uint CountNearbyAllies(const Object& robot, float range, bool inCombat = false) {
        uint allies = 0;
        auto range2 = range * range;

        IterateNearbySegments(Game::Level, robot, range, TraversalFlag::StopDoor | TraversalFlag::PassOpenDoors, [&](const Segment& seg, bool) {
            for (auto& objid : seg.Objects) {
                if (auto obj = Game::Level.TryGetObject(objid)) {
                    if (obj->IsRobot() && obj->Signature != robot.Signature) {
                        if (Vector3::DistanceSquared(obj->Position, robot.Position) > range2)
                            continue;

                        if (inCombat) {
                            if (GetAI(*obj).State == AIState::Combat)
                                allies++;
                        }
                        else {
                            allies++;
                        }
                    }
                }
            }
        });

        return allies;
    }

    // Returns true if able to reach the target
    bool ChaseTarget(AIRuntime& ai, const Object& robot, const NavPoint& target, ChaseMode chase, float maxDist = AI_MAX_CHASE_DISTANCE) {
        ai.PathDelay = 0;
        ai.TargetPosition = target;
        if (SetPathGoal(Game::Level, robot, ai, target, maxDist)) {
            ai.State = AIState::Chase;
            ai.Chase = chase;
            Chat(robot, "Chase path found");
            return true;
        }

        return false;
    }

    void ForNearbyRobots(RoomID startRoom, const Vector3& position, float radius, const std::function<void(Object&)>& action) {
        radius = radius * radius;

        TraverseRoomsByDistance(Game::Level, startRoom, position, radius, true, [&](const Room& room) {
            for (auto& segid : room.Segments) {
                if (auto seg = Game::Level.TryGetSegment(segid)) {
                    for (auto& objid : seg->Objects) {
                        if (auto object = Game::Level.TryGetObject(objid)) {
                            if (!object->IsRobot()) continue;
                            if (Vector3::DistanceSquared(position, object->Position) > radius)
                                continue;
                            action(*object);
                        }
                    }
                }
            }

            return true;
        });
    }

    void PlayAlertSound(const Object& robot, AIRuntime& ai) {
        auto& robotInfo = Resources::GetRobotInfo(robot);
        if (robotInfo.IsBoss) return; // Bosses handle sound differently
        ai.CombatSoundTimer = 2 + Random() * 2;
        Sound3D sound(robotInfo.SeeSound);
        sound.Radius = AI_SOUND_RADIUS;
        Sound::PlayFrom(sound, robot);
    }

    void AlertEnemiesInSegment(Level& level, const Segment& seg, const NavPoint& source, float soundRadius, float awareness) {
        for (auto& objId : seg.Objects) {
            if (auto obj = level.TryGetObject(objId)) {
                if (!obj->IsRobot()) continue;

                auto dist = Vector3::Distance(obj->Position, source.Position);
                if (dist > soundRadius) continue;

                auto& ai = GetAI(*obj);
                float t = dist / soundRadius;
                auto falloff = Saturate(2.0f - 2.0f * t) * 0.5f + 0.5f; // linear shoulder

                ai.AddAwareness(awareness * falloff);

                //auto prevAwareness = ai.Awareness;
                ai.TargetPosition = source;
                obj->NextThinkTime = 0;

                // Update chase target if we hear something
                if (ai.State == AIState::Chase)
                    ChaseTarget(ai, *obj, *ai.TargetPosition, ChaseMode::StopVisible);
            }
        }
    }

    void AlertEnemiesInRoom(Level& level, const Room& room, SegID soundSeg, const Vector3& soundPosition, float soundRadius, float awareness, float /*maxAwareness*/) {
        for (auto& segId : room.Segments) {
            auto pseg = level.TryGetSegment(segId);
            if (!pseg) continue;
            auto& seg = *pseg;

            AlertEnemiesInSegment(level, seg, { soundSeg, soundPosition }, soundRadius, awareness);
        }
    }

    // adds awareness to robots in nearby rooms
    void AlertRobotsOfNoise(const NavPoint& source, float soundRadius, float awareness) {
        for (auto& roomId : Game::ActiveRooms) {
            if (auto room = Game::Level.GetRoom(roomId)) {
                for (auto& segId : room->Segments) {
                    auto seg = Game::Level.TryGetSegment(segId);
                    if (!seg) continue;
                    AlertEnemiesInSegment(Game::Level, *seg, source, soundRadius, awareness);
                }
            }
        }

        //IterateNearbySegments(Game::Level, source, soundRadius, TraversalFlag::StopDoor | TraversalFlag::PassOpenDoors, [&](const Segment& seg, bool) {
        //    AlertEnemiesInSegment(Game::Level, seg, source, soundRadius, awareness);
        //});
    }

    void AlertAlliesOfDeath(const Object& dyingRobot) {
        auto action = [&](const Object& robot) {
            if (robot.Signature == dyingRobot.Signature) return;

            auto& robotInfo = Resources::GetRobotInfo(robot);
            auto& ai = GetAI(robot);
            if ((ai.State == AIState::Alert || ai.State == AIState::Combat) && robotInfo.FleeThreshold > 0) {
                ai.Fear += 1;
                Chat(robot, "They took out drone {}! I'm scared!", dyingRobot.Signature);
            }
            ai.Awareness += 1;
        };

        auto room = Game::Level.GetRoomID(dyingRobot);
        ForNearbyRobots(room, dyingRobot.Position, 160, action);
    }

    // Alerts nearby robots of a target. Used when a robot fires to wake up nearby robots, or by observer robots.
    // Returns true if a robot became fully alert.
    bool AlertRobotsOfTarget(const Object& source, float radius, const NavPoint& target, float awareness) {
        auto& level = Game::Level;
        auto srcRoom = level.GetRoomID(source);
        if (srcRoom == RoomID::None) return false;

        bool alertedRobot = false;

        auto action = [&](const Room& room) {
            for (auto& segId : room.Segments) {
                auto pseg = level.TryGetSegment(segId);
                if (!pseg) continue;
                auto& seg = *pseg;

                for (auto& objId : seg.Objects) {
                    if (auto obj = level.TryGetObject(objId)) {
                        if (!obj->IsRobot()) continue;
                        if (obj->Signature == source.Signature) continue; // Don't alert self

                        // todo: when a robot is first woken up, decide whether it will hold position or investigate
                        if (Random() < 0.5f) continue; // Don't alert at all half the time

                        auto dist = Vector3::Distance(obj->Position, source.Position);
                        if (dist > radius) continue;
                        auto random = 1 + RandomN11() * 0.25f; // Add some variance so robots in a room don't all wake up at same time
                        auto& ai = GetAI(*obj);
                        if (ai.State == AIState::Idle || ai.State == AIState::Alert || ai.State == AIState::Roam) {
                            if (ai.State == AIState::Idle) {
                                Chat(*obj, "Drone {} says it sees something", source.Signature);
                                PlayAlertSound(*obj, ai);
                            }

                            ai.State = AIState::Alert;
                            ai.TargetPosition = target;
                            ai.AddAwareness(awareness * random);
                            alertedRobot = true;
                        }
                    }
                }
            }

            return false;
        };

        TraverseRoomsByDistance(level, srcRoom, source.Position, radius, true, action);
        return alertedRobot;
    }

    void PlayDistressSound(const Object& robot) {
        // todo: always use class 1 drone sound (170)? 177 for tougher robots?
        Sound3D sound(Resources::GetRobotInfo(robot).AttackSound);
        sound.Pitch = 0.45f;
        sound.Radius = AI_SOUND_RADIUS;
        //sound.Radius = 250;
        Sound::PlayFrom(sound, robot);

        sound.Delay = 0.5f;
        Sound::PlayFrom(sound, robot);
    }

    // Low health scream for tougher robots (> 100 health?)
    void PlayAgonySound(const Object& robot) {
        Sound3D sound(SoundID(179)); // D1 sound
        sound.Volume = 1.25f;
        sound.Radius = AI_SOUND_RADIUS;
        Sound::PlayFrom(sound, robot);
    }

    // Returns hit information about if object can see a point
    IntersectResult HasLineOfSightEx(const Object& obj, const Vector3& point, bool precise) {
        auto [dir, dist] = GetDirectionAndDistance(point, obj.Position);
        LevelHit hit{};
        Ray ray = { obj.Position, dir };
        RayQuery query{ .MaxDistance = dist, .Start = obj.Segment, .Mode = precise ? RayQueryMode::Precise : RayQueryMode::Visibility };
        return Game::Intersect.RayLevelEx(ray, query, hit);
    }

    // Returns true if object can see a point
    bool HasLineOfSight(const Object& obj, const Vector3& point, bool precise) {
        return !Intersects(HasLineOfSightEx(obj, point, precise));
    }

    // Returns true if gun has precise visibility to a target
    IntersectResult HasFiringLineOfSight(const Object& obj, uint8 gun, const Vector3& target, ObjectMask mask) {
        auto gunPosition = GetGunpointWorldPosition(obj, gun);

        auto [dir, distance] = GetDirectionAndDistance(target, gunPosition);
        LevelHit hit{};
        RayQuery query{ .MaxDistance = distance, .Start = obj.Segment, .Mode = RayQueryMode::Precise };
        return Game::Intersect.RayLevelEx({ gunPosition, dir }, query, hit, mask, Game::GetObjectRef(obj).Id);
        //Render::Debug::DrawLine(gunPosition, target, visible ? Color(0, 1, 0) : Color(1, 0, 0));
    }

    bool SegmentIsAdjacent(const Segment& src, SegID adjacent) {
        for (auto& conn : src.Connections) {
            if (conn == adjacent) return true;
        }
        return false;
    }

    bool DeathRoll(Object& obj, float rollDuration, float elapsedTime, SoundID soundId, bool& dyingSoundPlaying, float volume, float dt) {
        auto& angularVel = obj.Physics.AngularVelocity;

        angularVel.x = elapsedTime / 9.0f;
        angularVel.y = elapsedTime / 5.0f;
        angularVel.z = elapsedTime / 7.0f;
        if ((int)obj.Signature % 2) angularVel.x *= -1;
        if ((int)obj.Signature % 3) angularVel.y *= -1;
        if ((int)obj.Signature % 5) angularVel.z *= -1;

        SoundResource resource(soundId);
        auto soundDuration = resource.GetDuration();
        if (soundDuration == 0) soundDuration = DEATH_SOUND_DURATION;
        auto& ri = Resources::GetRobotInfo(obj);

        if (elapsedTime > rollDuration - soundDuration) {
            auto& ai = GetAI(obj);
            if (ai.AmbientSound != SoundUID::None) {
                Sound::Stop(ai.AmbientSound);
                ai.AmbientSound = SoundUID::None;
            }

            // Going critical!
            if (!dyingSoundPlaying) {
                Sound3D sound(resource);
                sound.Volume = volume;
                sound.Radius = 1000; // Should be a global radius for bosses
                Sound::PlayFrom(sound, obj);
                dyingSoundPlaying = true;
            }

            if (Random() < dt * 16) {
                auto effect = ri.IsBoss ? "boss large fireball" : "large fireball";
                if (auto e = EffectLibrary.GetExplosion(effect)) {
                    // Larger periodic explosions with sound
                    //e->Variance = obj.Radius * 0.75f;
                    e->Volume = volume;
                    CreateExplosion(*e, Game::GetObjectRef(obj));
                }
            }
        }
        else if (Random() < dt * 8) {
            // Winding up, create fireballs on object
            auto effect = ri.IsBoss ? "boss small fireball" : "small fireball";
            if (auto e = EffectLibrary.GetExplosion(effect)) {
                //e->Variance = obj.Radius * 0.65f;
                e->Volume = volume;
                CreateExplosion(*e, Game::GetObjectRef(obj));
            }
        }

        return elapsedTime > rollDuration;
    }

    void MoveTowardsPoint(const Object& robot, AIRuntime& ai, const Vector3& point, float scale) {
        auto dir = point - robot.Position;
        dir.Normalize();
        auto& info = Resources::GetRobotInfo(robot);
        ai.Velocity += dir * DifficultyInfo(info).Speed * scale;
    }

    constexpr float FAST_WEAPON_SPEED = 200;
    constexpr float SLOW_WEAPON_SPEED = 30;

    void DecayAwareness(AIRuntime& ai, float rate = AI_AWARENESS_DECAY) {
        auto random = .75f + Random() * 0.25f; // Add some randomness so robots don't all stop firing at the same time
        ai.Awareness -= rate * ai.GetDeltaTime() * random;
        if (ai.Awareness < 0) ai.Awareness = 0;
    }

    // Vectors must have same origin and be on same plane
    float SignedAngleBetweenVectors(const Vector3& a, const Vector3& b, const Vector3& normal) {
        return std::atan2(a.Cross(b).Dot(normal), a.Dot(b));
    }

    void CycleGunpoint(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        ai.GunIndex = robotInfo.Guns > 0 ? (ai.GunIndex + 1) % robotInfo.Guns : 0;
        if (Game::Level.IsDescent1() && robot.ID == 23 && ai.GunIndex == 2)
            ai.GunIndex = 3; // HACK: skip to 3 due to gunpoint 2 being zero-filled on the D1 final boss. This should be fixed on the D2 model.

        if (robotInfo.WeaponType2 != WeaponID::None && ai.GunIndex == 0)
            ai.GunIndex = 1; // Reserve gun 0 for secondary weapon if present
    }

    // Returns the new position to fire at
    Vector3 LeadTarget(const Object& robot, SegID gunSeg, const Object& target, const Weapon& weapon) {
        if (target.Physics.Velocity.Length() < 20)
            return target.Position; // Don't lead slow targets

        if (GetSpeed(weapon) > FAST_WEAPON_SPEED)
            return target.Position; // Don't lead with fast weapons (vulcan, gauss, drillers). Unfair to player.

        auto targetDist = Vector3::Distance(target.Position, robot.Position);
        Vector3 targetVelDir;
        target.Physics.Velocity.Normalize(targetVelDir);
        float expectedTravelTime = targetDist / GetSpeed(weapon);
        auto projectedTarget = target.Position;

        {
            // Check target projected position
            Ray targetTrajectory(target.Position, targetVelDir);
            RayQuery query;
            query.MaxDistance = (target.Physics.Velocity * expectedTravelTime).Length();
            query.Start = target.Segment;
            LevelHit hit;
            if (HasFlag(Game::Intersect.RayLevelEx(targetTrajectory, query, hit), IntersectResult::HitWall)) {
                // target will hit wall, aim at wall minus object radius
                projectedTarget = hit.Point - targetVelDir * target.Radius;
                targetDist = Vector3::Distance(projectedTarget, robot.Position);
                expectedTravelTime = targetDist / GetSpeed(weapon);
            }

            projectedTarget = target.Position + target.Physics.Velocity * expectedTravelTime;
        }

        {
            auto targetDir = projectedTarget - robot.Position;
            targetDir.Normalize();

            //auto dot = robot.Rotation.Forward().Dot(targetDir);

            // Check shot line of sight
            Ray ray(robot.Position, targetDir);
            RayQuery query;
            query.Start = gunSeg;
            query.MaxDistance = Vector3::Distance(projectedTarget, robot.Position);
            LevelHit hit;
            if (!Game::Intersect.RayLevel(ray, query, hit)) {
                // Won't hit level, lead the target!
                return projectedTarget;
            }
        }

        return target.Position; // Wasn't able to lead target
    }

    void FireRobotWeapon(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, Vector3 target, bool primary, bool blind, bool lead) {
        if (!primary && robotInfo.WeaponType2 == WeaponID::None) return; // no secondary set

        auto weaponId = primary ? robotInfo.WeaponType : robotInfo.WeaponType2;
        auto& weapon = Resources::GetWeapon(weaponId);
        uint8 gun = primary ? ai.GunIndex : 0;
        auto forward = robot.Rotation.Forward();

        // Find world position of gunpoint
        auto gunOffset = GetSubmodelOffset(robot, { robotInfo.GunSubmodels[gun], robotInfo.GunPoints[gun] });
        auto gunPosition = Vector3::Transform(gunOffset, robot.GetTransform());

        if (blind) {
            // add inaccuracy if target is cloaked or doing a blind-fire
            target += RandomVector() * 5.0f;
        }
        else if (auto targetObj = Game::GetObject(ai.Target); targetObj && lead) {
            target = LeadTarget(robot, robot.Segment, *targetObj, weapon);
        }

        // project target to centerline of gunpoint
        auto projTarget = forward * forward.Dot(target - gunPosition) + gunPosition;
        //Render::Debug::DrawLine(projTarget, gunPosition, Color(1, 0, 0));
        auto projDist = Vector3::Distance(gunPosition, projTarget);

        auto halfAimRads = robotInfo.AimAngle * DegToRad * 0.5f;

        auto aimDir = GetDirection(target, gunPosition);
        auto aimAngle = AngleBetweenVectors(aimDir, forward);
        //SPDLOG_INFO("Aim angle deg: {}", aimAngle * RadToDeg);


        if (aimAngle > DirectX::XM_PIDIV2) {
            // If the projected target is behind the gunpoint, fire straight instead.
            // Otherwise the aim clamping causes the robot to shoot backwards.
            target = gunPosition + forward * 20;
        }
        else if (aimAngle > halfAimRads) {
            // Clamp the target to the robot's aim angle
            auto projDir = target - projTarget;
            projDir.Normalize();
            auto maxLeadDist = tanf(halfAimRads) * projDist;
            target = projTarget + maxLeadDist * projDir;

            //auto [aimDir2, aimDist2] = GetDirectionAndDistance(target, gunPosition);
            //auto aimAngle2 = AngleBetweenVectors(aimDir2, forward);
            //SPDLOG_INFO("Aim angle deg 2: {}", aimAngle2 * RadToDeg);
            //ASSERT(aimAngle2 <= robotInfo.AimAngle * DegToRad);
        }

        // Add inaccuracy
        auto targetDir = target - gunPosition;
        targetDir.Normalize();

        {
            // Randomize target based on aim. 255 -> 1, 0 -> 8
            auto aim = 8.0f - 7.0f * FixToFloat(robotInfo.Aim << 8);
            aim += float(4 - (int)Game::Difficulty) * 0.5f; // Add inaccuracy based on difficulty (2 to 0)

            // todo: seismic disturbance inaccuracy from earthshaker

            if (Game::ControlCenterDestroyed) {
                // 1 to 3.0f as timer counts down
                auto seismic = 1.0f + (16 - std::min(Game::CountdownSeconds, 16)) / 8.0f;
                aim += seismic * 6;
            }

            auto matrix = VectorToRotation(targetDir);
            auto spread = RandomPointInCircle(aim);

            //auto direction = Game::GetSpreadDirection(robot, { point.x, point.y });
            target += matrix.Right() * spread.x;
            target += matrix.Up() * spread.y;

            // Recalculate target dir
            targetDir = target - gunPosition;
            targetDir.Normalize();
        }

        // Check that the target point is in front of the gun, otherwise set it to shoot straight
        Plane plane(gunPosition, forward);
        if (plane.DotCoordinate(target) <= 0) {
            SPDLOG_WARN("Robot tried to shoot backwards");
            target = gunPosition + forward * 20;
        }

        // Fire the weapon
        Game::FireWeaponInfo info = { .id = weaponId, .gun = gun, .customDir = &targetDir };
        Game::FireWeapon(robot, info);
        Game::PlayWeaponSound(weaponId, weapon.Extended.FireVolume, robot, gun);

        if (primary)
            CycleGunpoint(robot, ai, robotInfo);
    }

    void RandomDodge(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (ai.DodgeDelay > 0) return; // not ready to dodge again

        auto angle = Random() * DirectX::XM_2PI;
        auto transform = Matrix::CreateFromAxisAngle(robot.Rotation.Forward(), angle);
        auto dodgeDir = Vector3::Transform(robot.Rotation.Right(), transform);

        ai.DodgeVelocity = dodgeDir * DifficultyInfo(robotInfo).EvadeSpeed * 30;
        ai.DodgeDelay = (5 - (int)Game::Difficulty) / 2.0f + 0.25f + Random() * 0.5f; // (2 to 0) + 0.25 + (0..0.5) delay
        ai.DodgeTime = AI_DODGE_TIME * 0.5f + AI_DODGE_TIME * 0.5f * Random();
    }

    void DodgeProjectile(const Object& robot, AIRuntime& ai, const Object& projectile, const RobotInfo& robotInfo) {
        if (projectile.Physics.Velocity.LengthSquared() < 5 * 5) return; // Don't dodge slow projectiles. also prevents crash at 0 velocity.

        auto [projDir, projDist] = GetDirectionAndDistance(projectile.Position, robot.Position);
        // Looks weird to dodge distant projectiles. also they might hit another target
        // Consider increasing this for massive robots?
        if (projDist > AI_MAX_DODGE_DISTANCE) return;
        if (!PointIsInFOV(robot.Rotation.Forward(), projDir, DifficultyInfo(robotInfo).FieldOfView)) return;

        Vector3 projTravelDir;
        projectile.Physics.Velocity.Normalize(projTravelDir);
        Ray projRay = { projectile.Position, projTravelDir };
        auto dodgePoint = ProjectRayOntoPlane(projRay, robot.Position, -projTravelDir);
        if (!dodgePoint) return;
        auto dodgeDir = robot.Position - *dodgePoint;
        if (dodgeDir.Length() > robot.Radius * 1.5f) return; // Don't dodge projectiles that won't hit us
        dodgeDir.Normalize();

        //if (robotInfo.Attack == AttackType::Melee && ai.Target) {
        //    auto targetDir = *ai.Target - robot.Position;
        //    targetDir.Normalize();
        //    dodgeDir += targetDir * .5;
        //    dodgeDir.Normalize();
        //}

        ai.DodgeVelocity = dodgeDir * DifficultyInfo(robotInfo).EvadeSpeed * 30;
        ai.DodgeDelay = (5 - (int)Game::Difficulty) / 2.0f + 0.25f + Random() * 0.5f; // (2 to 0) + 0.25 + (0..0.5) delay
        float dodgeTime = AI_DODGE_TIME * 0.5f + AI_DODGE_TIME * 0.5f * Random();
        auto& weapon = Resources::GetWeapon((WeaponID)projectile.ID);
        if (weapon.IsHoming)
            dodgeTime += AI_DODGE_TIME; // homing weapons require a hard dodge to evade

        ai.DodgeTime = dodgeTime;

        if (robotInfo.FleeThreshold > 0 && ai.State == AIState::Combat)
            ai.Fear += 0.4f; // Scared of being hit
    }

    void DodgeProjectiles(Level& level, const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (ai.DodgeDelay > 0) return; // not ready to dodge again

        IterateNearbySegments(level, robot, 100, TraversalFlag::PassTransparent, [&](const Segment& seg, bool) {
            for (auto& objId : seg.Objects) {
                if (auto weapon = level.TryGetObject(objId)) {
                    if (weapon->Type != ObjectType::Weapon) continue;
                    if (auto parent = level.TryGetObject(weapon->Parent)) {
                        if (parent->IsRobot()) continue;

                        DodgeProjectile(robot, ai, *weapon, robotInfo);
                        return;
                    }
                }
            }
        });
    }

    // Tries to path towards the player or move directly to it if in the same room
    void MoveTowardsTarget(Level& level, const Object& robot,
                           AIRuntime& ai, const Vector3& objDir, const RobotInfo& robotInfo) {
        if (!ai.TargetPosition) return;

        auto sight = HasLineOfSightEx(robot, ai.TargetPosition->Position, false);

        if (robotInfo.Attack == AttackType::Melee && sight == IntersectResult::ThroughWall) {
            ChaseTarget(ai, robot, *ai.TargetPosition, ChaseMode::StopAtPosition);
            // path to target, but only if it's not tried recently
        }


        if (!Intersects(sight)) {
            Ray ray(robot.Position, objDir);
            //AvoidConnectionEdges(level, ray, desiredIndex, obj, thrust);
            AvoidRoomEdges(level, ray, robot, ai.TargetPosition->Position);
            //auto& seg = level.GetSegment(robot.Segment);
            //AvoidSideEdges(level, ray, seg, side, robot, 0, player.Position);
            MoveTowardsPoint(robot, ai, ai.TargetPosition->Position);
            //ai.PathDelay = 0;
        }
        //else if (ai.PathDelay <= 0) {
        //    if (!ChaseTarget(ai, robot, ai.TargetSegment, ai.TargetPosition->Position))
        //        ai.PathDelay = 5; // Don't try pathing again for a while
        //    //    if(!SetPathGoal(level, robot, ai, ai.TargetSegment, ai.TargetPosition->Position, AI_MAX_CHASE_DISTANCE))
        //}
    }

    // Moves towards a random segment further away from the player. Prefers room portals.
    void MoveAwayFromTarget(const Vector3& target, const Object& robot, AIRuntime& ai) {
        auto targetDir = target - robot.Position;
        targetDir.Normalize();
        Ray ray(robot.Position, -targetDir);
        LevelHit hit;
        RayQuery query{ .MaxDistance = 10, .Start = robot.Segment };
        if (Intersect.RayLevel(ray, query, hit))
            return; // no room to move backwards

        // todo: try escaping through portals if there are any in the player's FOV
        MoveTowardsPoint(robot, ai, robot.Position - targetDir * 10);
    }

    void MoveToCircleDistance(Level& level, const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (!ai.TargetPosition) return;

        auto circleDistance = DifficultyInfo(robotInfo).CircleDistance;
        if (circleDistance < 0) return; // hold position

        auto [dir, dist] = GetDirectionAndDistance(ai.TargetPosition->Position, robot.Position);
        auto minDist = std::min(circleDistance * 0.75f, circleDistance - 10);
        auto maxDist = std::max(circleDistance * 1.25f, circleDistance + 10);

        if (robotInfo.Attack == AttackType::Ranged && (dist > minDist && dist < maxDist))
            return; // in deadzone, no need to move. Otherwise robots clump up on each other.
        else if (robotInfo.Attack == AttackType::Melee && dist < circleDistance)
            return;

        if (dist > circleDistance)
            MoveTowardsTarget(level, robot, ai, dir, robotInfo);
        else
            MoveAwayFromTarget(ai.TargetPosition->Position, robot, ai);
    }

    void PlayRobotAnimation(const Object& robot, Animation state, float time, float moveMult, float delay) {
        auto& robotInfo = Resources::GetRobotInfo(robot);

        //float remaining = 1;
        // if a new animation is requested before the previous one finishes, speed up the new one as it has less distance
        //if (ail.AnimationTime < ail.AnimationDuration)
        //    remaining = (ail.AnimationDuration - ail.AnimationTime) / ail.AnimationDuration;

        auto& ai = GetAI(robot);
        ai.AnimationDuration = time /** remaining*/;
        ai.AnimationTimer = -delay;
        ai.AnimationState = state;

        for (int gun = 0; gun <= robotInfo.Guns; gun++) {
            const auto robotJoints = Resources::GetRobotJoints(robot.ID, gun, state);

            for (auto& joint : robotJoints) {
                const auto& angle = robot.Render.Model.Angles[joint.ID];

                if (angle == joint.Angle * moveMult) {
                    ai.DeltaAngles[joint.ID] = Vector3::Zero;
                    continue;
                }

                //ai.GoalAngles[joint.ID] = jointAngle;
                ai.DeltaAngles[joint.ID] = joint.Angle * moveMult - angle;
            }
        }
    }

    bool IsAnimating(const Object& robot) {
        if (!robot.IsRobot()) return false;

        auto& ai = GetAI(robot);
        return ai.AnimationTimer <= ai.AnimationDuration && ai.AnimationTimer >= 0;
    }

    void AnimateRobot(Object& robot, AIRuntime& ai, float dt) {
        assert(robot.IsRobot());
        auto& model = Resources::GetModel(robot.Render.Model.ID);

        ai.AnimationTimer += dt;
        if (ai.AnimationTimer > ai.AnimationDuration || ai.AnimationTimer < 0) return;

        for (int joint = 1; joint < model.Submodels.size(); joint++) {
            auto& angles = robot.Render.Model.Angles[joint];
            angles += ai.DeltaAngles[joint] / ai.AnimationDuration * dt;
        }
    }

    void RobotTouchObject(const Object& robot, const Object& obj) {
        ASSERT(robot.IsRobot());

        auto& ai = GetAI(robot);

        if (obj.IsRobot() || obj.IsPlayer()) {
            ai.LastCollision = Game::Time;
        }

        if (!Game::EnableAi()) return;

        if (obj.IsPlayer()) {
            if (ai.State != AIState::Path && ai.State != AIState::FindHelp) {
                if (ai.State != AIState::Combat) {
                    PlayAlertSound(robot, ai);
                    Chat(robot, "Something touched me!");
                }

                ai.Target = Game::GetObjectRef(obj);
                ai.TargetPosition = { obj.Segment, obj.Position };
                ai.Awareness = 1;
                ai.State = AIState::Combat;
            }
        }
    }

    void DamageRobot(const NavPoint& sourcePos, Object& robot, float damage, float stunMult, Object* source) {
        auto& info = Resources::GetRobotInfo(robot);
        auto& ai = GetAI(robot);

        if (ai.State == AIState::Idle) {
            ai.State = AIState::Alert;
            Chat(robot, "What hit me!?");
        }

        if (source && ai.State != AIState::Combat) {
            // Try randomly dodging if taking damage
            RandomDodge(robot, ai, info);

            if (source->IsPlayer()) {
                // We were hit by the player but don't know exactly where they are
                ai.TargetPosition = sourcePos;
                ai.LastHitByPlayer = 0;
                ai.Awareness = AI_AWARENESS_MAX;

                // Break out of pathing if shot
                if (ai.State == AIState::MatcenPath)
                    ai.State = AIState::Combat;
            }
            else if (source->IsRobot()) {
                Chat(robot, "Where are you aiming drone {}!?", source->Signature);
                damage *= Game::FRIENDLY_FIRE_MULT;
            }
        }

        if (!Settings::Cheats.DisableWeaponDamage) {
            // Make phasing robots (bosses and matcens) take less damage
            if (robot.Effects.GetPhasePercent() > 0)
                damage *= std::max(1 - robot.Effects.GetPhasePercent(), 0.1f);

            // Apply damage
            robot.HitPoints -= damage;
        }

        if (info.IsBoss) {
            // Bosses are immune to stun and slow and perform special actions when hit
            Game::DamageBoss(robot, sourcePos, damage, source);
        }
        else {
            // Apply slow
            float ehp = info.HitPoints * info.StunResist;
            float damageScale = 1 - (ehp - damage * stunMult) / ehp; // percentage of life dealt
            float slowTime = std::lerp(0.0f, 1.0f, damageScale / MAX_SLOW_THRESHOLD);
            if (ai.RemainingSlow > 0) slowTime += ai.RemainingSlow;
            ai.RemainingSlow = std::clamp(slowTime, 0.1f, MAX_SLOW_TIME);

            float stunTime = damageScale / MAX_STUN_PERCENT * MAX_STUN_TIME;

            // Apply stun
            if (damage * stunMult > STUN_THRESHOLD && stunTime > MIN_STUN_TIME) {
                //SPDLOG_INFO("Stunning {} for {}", robot.Signature, stunTime > MAX_STUN_TIME ? MAX_STUN_TIME : stunTime);
                if (ai.RemainingStun > 0) stunTime += ai.RemainingStun;
                stunTime = std::clamp(stunTime, MIN_STUN_TIME, MAX_STUN_TIME);
                ai.RemainingStun = stunTime;
                PlayRobotAnimation(robot, Animation::Flinch, 0.2f);

                if (auto beam = EffectLibrary.GetBeamInfo("stunned object arcs")) {
                    auto startObj = Game::GetObjectRef(robot);
                    beam->Radius = { robot.Radius * 0.6f, robot.Radius * 0.9f };
                    AttachBeam(*beam, stunTime, startObj);
                    beam->StartDelay = stunTime / 3;
                    AttachBeam(*beam, stunTime - beam->StartDelay, startObj);
                    beam->StartDelay = stunTime * 2 / 3;
                    AttachBeam(*beam, stunTime - beam->StartDelay, startObj);
                    //SetFlag(robot.Physics.Flags, PhysicsFlag::Gravity);
                }
            }

            if (robot.HitPoints <= 0 && info.DeathRoll == 0) {
                AlertAlliesOfDeath(robot);
                ExplodeObject(robot); // Explode normal robots immediately
            }
        }
    }

    enum class AIEvent {
        HitByWeapon,
        HitObj,
        MeleeHit,
        HearNoise,
        SeePlayer,
        TakeDamage,
    };

    //using RobotBehavior = std::function<void(Object&, AIRuntime&, AIEvent)>;
    ////WeaponBehavior& GetWeaponBehavior(const string& name);

    //Dictionary<string, RobotBehavior> RobotBehaviors = {
    //    { "default", DefaultBehavior },
    //    { "fusion-hulk", VulcanBehavior },
    //    { "trooper", HelixBehavior },
    //};

    bool RollShouldLead() {
        auto leadChance = (int)Game::Difficulty / 4.0f; // 50% on hotshot, 75% on ace, 100% on insane
        bool shouldLead = Random() <= leadChance * 0.9f; // Don't always lead even on insane, keep the player guessing
        if (Game::Difficulty < DifficultyLevel::Hotshot) shouldLead = false; // Don't lead on rookie and trainee, also weapons are too slow to meaningfully lead.
        return shouldLead;
    }

    void FireRobotPrimary(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const Vector3& target, bool blind) {
        ai.FireDelay = 0;

        // multishot: consume as many projectiles as possible based on burst count
        // A multishot of 1 and a burst of 3 would fire 2 projectiles then 1 projectile
        auto burstDelay = robotInfo.BurstDelay;
        if (ai.Angry) burstDelay *= AI_ANGER_SPEED; // Use a lower burst delay when angry

        auto shouldLead = RollShouldLead(); // only roll once per fire

        // Don't lead through walls as robots will often hit the grating instead
        if (HasFiringLineOfSight(robot, ai.GunIndex, ai.TargetPosition->Position, ObjectMask::Robot) == IntersectResult::ThroughWall)
            shouldLead = false;

        for (int i = 0; i < robotInfo.Multishot; i++) {
            FireRobotWeapon(robot, ai, robotInfo, target, true, blind, shouldLead);
            ai.BurstShots++;

            if (ai.BurstShots >= DifficultyInfo(robotInfo).ShotCount) {
                ai.BurstShots = 0;
                auto fireDelay = DifficultyInfo(robotInfo).FireDelay;
                ai.FireDelay = ai.Angry ? fireDelay * AI_ANGER_SPEED : fireDelay;
                break; // Ran out of shots
            }
            else {
                ai.FireDelay = burstDelay;
            }
        }


        PlayRobotAnimation(robot, Animation::Recoil, 0.25f);
    }

    // start charging when player is in FOV and can fire
    // keep charging even if player goes out of view
    // fire at last known location
    void WeaponChargeBehavior(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float dt) {
        ai.NextChargeSoundDelay -= dt;
        ai.WeaponCharge += dt;

        if (ai.NextChargeSoundDelay <= 0) {
            ai.NextChargeSoundDelay = 0.125f + Random() / 8;

            if (auto fx = EffectLibrary.GetSparks("robot_fusion_charge")) {
                auto parent = Game::GetObjectRef(robot);
                Sound3D sound(SoundID::FusionWarmup);
                sound.Radius = AI_SOUND_RADIUS;
                ai.SoundHandle = Sound::PlayFrom(sound, robot);

                for (uint8 i = 0; i < robotInfo.Guns; i++) {
                    auto offset = GetGunpointOffset(robot, i);
                    AttachSparkEmitter(*fx, parent, offset);
                }
            }
        }

        //if (ai.WeaponCharge >= Difficulty(info).FireDelay * 2) {
        if (ai.WeaponCharge >= robotInfo.ChargeTime) {
            Sound::Stop(ai.SoundHandle);
            // Release shot even if target has moved out of view
            auto target = ai.TargetPosition ? ai.TargetPosition->Position : robot.Position + robot.Rotation.Forward() * 40;
            FireRobotPrimary(robot, ai, robotInfo, target, true);

            ai.WeaponCharge = 0;
            ai.ChargingWeapon = false;
        }
    }

    // Tries to circle strafe the target.
    void CircleStrafe(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (!ai.TargetPosition) return;

        bool checkDir = false;
        // Move in a consistent direction for the strafe
        if (ai.StrafeTimer <= 0) {
            ai.StrafeAngle = Random() * DirectX::XM_2PI;
            ai.StrafeTimer = Random() * 2 + 1.5f;
            checkDir = true;
        }

        if (ai.StrafeAngle < 0)
            return; // angle not set

        auto targetDir = ai.TargetPosition->Position - robot.Position;
        targetDir.Normalize();

        auto transform = Matrix::CreateFromAxisAngle(targetDir, ai.StrafeAngle);
        auto dir = Vector3::Transform(robot.Rotation.Right(), transform);

        if (checkDir) {
            LevelHit hit{};
            RayQuery query{ .MaxDistance = 20, .Start = robot.Segment };
            Ray ray(robot.Position, dir);

            auto intersect = Game::Intersect.RayLevelEx(ray, query, hit);
            if (!Intersects(intersect) && intersect != IntersectResult::ThroughWall) {
                ai.StrafeAngle = -1;
                ai.StrafeTimer = 0.125f;
                return; // Try again
            }
        }

        ai.Velocity += dir * DifficultyInfo(robotInfo).Speed * .25f;
    }

    // Tries to move behind the target, adjusting the direction every few seconds
    void GetBehindTarget(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const Object& target) {
        auto targetDir = ai.TargetPosition->Position - robot.Position;
        targetDir.Normalize();

        auto targetFacing = target.Rotation.Forward();
        if (targetFacing.Dot(targetDir) > 0)
            return; // Already behind the target!

        // Try to make the target facing dot product larger!

        if (ai.StrafeTimer <= 0) {
            auto right = robot.Position + robot.Rotation.Right() * 5;
            auto left = robot.Position - robot.Rotation.Right() * 5;

            auto testTargetDir = ai.TargetPosition->Position - right;
            testTargetDir.Normalize();
            auto rightTargetDot = targetFacing.Dot(testTargetDir);

            testTargetDir = ai.TargetPosition->Position - left;
            testTargetDir.Normalize();
            auto leftTargetDot = targetFacing.Dot(testTargetDir);

            ai.StrafeDir = rightTargetDot > leftTargetDot ? robot.Rotation.Right() : -robot.Rotation.Right();

            LevelHit hit{};
            RayQuery query{ .MaxDistance = 20, .Start = robot.Segment };
            Ray ray(robot.Position, ai.StrafeDir);

            if (Intersects(Game::Intersect.RayLevelEx(ray, query, hit))) {
                // flip direction and try again
                ai.StrafeDir *= -1;

                if (Intersects(Game::Intersect.RayLevelEx(ray, query, hit))) {
                    ai.StrafeAngle = -1;
                    ai.StrafeTimer = 0.5f;
                    return; // Can't dodge, try later
                }
            }

            ai.StrafeDir += targetDir * 2;
            ai.StrafeDir.Normalize();
            ai.StrafeTimer = 2; // Only update strafe dir every 2 seconds
        }

        // todo: check if hits wall
        ai.Velocity += ai.StrafeDir * DifficultyInfo(robotInfo).Speed * 0.5f;
    }

    void UpdateRangedAI(Object& robot, const RobotInfo& robotInfo, AIRuntime& ai, float dt, bool blind) {
        if (ai.CombatState == AICombatState::Wait && blind)
            return; // Don't allow supressing fire when waiting

        if (robotInfo.WeaponType2 != WeaponID::None && ai.FireDelay2 <= 0) {
            // Check if an ally robot is in the way and try strafing if it is
            auto sight = HasFiringLineOfSight(robot, 0, ai.TargetPosition->Position, ObjectMask::Robot);
            if (Intersects(sight)) {
                CircleStrafe(robot, ai, robotInfo);
                return;
            }

            // Secondary weapons have no animations or wind up
            FireRobotWeapon(robot, ai, robotInfo, ai.TargetPosition->Position, false, blind, false);
            ai.FireDelay2 = DifficultyInfo(robotInfo).FireDelay2;
        }
        else {
            if (robotInfo.Guns == 0) return; // Can't shoot, I have no guns!

            if (ai.AnimationState != Animation::Fire && !ai.PlayingAnimation()) {
                PlayRobotAnimation(robot, Animation::Alert, 1.0f);
            }

            auto& weapon = Resources::GetWeapon(robotInfo.WeaponType);
            // Use the last time the target was seen instead of the delayed target tracking used for chasing.
            auto targetPos = ai.CombatState == AICombatState::BlindFire && ai.LastSeenTargetPosition
                ? ai.LastSeenTargetPosition->Position
                : ai.TargetPosition->Position;

            if (ai.ChargingWeapon) {
                WeaponChargeBehavior(robot, ai, robotInfo, dt); // Charge up during fire animation
            }
            else if (ai.AnimationState != Animation::Fire && ai.FireDelay < 0.25f) {
                // Start firing

                if (ai.CombatState != AICombatState::BlindFire) {
                    // Check if an ally robot is in the way and try strafing if it is
                    auto sight = HasFiringLineOfSight(robot, ai.GunIndex, ai.TargetPosition->Position, ObjectMask::Robot);
                    if (Intersects(sight)) {
                        CircleStrafe(robot, ai, robotInfo);
                        CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                        ai.FireDelay = 0.25f + 1 / 8.0f; // Try again in 1/8th of a second
                        return;
                    }
                }

                auto aimDir = targetPos - robot.Position;
                aimDir.Normalize();

                if (AngleBetweenVectors(aimDir, robot.Rotation.Forward()) <= robotInfo.AimAngle * DegToRad) {
                    // Target is within the cone of the weapon, start firing
                    PlayRobotAnimation(robot, Animation::Fire, ai.FireDelay.Remaining() * 0.8f);
                }

                if (weapon.Extended.Chargable)
                    ai.ChargingWeapon = true;
            }
            else if (ai.FireDelay <= 0 && !ai.PlayingAnimation()) {
                // Check that the target hasn't gone out of LOS when using explosive weapons.
                // Robots can easily blow themselves up in this case.
                //if (weapon.SplashRadius > 0 && !HasLineOfSight(robot, ai.GunIndex, ai.TargetPosition->Position, ObjectMask::Robot)) {
                //    CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                //    return;
                //}

                // Fire animation finished, release a projectile
                FireRobotPrimary(robot, ai, robotInfo, targetPos, blind);

                if (Settings::Cheats.ShowPathing)
                    Graphics::DrawPoint(targetPos, Color(1, 0, 0));
            }
        }
    }

    void UpdateMeleeAttackAI(const Object& robot, const RobotInfo& robotInfo, AIRuntime& ai,
                             Object& target, const Vector3& targetDir, float dt) {
        constexpr float MELEE_RANGE = 10; // how close to actually deal damage
        constexpr float MELEE_SWING_TIME = 0.175f;
        constexpr float BACKSWING_TIME = 0.45f;
        constexpr float BACKSWING_RANGE = MELEE_RANGE * 3; // When to prepare a swing
        constexpr float MELEE_GIVE_UP = 2.0f;

        // Recoil animation is swung 'downward'
        // Fire animation is 'raised'

        if (ai.ChargingWeapon)
            ai.WeaponCharge += dt; // Raising arms to swing counts as "charging"

        auto dist = Vector3::Distance(robot.Position, target.Position);

        if (!ai.PlayingAnimation()) {
            if (ai.ChargingWeapon) {
                if (ai.AnimationState == Animation::Flinch) {
                    // got stunned while charging weapon, reset swing
                    PlayRobotAnimation(robot, Animation::Alert, BACKSWING_TIME);
                    ai.ChargingWeapon = false;
                    ai.FireDelay = DifficultyInfo(robotInfo).FireDelay;
                }
                else if (ai.BurstShots > 0) {
                    // Alternate between fire and recoil when attacking multiple times
                    auto nextAnim = ai.AnimationState == Animation::Fire ? Animation::Recoil : Animation::Fire;
                    auto animTime = BACKSWING_TIME * (0.4f + Random() * 0.25f);
                    PlayRobotAnimation(robot, nextAnim, animTime);
                    ai.FireDelay = ai.MeleeHitDelay = animTime * 0.5f;
                }
                else if (ai.AnimationState == Animation::Fire) {
                    // Arms are raised
                    if (dist < robot.Radius + MELEE_RANGE) {
                        // Player moved close enough, swing
                        PlayRobotAnimation(robot, Animation::Recoil, MELEE_SWING_TIME);
                        ai.MeleeHitDelay = MELEE_SWING_TIME / 2;
                    }
                    else if (dist > robot.Radius + BACKSWING_RANGE && ai.WeaponCharge > MELEE_GIVE_UP) {
                        // Player moved out of range for too long, give up
                        PlayRobotAnimation(robot, Animation::Alert, BACKSWING_TIME);
                        ai.ChargingWeapon = false;
                        ai.FireDelay = DifficultyInfo(robotInfo).FireDelay;
                    }
                }
            }
            else {
                // Reset to default
                PlayRobotAnimation(robot, Animation::Alert, 0.3f);
            }
        }

        if (ai.AnimationState == Animation::Recoil || ai.BurstShots > 0) {
            if (ai.ChargingWeapon && ai.MeleeHitDelay <= 0) {
                if (ai.BurstShots + 1 < DifficultyInfo(robotInfo).ShotCount) {
                    ai.MeleeHitDelay = 10; // Will recalculate above when picking animations
                    ai.BurstShots++;
                }
                else {
                    ai.FireDelay = DifficultyInfo(robotInfo).FireDelay;
                    ai.ChargingWeapon = false;
                    ai.BurstShots = 0;
                }

                // Is target in range and in front of the robot?
                if (dist < robot.Radius + MELEE_RANGE && targetDir.Dot(robot.Rotation.Forward()) > 0) {
                    auto soundId = Game::Level.IsDescent1() ? (RandomInt(1) ? SoundID::TearD1_01 : SoundID::TearD1_02) : SoundID::TearD1_01;
                    Sound::Play(Sound3D(soundId), robot);
                    Game::Player.ApplyDamage(DifficultyInfo(robotInfo).MeleeDamage, false); // todo: make this generic. Damaging object should update the linked player

                    target.Physics.Velocity += targetDir * 5; // shove the target backwards
                    ai.Awareness = 1; // Hit something, reset awareness (cloaked targets)

                    if (auto sparks = EffectLibrary.GetSparks("melee hit")) {
                        auto position = robot.Position + targetDir * robot.Radius;
                        AddSparkEmitter(*sparks, robot.Segment, position);

                        LightEffectInfo light{};
                        light.LightColor = sparks->Color * .4f;
                        light.Radius = 18;
                        light.FadeTime = sparks->FadeTime / 2;
                        AddLight(light, position, light.FadeTime, robot.Segment);
                    }
                }
            }
        }
        else if (ai.FireDelay <= 0 && dist < robot.Radius + BACKSWING_RANGE && !ai.ChargingWeapon) {
            PlayRobotAnimation(robot, Animation::Fire, BACKSWING_TIME); // raise arms to attack
            ai.ChargingWeapon = true;
            ai.WeaponCharge = 0;
            ai.BurstShots = 0;
        }
    }

    // Moves a robot towards a direction
    void MoveTowardsDir(Object& robot, const Vector3& dir, float dt, float scale) {
        scale = std::min(1.0f, scale);
        auto& aiInfo = Resources::GetRobotInfo(robot);
        Vector3 idealVel = dir * DifficultyInfo(aiInfo).Speed * scale;
        Vector3 deltaVel = idealVel - robot.Physics.Velocity;
        float deltaSpeed = deltaVel.Length();
        deltaVel.Normalize();
        float maxDeltaVel = DifficultyInfo(aiInfo).Speed; // todo: new field. this is between 0.5 and 2 of the base velocity
        float maxDeltaSpeed = dt * maxDeltaVel * scale;

        if (deltaSpeed > maxDeltaSpeed)
            robot.Physics.Velocity += deltaVel * maxDeltaSpeed;
        else
            robot.Physics.Velocity = idealVel;
    }

    void ApplyVelocity(Object& robot, const AIRuntime& ai, float dt) {
        if (ai.Velocity == Vector3::Zero) return;
        auto& robotInfo = Resources::GetRobotInfo(robot);
        auto idealVel = ai.Velocity;
        Vector3 deltaVel = idealVel - robot.Physics.Velocity;
        float deltaSpeed = deltaVel.Length();
        deltaVel.Normalize();

        auto slow = std::clamp(ai.RemainingSlow, 0.0f, MAX_SLOW_TIME);
        // melee robots are slow resistant
        const auto maxSlow = robotInfo.Attack == AttackType::Melee && !robot.IsPhasing() ? MAX_SLOW_EFFECT / 3 : MAX_SLOW_EFFECT;
        float slowScale = slow > 0 ? 1 - maxSlow * slow / MAX_SLOW_TIME : 1;
        float maxDeltaSpeed = dt * DifficultyInfo(robotInfo).Speed * slowScale;

        if (deltaSpeed > maxDeltaSpeed)
            robot.Physics.Velocity += deltaVel * maxDeltaSpeed * 2; // x2 so max velocity is actually reached
        else
            robot.Physics.Velocity = idealVel;

        auto speed = robot.Physics.Velocity.Length();
        auto maxSpeed = DifficultyInfo(robotInfo).Speed;
        if (ai.State == AIState::FindHelp) maxSpeed *= 1.5f;

        if (speed > maxSpeed)
            robot.Physics.Velocity *= 0.75f;

        //SPDLOG_INFO("Speed: {}", robot.Physics.Velocity.Length());
    }

    void PlayCombatNoise(const Object& robot, AIRuntime& ai) {
        if (ai.CombatSoundTimer > 0) return;

        // Strange to check for being cornered here, but it is convenient with the sound timer
        auto& robotInfo = Resources::GetRobotInfo(robot);

        if (robotInfo.AngerBehavior) {
            ai.Angry = DronesInCombat <= 2;
        }

        ai.CombatSoundTimer = (1 + Random() * 0.75f) * 2.5f;

        Sound3D sound(robotInfo.AttackSound);
        sound.Pitch = Random() < 0.60f ? 0.0f : -0.05f - Random() * 0.10f;
        if (ai.Angry) sound.Pitch = 0.3f;
        sound.Radius = AI_SOUND_RADIUS;
        Sound::PlayFrom(sound, robot);
    }

    bool ScanForTarget(const Object& robot, AIRuntime& ai, bool* isThroughWall) {
        // For now always use the player object
        // Instead this should scan nearby targets (other robots or players)
        auto& target = Game::GetPlayerObject();
        if (target.Type == ObjectType::Ghost) return false;

        auto [targetDir, targetDist] = GetDirectionAndDistance(target.Position, robot.Position);

        auto& robotInfo = Resources::GetRobotInfo(robot);
        auto hasLos = HasLineOfSightEx(robot, target.Position, false);

        if (isThroughWall && hasLos == IntersectResult::ThroughWall)
            *isThroughWall = true;

        if (IsCloakEffective(target) || hasLos == IntersectResult::HitWall)
            return false;

        if (!PointIsInFOV(robot.Rotation.Forward(), targetDir, DifficultyInfo(robotInfo).FieldOfView))
            return false;

        float falloff = 1.0f;
        // Add a distance falloff, but don't go to zero even at max range
        if (targetDist > AI_VISION_FALLOFF_NEAR)
            falloff = 1 - Saturate((targetDist - AI_VISION_FALLOFF_NEAR) / (AI_VISION_FALLOFF_FAR - AI_VISION_FALLOFF_NEAR)) * AI_VISION_MAX_PENALTY;

        // Account for visibility, but only when not very close and not a boss
        if (targetDist > AI_VISION_FALLOFF_NEAR * .5f && !IsBossRobot(robot))
            falloff *= Game::Player.GetShipVisibility();

        auto reactionTime = AI_REACTION_TIME * (5 - (int)Game::Difficulty);
        ai.Awareness += falloff * ai.GetDeltaTime() / reactionTime;
        ai.Awareness = Saturate(ai.Awareness);

        ai.Target = Game::GetObjectRef(target);
        ai.TargetPosition = { target.Segment, target.Position };
        return ai.Awareness >= 1;
    }

    void OnIdle(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo) {
        ScanForTarget(robot, ai);

        if (ai.Awareness >= 1 && ai.Target) {
            // Delay weapons so robots don't shoot immediately on waking up
            ai.FireDelay = DifficultyInfo(robotInfo).FireDelay * .4f;
            ai.FireDelay2 = DifficultyInfo(robotInfo).FireDelay2 * .4f;

            // Time to fight!
            Chat(robot, "I see a bad guy!");
            ai.State = AIState::Combat;
            PlayAlertSound(robot, ai);
            //PlayRobotAnimation(robot, AnimState::Alert);
        }
        else if (ai.Awareness >= 1) {
            Chat(robot, "I need to fight but don't see anything");
            ai.State = AIState::Alert;
        }
        else {
            if (!ai.PlayingAnimation() && ai.AnimationState != Animation::Rest)
                PlayRobotAnimation(robot, Animation::Rest);

            robot.NextThinkTime = Game::Time + 0.125f;
        }

        // Fidget animation
        //if (!ai.PlayingAnimation()) {
        //    float delay = 3.0f + Random() * 5;
        //    float duration = 4 + Random() * 2.5;
        //    auto anim = ai.AnimationState != AnimState::Rest ? AnimState::Rest : AnimState::Flinch;
        //    PlayRobotAnimation(robot, anim, duration, 5, delay);
        //}
    }

    void MakeIdle(AIRuntime& ai) {
        ai.LastSeenTargetPosition = ai.TargetPosition = {}; // Clear target if robot loses interest.
        ai.State = AIState::Idle;
    }

    bool FindHelp(AIRuntime& ai, const Object& robot) {
        // Search active rooms for help from an idle or alert robot
        Chat(robot, "I need help!");

        Object* nearestHelp = nullptr;
        float nearestDist = FLT_MAX;

        auto action = [&](const Segment& seg, bool& stop) {
            for (auto& objid : seg.Objects) {
                if (auto help = Game::Level.TryGetObject(objid)) {
                    if (!help->IsRobot()) continue;

                    auto& helpAI = GetAI(*help);
                    auto& robotInfo = Resources::GetRobotInfo(*help);

                    // don't flee to robots that also flee. basically prevent scouts from running to other scouts.
                    // preferably this would be checked with a behavior flag instead of the threshold
                    if (robotInfo.FleeThreshold > 0)
                        continue;

                    if (helpAI.State == AIState::Alert || helpAI.State == AIState::Idle) {
                        // Found a robot that can help us

                        auto dist = Vector3::DistanceSquared(help->Position, robot.Position);
                        if (dist < nearestDist) {
                            nearestHelp = help;
                            nearestDist = dist;
                        }
                    }
                }
            }

            stop = nearestHelp != nullptr;
        };

        auto flags = TraversalFlag::StopLockedDoor | TraversalFlag::StopSecretDoor;
        IterateNearbySegments(Game::Level, robot, AI_HELP_SEARCH_RADIUS, flags, action);

        if (nearestHelp) {
            NavPoint goal = { nearestHelp->Segment, nearestHelp->Position };
            if (SetPathGoal(Game::Level, robot, ai, goal, AI_HELP_SEARCH_RADIUS)) {
                PlayDistressSound(robot);
                Chat(robot, "Maybe drone {} can help me", nearestHelp->Signature);
                ai.State = AIState::FindHelp;
                ai.Ally = Game::GetObjectRef(*nearestHelp);
                ai.AlertTimer = 3 + Random() * 2;
            }
            ai.Fear = 0;
            return true;
        }
        else {
            Chat(robot, "... but I'm all alone :(");
            ai.Fear = 100;
            // Fight back harder or run away randomly

            ai.Path = GenerateRandomPath(robot.Segment, 8);
            ai.PathIndex = 0;
            ai.PathDelay = AI_PATH_DELAY;
            return false;
        }
    }

    void UpdateFindHelp(AIRuntime& ai, Object& robot) {
        if (ai.Path.empty() || !ai.TargetPosition) {
            // Target can become none if it dies
            ai.State = AIState::Alert;
            return;
        }

        if (ai.AlertTimer <= 0) {
            PlayDistressSound(robot);
            AlertRobotsOfTarget(robot, Resources::GetRobotInfo(robot).AlertRadius, *ai.TargetPosition, 0.5f);
            ai.AlertTimer = 3 + Random() * 2;
            Chat(robot, "Help!");
        }

        PathTowardsGoal(robot, ai, false, false);

        auto [goalDir, goalDist] = GetDirectionAndDistance(ai.Path.back().Position, robot.Position);

        constexpr float REACHED_GOAL_DIST = 50;
        if (goalDist > REACHED_GOAL_DIST) return;

        auto ally = Game::GetObject(ai.Ally);
        if (!ally) {
            Chat(robot, "Where did my friend go? :(");
            ai.State = AIState::Alert;
            return;
        }

        // Is my friend still there?
        auto allyDist = Vector3::Distance(ally->Position, robot.Position);

        if (allyDist < REACHED_GOAL_DIST) {
            auto& allyAI = GetAI(*ally);
            if (ally->Control.AI.Behavior == AIBehavior::Still) {
                Chat(robot, "Drone {} I'm staying here with you", ai.Ally.Signature);
                allyAI.State = AIState::Alert;
                allyAI.Awareness = 1;
                allyAI.Target = ai.Target;
                allyAI.TargetPosition = ai.TargetPosition;
                ai.State = AIState::Alert;
                robot.Control.AI.Behavior = AIBehavior::Still;
                // Maybe alert another robot?
            }
            else {
                Chat(robot, "Hey drone {} go beat this guy up", ai.Ally.Signature);
                // Both robots path back to the target
                SetPathGoal(Game::Level, robot, ai, *ai.TargetPosition, AI_MAX_CHASE_DISTANCE);
                SetPathGoal(Game::Level, robot, allyAI, *ai.TargetPosition, AI_MAX_CHASE_DISTANCE);
                ai.State = AIState::Chase;
                allyAI.State = AIState::Chase;
            }

            ai.Fear = 0;
            ai.FleeTimer = 15 + Random() * 10; // Don't flee again for a while
        }
        //else {
        //    // Path to their new location
        //    PlayDistressSound(robot);
        //    SetPathGoal(Game::Level, robot, ai, { ally->Segment, ally->Position }, AI_MAX_CHASE_DISTANCE);
        //}
    }

    void UpdateRetreatAI() {
        // Fall back to cover? Find help?
    }

    // Causes a robot to retreat to a random segment away from a point, if possible.
    void Retreat(AIRuntime& /*ai*/, const Object& robot, const Vector3& from, float distance) {
        //auto seg = Game::Level.TryGetSegment(robot.Segment);
        //if(!seg) return;
        auto room = Game::Level.GetRoom(robot);
        if (!room) return;

        auto fromDir = from - robot.Position;
        fromDir.Normalize();

        float bestDot = 1;
        Tag bestPortal;

        for (auto& portal : room->Portals) {
            auto side = Game::Level.TryGetSide(portal.Tag);
            if (!side) continue;

            auto dir = side->Center - robot.Position;
            dir.Normalize();
            auto dot = dir.Dot(fromDir);
            if (dot < bestDot) {
                bestPortal = portal.Tag;
                bestDot = dot;
            }
        }

        if (bestPortal) {
            auto& side = Game::Level.GetSide(bestPortal);

            auto dist = Vector3::DistanceSquared(side.Center, robot.Position);
            if (dist < distance) {
                // portal is too close, go to next room a pick a different portal
            }
        }
        //room->Portals
    }

    // Chooses how to react to the target going out of sight
    void OnLostLineOfSight(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo) {
        if (Game::Difficulty < DifficultyLevel::Hotshot) {
            ai.CombatState = AICombatState::Wait; // Wait on trainee and rookie
            Chat(robot, "Holding position");
            return;
        }

        if (ai.ChargingWeapon) return; // keep charging weapon

        // Chase and suppress chance are percentages to perform those actions. If less than 1, can choose to do nothing.

        // Bucket chances together and adjust their weighting
        float chaseChance = robotInfo.ChaseChance;
        float suppressChance = robotInfo.SuppressChance;
        if (robotInfo.Attack == AttackType::Melee || robotInfo.Guns == 0)
            suppressChance = 0; // Melee robots can't shoot

        if (robot.Control.AI.Behavior == AIBehavior::Station)
            chaseChance *= 2; // patrolling robots twice as likely to chase

        if (robot.Control.AI.Behavior == AIBehavior::Still) {
            chaseChance = 0; // still robots can't chase
            suppressChance *= 2; // still robots are more likely to blind fire
        }

        auto totalChance = chaseChance + suppressChance;
        if (totalChance > 1) {
            // If chase or suppress sum over 1, rescale
            auto weight = 1 / totalChance;
            chaseChance *= weight;
            suppressChance *= weight;
        }

        // roll the behavior!
        auto roll = Random();
        if (roll < chaseChance) {
            Chat(robot, "Chasing");
            robot.NextThinkTime = 0;
            ai.CombatState = AICombatState::Chase;
        }
        else if (roll < chaseChance + suppressChance) {
            Chat(robot, "Suppressing fire!");
            ai.Awareness = 1; // Reset awareness so robot stays alert for a while
            ai.BurstShots = 0; // Reset shot counter
            robot.NextThinkTime = 0;
            ai.CombatState = AICombatState::BlindFire; // Calls the normal firing AI
        }
        else {
            Chat(robot, "Wait");
            ai.CombatState = AICombatState::Wait;
        }
    }

    void AlertNearby(AIRuntime& ai, const Object& robot, const RobotInfo& robotInfo) {
        if (Game::Difficulty <= DifficultyLevel::Trainee)
            return; // Don't alert on trainee

        if (ai.AlertTimer > 0 || !ai.TargetPosition || robotInfo.AlertRadius <= 0)
            return;

        constexpr float ALERT_FREQUENCY = 0.2f; // Smooth out alerts
        auto skillMult = Game::Difficulty >= DifficultyLevel::Insane ? 1.5f : 1;
        auto amount = robotInfo.AlertAwareness * ALERT_FREQUENCY * skillMult;
        AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.TargetPosition, amount);
        ai.AlertTimer = ALERT_FREQUENCY;
    }

    void UpdateCombatAI(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float dt) {
        DodgeProjectiles(Game::Level, robot, ai, robotInfo);

        auto pTarget = Game::GetObject(ai.Target);
        if (!pTarget) {
            // Target died or didn't have one, return to alert state and find a new one
            ai.State = AIState::Alert;
            return;
        }

        if (robot.Control.AI.Behavior != AIBehavior::Still || robotInfo.Attack == AttackType::Melee)
            MoveToCircleDistance(Game::Level, robot, ai, robotInfo);

        auto& target = *pTarget;
        auto targetPos = target.Position + target.Physics.Velocity * 0.25F; // lead target
        auto targetDir = GetDirection(targetPos, robot.Position);
        auto hasLos = HasLineOfSight(robot, target.Position);

        // Use the last known position as the target dir if target is obscured
        if (!hasLos || IsCloakEffective(target)) {
            if (!ai.TargetPosition) {
                SPDLOG_WARN("Robot {} had a target with no target position, clearing target", robot.Signature);
                ai.Target = {};
                return;
            }

            targetDir = GetDirection(ai.TargetPosition->Position, robot.Position);
        }

        // Track the known target position, even without LOS. Causes AI to look more intelligent by pre-aiming.
        TurnTowardsDirection(robot, targetDir, DifficultyInfo(robotInfo).TurnTime);

        // Update target location if it is in line of sight and not cloaked
        if ((hasLos && !target.IsCloaked()) || (hasLos && target.IsCloaked() && !IsCloakEffective(target))) {
            ai.LastSeenTargetPosition = ai.TargetPosition = { target.Segment, target.Position };
            ai.Awareness = AI_AWARENESS_MAX;
            ai.CombatState = AICombatState::Normal;
            ai.LostSightDelay = 0.25f; // Let the AI 'cheat' for 1 second after losing direct sight (object permeance?)

            // Try to get behind target unless dodging. Maybe make this only happen sometimes?
            if (robotInfo.GetBehind && robot.Control.AI.Behavior != AIBehavior::Still && ai.DodgeTime <= 0)
                GetBehindTarget(robot, ai, robotInfo, target);

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(ai.TargetPosition->Position, Color(1, 0, 0));

            AlertNearby(ai, robot, robotInfo);
            PlayCombatNoise(robot, ai);
        }
        else {
            ai.LostSightDelay -= dt;
            DecayAwareness(ai);
            // Robot can either choose to chase the target or hold position and blind fire

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(ai.TargetPosition->Position, Color(1, .5, .5));

            if (ai.CombatState == AICombatState::Normal && ai.StrafeTimer <= 0 && ai.LostSightDelay <= 0) {
                OnLostLineOfSight(ai, robot, robotInfo);
            }

            if (ai.CombatState == AICombatState::Wait) {
                // Get ready
                if (ai.TargetPosition) {
                    TurnTowardsPoint(robot, ai.TargetPosition->Position, DifficultyInfo(robotInfo).TurnTime);
                }
            }
            else if (ai.CombatState == AICombatState::Chase && ai.TargetPosition && ai.Fear < 1) {
                // Chasing a cloaked target does no good, AI just gets confused.
                // Also don't chase the player ghost
                if (!target.IsCloaked() && target.Type != ObjectType::Ghost && ai.ChaseTimer <= 0) {
                    Chat(robot, "Come back here!");
                    if (!ChaseTarget(ai, robot, *ai.TargetPosition, ChaseMode::StopAtPosition))
                        ai.ChaseTimer = 5.0f;
                }
            }

            // Prevent robots from exiting attack mode mid-charge
            if (ai.Awareness <= 0 && ai.WeaponCharge <= 0) {
                Chat(robot, "Stay on alert");
                ai.State = AIState::Alert;
                ai.Awareness = 1; // Reset awareness so robot stays alert for a while
                ai.BurstShots = 0; // Reset shot counter
            }
        }

        // Prevent attacking during phasing (matcens and teleports)
        if (ai.TargetPosition && !robot.IsPhasing()) {
            if (robotInfo.Attack == AttackType::Ranged)
                UpdateRangedAI(robot, robotInfo, ai, dt, !hasLos);
            else if (robotInfo.Attack == AttackType::Melee)
                UpdateMeleeAttackAI(robot, robotInfo, ai, target, targetDir, dt);
        }

        // Only robots that flee can find help. Limit to hotshot and above.
        if (robotInfo.FleeThreshold > 0 &&
            robot.Control.AI.Behavior != AIBehavior::Still &&
            ai.AnimationState == Animation::Alert && // Only check when not firing
            Game::Difficulty >= DifficultyLevel::Hotshot) {
            if (!ai.FleeTimer.IsSet()) {
                ai.FleeTimer = 2 + Random() * 2; // Periodically think about fleeing
            }

            if (ai.FleeTimer.Expired() && FleeingDrones == 0) {
                auto chance = Random(); // only flee half the time
                if (chance > 0.5 && (robot.HitPoints / robot.MaxHitPoints <= robotInfo.FleeThreshold || ai.Fear >= 1)) {
                    // Wounded or scared enough to flee, but would rather fight if there's allies nearby
                    //SPDLOG_INFO("Searching for help. Fighting allies: {}", DronesInCombat);

                    if (DronesInCombat <= AI_ALLY_FLEE_MIN) {
                        FindHelp(ai, robot);
                    }
                    else {
                        Chat(robot, "I'm scared but my friends are here");
                    }
                }

                ai.FleeTimer.Reset();
            }
        }
    }

    void UpdateMeleeCombatAI(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float dt) {
        DodgeProjectiles(Game::Level, robot, ai, robotInfo);

        auto pTarget = Game::GetObject(ai.Target);
        if (!pTarget) {
            // Target died or didn't have one, return to alert state and find a new one
            ai.State = AIState::Alert;
            return;
        }

        MoveToCircleDistance(Game::Level, robot, ai, robotInfo);

        auto& target = *pTarget;
        auto targetDir = GetDirection(target.Position, robot.Position);
        auto hasLos = HasLineOfSight(robot, target.Position);

        // Use the last known position as the target dir if target is obscured
        if (!hasLos || IsCloakEffective(target)) {
            if (!ai.TargetPosition) {
                SPDLOG_WARN("Robot {} had a target with no target position, clearing target", robot.Signature);
                ai.Target = {};
                return;
            }

            targetDir = GetDirection(ai.TargetPosition->Position, robot.Position);
        }

        // Track the known target position, even without LOS. Causes AI to look more intelligent by pre-aiming.
        TurnTowardsDirection(robot, targetDir, DifficultyInfo(robotInfo).TurnTime);

        // Update target location if it is in line of sight and not cloaked
        if ((hasLos && !target.IsCloaked()) || (hasLos && target.IsCloaked() && !IsCloakEffective(target))) {
            ai.TargetPosition = { target.Segment, target.Position };
            ai.Awareness = AI_AWARENESS_MAX;
            ai.CombatState = AICombatState::Normal;
            ai.LostSightDelay = 1.0f; // Let the AI 'cheat' for 1 second after losing direct sight (object permeance?)

            // Try to get behind target unless dodging. Maybe make this only happen sometimes?
            if (robotInfo.GetBehind && robot.Control.AI.Behavior != AIBehavior::Still && ai.DodgeTime <= 0)
                GetBehindTarget(robot, ai, robotInfo, target);

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(ai.TargetPosition->Position, Color(1, 0, 0));

            AlertNearby(ai, robot, robotInfo);
            PlayCombatNoise(robot, ai);
        }
        else {
            ai.LostSightDelay -= dt;
            DecayAwareness(ai);

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(ai.TargetPosition->Position, Color(1, .5, .5));

            // Chasing a cloaked target does no good, AI just gets confused.
            // Also don't chase the player ghost
            if (!target.IsCloaked() && target.Type != ObjectType::Ghost && ai.ChaseTimer <= 0) {
                if (Random() < robotInfo.ChaseChance) {
                    Chat(robot, "Pursuing hostile");
                    if (!ChaseTarget(ai, robot, *ai.TargetPosition, ChaseMode::StopAtPosition))
                        ai.ChaseTimer = 5.0f;
                }
                else {
                    ai.ChaseTimer = 5.0f;
                }
            }

            if (ai.CombatState == AICombatState::Normal && ai.StrafeTimer <= 0 && ai.LostSightDelay <= 0) {
                OnLostLineOfSight(ai, robot, robotInfo);
            }
        }

        // Prevent attacking during phasing (matcens and teleports)
        if (ai.TargetPosition && !robot.IsPhasing()) {
            UpdateMeleeAttackAI(robot, robotInfo, ai, target, targetDir, dt);
        }
    }

    void BeginAIFrame() {
        DronesInCombat = DronesInCombatCounter;
        DronesInCombatCounter = 0;

        FleeingDrones = FleeingDronesCounter;
        FleeingDronesCounter = 0;
    }

    void UpdateAlertAI(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float /*dt*/) {
        DodgeProjectiles(Game::Level, robot, ai, robotInfo);

        if (!ai.PlayingAnimation() && ai.AnimationState != Animation::Alert)
            PlayRobotAnimation(robot, Animation::Alert, 1);

        if (ScanForTarget(robot, ai) && ai.Target) {
            ai.State = AIState::Combat;
            ai.CombatState = AICombatState::Normal;
            Chat(robot, "I found a bad guy!");
            return; // Found a target, start firing!
        }

        // Turn towards point of interest if we have one
        if (ai.TargetPosition) {
            TurnTowardsPoint(robot, ai.TargetPosition->Position, DifficultyInfo(robotInfo).TurnTime);
            bool validState = ai.CombatState == AICombatState::Normal || ai.CombatState == AICombatState::Chase;

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(ai.TargetPosition->Position, Color(1, 0, 1));

            // Move around a little to look more alive
            if (ai.DodgeDelay <= 0) {
                ai.DodgeVelocity = RandomLateralDirection(robot) * 1.25f;
                ai.DodgeDelay = 2.0f + Random() * 0.5f;
                ai.DodgeTime = 0.6f;
            }

            if (validState && ai.ChaseTimer <= 0 &&
                ai.Awareness >= AI_AWARENESS_MAX &&
                robot.Control.AI.Behavior != AIBehavior::Still) {
                ai.ChaseTimer = AI_CURIOSITY_INTERVAL; // Only check periodically

                if (Random() < robotInfo.Curiosity) {
                    // Only path to target if we can't see it
                    if (!HasLineOfSight(robot, ai.TargetPosition->Position)) {
                        // todo: sometimes the target isn't reachable due to locked doors or walls, use other behaviors
                        // todo: limit chase segment depth so robot doesn't path around half the level
                        Chat(robot, "I better check it out");
                        ChaseTarget(ai, robot, *ai.TargetPosition, ChaseMode::StopVisible);
                    }
                }
                else {
                    Chat(robot, "I hear something but will wait here");
                }
            }
        }

        DecayAwareness(ai); // Decay awareness at end, otherwise combat/pathing never occurs

        if (ai.Awareness <= 0) {
            MakeIdle(ai);
            Chat(robot, "I'm bored...");
        }
    }

    void SupervisorBehavior(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float /*dt*/) {
        if (!Game::EnableAi()) return;

        // Periodically alert allies while not idle
        if (ai.State != AIState::Idle && ai.AlertTimer <= 0 && ai.TargetPosition) {
            PlayAlertSound(robot, ai);
            AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.TargetPosition, 10);
            ai.AlertTimer = 3;
            Chat(robot, "Intruder alert!");
        }

        // Supervisors are either in path mode or idle. They cannot peform any other action.
        if (ai.State == AIState::Path) {
            if (!PathTowardsGoal(robot, ai, false, false)) {
                ai.ClearPath();
                ai.State = AIState::Alert;
            }

            //MakeCombatNoise(robot, ai);
        }
        else {
            DecayAwareness(ai);

            if (ScanForTarget(robot, ai)) {
                auto target = Game::GetObject(ai.Target);
                ai.State = AIState::Path;
                ai.CombatState = AICombatState::Normal;
                ai.Path = GenerateRandomPath(robot.Segment, 15, NavigationFlag::OpenKeyDoors, target ? target->Segment : SegID::None);
                ai.PathIndex = 0;
                ai.Awareness = 1;
                Chat(robot, "Hostile sighted!");
            }
            else if (ai.Awareness <= 0 && ai.State != AIState::Idle) {
                MakeIdle(ai);
                Chat(robot, "All quiet");
            }
        }
    }

    void MineLayerBehavior(AIRuntime& ai, Object& robot, const RobotInfo& /*robotInfo*/, float /*dt*/) {
        if (!Game::EnableAi()) return;

        ScanForTarget(robot, ai);

        // Periodically alert allies while not idle
        //if (ai.State != AIState::Idle && ai.AlertTimer <= 0 && ai.TargetPosition) {
        //    AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.TargetPosition, 0.75f);
        //    ai.AlertTimer = 4 + Random() * 2;
        //    Sound3D sound(robotInfo.SeeSound);
        //    sound.Volume = 0.4f;
        //    Sound::PlayFrom(sound, robot);
        //}

        // Mine layers are either in path mode or idle. They cannot peform any other action.
        if (ai.State == AIState::Path) {
            if (!ai.PlayingAnimation() && ai.AnimationState != Animation::Alert)
                PlayRobotAnimation(robot, Animation::Alert);

            if (!PathTowardsGoal(robot, ai, false, false)) {
                ai.ClearPath();
                ai.State = AIState::Alert;
            }

            if (ai.FireDelay <= 0) {
                auto weapon = robot.Control.AI.SmartMineFlag() ? WeaponID::SmartMine : WeaponID::ProxMine;
                Game::FireWeaponInfo info = { .id = weapon, .gun = 0, .showFlash = false };
                Game::FireWeapon(robot, info);
                ai.FireDelay = AI_MINE_LAYER_DELAY;
            }

            PlayCombatNoise(robot, ai);
        }
        else if (ai.Awareness > 0 && ai.Path.empty()) {
            // Keep pathing until awareness fully decays
            auto target = Game::GetObject(ai.Target);
            ai.State = AIState::Path;
            ai.CombatState = AICombatState::Normal;
            ai.Path = GenerateRandomPath(robot.Segment, 6, NavigationFlag::None, target ? target->Segment : SegID::None);

            // If path is short, it might be due to being cornered by the player. Try again ignoring the player.
            if (ai.Path.size() < 3)
                ai.Path = GenerateRandomPath(robot.Segment, 6, NavigationFlag::None);

            ai.PathIndex = 0;
            ai.AlertTimer = 1 + Random() * 2;
            ai.FireDelay = AI_MINE_LAYER_DELAY * Random();
        }

        if (ai.Awareness <= 0 && ai.State != AIState::Idle) {
            // Go to sleep
            ai.ClearPath();
            PlayRobotAnimation(robot, Animation::Rest);
            MakeIdle(ai);
        }

        DecayAwareness(ai, 1 / 10.0f); // 10 second awake time
    }

    void UpdateMatcenPathing(AIRuntime& ai, Object& robot) {
        bool fromMatcen = robot.SourceMatcen != MatcenID::None;

        // Check if reached goal
        if (!PathTowardsGoal(robot, ai, false, fromMatcen)) {
            ai.ClearPath();
            ai.State = AIState::Alert;
        }

        if (ai.State == AIState::MatcenPath) {
            // Saw an enemy
            if (ScanForTarget(robot, ai)) {
                ai.ClearPath();
                ai.State = AIState::Combat;
            }
        }

        // todo: mode to follow path while fighting
        // Stop pathing if an enemy is seen and not still in the matcen
        //  !ai.Path.empty() && robot.Segment != ai.Path.front().Segment
        //bool canStopPathing = !fromMatcen ? true : !ai.Path.empty() && robot.Segment != ai.Path.front().Segment;
        //if (canStopPathing && ScanForTarget(robot, ai)) {
        //    ai.ClearPath();
        //    ai.State = AIState::Combat;
        //}
    }

    void DefaultBehavior(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float dt) {
        if (!Game::EnableAi()) return;

        switch (ai.State) {
            case AIState::Idle:
                OnIdle(ai, robot, robotInfo);
                break;
            case AIState::Alert:
                UpdateAlertAI(ai, robot, robotInfo, dt);
                break;
            case AIState::Combat:
                UpdateCombatAI(ai, robot, robotInfo, dt);
                break;
            case AIState::Roam:
                break;
            case AIState::Path:
            case AIState::MatcenPath:
                UpdateMatcenPathing(ai, robot);
                break;
            case AIState::FindHelp:
                UpdateFindHelp(ai, robot);
                break;
            case AIState::Chase:
                DodgeProjectiles(Game::Level, robot, ai, robotInfo);

                if (ai.Path.empty()) {
                    ai.State = AIState::Alert;
                }
                else {
                    // Stop chasing once robot can see source of sound, otherwise move to the location.
                    // This is so a fleeing player is pursued around corners
                    bool stopOnceVisible = ai.Chase == ChaseMode::StopVisible;
                    PathTowardsGoal(robot, ai, true, stopOnceVisible);
                    //Chat(robot, "Pathing...");

                    if (ai.TargetPosition && ai.TargetPosition->Segment == robot.Segment) {
                        // Clear target if pathing towards it discovers the target isn't there.
                        // This is so the robot doesn't turn around while chasing
                        ai.TargetPosition = {};
                    }

                    bool throughWall = false;
                    if (ScanForTarget(robot, ai, &throughWall) && (!throughWall || robotInfo.Attack == AttackType::Ranged)) {
                        ai.ClearPath(); // Stop chasing if robot finds a target
                        ai.State = AIState::Combat;
                        Chat(robot, "You can't hide from me!");
                    }
                }
                break;
            default: ;
        }
    }

    void UpdateRobotAI(Object& robot, float dt) {
        auto& ai = GetAI(robot);
        auto& robotInfo = Resources::GetRobotInfo(robot.ID);

        // Reset thrust accumulation
        robot.Physics.Thrust = Vector3::Zero;
        robot.Physics.AngularThrust = Vector3::Zero;
        ai.Velocity = Vector3::Zero;

        auto decr = [&dt](float& value) {
            value -= dt;
            if (value < 0) value = 0;
        };

        decr(ai.RemainingSlow);
        decr(ai.RemainingStun);
        decr(ai.MeleeHitDelay);

        // bit of a hack to clear no-collide from spawned robots
        if (HasFlag(robot.Physics.Flags, PhysicsFlag::NoCollideRobots) && Game::Time >= robot.NextThinkTime)
            ClearFlag(robot.Physics.Flags, PhysicsFlag::NoCollideRobots);

        // Bosses have their own death roll
        if (robot.HitPoints <= 0 && robotInfo.DeathRoll > 0 && !robotInfo.IsBoss) {
            ai.DeathRollTimer += dt;
            auto duration = (float)std::min(robotInfo.DeathRoll / 2 + 1, 6);
            auto volume = robotInfo.IsBoss ? 2 : robotInfo.DeathRoll / 4.0f;
            bool explode = DeathRoll(robot, duration, ai.DeathRollTimer, robotInfo.DeathRollSound,
                                     ai.DyingSoundPlaying, volume, dt);

            if (explode) {
                AlertAlliesOfDeath(robot);
                ExplodeObject(robot);

                // explode object, create sound
                if (Game::LevelNumber < 0) {
                    // todo: respawn thief on secret levels
                }
            }
            return; // Can't act while dying
        }

        if (ai.RemainingStun > 0)
            return; // Can't act while stunned
        //else if (HasFlag(robot.Physics.Flags, PhysicsFlag::Gravity))
        //    ClearFlag(robot.Physics.Flags, PhysicsFlag::Gravity); // Unstunned

        AnimateRobot(robot, ai, dt);

        if (robot.NextThinkTime == NEVER_THINK || robot.NextThinkTime > Game::Time)
            return;

        if (robotInfo.IsBoss && Game::Level.IsDescent1())
            Game::BossBehaviorD1(ai, robot, robotInfo, dt);
        else if (robotInfo.Script == "Supervisor")
            SupervisorBehavior(ai, robot, robotInfo, dt);
        else if (robot.Control.AI.Behavior == AIBehavior::RunFrom)
            MineLayerBehavior(ai, robot, robotInfo, dt);
        else
            DefaultBehavior(ai, robot, robotInfo, dt);

        if (ai.DodgeTime > 0 && ai.DodgeVelocity != Vector3::Zero && Game::EnableAi())
            ai.Velocity += ai.DodgeVelocity;

        ai.Awareness = std::clamp(ai.Awareness, 0.0f, 1.0f);

        // Force aware robots to always update
        SetFlag(robot.Flags, ObjectFlag::AlwaysUpdate, ai.State != AIState::Idle);

        //ClampThrust(robot, ai);
        ApplyVelocity(robot, ai, dt);
        ai.LastUpdate = Game::Time;

        if (ai.State == AIState::Combat || ai.State == AIState::FindHelp /*|| ai.State == AIState::Alert*/)
            DronesInCombatCounter++;

        if (ai.State == AIState::FindHelp)
            FleeingDronesCounter++;
    }

    void UpdateAI(Object& obj, float dt) {
        if (obj.Type == ObjectType::Robot) {
            Game::Debug::ActiveRobots++;
            //Render::Debug::DrawPoint(obj.Position, Color(1, 0, 0));
            UpdateRobotAI(obj, dt);
        }
        else if (obj.Type == ObjectType::Reactor) {
            Game::UpdateReactorAI(obj, dt);
        }
    }

    float AIRuntime::GetDeltaTime() const {
        // the update rate of AI can vary based on state, so calculate it here
        return float(Game::Time - LastUpdate);
    }
}
