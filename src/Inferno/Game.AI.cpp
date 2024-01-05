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
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.Particles.h"

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
    void Chat(const Object& robot, const string_view fmt, Args&&... args) {
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

    const RobotDifficultyInfo& Difficulty(const RobotInfo& info) {
        return info.Difficulty[Game::Difficulty];
    }

    uint CountNearbyAllies(const Object& robot, float range, bool inCombat = false) {
        uint allies = 0;
        auto range2 = range * range;

        IterateNearbySegments(Game::Level, robot, range, IterateFlags::StopWall, [&](const Segment& seg, bool) {
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

                if (HasLineOfSight(*obj, source.Position)) {
                    ai.AddAwareness(awareness * falloff);
                }
                else {
                    ai.AddAwareness(awareness * falloff * 0.5f);
                }

                //auto prevAwareness = ai.Awareness;
                ai.TargetPosition = source;
                obj->NextThinkTime = 0;

                // Update chase target if we hear something
                if (ai.State == AIState::Chase)
                    ChaseTarget(ai, *obj, *ai.TargetPosition, ChaseMode::Sound);
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
        IterateNearbySegments(Game::Level, source, soundRadius, IterateFlags::StopDoor, [&](const Segment& seg, bool) {
            AlertEnemiesInSegment(Game::Level, seg, source, soundRadius, awareness);
        });
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

    bool PointIsInFOV(const Object& robot, const Vector3& pointDir, const RobotInfo& robotInfo) {
        auto dot = robot.Rotation.Forward().Dot(pointDir);
        auto& diff = robotInfo.Difficulty[Game::Difficulty];
        return dot >= diff.FieldOfView;
    }

    // Returns true if object can see a point
    bool HasLineOfSight(const Object& obj, const Vector3& point, bool precise) {
        auto [dir, dist] = GetDirectionAndDistance(point, obj.Position);
        LevelHit hit{};
        Ray ray = { obj.Position, dir };
        RayQuery query{ .MaxDistance = dist, .Start = obj.Segment, .Mode = precise ? RayQueryMode::Precise : RayQueryMode::Visibility };
        return !Game::Intersect.RayLevel(ray, query, hit);
    }

    // Returns true if gun has precise visibility to a target
    bool HasFiringLineOfSight(const Object& obj, uint8 gun, const Vector3& target, ObjectMask mask) {
        auto gunPosition = GetGunpointWorldPosition(obj, gun);

        auto [dir, distance] = GetDirectionAndDistance(target, gunPosition);
        LevelHit hit{};
        RayQuery query{ .MaxDistance = distance, .Start = obj.Segment, .Mode = RayQueryMode::Precise };
        bool visible = !Game::Intersect.RayLevel({ gunPosition, dir }, query, hit, mask, Game::GetObjectRef(obj).Id);
        //Render::Debug::DrawLine(gunPosition, target, visible ? Color(0, 1, 0) : Color(1, 0, 0));
        return visible;
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
                if (auto e = Render::EffectLibrary.GetExplosion(effect)) {
                    // Larger periodic explosions with sound
                    //e->Variance = obj.Radius * 0.75f;
                    e->Parent = Game::GetObjectRef(obj);
                    e->Volume = volume;
                    Render::CreateExplosion(*e, obj.Segment, obj.Position);
                }
            }
        }
        else if (Random() < dt * 8) {
            // Winding up, create fireballs on object
            auto effect = ri.IsBoss ? "boss small fireball" : "small fireball";
            if (auto e = Render::EffectLibrary.GetExplosion(effect)) {
                //e->Variance = obj.Radius * 0.65f;
                e->Parent = Game::GetObjectRef(obj);
                e->Volume = volume;
                Render::CreateExplosion(*e, obj.Segment, obj.Position);
            }
        }

        return elapsedTime > rollDuration;
    }

    void MoveTowardsPoint(const Object& robot, AIRuntime& ai, const Vector3& point, float scale) {
        auto dir = point - robot.Position;
        dir.Normalize();
        auto& info = Resources::GetRobotInfo(robot);
        ai.Velocity += dir * Difficulty(info).Speed * scale;
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

        if (weapon.Speed[Game::Difficulty] > FAST_WEAPON_SPEED)
            return target.Position; // Don't lead with fast weapons (vulcan, gauss, drillers). Unfair to player.

        auto targetDist = Vector3::Distance(target.Position, robot.Position);
        Vector3 targetVelDir;
        target.Physics.Velocity.Normalize(targetVelDir);
        float expectedTravelTime = targetDist / weapon.Speed[Game::Difficulty];
        auto projectedTarget = target.Position;

        {
            // Check target projected position
            Ray targetTrajectory(target.Position, targetVelDir);
            RayQuery query;
            query.MaxDistance = (target.Physics.Velocity * expectedTravelTime).Length();
            query.Start = target.Segment;
            LevelHit hit;
            if (Game::Intersect.RayLevel(targetTrajectory, query, hit)) {
                // target will hit wall, aim at wall minus object radius
                projectedTarget = hit.Point - targetVelDir * target.Radius;
                targetDist = Vector3::Distance(projectedTarget, robot.Position);
                expectedTravelTime = targetDist / weapon.Speed[Game::Difficulty];
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

    void FireRobotWeapon(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, Vector3 target, bool primary, bool blind, bool lead) {
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

        {
            // todo: seismic disturbance inaccuracy from earthshaker

            // Randomize target based on aim. 255 -> 1, 0 -> 8
            auto aim = 8.0f - 7.0f * FixToFloat(robotInfo.Aim << 8);
            aim += float(4 - Game::Difficulty) * 0.5f; // Add inaccuracy based on difficulty (2 to 0)

            if (Game::ControlCenterDestroyed) {
                // 1 to 3.0f as timer counts down
                auto seismic = 1.0f + (16 - std::min(Game::CountdownSeconds, 16)) / 8.0f;
                aim += seismic * 6;
            }

            target += { RandomN11() * aim, RandomN11() * aim, RandomN11() * aim };
        }

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

        // Check that the target point is in front of the gun, otherwise set it to shoot straight
        Plane plane(gunPosition, forward);
        if (plane.DotCoordinate(target) <= 0) {
            SPDLOG_WARN("Robot tried to shoot backwards");
            target = gunPosition + forward * 20;
        }

        // Fire the weapon
        auto targetDir = target - gunPosition;
        targetDir.Normalize();
        auto id = Game::GetObjectRef(robot);
        Game::FireWeapon(id, weaponId, gun, &targetDir);

        if (primary)
            CycleGunpoint(robot, ai, robotInfo);
    }

    void DodgeProjectile(const Object& robot, AIRuntime& ai, const Object& projectile, const RobotInfo& robotInfo) {
        if (projectile.Physics.Velocity.LengthSquared() < 5 * 5) return; // Don't dodge slow projectiles. also prevents crash at 0 velocity.

        auto [projDir, projDist] = GetDirectionAndDistance(projectile.Position, robot.Position);
        // Looks weird to dodge distant projectiles. also they might hit another target
        // Consider increasing this for massive robots?
        if (projDist > AI_MAX_DODGE_DISTANCE) return;
        if (!PointIsInFOV(robot, projDir, robotInfo)) return;

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

        ai.DodgeVelocity = dodgeDir * Difficulty(robotInfo).EvadeSpeed * 30;
        ai.DodgeDelay = (5 - Game::Difficulty) / 2.0f + 0.25f + Random() * 0.5f; // (2 to 0) + 0.25 + (0..0.5) delay
        ai.DodgeTime = AI_DODGE_TIME * 0.5f + AI_DODGE_TIME * 0.5f * Random();

        if (robotInfo.FleeThreshold > 0 && ai.State == AIState::Combat)
            ai.Fear += 0.4f; // Scared of being hit
    }

    void CheckProjectiles(Level& level, const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (ai.DodgeDelay > 0) return; // not ready to dodge again

        IterateNearbySegments(level, robot, 100, IterateFlags::StopOpaqueWall, [&](const Segment& seg, bool) {
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
                           AIRuntime& ai, const Vector3& objDir) {
        if (!ai.TargetPosition) return;

        if (HasLineOfSight(robot, ai.TargetPosition->Position)) {
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

        auto circleDistance = Difficulty(robotInfo).CircleDistance;
        auto [dir, dist] = GetDirectionAndDistance(ai.TargetPosition->Position, robot.Position);
        auto minDist = std::min(circleDistance * 0.75f, circleDistance - 10);
        auto maxDist = std::max(circleDistance * 1.25f, circleDistance + 10);

        if (robotInfo.Attack == AttackType::Ranged && (dist > minDist || dist < maxDist))
            return; // in deadzone, no need to move. Otherwise robots clump up on each other.
        else if (robotInfo.Attack == AttackType::Melee && dist < circleDistance)
            return;

        if (dist > circleDistance)
            MoveTowardsTarget(level, robot, ai, dir);
        else
            MoveAwayFromTarget(ai.TargetPosition->Position, robot, ai);
    }

    void PlayRobotAnimation(const Object& robot, AnimState state, float time, float moveMult, float delay) {
        auto& robotInfo = Resources::GetRobotInfo(robot);
        auto& angles = robot.Render.Model.Angles;

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
                //auto& goalAngle = robotJoints[j].Angle;
                auto& angle = angles[joint.ID];
                Vector3 jointAngle = joint.Angle;

                if (angle == jointAngle * moveMult) {
                    ai.DeltaAngles[joint.ID] = Vector3::Zero;
                    continue;
                }

                ai.GoalAngles[joint.ID] = jointAngle;
                ai.DeltaAngles[joint.ID] = jointAngle * moveMult - angle;
            }

            //if (atGoal) {
            //    ail.AchievedState[gun] = ail.GoalState[gun];
            //    if (ail.AchievedState[gun] == AIState::Recoil)
            //        ail.GoalState[gun] = AIState::Fire;

            //    if (ail.AchievedState[gun] == AIState::Flinch)
            //        ail.GoalState[gun] = AIState::Lock;
            //}
        }
    }

    void AnimateRobot(Object& robot, AIRuntime& ai, float dt) {
        assert(robot.IsRobot());
        auto& model = Resources::GetModel(robot.Render.Model.ID);

        ai.AnimationTimer += dt;
        if (ai.AnimationTimer > ai.AnimationDuration || ai.AnimationTimer < 0) return;

        for (int joint = 1; joint < model.Submodels.size(); joint++) {
            auto& curAngle = robot.Render.Model.Angles[joint];
            curAngle += ai.DeltaAngles[joint] / ai.AnimationDuration * dt;
        }
    }

    void RobotTouchObject(const Object& robot, const Object& obj) {
        ASSERT(robot.IsRobot());

        auto& ai = GetAI(robot);

        if (obj.IsRobot() || obj.IsPlayer()) {
            ai.LastCollision = Game::Time;
        }

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
            if (source->IsPlayer()) {
                // We were hit by the player but don't know exactly where they are
                ai.TargetPosition = sourcePos;
                ai.LastHitByPlayer = 0;
                ai.Awareness = AI_AWARENESS_MAX;
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
            float damageScale = 1 - (info.HitPoints - damage * stunMult) / info.HitPoints; // percentage of life dealt
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
                PlayRobotAnimation(robot, AnimState::Flinch, 0.2f);

                if (auto beam = Render::EffectLibrary.GetBeamInfo("stunned object arcs")) {
                    auto startObj = Game::GetObjectRef(robot);
                    beam->Radius = { robot.Radius * 0.6f, robot.Radius * 0.9f };
                    Render::AddBeam(*beam, stunTime, startObj);
                    beam->StartDelay = stunTime / 3;
                    Render::AddBeam(*beam, stunTime - beam->StartDelay, startObj);
                    beam->StartDelay = stunTime * 2 / 3;
                    Render::AddBeam(*beam, stunTime - beam->StartDelay, startObj);
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
        auto leadChance = Game::Difficulty / 4.0f; // 50% on hotshot, 75% on ace, 100% on insane
        bool shouldLead = Random() <= leadChance * 0.9f; // Don't always lead even on insane, keep the player guessing
        if (Game::Difficulty < 2) shouldLead = false; // Don't lead on rookie and trainee, also weapons are too slow to meaningfully lead.
        return shouldLead;
    }

    void FireRobotPrimary(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const Vector3& target, bool blind) {
        ai.FireDelay = 0;

        // multishot: consume as many projectiles as possible based on burst count
        // A multishot of 1 and a burst of 3 would fire 2 projectiles then 1 projectile
        // Multishot incurs extra fire delay per projectile
        auto burstDelay = std::min(1 / 8.0f, Difficulty(robotInfo).FireDelay / 2);
        if (ai.Angry) burstDelay *= AI_ANGER_SPEED;

        auto shouldLead = RollShouldLead(); // only roll once per fire

        for (int i = 0; i < robotInfo.Multishot; i++) {
            // Use an even lower burst delay when angry
            ai.FireDelay += burstDelay;

            FireRobotWeapon(robot, ai, robotInfo, target, true, blind, shouldLead);
            ai.BurstShots++;
            if (ai.BurstShots >= Difficulty(robotInfo).ShotCount) {
                ai.BurstShots = 0;
                auto fireDelay = Difficulty(robotInfo).FireDelay;
                ai.FireDelay += ai.Angry ? fireDelay * AI_ANGER_SPEED : fireDelay;
                ai.FireDelay -= burstDelay; // undo burst delay if this was the last shot
                break; // Ran out of shots
            }
        }

        PlayRobotAnimation(robot, AnimState::Recoil, 0.25f);
    }

    // start charging when player is in FOV and can fire
    // keep charging even if player goes out of view
    // fire at last known location
    void WeaponChargeBehavior(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float dt) {
        ai.NextChargeSoundDelay -= dt;
        ai.WeaponCharge += dt;

        if (ai.NextChargeSoundDelay <= 0) {
            ai.NextChargeSoundDelay = 0.125f + Random() / 8;

            if (auto fx = Render::EffectLibrary.GetSparks("robot_fusion_charge")) {
                fx->Parent = Game::GetObjectRef(robot);
                Sound3D sound(SoundID::FusionWarmup);
                sound.Radius = AI_SOUND_RADIUS;
                ai.SoundHandle = Sound::PlayFrom(sound, robot);

                for (uint8 i = 0; i < robotInfo.Guns; i++) {
                    fx->ParentSubmodel.Offset = GetGunpointOffset(robot, i);
                    Render::AddSparkEmitter(*fx, robot.Segment);
                }
            }
        }

        //if (ai.WeaponCharge >= Difficulty(info).FireDelay * 2) {
        if (ai.WeaponCharge >= 1) {
            Sound::Stop(ai.SoundHandle);
            // Release shot even if target has moved out of view
            auto target = ai.TargetPosition ? ai.TargetPosition->Position : robot.Position + robot.Rotation.Forward() * 40;
            bool blind = false; // this is not correct
            FireRobotPrimary(robot, ai, robotInfo, target, blind);

            ai.WeaponCharge = 0;
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

            if (Game::Intersect.RayLevel(ray, query, hit)) {
                ai.StrafeAngle = -1;
                ai.StrafeTimer = 0.125f;
                return; // Try again
            }
        }

        ai.Velocity += dir * Difficulty(robotInfo).Speed * .25f;
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

            if (Game::Intersect.RayLevel(ray, query, hit)) {
                // flip direction and try again
                ai.StrafeDir *= -1;

                if (Game::Intersect.RayLevel(ray, query, hit)) {
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
        ai.Velocity += ai.StrafeDir * Difficulty(robotInfo).Speed * 0.5f;
    }

    void UpdateRangedAI(const Object& robot, const RobotInfo& robotInfo, AIRuntime& ai, float dt, bool blind) {
        if (ai.CombatState == AICombatState::Wait && blind)
            return; // Don't allow supressing fire when waiting

        if (robotInfo.WeaponType2 != WeaponID::None && ai.FireDelay2 <= 0) {
            // Check if an ally robot is in the way
            if (!HasFiringLineOfSight(robot, 0, ai.TargetPosition->Position, ObjectMask::Robot)) {
                //WiggleRobot(robot, ai, 0.5f);
                CircleStrafe(robot, ai, robotInfo);
                return;
            }

            // Secondary weapons have no animations or wind up
            FireRobotWeapon(robot, ai, robotInfo, ai.TargetPosition->Position, false, blind, false);
            ai.FireDelay2 = Difficulty(robotInfo).FireDelay2;
        }
        else {
            if (robotInfo.Guns == 0) return; // Can't shoot, I have no guns!

            if (ai.AnimationState != AnimState::Fire && !ai.PlayingAnimation()) {
                PlayRobotAnimation(robot, AnimState::Alert, 1.0f);
            }

            auto& weapon = Resources::GetWeapon(robotInfo.WeaponType);

            if (ai.AnimationState != AnimState::Fire && ai.FireDelay < 0.25f) {
                // Check if an ally robot is in the way
                if (!HasFiringLineOfSight(robot, ai.GunIndex, ai.TargetPosition->Position, ObjectMask::Robot)) {
                    CircleStrafe(robot, ai, robotInfo);
                    CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                    ai.FireDelay = 0.25f + 1 / 8.0f; // Try again in 1/8th of a second
                    return;
                }

                auto aimDir = ai.TargetPosition->Position - robot.Position;
                aimDir.Normalize();

                if (AngleBetweenVectors(aimDir, robot.Rotation.Forward()) <= robotInfo.AimAngle * DegToRad * 0.5f) {
                    // Target is within the cone of the weapon, start firing
                    PlayRobotAnimation(robot, AnimState::Fire, ai.FireDelay.Remaining() * 0.8f);
                }
            }
            else if (ai.AnimationState == AnimState::Fire && weapon.Extended.Chargable) {
                WeaponChargeBehavior(robot, ai, robotInfo, dt); // Charge up during fire animation
            }
            else if (ai.FireDelay <= 0 && !ai.PlayingAnimation()) {
                // Check that the target hasn't gone out of LOS when using explosive weapons.
                // Robots can easily blow themselves up in this case.
                //if (weapon.SplashRadius > 0 && !HasLineOfSight(robot, ai.GunIndex, ai.TargetPosition->Position, ObjectMask::Robot)) {
                //    CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                //    //WiggleRobot(robot, ai, 0.5f);
                //    return;
                //}

                // Fire animation finished, release a projectile
                FireRobotPrimary(robot, ai, robotInfo, ai.TargetPosition->Position, blind);
            }
        }
    }

    void UpdateMeleeAI(const Object& robot, const RobotInfo& robotInfo, AIRuntime& ai,
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
                if (ai.AnimationState == AnimState::Flinch) {
                    // got stunned while charging weapon, reset swing
                    PlayRobotAnimation(robot, AnimState::Alert, BACKSWING_TIME);
                    ai.ChargingWeapon = false;
                    ai.FireDelay = Difficulty(robotInfo).FireDelay;
                }
                else if (ai.BurstShots > 0) {
                    // Alternate between fire and recoil when attacking multiple times
                    auto nextAnim = ai.AnimationState == AnimState::Fire ? AnimState::Recoil : AnimState::Fire;
                    auto animTime = BACKSWING_TIME * (0.4f + Random() * 0.25f);
                    PlayRobotAnimation(robot, nextAnim, animTime);
                    ai.FireDelay = ai.MeleeHitDelay = animTime * 0.5f;
                }
                else if (ai.AnimationState == AnimState::Fire) {
                    // Arms are raised
                    if (dist < robot.Radius + MELEE_RANGE) {
                        // Player moved close enough, swing
                        PlayRobotAnimation(robot, AnimState::Recoil, MELEE_SWING_TIME);
                        ai.MeleeHitDelay = MELEE_SWING_TIME / 2;
                    }
                    else if (dist > robot.Radius + BACKSWING_RANGE && ai.WeaponCharge > MELEE_GIVE_UP) {
                        // Player moved out of range for too long, give up
                        PlayRobotAnimation(robot, AnimState::Alert, BACKSWING_TIME);
                        ai.ChargingWeapon = false;
                        ai.FireDelay = Difficulty(robotInfo).FireDelay;
                    }
                }
            }
            else {
                // Reset to default
                PlayRobotAnimation(robot, AnimState::Alert, 0.3f);
            }
        }

        if (ai.AnimationState == AnimState::Recoil || ai.BurstShots > 0) {
            if (ai.ChargingWeapon && ai.MeleeHitDelay <= 0) {
                if (ai.BurstShots + 1 < Difficulty(robotInfo).ShotCount) {
                    ai.MeleeHitDelay = 10; // Will recalculate above when picking animations
                    ai.BurstShots++;
                }
                else {
                    ai.FireDelay = Difficulty(robotInfo).FireDelay;
                    ai.ChargingWeapon = false;
                    ai.BurstShots = 0;
                }

                // Is target in range and in front of the robot?
                if (dist < robot.Radius + MELEE_RANGE && targetDir.Dot(robot.Rotation.Forward()) > 0) {
                    auto soundId = Game::Level.IsDescent1() ? (RandomInt(1) ? SoundID::TearD1_01 : SoundID::TearD1_02) : SoundID::TearD1_01;
                    Sound::Play(Sound3D(soundId), robot);
                    Game::Player.ApplyDamage(Difficulty(robotInfo).MeleeDamage, false); // todo: make this generic. Damaging object should update the linked player

                    target.Physics.Velocity += targetDir * 5; // shove the target backwards
                    ai.Awareness = 1; // Hit something, reset awareness (cloaked targets)

                    if (auto sparks = Render::EffectLibrary.GetSparks("melee hit")) {
                        auto position = robot.Position + targetDir * robot.Radius;
                        Render::AddSparkEmitter(*sparks, robot.Segment, position);

                        Render::DynamicLight light{};
                        light.LightColor = sparks->Color * .4f;
                        light.Radius = 18;
                        light.Position = position;
                        light.Duration = light.FadeTime = 0.5f;
                        light.Segment = robot.Segment;
                        Render::AddDynamicLight(light);
                    }
                }
            }
        }
        else if (ai.FireDelay <= 0 && dist < robot.Radius + BACKSWING_RANGE && !ai.ChargingWeapon) {
            PlayRobotAnimation(robot, AnimState::Fire, BACKSWING_TIME); // raise arms to attack
            ai.ChargingWeapon = true;
            ai.WeaponCharge = 0;
            ai.BurstShots = 0;
        }
    }

    // Moves a robot towards a direction
    void MoveTowardsDir(Object& robot, const Vector3& dir, float dt, float scale) {
        scale = std::min(1.0f, scale);
        auto& aiInfo = Resources::GetRobotInfo(robot);
        Vector3 idealVel = dir * Difficulty(aiInfo).Speed * scale;
        Vector3 deltaVel = idealVel - robot.Physics.Velocity;
        float deltaSpeed = deltaVel.Length();
        deltaVel.Normalize();
        float maxDeltaVel = Difficulty(aiInfo).Speed; // todo: new field. this is between 0.5 and 2 of the base velocity
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
        float maxDeltaSpeed = dt * Difficulty(robotInfo).Speed * slowScale;

        if (deltaSpeed > maxDeltaSpeed)
            robot.Physics.Velocity += deltaVel * maxDeltaSpeed * 2; // x2 so max velocity is actually reached
        else
            robot.Physics.Velocity = idealVel;

        auto speed = robot.Physics.Velocity.Length();
        auto maxSpeed = Difficulty(robotInfo).Speed;
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

    bool ScanForTarget(const Object& robot, AIRuntime& ai) {
        // For now always use the player object
        // Instead this should scan nearby targets (other robots or players)
        auto& target = Game::GetPlayerObject();
        if (target.Type == ObjectType::Ghost) return false;

        auto [targetDir, targetDist] = GetDirectionAndDistance(target.Position, robot.Position);
        if (target.IsCloakEffective() || !HasLineOfSight(robot, target.Position))
            return false;

        auto& robotInfo = Resources::GetRobotInfo(robot);
        if (!PointIsInFOV(robot, targetDir, robotInfo))
            return false;

        float falloff = 1.0f;
        // Add a distance falloff, but don't go to zero even at max range
        if (targetDist > AI_VISION_FALLOFF_NEAR)
            falloff = 1 - Saturate((targetDist - AI_VISION_FALLOFF_NEAR) / (AI_VISION_FALLOFF_FAR - AI_VISION_FALLOFF_NEAR)) * AI_VISION_MAX_PENALTY;

        // Account for visibility, but only when not very close
        if (targetDist > AI_VISION_FALLOFF_NEAR * .5f)
            falloff *= Game::Player.GetShipVisibility();

        auto reactionTime = AI_REACTION_TIME * (5 - Game::Difficulty);
        ai.Awareness += falloff * ai.GetDeltaTime() / reactionTime;
        ai.Awareness = Saturate(ai.Awareness);

        ai.Target = Game::GetObjectRef(target);
        ai.TargetPosition = { target.Segment, target.Position };
        return true;
    }

    void OnIdle(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo) {
        ScanForTarget(robot, ai);

        if (ai.Awareness >= 1 && ai.Target) {
            // Delay weapons so robots don't shoot immediately on waking up
            ai.FireDelay = Difficulty(robotInfo).FireDelay * .4f;
            ai.FireDelay2 = Difficulty(robotInfo).FireDelay2 * .4f;

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
            if (!ai.PlayingAnimation() && ai.AnimationState != AnimState::Rest)
                PlayRobotAnimation(robot, AnimState::Rest);

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
        ai.TargetPosition = {}; // Clear target if robot loses interest.
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

        IterateNearbySegments(Game::Level, robot, AI_HELP_SEARCH_RADIUS, IterateFlags::StopLockedDoor, action);

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
    void OnLostLineOfSight(AIRuntime& ai, const Object& robot, const RobotInfo& robotInfo) {
        if (Game::Difficulty < 2) {
            ai.CombatState = AICombatState::Wait; // Just wait on trainee and rookie
            Chat(robot, "Holding position");
            return;
        }

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
        if (roll < chaseChance)
            ai.CombatState = AICombatState::Chase;
        else if (roll < chaseChance + suppressChance)
            ai.CombatState = AICombatState::BlindFire; // Calls the normal firing AI
        else
            ai.CombatState = AICombatState::Wait;
    }

    void AlertNearby(AIRuntime& ai, const Object& robot, const RobotInfo& robotInfo) {
        if (Game::Difficulty <= 0)
            return; // Don't alert on trainee

        if (ai.AlertTimer > 0 || !ai.TargetPosition || robotInfo.AlertRadius <= 0)
            return;

        constexpr float ALERT_FREQUENCY = 0.2f; // Smooth out alerts
        auto skillMult = Game::Difficulty == 4 ? 1.5f : 1;
        auto amount = robotInfo.AlertAwareness * ALERT_FREQUENCY * skillMult;
        AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.TargetPosition, amount);
        ai.AlertTimer = ALERT_FREQUENCY;
    }

    void UpdateCombatAI(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float dt) {
        CheckProjectiles(Game::Level, robot, ai, robotInfo);

        // Shouldn't combat only happen when a target exists?
        auto pTarget = Game::GetObject(ai.Target);
        if (!pTarget) {
            // Target died or didn't have one, return to alert state and find a new one
            ai.State = AIState::Alert;
            return;
        }

        if (robot.Control.AI.Behavior != AIBehavior::Still || robotInfo.Attack == AttackType::Melee)
            MoveToCircleDistance(Game::Level, robot, ai, robotInfo);

        auto& target = *pTarget;
        auto targetDir = GetDirection(target.Position, robot.Position);
        auto hasLos = HasLineOfSight(robot, target.Position);

        // Use the last known position as the target dir if target is obscured
        if (!hasLos || target.IsCloakEffective()) {
            if (!ai.TargetPosition) {
                SPDLOG_WARN("Robot {} had a target with no target position, clearing target", robot.Signature);
                ai.Target = {};
                return;
            }

            targetDir = GetDirection(ai.TargetPosition->Position, robot.Position);
        }

        // Track the known target position, even without LOS. Causes AI to look more intelligent by pre-aiming.
        TurnTowardsDirection(robot, targetDir, Difficulty(robotInfo).TurnTime);

        // Update target location if it is in line of sight and not cloaked
        if ((hasLos && !target.IsCloaked()) || (hasLos && target.IsCloaked() && !target.IsCloakEffective())) {
            ai.TargetPosition = { target.Segment, target.Position };
            ai.Awareness = AI_AWARENESS_MAX;
            ai.CombatState = AICombatState::Normal;
            ai.LostSightDelay = 1.0f; // Let the AI 'cheat' for 1 second after losing direct sight (object permeance?)

            // Try to get behind target unless dodging. Maybe make this only happen sometimes?
            if (robotInfo.GetBehind && robot.Control.AI.Behavior != AIBehavior::Still && ai.DodgeTime <= 0)
                GetBehindTarget(robot, ai, robotInfo, target);

            if (Settings::Cheats.ShowPathing)
                Render::Debug::DrawPoint(ai.TargetPosition->Position, Color(1, 0, 0));

            AlertNearby(ai, robot, robotInfo);
            PlayCombatNoise(robot, ai);
        }
        else {
            ai.LostSightDelay -= dt;
            DecayAwareness(ai);
            // Robot can either choose to chase the target or hold position and blind fire

            if (Settings::Cheats.ShowPathing)
                Render::Debug::DrawPoint(ai.TargetPosition->Position, Color(1, .5, .5));

            if (ai.CombatState == AICombatState::Normal && ai.StrafeTimer <= 0 && ai.LostSightDelay <= 0) {
                OnLostLineOfSight(ai, robot, robotInfo);
            }

            if (ai.CombatState == AICombatState::Wait) {
                // Get ready
                if (ai.TargetPosition) {
                    TurnTowardsPoint(robot, ai.TargetPosition->Position, Difficulty(robotInfo).TurnTime);
                }
            }
            else if (ai.CombatState == AICombatState::Chase && ai.TargetPosition && ai.Fear < 1) {
                // Chasing a cloaked target does no good, AI just gets confused.
                // Also don't chase the player ghost
                if (!target.IsCloaked() && target.Type != ObjectType::Ghost && ai.ChaseTimer <= 0) {
                    if (Random() < robotInfo.ChaseChance) {
                        Chat(robot, "Come back here!");
                        if (!ChaseTarget(ai, robot, *ai.TargetPosition, ChaseMode::Sight))
                            ai.ChaseTimer = 5.0f;
                    }
                    else {
                        ai.ChaseTimer = 5.0f;
                    }
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
                UpdateMeleeAI(robot, robotInfo, ai, target, targetDir, dt);
        }

        // Only robots that flee can find help. Limit to hotshot and above.
        if (robotInfo.FleeThreshold > 0 && robot.Control.AI.Behavior != AIBehavior::Still && Game::Difficulty > 1) {
            if (!ai.FleeTimer.IsSet()) {
                ai.FleeTimer = 2 + Random() * 2; // Periodically think about fleeing
            }

            if (ai.FleeTimer.Expired() && FleeingDrones == 0) {
                if (robot.HitPoints / robot.MaxHitPoints <= robotInfo.FleeThreshold || ai.Fear >= 1) {
                    // Wounded or scared enough to flee, but would rather fight if there's allies nearby
                    //SPDLOG_INFO("Nearby allies: {}", allies);

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

    void BeginAIFrame() {
        DronesInCombat = DronesInCombatCounter;
        DronesInCombatCounter = 0;

        FleeingDrones = FleeingDronesCounter;
        FleeingDronesCounter = 0;
    }

    void UpdateAlertAI(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float /*dt*/) {
        CheckProjectiles(Game::Level, robot, ai, robotInfo);

        if (!ai.PlayingAnimation() && ai.AnimationState != AnimState::Alert)
            PlayRobotAnimation(robot, AnimState::Alert, 1);

        if (ScanForTarget(robot, ai) && ai.Awareness >= 1 && ai.Target) {
            ai.State = AIState::Combat;
            ai.CombatState = AICombatState::Normal;
            Chat(robot, "I found a bad guy!");
            return; // Found a target, start firing!
        }

        // Turn towards point of interest if we have one
        if (ai.TargetPosition) {
            TurnTowardsPoint(robot, ai.TargetPosition->Position, Difficulty(robotInfo).TurnTime);
            bool validState = ai.CombatState == AICombatState::Normal || ai.CombatState == AICombatState::Chase;

            if (Settings::Cheats.ShowPathing)
                Render::Debug::DrawPoint(ai.TargetPosition->Position, Color(1, 0, 1));

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
                        ChaseTarget(ai, robot, *ai.TargetPosition, ChaseMode::Sound);
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

            if (ScanForTarget(robot, ai) && ai.Awareness >= 1) {
                auto target = Game::GetObject(ai.Target);
                ai.State = AIState::Path;
                ai.CombatState = AICombatState::Normal;
                ai.Path = GenerateRandomPath(robot.Segment, 15, NavigationFlags::OpenKeyDoors, target ? target->Segment : SegID::None);
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

    void MineLayerBehavior(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float /*dt*/) {
        ScanForTarget(robot, ai);

        // Periodically alert allies while not idle
        if (ai.State != AIState::Idle && ai.AlertTimer <= 0 && ai.TargetPosition) {
            AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.TargetPosition, 0.75f);
            ai.AlertTimer = 4 + Random() * 2;
            Sound3D sound(robotInfo.SeeSound);
            sound.Volume = 0.4f;
            Sound::PlayFrom(sound, robot);
        }

        // Mine layers are either in path mode or idle. They cannot peform any other action.
        if (ai.State == AIState::Path) {
            if (!PathTowardsGoal(robot, ai, false, false)) {
                ai.ClearPath();
                ai.State = AIState::Alert;
            }

            if (ai.FireDelay <= 0) {
                auto weapon = robot.Control.AI.SmartMineFlag() ? WeaponID::SmartMine : WeaponID::ProxMine;
                Game::FireWeapon(Game::GetObjectRef(robot), weapon, 0, nullptr, 1, false);
                ai.FireDelay = AI_MINE_LAYER_DELAY;
            }
            PlayCombatNoise(robot, ai);
        }
        else {
            // Keep pathing until awareness fully decays
            if (ai.Awareness >= 1 && ai.Path.empty()) {
                auto target = Game::GetObject(ai.Target);
                ai.State = AIState::Path;
                ai.CombatState = AICombatState::Normal;
                ai.Path = GenerateRandomPath(robot.Segment, 15, NavigationFlags::None, target ? target->Segment : SegID::None);
                ai.PathIndex = 0;
                ai.AlertTimer = 1 + Random() * 2;
            }
            else if (ai.Awareness <= 0) {
                PlayRobotAnimation(robot, AnimState::Rest);
                MakeIdle(ai);
            }

            DecayAwareness(ai);
        }
    }


    void DefaultBehavior(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float dt) {
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
                if (!PathTowardsGoal(robot, ai, false, false)) {
                    ai.ClearPath();
                    ai.State = AIState::Alert;
                }
                break;
            case AIState::FindHelp:
                UpdateFindHelp(ai, robot);
                break;
            case AIState::Chase:
                CheckProjectiles(Game::Level, robot, ai, robotInfo);

                if (ai.Path.empty()) {
                    ai.State = AIState::Alert;
                }
                else {
                    // Stop chasing once robot can see source of sound, otherwise move to the location.
                    // This is so a fleeing player is pursued around corners
                    bool stopOnceVisible = ai.Chase == ChaseMode::Sound;
                    PathTowardsGoal(robot, ai, true, stopOnceVisible);

                    if (ai.TargetPosition && ai.TargetPosition->Segment == robot.Segment) {
                        // Clear target if pathing towards it discovers the target isn't there.
                        // This is so the robot doesn't turn around while chasing
                        ai.TargetPosition = {};
                    }

                    if (ScanForTarget(robot, ai) && ai.Awareness >= 1) {
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

        if (Settings::Cheats.DisableAI) return;

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

        if (ai.DodgeTime > 0 && ai.DodgeVelocity != Vector3::Zero)
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
            Debug::ActiveRobots++;
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
