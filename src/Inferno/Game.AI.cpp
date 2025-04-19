#include "pch.h"

#include "Types.h"
#include "Game.AI.h"

#include <algorithm>
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
#include "Graphics/Render.Debug.h"
#include "VisualEffects.h"

namespace Inferno {
    namespace {
        List<AIRuntime> RuntimeState;
        IntersectContext Intersect(Game::Level);

        uint DronesInCombat = 0, DronesInCombatCounter = 0;
        uint FleeingDrones = 0, FleeingDronesCounter = 0;

        constexpr float AI_DODGE_TIME = 0.5f; // Time to dodge a projectile. Should probably scale based on mass.
        constexpr float AI_MAX_DODGE_DISTANCE = 100; // Range at which projectiles are dodged
        constexpr float DEATH_SOUND_DURATION = 2.68f;
        constexpr float AI_SOUND_RADIUS = 300.0f; // Radius for combat sound playback

        constexpr float FIRING_ALERT_RADIUS = 160; // Alert robots in this range when firing
        constexpr float AI_AWARENESS_DECAY = 1 / 5.0f; // Awareness lost per second

        constexpr float AI_DEFAULT_AWAKE_TIME = 5.0f; // how long a robot stays awake after becoming fully aware or entering combat
        constexpr float AI_BLIND_FIRE_TIME = 2.0f; // how long a robot stays awake after becoming fully aware or entering combat
        constexpr float AI_MINE_LAYER_AWAKE_TIME = 8.0f;

        // Slow is applied to robots hit by the player to compensate for the removal of stun
        constexpr float MAX_SLOW_TIME = 2.0f; // Max duration of slow
        constexpr float MAX_SLOW_EFFECT = 0.5f; // Max percentage of slow to apply to a robot
        constexpr float MAX_SLOW_THRESHOLD = 0.4f; // Percentage of life dealt to reach max slow

        constexpr float STUN_THRESHOLD = 27.5; // Minimum damage to stun a robot. Concussion is 30 damage.
        constexpr float MAX_STUN_PERCENT = 0.6f; // Percentage of life required in one hit to reach max stun time
        constexpr float MAX_STUN_TIME = 1.5f; // max stun in seconds
        constexpr float MIN_STUN_TIME = 0.25f; // min stun in seconds. Stuns under this duration are discarded.
        constexpr auto SUPERVISOR_SCRIPT = "Supervisor";

        constexpr auto MELEE_RANGE = 40.0f; // How close for a robot to be considered in melee for AI purposes. Will try moving directly towards target instead of pathing

        GameTimer GlobalFleeTimer;
        AIRuntime NULL_AI_RUNTIME; // don't use, only exists as a failsafe
    }

    void ChangeState(Object& robot, AIRuntime& ai, AIState state);

    template <typename... Args>
    void Chat(const Object& robot, const string_view fmt, Args&... args) {
        string message = fmt::vformat(fmt, fmt::make_format_args(args...));
        auto& info = Resources::GetRobotInfo(robot);
        auto& ai = GetAI(robot);
        fmt::println("{:6.2f} {} {} [{}]: {}", Game::Time, info.Name, robot.Signature, AI_STATE_NAMES[(int)ai.State], message);
    }

    void ResetAI() {
        for (auto& ai : RuntimeState)
            ai = {};

        Game::InitBoss();
    }

    void ResetAITargets() {
        for (auto& ai : RuntimeState) {
            ai.TargetObject = {};
            ai.Target = {};
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

        if (!Seq::inRange(RuntimeState, (int)ref.Id)) {
            __debugbreak();
            SPDLOG_WARN("Tried to access null AI data");
            return NULL_AI_RUNTIME;
        }

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

    // Returns true if the current behavior and state allow chasing
    bool CanChase(const Object& robot, const AIRuntime& ai, const NavPoint* target = nullptr) {
        auto& robotInfo = Resources::GetRobotInfo(robot);

        // Check behavior
        auto behavior = robot.Control.AI.Behavior;
        if (behavior == AIBehavior::Still || behavior == AIBehavior::Hide || behavior == AIBehavior::RunFrom)
            return false;

        if (ai.State == AIState::Path && !ai.path.interruptable)
            return false;

        if (ai.Fear >= 1 && robotInfo.FleeThreshold > 0)
            return false; // Don't allow robots that are afraid to chase

        if (target) {
            // Check chase range
            auto distance = Vector3::Distance(target->Position, robot.Position);
            if (distance > robotInfo.ChaseDistance)
                return false;
        }

        return true;
    }

    // Returns true if able to reach the target
    bool ChaseTarget(Object& robot, AIRuntime& ai, const NavPoint& target, PathMode mode, float maxDist) {
        ai.PathDelay = 0;
        ai.Target = target;
        if (SetPathGoal(Game::Level, robot, ai, target, mode, maxDist)) {
            ChangeState(robot, ai, AIState::Path);
            ai.path.faceGoal = true;
            ai.path.interruptable = true;
            //Chat(robot, "Chase path found");
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

        if (ai.CombatSoundTimer <= 0) {
            ai.CombatSoundTimer = 2 + Random() * 2;
            Sound3D sound(robotInfo.SeeSound);
            sound.Volume = 1.15f;
            sound.Radius = AI_SOUND_RADIUS;
            Sound::PlayFrom(sound, robot);
        }
    }

    void AlertEnemiesInSegment(Level& level, const Segment& seg, const NavPoint& source, float soundRadius, float awareness, const Object* sourceObj) {
        for (auto& objId : seg.Objects) {
            if (auto segObj = level.TryGetObject(objId)) {
                if (!segObj->IsRobot()) continue;

                auto dist = Vector3::Distance(segObj->Position, source.Position);
                if (dist > soundRadius) continue;

                auto& ai = GetAI(*segObj);
                float t = dist / soundRadius;
                auto falloff = Saturate(2.0f - 2.0f * t) * 0.5f + 0.5f; // linear shoulder

                ai.AddAwareness(awareness * falloff);
                ai.Target = source;
                segObj->NextThinkTime = 0;
                auto& info = Resources::GetRobotInfo(*segObj);

                if (sourceObj
                    && sourceObj->IsPlayer()
                    && ai.Awareness >= 1
                    && sourceObj->IsCloaked()
                    && HasLineOfSight(*segObj, source.Position)
                    && info.Attack == AttackType::Ranged) {
                    ai.TargetObject = Game::GetObjectRef(*sourceObj);
                    PlayAlertSound(*segObj, ai);
                    Chat(*segObj, "I think something is there!");
                    ChangeState(*segObj, ai, AIState::BlindFire);
                }

                if (ai.State == AIState::Path && ai.path.interruptable) {
                    // Update chase target if we hear something
                    if (ChaseTarget(*segObj, ai, *ai.Target, PathMode::StopVisible, info.ChaseDistance)) {
                        ai.path.faceGoal = true;
                    }
                }
            }
        }
    }

    //void AlertEnemiesInRoom(Level& level, const Room& room, SegID soundSeg, const Vector3& soundPosition, float soundRadius, float awareness, float /*maxAwareness*/) {
    //    for (auto& segId : room.Segments) {
    //        auto pseg = level.TryGetSegment(segId);
    //        if (!pseg) continue;
    //        auto& seg = *pseg;

    //        AlertEnemiesInSegment(level, seg, { soundSeg, soundPosition }, soundRadius, awareness);
    //    }
    //}

    // adds awareness to robots in nearby rooms
    void AlertRobotsOfNoise(const NavPoint& source, float soundRadius, float awareness, const Object* sourceObj) {
        for (auto& roomId : Game::ActiveRooms) {
            if (auto room = Game::Level.GetRoom(roomId)) {
                for (auto& segId : room->Segments) {
                    auto seg = Game::Level.TryGetSegment(segId);
                    if (!seg) continue;
                    AlertEnemiesInSegment(Game::Level, *seg, source, soundRadius, awareness, sourceObj);
                }
            }
        }

        //IterateNearbySegments(Game::Level, source, soundRadius, TraversalFlag::StopDoor | TraversalFlag::PassOpenDoors, [&](const Segment& seg, bool) {
        //    AlertEnemiesInSegment(Game::Level, seg, source, soundRadius, awareness);
        //});
    }

    void AlertAlliesOfDeath(const Object& dyingRobot) {
        Chat(dyingRobot, "Goodbye world");

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
    bool AlertRobotsOfTarget(const Object& sourceRobot, float radius, const NavPoint& target, float awareness, bool requestHelp = false) {
        auto& level = Game::Level;
        auto srcRoom = level.GetRoomID(sourceRobot);
        if (srcRoom == RoomID::None) return false;

        bool alertedRobot = false;
        auto& srcAi = Resources::GetRobotInfo(sourceRobot);
        bool supervisor = srcAi.Script == SUPERVISOR_SCRIPT;

        auto action = [&](const Room& room) {
            for (auto& segId : room.Segments) {
                auto pseg = level.TryGetSegment(segId);
                if (!pseg) continue;
                auto& seg = *pseg;

                for (auto& objId : seg.Objects) {
                    if (auto obj = level.TryGetObject(objId)) {
                        if (!obj->IsRobot()) continue;
                        if (obj->Signature == sourceRobot.Signature) continue; // Don't alert self

                        auto dist = Vector3::Distance(obj->Position, sourceRobot.Position);
                        if (dist > radius) continue;
                        auto random = 0.75f + Random() * 0.5f; // Add some variance so robots in a room don't all wake up at same time
                        auto& ai = GetAI(*obj);

                        if (ai.State != AIState::Idle && ai.State != AIState::Alert && ai.State != AIState::Roam)
                            continue;

                        if (supervisor && obj->ID == sourceRobot.ID)
                            continue; // don't alert supervisors from other supervisors, they will never go to sleep

                        ai.Target = target; // Update target if not fighting
                        ai.AddAwareness(awareness * random);

                        if (ai.Awareness >= 1) {
                            if ((ai.State == AIState::Idle || ai.State == AIState::Alert)
                                && requestHelp
                                && CanChase(*obj, ai, &target)) {
                                auto& info = Resources::GetRobotInfo(*obj);

                                Chat(*obj, "Drone {} says it sees something", sourceRobot.Signature);
                                if (SetPathGoal(level, *obj, ai, target, PathMode::StopVisible, info.AmbushDistance)) {
                                    PlayAlertSound(*obj, ai);
                                    Chat(*obj, "I'm close enough to check it out");
                                    ChangeState(*obj, ai, AIState::Path);
                                    ai.path.interruptable = true;
                                    ai.path.faceGoal = true;
                                    alertedRobot = true;
                                }
                            }
                            else {
                                ChangeState(*obj, ai, AIState::Alert);
                            }
                        }
                    }
                }
            }

            return false;
        };

        TraverseRoomsByDistance(level, srcRoom, sourceRobot.Position, radius, true, action);
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

    void DecayAwareness(AIRuntime& ai) {
        ai.Awareness -= ai.GetDeltaTime() * AI_AWARENESS_DECAY;
        ai.Awareness = std::max(ai.Awareness, 0.0f);
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

    // Clamps a target point to the robot's aim angle
    Vector3 ClampTargetToFov(const Vector3& gunDirection, const Vector3& gunPosition, const Vector3& target, float halfAimRads) {
        // project target to centerline of gunpoint
        auto projTarget = gunDirection * gunDirection.Dot(target - gunPosition) + gunPosition;
        auto projDist = Vector3::Distance(gunPosition, projTarget);
        auto projDir = target - projTarget;
        projDir.Normalize();
        auto maxLeadDist = tanf(halfAimRads) * projDist;
        return projTarget + maxLeadDist * projDir;
    }

    // Returns the new position to fire at
    Vector3 LeadTarget(const Object& robot, SegID gunSeg, const Object& target, const Weapon& weapon, float maxAngleRads) {
        if (target.Physics.Velocity.Length() < 20)
            return target.Position; // Don't lead slow targets

        if (GetSpeed(weapon) > FAST_WEAPON_SPEED)
            return target.Position; // Don't lead with fast weapons (vulcan, gauss, drillers). Unfair to player.

        auto targetDir = target.Position - robot.Position;
        auto targetDist = targetDir.Length();
        targetDir.Normalize();

        Vector3 targetVelDir;
        target.Physics.Velocity.Normalize(targetVelDir);
        float expectedTravelTime = targetDist / GetSpeed(weapon);
        //auto projectedTarget = target.Position;
        auto projectedTarget = target.Position + target.Physics.Velocity * expectedTravelTime;
        auto forward = robot.Rotation.Forward();

        // Constrain the projected target to the plane of the target.
        // This is so moving towards the robot doesn't cause it to shoot at a nearby wall
        projectedTarget = ProjectPointOntoPlane(projectedTarget, target.Position, targetDir);

        {
            // Check target projected position
            //Ray targetTrajectory(target.Position, targetVelDir);
            //RayQuery query;
            //query.MaxDistance = (target.Physics.Velocity * expectedTravelTime).Length();
            //query.Start = target.Segment;
            //LevelHit hit;
            //if (HasFlag(Game::Intersect.RayLevelEx(targetTrajectory, query, hit), IntersectResult::HitWall)) {
            //    // target will hit wall, aim at target position
            //    return target.Position;

            //    // aim at wall minus object radius
            //    /*projectedTarget = hit.Point - targetVelDir * target.Radius;
            //    targetDist = Vector3::Distance(projectedTarget, robot.Position);
            //    expectedTravelTime = targetDist / GetSpeed(weapon);*/
            //}

            //projectedTarget = target.Position + target.Physics.Velocity * expectedTravelTime;
        }


        {
            auto projectedDir = projectedTarget - robot.Position;
            projectedDir.Normalize();

            // Clamp the target to the robot's aim angle
            auto aimAngle = AngleBetweenVectors(projectedDir, forward);
            if (aimAngle > maxAngleRads) {
                //SPDLOG_INFO("Clamping aim angle");
                projectedTarget = ClampTargetToFov(forward, robot.Position, projectedTarget, maxAngleRads);
            }

            // Check projected shot line of sight
            Ray ray(robot.Position, projectedDir);
            RayQuery query;
            query.Start = gunSeg;
            query.MaxDistance = Vector3::Distance(projectedTarget, robot.Position);

            //Render::Debug::DrawLine(robot.Position, robot.Position + targetDir * query.MaxDistance, Color(1, 0, 0));

            LevelHit hit;
            if (Game::Intersect.RayLevelEx(ray, query, hit) == IntersectResult::None) {
                // Won't hit level, lead the target!
                //SPDLOG_INFO("leading target");
                return projectedTarget;
            }
            else {
                // Back off by half the lead distance and try again
                // No need to clamp by FOV again because we did it earlier
                projectedTarget = (projectedTarget + target.Position) / 2;
                projectedDir = projectedTarget - robot.Position;
                projectedDir.Normalize();
                ray = Ray(robot.Position, projectedDir);

                //Render::Debug::DrawLine(robot.Position, robot.Position + targetDir * query.MaxDistance, Color(1, 1, 1));
                hit = {};
                auto result = Game::Intersect.RayLevelEx(ray, query, hit);
                if (result == IntersectResult::None) {
                    //SPDLOG_INFO("half leading");
                    return projectedTarget;
                }
            }
        }

        //SPDLOG_INFO("Not leading target");
        return target.Position; // Wasn't able to lead target
    }

    void FireRobotWeapon(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, Vector3 target, bool primary, bool blind, bool lead) {
        if (!primary && robotInfo.WeaponType2 == WeaponID::None) return; // no secondary set

        const auto weaponId = primary ? robotInfo.WeaponType : robotInfo.WeaponType2;
        const auto& weapon = Resources::GetWeapon(weaponId);
        uint8 gun = primary ? ai.GunIndex : 0;
        const auto forward = robot.Rotation.Forward();

        // Find world position of gunpoint
        auto gunOffset = GetSubmodelOffset(robot, { robotInfo.GunSubmodels[gun], robotInfo.GunPoints[gun] });
        auto gunPosition = Vector3::Transform(gunOffset, robot.GetTransform());
        auto halfAimRads = robotInfo.AimAngle * DegToRad * 0.5f;

        if (blind) {
            // add inaccuracy if target is cloaked or doing a blind-fire
            target += RandomVector() * 5.0f;
        }
        else if (auto targetObj = Game::GetObject(ai.TargetObject); targetObj && lead) {
            target = LeadTarget(robot, robot.Segment, *targetObj, weapon, halfAimRads);
        }

        //Render::Debug::DrawLine(projTarget, gunPosition, Color(0, 1, 0));

        auto aimDir = GetDirection(target, gunPosition);
        auto aimAngle = AngleBetweenVectors(aimDir, forward);
        //SPDLOG_INFO("Aim angle deg: {}", aimAngle * RadToDeg);

        if (aimAngle > DirectX::XM_PIDIV2) {
            // If the projected target is behind the gunpoint, fire straight instead.
            // Otherwise the aim clamping causes the robot to shoot backwards.
            target = gunPosition + forward * 20;
        }

        auto targetDir = target - gunPosition;
        targetDir.Normalize();

        {
            // Randomize target position based on aim. 255 -> 1, 0 -> 8
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
            targetDir = forward;
        }

        //Render::Debug::DrawLine(gunPosition, gunPosition + targetDir * 10, Color(1, 0, 0));

        if (GunpointIntersectsWall(robot, gun)) {
            SPDLOG_WARN("Robot gun clips wall!");
        }
        else {
            // Fire the weapon
            Game::FireWeaponInfo info = { .id = weaponId, .gun = gun, .customDir = &targetDir };
            Game::FireWeapon(robot, info);
            Game::PlayWeaponSound(weaponId, weapon.Extended.FireVolume, robot, gun);
        }

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

    void DodgeProjectiles(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, Level& level) {
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
    void MoveTowardsTarget(Level& level, Object& robot, AIRuntime& ai, const Vector3& objDir, const RobotInfo& robotInfo) {
        if (!ai.Target) return;

        auto sight = HasLineOfSightEx(robot, ai.Target->Position, false);
        auto distance = Vector3::Distance(ai.Target->Position, robot.Position);

        if (robotInfo.Attack == AttackType::Melee) {
            // Melee robots try to find a path around a wall
            if (sight == IntersectResult::ThroughWall && distance > MELEE_RANGE) {
                if (ChaseTarget(robot, ai, *ai.Target, PathMode::StopAtEnd, robotInfo.ChaseDistance))
                    ai.path.faceGoal = true;
            }

            // Only avoid room geometry outside of melee range, so robots will actively attack around corners and grates
            if (distance > MELEE_RANGE) {
                Ray ray(robot.Position, objDir);
                AvoidRoomEdges(level, ray, robot, ai.Target->Position);
            }

            MoveTowardsPoint(robot, ai, ai.Target->Position);
        }
        else if (!Intersects(sight)) {
            // ranged robots
            Ray ray(robot.Position, objDir);
            AvoidRoomEdges(level, ray, robot, ai.Target->Position);
            MoveTowardsPoint(robot, ai, ai.Target->Position);
        }

        if (robotInfo.Attack == AttackType::Melee && sight == IntersectResult::ThroughWall && distance > MELEE_RANGE) {
            // Melee robots try to find a path around a wall
            if (ChaseTarget(robot, ai, *ai.Target, PathMode::StopAtEnd, robotInfo.ChaseDistance))
                ai.path.faceGoal = true;
            // path to target, but only if it's not tried recently
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

    void MoveToCircleDistance(Level& level, Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (!ai.Target) return;

        auto circleDistance = DifficultyInfo(robotInfo).CircleDistance;
        if (circleDistance < 0) return; // hold position

        auto [dir, dist] = GetDirectionAndDistance(ai.Target->Position, robot.Position);
        if (dist > robotInfo.ChaseDistance) return; // Don't try circling if target is too far

        auto minDist = std::min(circleDistance * 0.75f, circleDistance - 10);
        auto maxDist = std::max(circleDistance * 1.25f, circleDistance + 10);

        if (robotInfo.Attack == AttackType::Ranged && (dist > minDist && dist < maxDist))
            return; // in deadzone, no need to move. Otherwise robots clump up on each other.
        else if (robotInfo.Attack == AttackType::Melee && dist < circleDistance)
            return;

        if (dist > circleDistance)
            MoveTowardsTarget(level, robot, ai, dir, robotInfo);
        else
            MoveAwayFromTarget(ai.Target->Position, robot, ai);
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

    void RobotTouchObject(Object& robot, const Object& obj) {
        ASSERT(robot.IsRobot());

        auto& ai = GetAI(robot);

        if (obj.IsRobot() || obj.IsPlayer()) {
            ai.LastCollision = Game::Time;
        }

        if (!Game::EnableAi()) return;

        if (obj.IsPlayer()) {
            if (ai.State == AIState::FindHelp) return;
            if (ai.State == AIState::Path && !ai.path.interruptable) return;

            if (ai.State == AIState::Idle || ai.State == AIState::Alert) {
                PlayAlertSound(robot, ai);
                Chat(robot, "Something touched me!");
            }

            ai.TargetObject = Game::GetObjectRef(obj);
            ai.Target = { obj.Segment, obj.Position };
            ChangeState(robot, ai, obj.IsCloaked() ? AIState::BlindFire : AIState::Combat);
        }
    }

    void DamageRobot(const NavPoint& sourcePos, Object& robot, float damage, float stunMult, Object* source) {
        auto& robotInfo = Resources::GetRobotInfo(robot);
        auto& ai = GetAI(robot);

        if (ai.State == AIState::Idle && !Settings::Cheats.DisableAI) {
            Chat(robot, "What hit me!?");
            ChangeState(robot, ai, AIState::Alert);
        }

        if (source && ai.State != AIState::Combat && !Settings::Cheats.DisableAI) {
            // Try randomly dodging if taking damage
            RandomDodge(robot, ai, robotInfo);

            if (source->IsPlayer()) {
                // We were hit by the player but don't know exactly where they are
                ai.Target = sourcePos;
                ai.LastHitByPlayer = 0;
                ai.Awareness = AI_AWARENESS_MAX;

                // Path towards player if robot takes damage and is out of LOS. This is so they aren't easily sniped around corners.
                if (ai.State == AIState::Alert || ai.State == AIState::Idle) {
                    bool hasLos = source ? HasLineOfSight(robot, source->Position) : false;
                    if (!hasLos) {
                        ChaseTarget(robot, ai, NavPoint(Game::GetPlayerObject()), PathMode::StopVisible, robotInfo.ChaseDistance);
                        ai.path.faceGoal = true;
                        ai.path.interruptable = true;
                    }
                }
                else if (ai.State == AIState::Path && ai.path.interruptable) {
                    // Break out of pathing if shot
                    bool hasLos = source ? HasLineOfSight(robot, source->Position) : false;
                    if (hasLos)
                        ChangeState(robot, ai, AIState::Combat);
                }
            }
            else if (source->IsRobot()) {
                Chat(robot, "Where are you aiming drone {}!?", source->Signature);
                ai.DodgeDelay = 0;
                RandomDodge(robot, ai, robotInfo);
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

        if (robotInfo.IsBoss) {
            // Bosses are immune to stun and slow and perform special actions when hit
            Game::DamageBoss(robot, sourcePos, damage, source);
        }
        else {
            // Apply slow
            float ehp = robotInfo.HitPoints * robotInfo.StunResist;
            float damageScale = 1 - (ehp - damage * stunMult) / ehp; // percentage of life dealt
            float slowTime = std::lerp(0.0f, 1.0f, damageScale / MAX_SLOW_THRESHOLD);
            if (ai.RemainingSlow > 0) slowTime += ai.RemainingSlow;
            ai.RemainingSlow = std::clamp(slowTime, 0.1f, MAX_SLOW_TIME);

            float maxStunTime = std::min(1 / std::max(robotInfo.StunResist, 0.5f), 1.0f) * MAX_STUN_TIME; // scale max stun based on resist if it's under 1, up to 2x
            float stunTime = damageScale / MAX_STUN_PERCENT * maxStunTime;

            // Apply stun
            if (damage * stunMult > STUN_THRESHOLD && stunTime > MIN_STUN_TIME) {
                //SPDLOG_INFO("Stunning {} for {}", robot.Signature, stunTime > MAX_STUN_TIME ? MAX_STUN_TIME : stunTime);
                if (ai.RemainingStun > 0) stunTime += ai.RemainingStun;
                stunTime = std::clamp(stunTime, MIN_STUN_TIME, maxStunTime);
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

            if (robot.HitPoints <= 0 && robotInfo.DeathRoll == 0) {
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

    bool RollShouldLead() {
        auto leadChance = (int)Game::Difficulty / 4.0f; // 50% on hotshot, 75% on ace, 100% on insane
        bool shouldLead = Random() <= leadChance * 0.9f; // Don't always lead even on insane, keep the player guessing
        if (Game::Difficulty < DifficultyLevel::Hotshot) shouldLead = false; // Don't lead on rookie and trainee, also weapons are too slow to meaningfully lead.
        return shouldLead;
    }

    void FireRobotPrimary(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const NavPoint& target, bool blind) {
        ai.FireDelay = 0;

        // multishot: consume as many projectiles as possible based on burst count
        // A multishot of 1 and a burst of 3 would fire 2 projectiles then 1 projectile
        auto burstDelay = robotInfo.BurstDelay;
        if (ai.Angry) burstDelay *= AI_ANGER_SPEED; // Use a lower burst delay when angry

        auto shouldLead = RollShouldLead(); // only roll once per fire

        // Don't lead through walls as robots will often hit the grating instead

        if (HasFiringLineOfSight(robot, ai.GunIndex, target.Position, ObjectMask::Robot) == IntersectResult::ThroughWall)
            shouldLead = false;

        for (int i = 0; i < robotInfo.Multishot; i++) {
            if (i == 0) {
                // When a volley starts alert nearby robots
                AlertRobotsOfTarget(robot, FIRING_ALERT_RADIUS, target, 1);
            }

            FireRobotWeapon(robot, ai, robotInfo, target.Position, true, blind, shouldLead);
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
    void WeaponChargeBehavior(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, bool blind, float dt) {
        ai.NextChargeSoundDelay -= dt;
        ai.WeaponCharge += dt;

        if (ai.NextChargeSoundDelay <= 0) {
            ai.NextChargeSoundDelay = 0.125f + Random() / 8;

            if (auto fx = EffectLibrary.GetSparks("robot fusion charge")) {
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
            // Release shot at last seen position even if target has moved out of view
            auto target = ai.Target ? *ai.Target : NavPoint(robot.Segment, robot.Position + robot.Rotation.Forward() * 40);
            //auto target = ai.TargetPosition ? *ai.TargetPosition : NavPoint(robot.Segment, robot.Position + robot.Rotation.Forward() * 40);
            FireRobotPrimary(robot, ai, robotInfo, target, blind);

            ai.WeaponCharge = 0;
            ai.ChargingWeapon = false;
        }
    }

    // Tries to circle strafe the target.
    void CircleStrafe(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (!ai.Target) return;

        bool checkDir = false;
        // Move in a consistent direction for the strafe
        if (ai.StrafeTimer <= 0) {
            ai.StrafeAngle = Random() * DirectX::XM_2PI;
            ai.StrafeTimer = Random() * 2 + 1.5f;
            checkDir = true;
        }

        if (ai.StrafeAngle < 0)
            return; // angle not set

        auto targetDir = ai.Target->Position - robot.Position;
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
        if (!ai.Target) return;
        auto targetDir = ai.Target->Position - robot.Position;
        targetDir.Normalize();

        auto targetFacing = target.Rotation.Forward();
        if (targetFacing.Dot(targetDir) > 0)
            return; // Already behind the target!

        // Try to make the target facing dot product larger!

        if (ai.StrafeTimer <= 0) {
            auto right = robot.Position + robot.Rotation.Right() * 5;
            auto left = robot.Position - robot.Rotation.Right() * 5;

            auto testTargetDir = ai.Target->Position - right;
            testTargetDir.Normalize();
            auto rightTargetDot = targetFacing.Dot(testTargetDir);

            testTargetDir = ai.Target->Position - left;
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

    void BlindFireRoutine(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float dt) {
        if (robotInfo.Attack == AttackType::Melee || robotInfo.Guns == 0 || !ai.Target) {
            ChangeState(robot, ai, AIState::Alert); // Invalid robot state to blind fire
            return;
        }

        TurnTowardsPoint(robot, ai.Target->Position, DifficultyInfo(robotInfo).TurnTime);

        if (ai.AnimationState != Animation::Fire && !ai.PlayingAnimation()) {
            PlayRobotAnimation(robot, Animation::Alert, 1.0f);
        }

        auto& weapon = Resources::GetWeapon(robotInfo.WeaponType);
        // Use the last time the target was seen instead of the delayed target tracking used for chasing.
        const auto& lastSeen = *ai.Target;

        if (ai.ChargingWeapon) {
            WeaponChargeBehavior(robot, ai, robotInfo, true, dt); // Charge up during fire animation
        }
        else if (ai.AnimationState != Animation::Fire && ai.FireDelay < 0.25f) {
            // Start firing

            auto aimDir = lastSeen.Position - robot.Position;
            aimDir.Normalize();

            if (HasLineOfSight(robot, lastSeen.Position) &&
                AngleBetweenVectors(aimDir, robot.Rotation.Forward()) <= robotInfo.AimAngle * DegToRad) {
                // Target is within the cone of the weapon, start firing
                PlayRobotAnimation(robot, Animation::Fire, ai.FireDelay.Remaining() * 0.8f);
            }

            if (weapon.Extended.Chargable)
                ai.ChargingWeapon = true;
        }
        else if (ai.FireDelay <= 0 && !ai.PlayingAnimation()) {
            // Fire animation finished, release a projectile
            FireRobotPrimary(robot, ai, robotInfo, lastSeen, true);

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(lastSeen.Position, Color(1, 0, 0));
        }

        if (ScanForTarget(robot, ai)) {
            Chat(robot, "Target dares to show!");
            ChangeState(robot, ai, AIState::Combat);
        }
        else if (ai.ActiveTime <= 0 /*&& !ai.IsFiring()*/) {
            // time ran out
            Chat(robot, "Stay on alert");
            ChangeState(robot, ai, AIState::Alert);
        }
    }

    void RangedRoutine(Object& robot, const RobotInfo& robotInfo, AIRuntime& ai, float dt, bool blind) {
        if (!ai.Target) {
            return;
        }

        const auto& target = *ai.Target;

        if (robotInfo.WeaponType2 != WeaponID::None && ai.FireDelay2 <= 0) {
            // Check if an ally robot is in the way and try strafing if it is
            auto sight = HasFiringLineOfSight(robot, 0, target.Position, ObjectMask::Robot);
            if (Intersects(sight)) {
                CircleStrafe(robot, ai, robotInfo);
                return;
            }

            // Secondary weapons have no animations or wind up
            FireRobotWeapon(robot, ai, robotInfo, target.Position, false, blind, false);
            ai.FireDelay2 = DifficultyInfo(robotInfo).FireDelay2;
        }
        else {
            if (robotInfo.Guns == 0) return; // Can't shoot, I have no guns!

            if (ai.AnimationState != Animation::Fire && !ai.PlayingAnimation()) {
                PlayRobotAnimation(robot, Animation::Alert, 1.0f);
            }

            auto& weapon = Resources::GetWeapon(robotInfo.WeaponType);
            // Use the last time the target was seen instead of the delayed target tracking used for chasing.

            if (ai.ChargingWeapon) {
                WeaponChargeBehavior(robot, ai, robotInfo, blind, dt); // Charge up during fire animation
            }
            else if (ai.AnimationState != Animation::Fire && ai.FireDelay < 0.25f) {
                // Start firing

                // Check if an ally robot is in the way and try strafing if it is
                auto sight = HasFiringLineOfSight(robot, ai.GunIndex, target.Position, ObjectMask::Robot);
                if (Intersects(sight)) {
                    CircleStrafe(robot, ai, robotInfo);
                    CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                    ai.FireDelay = 0.25f + 1 / 8.0f; // Try again in 1/8th of a second
                    return;
                }

                auto aimDir = target.Position - robot.Position;
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
                FireRobotPrimary(robot, ai, robotInfo, target, blind);

                if (Settings::Cheats.ShowPathing)
                    Graphics::DrawPoint(target.Position, Color(1, 0, 0));
            }
        }
    }

    void MeleeRoutine(const Object& robot, const RobotInfo& robotInfo, AIRuntime& ai,
                      Object& target, const Vector3& targetDir, float dt) {
        constexpr float MELEE_ATTACK_RANGE = 10; // how close to actually deal damage
        constexpr float MELEE_SWING_TIME = 0.175f;
        constexpr float BACKSWING_TIME = 0.45f;
        constexpr float BACKSWING_RANGE = MELEE_ATTACK_RANGE * 3; // When to prepare a swing
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
                    if (dist < robot.Radius + MELEE_ATTACK_RANGE) {
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
                if (dist < robot.Radius + MELEE_ATTACK_RANGE && targetDir.Dot(robot.Rotation.Forward()) > 0) {
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

        auto slow = std::clamp(ai.RemainingSlow * 1.5f, 0.0f, MAX_SLOW_TIME);
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

    void ChangeState(Object& robot, AIRuntime& ai, AIState state) {
        auto& robotInfo = Resources::GetRobotInfo(robot);

        switch (state) {
            case AIState::Idle:
                ai.State = state;
                ai.Target = {}; // Clear target if robot loses interest.
                break;
            case AIState::Alert:
                ai.Awareness = 1;
                ai.ActiveTime = AI_DEFAULT_AWAKE_TIME * (1 + Random() * 0.25f);
                ai.State = state;
                ai.ChargingWeapon = false;
                break;
            case AIState::Roam:
                // NYI
                break;
            case AIState::Combat:
                // Delay weapons so robots don't shoot immediately on waking up
                if (ai.State == AIState::Idle || ai.State == AIState::Alert) {
                    ai.FireDelay = DifficultyInfo(robotInfo).FireDelay * .4f;
                    ai.FireDelay2 = DifficultyInfo(robotInfo).FireDelay2 * .4f;
                }

                ai.ActiveTime = AI_DEFAULT_AWAKE_TIME * (1 + Random() * 0.25f);
                ai.State = state;


                PlayAlertSound(robot, ai);
                break;
            case AIState::BlindFire:
                if (robotInfo.Attack == AttackType::Melee) {
                    SPDLOG_WARN("Melee robots cannot blind fire");
                    ai.State = AIState::Alert;
                    return;
                }

                if (robotInfo.Guns == 0) {
                    SPDLOG_WARN("Robot has no guns to blind fire with");
                    ai.State = AIState::Alert;
                    return; // Can't shoot, I have no guns!
                }

                if (!ai.Target) {
                    SPDLOG_WARN("Robot with no target attempted to blind fire");
                    ai.State = AIState::Alert;
                    return;
                }

                ai.Awareness = 1; // Reset awareness so robot stays alert for a while
                ai.BurstShots = 0; // Reset shot counter
                robot.NextThinkTime = 0;
                ai.ActiveTime = AI_BLIND_FIRE_TIME * (1 + Random() * 0.5f);
                ai.State = state;
                break;
            case AIState::FindHelp:
                PlayDistressSound(robot);
                ai.AlertTimer = 3 + Random() * 2;
                ai.State = state;
                ASSERT(ai.Ally.Id != ObjID::None); // Need an ally to run to
                break;
            case AIState::Path:
                if (ai.path.nodes.empty()) {
                    ASSERT(!ai.path.nodes.empty());
                    return;
                }

                ai.path.index = 0;
                ai.State = state;
                break;
        }
    }

    bool ScanForTarget(const Object& robot, AIRuntime& ai, bool* isThroughWall, float* distance) {
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
        if (targetDist > AI_VISION_FALLOFF_NEAR && !IsBossRobot(robot))
            falloff *= Game::Player.GetShipVisibility();

        if (distance) *distance = targetDist;
        auto reactionTime = AI_REACTION_TIME * (5 - (int)Game::Difficulty);
        ai.Awareness += falloff * ai.GetDeltaTime() / reactionTime;
        ai.Awareness = Saturate(ai.Awareness);

        ai.TargetObject = Game::GetObjectRef(target);
        ai.Target = { target.Segment, target.Position };
        return ai.Awareness >= 1;
    }

    void IdleRoutine(Object& robot, AIRuntime& ai, const RobotInfo& /*robotInfo*/) {
        ScanForTarget(robot, ai);

        if (ai.Awareness >= 1 && ai.TargetObject) {
            // Time to fight!
            Chat(robot, "Enemy spotted!");
            ChangeState(robot, ai, AIState::Combat);
        }
        else if (ai.Awareness >= 1) {
            ChangeState(robot, ai, AIState::Alert);
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

    bool FindHelp(AIRuntime& ai, Object& robot) {
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

                        auto dist = Vector3::Distance(help->Position, robot.Position);
                        if (dist < nearestDist && dist > AI_HELP_MIN_SEARCH_RADIUS) {
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
            if (SetPathGoal(Game::Level, robot, ai, goal, PathMode::StopAtEnd, AI_HELP_SEARCH_RADIUS)) {
                ai.Ally = Game::GetObjectRef(*nearestHelp);
                Chat(robot, "Maybe drone {} can help me", nearestHelp->Signature);
                ChangeState(robot, ai, AIState::FindHelp);
                ai.path.interruptable = false;
                ai.path.faceGoal = false;
            }
            return true;
        }
        else {
            Chat(robot, "... but I'm all alone :(");
            ai.Fear = 100;
            // Fight back harder or run away randomly

            ai.path.nodes = GenerateRandomPath(Game::Level, robot.Segment, 8);
            ai.path.index = 0;
            ai.PathDelay = AI_PATH_DELAY;
            return false;
        }
    }

    void FindHelpRoutine(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (ai.path.nodes.empty() || !ai.Target) {
            // Target can become none if it dies
            Chat(robot, "Where did the enemy go?");
            ChangeState(robot, ai, AIState::Alert);
            return;
        }

        if (ai.AlertTimer <= 0) {
            PlayDistressSound(robot);
            AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.Target, 0.5f, true);
            ai.AlertTimer = 3 + Random() * 2;
            Chat(robot, "Help!");
        }

        PathTowardsGoal(robot, ai);

        if (ai.path.nodes.empty())
            return; // Reached goal;

        auto [goalDir, goalDist] = GetDirectionAndDistance(ai.path.nodes.back().Position, robot.Position);

        constexpr float REACHED_GOAL_DIST = 50;
        if (goalDist > REACHED_GOAL_DIST) return;

        auto ally = Game::GetObject(ai.Ally);
        if (!ally) {
            Chat(robot, "Where did my friend go? :(");
            ChangeState(robot, ai, AIState::Alert);
            return;
        }

        // Is my friend still there?
        auto allyDist = Vector3::Distance(ally->Position, robot.Position);

        if (allyDist < REACHED_GOAL_DIST) {
            auto& allyAI = GetAI(*ally);
            if (ally->Control.AI.Behavior == AIBehavior::Still) {
                Chat(robot, "Drone {} I'm staying here with you", ai.Ally.Signature);
                allyAI.TargetObject = ai.TargetObject;
                allyAI.Target = ai.Target;
                ChangeState(robot, ai, AIState::Alert);
                ChangeState(*ally, allyAI, AIState::Alert);
                robot.Control.AI.Behavior = AIBehavior::Still;
                // Maybe alert another robot?
            }
            else {
                Chat(robot, "Hey drone {} go beat up the intruder, but I'm staying here!", ai.Ally.Signature);
                auto& allyInfo = Resources::GetRobotInfo(*ally);

                if (SetPathGoal(Game::Level, *ally, allyAI, *ai.Target, PathMode::StopAtEnd, allyInfo.ChaseDistance)) {
                    ChangeState(robot, ai, AIState::Alert);
                    //SetPathGoal(Game::Level, robot, ai, *ai.Target, PathMode::StopAtEnd, allyInfo.ChaseDistance);
                    allyAI.path.interruptable = true;
                    allyAI.path.faceGoal = true;
                    //ai.path.interruptable = true;
                }

                ChangeState(robot, ai, AIState::Alert);
            }

            ai.FleeTimer = 15 + Random() * 10; // Don't flee again for a while
        }
        //else {
        //    // Path to their new location
        //    PlayDistressSound(robot);
        //    SetPathGoal(Game::Level, robot, ai, { ally->Segment, ally->Position }, AI_MAX_CHASE_DISTANCE);
        //}
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
    }

    // Chooses how to react to the target going out of sight
    void OnLostLineOfSight(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo) {
        if (Game::Difficulty < DifficultyLevel::Hotshot) {
            Chat(robot, "Holding position");
            // Wait on trainee and rookie
            ChangeState(robot, ai, AIState::Alert);
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
        if (roll < chaseChance && ai.Target) {
            if (CanChase(robot, ai, &ai.Target.value()) && ChaseTarget(robot, ai, *ai.Target, PathMode::StopAtEnd, robotInfo.ChaseDistance)) {
                Chat(robot, "Pursuing target!");
                ai.path.faceGoal = true;
                return;
            }
            else {
                Chat(robot, "Target is too far from my post, holding position");
                ChangeState(robot, ai, AIState::Alert);
                return;
            }
        }

        if (roll < chaseChance + suppressChance) {
            ChangeState(robot, ai, AIState::BlindFire);
            return;
        }

        Chat(robot, "I've lost the target");
        ChangeState(robot, ai, AIState::Alert);
    }

    void AlertNearby(AIRuntime& ai, const Object& robot, const RobotInfo& robotInfo) {
        if (Game::Difficulty <= DifficultyLevel::Trainee)
            return; // Don't alert on trainee

        if (ai.AlertTimer > 0 || !ai.Target || robotInfo.AlertRadius <= 0)
            return;

        constexpr float ALERT_FREQUENCY = 0.2f; // Smooth out alerts
        //auto skillMult = Game::Difficulty >= DifficultyLevel::Insane ? 1.5f : 1;
        auto skillMult = 1;
        auto amount = robotInfo.AlertAwareness * ALERT_FREQUENCY * skillMult;
        AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.Target, amount);
        ai.AlertTimer = ALERT_FREQUENCY;
    }

    //bool MaybeChase(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, const Object& target) {
    //    // Chasing a cloaked target does no good, AI just gets confused.
    //    // Also don't chase the player ghost or if the robot is set not to circle the target
    //    if (target.IsCloaked() || target.Type == ObjectType::Ghost || ai.ChaseTimer > 0)
    //        return false;

    //    auto targetDistance = Vector3::Distance(ai.Target->Position, robot.Position);

    //    if (targetDistance < robotInfo.ChaseDistance && Random() < robotInfo.ChaseChance) {
    //        if (ChaseTarget(robot, ai, *ai.Target, PathMode::StopAtEnd, robotInfo.ChaseDistance)) {
    //            Chat(robot, "Pursuing hostile");
    //            ai.path.faceGoal = true;
    //            return true;
    //        }
    //    }

    //    ai.ChaseTimer = AI_CURIOSITY_INTERVAL;
    //    return false;
    //}

    // Only robots that flee can find help. Limit to hotshot and above.
    void MaybeFlee(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        if (GlobalFleeTimer > 0) return;
        if (robotInfo.FleeThreshold <= 0) return; // Can't flee
        if (ai.IsFiring()) return; // Don't interrupt firing
        if (robot.Control.AI.Behavior == AIBehavior::Still) return; // Still enemies can't flee
        if (Game::Difficulty < DifficultyLevel::Hotshot) return; // limit the difficulty

        bool shouldFlee = robot.HitPoints / robot.MaxHitPoints <= robotInfo.FleeThreshold || ai.Fear >= 1;

        if (shouldFlee && ai.FleeTimer < 0 && FleeingDrones == 0) {
            if (shouldFlee /*Random() > 0.5*/) {
                if (DronesInCombat <= AI_ALLY_FLEE_MIN) {
                    FindHelp(ai, robot);
                    GlobalFleeTimer = AI_GLOBAL_FLEE_DELAY; // Only allow one robot to flee every so often
                }
                else {
                    // Wounded or scared enough to flee, but would rather fight if there's allies nearby
                    Chat(robot, "I'm scared but my friends are here");
                }
            }

            ai.FleeTimer = 2 + Random() * 5;
        }
    }

    void CombatRoutine(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float dt) {
        auto pTarget = Game::GetObject(ai.TargetObject);
        if (!pTarget) {
            // Target died or didn't have one, return to alert state and find a new one
            ChangeState(robot, ai, AIState::Alert);
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
            if (!ai.Target) {
                SPDLOG_WARN("Robot {} had a target obj with no position, clearing target", robot.Signature);
                ai.TargetObject = {};
                return;
            }

            targetDir = GetDirection(ai.Target->Position, robot.Position);
        }

        // Track the known target position, even without LOS. Causes AI to look more intelligent by pre-aiming.
        TurnTowardsDirection(robot, targetDir, DifficultyInfo(robotInfo).TurnTime);

        // Update target location if it is in line of sight and not cloaked
        if (hasLos && !IsCloakEffective(target)) {
            ai.Target = { target.Segment, target.Position };
            ai.Awareness = AI_AWARENESS_MAX;
            ai.LostSightDelay = 0.4f; // Wait a moment when target goes out of sight before chasing
            ai.ActiveTime = std::min(AI_DEFAULT_AWAKE_TIME, ai.ActiveTime);

            // Try to get behind target unless dodging. Maybe make this only happen sometimes?
            if (robotInfo.GetBehind && robot.Control.AI.Behavior != AIBehavior::Still && ai.DodgeTime <= 0)
                GetBehindTarget(robot, ai, robotInfo, target);

            if (Settings::Cheats.ShowPathing && ai.Target)
                Graphics::DrawPoint(ai.Target->Position, Color(1, 0, 0));

            AlertNearby(ai, robot, robotInfo);
            PlayCombatNoise(robot, ai);
        }
        else {
            ai.LostSightDelay -= dt;
            // Robot can either choose to chase the target or hold position and blind fire

            if (Settings::Cheats.ShowPathing && ai.Target)
                Graphics::DrawPoint(ai.Target->Position, Color(1, .5, .5));

            // <= 8 failsafe for robots that constantly fire like PTMC defense
            if (/*ai.StrafeTimer <= 0 &&*/ (ai.LostSightDelay <= 0 && !ai.IsFiring()) || ai.LostSightDelay <= 8) {
                //ai.Target = { target.Segment, target.Position }; // cheat and update the target with the real position before pathing
                OnLostLineOfSight(ai, robot, robotInfo);
            }
            /*else if (MaybeChase(ai, robot, robotInfo, target))
                return;*/

            // Prevent robots from exiting attack mode mid-attack
            //if (ai.ActiveTime <= 0 && !ai.IsFiring()) {
            //    Chat(robot, "Stay on alert");
            //    ChangeState(robot, ai, AIState::Alert);
            //    //ai.BurstShots = 0; // Reset shot counter
            //}
        }

        // Prevent attacking during phasing (matcens and teleports)
        if (!robot.IsPhasing()) {
            if (robotInfo.Attack == AttackType::Ranged)
                RangedRoutine(robot, robotInfo, ai, dt, !hasLos || IsCloakEffective(Game::GetPlayerObject()));
            else if (robotInfo.Attack == AttackType::Melee)
                MeleeRoutine(robot, robotInfo, ai, target, targetDir, dt);
        }
    }

    void BeginAIFrame() {
        DronesInCombat = DronesInCombatCounter;
        DronesInCombatCounter = 0;

        FleeingDrones = FleeingDronesCounter;
        FleeingDronesCounter = 0;
    }

    void AlertRoutine(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float /*dt*/) {
        if (!ai.PlayingAnimation() && ai.AnimationState != Animation::Alert)
            PlayRobotAnimation(robot, Animation::Alert, 1);

        if (ScanForTarget(robot, ai) && ai.TargetObject) {
            Chat(robot, "Enemy spotted!");
            ChangeState(robot, ai, AIState::Combat);
            return; // Found a target, start firing!
        }

        // Turn towards point of interest if we have one
        if (ai.Target) {
            TurnTowardsPoint(robot, ai.Target->Position, DifficultyInfo(robotInfo).TurnTime);
            //bool validState = ai.CombatState == AICombatState::Normal || ai.CombatState == AICombatState::Chase;

            if (Settings::Cheats.ShowPathing)
                Graphics::DrawPoint(ai.Target->Position, Color(1, 0, 1));

            // Move around a little to look more alive
            if (ai.DodgeDelay <= 0) {
                ai.DodgeVelocity = RandomLateralDirection(robot) * 2.0f;
                ai.DodgeDelay = 2.0f + Random() * 0.5f;
                ai.DodgeTime = 0.6f + Random() * 0.4f;
            }

            if (ai.ChaseTimer <= 0 &&
                ai.Awareness >= AI_AWARENESS_MAX &&
                CanChase(robot, ai)) {
                ai.ChaseTimer = AI_CURIOSITY_INTERVAL; // Only check periodically

                auto targetDistanceSq = Vector3::DistanceSquared(ai.Target->Position, robot.Position);
                auto ambushDistanceSq = robotInfo.AmbushDistance * robotInfo.AmbushDistance;

                if (targetDistanceSq > ambushDistanceSq) {
                    Chat(robot, "I hear something but it's too far from my post");
                }
                else if (Random() < robotInfo.Curiosity) {
                    // Only path to target if we can't see it
                    if (!HasLineOfSight(robot, ai.Target->Position)) {
                        // todo: sometimes the target isn't reachable due to locked doors or walls, use other behaviors

                        if (ChaseTarget(robot, ai, *ai.Target, PathMode::StopVisible, robotInfo.ChaseDistance)) {
                            ai.path.faceGoal = true;
                            ai.path.interruptable = true;
                            Chat(robot, "I hear something, better check it out");
                        }
                    }
                }
                else {
                    Chat(robot, "I hear something but will wait here");
                }
            }
        }

        if (ai.ActiveTime <= 0) {
            Chat(robot, "All quiet");
            ChangeState(robot, ai, AIState::Idle);
        }
    }

    void SupervisorBehavior(AIRuntime& ai, Object& robot, const RobotInfo& robotInfo, float /*dt*/) {
        if (!Game::EnableAi()) return;

        // Periodically alert allies while not idle
        if (ai.State != AIState::Idle && ai.AlertTimer <= 0 && ai.Target) {
            Sound3D sound(robotInfo.SeeSound);
            sound.Volume = 1.15f;
            sound.Radius = AI_SOUND_RADIUS;
            sound.Pitch = -Random() * 0.35f;
            Sound::PlayFrom(sound, robot);

            AlertRobotsOfTarget(robot, robotInfo.AlertRadius, *ai.Target, 10, true);
            ai.AlertTimer = 5;
            Chat(robot, "Intruder alert!");
        }

        // Supervisors are either in path mode or idle. They cannot peform any other action.
        if (ai.State == AIState::Path) {
            PathTowardsGoal(robot, ai);
            //MakeCombatNoise(robot, ai);
        }
        else {
            if (ScanForTarget(robot, ai)) {
                auto target = Game::GetObject(ai.TargetObject);
                ai.path.nodes = GenerateRandomPath(Game::Level, robot.Segment, 15, NavigationFlag::OpenKeyDoors, target ? target->Segment : SegID::None);
                ai.path.interruptable = false;
                ai.path.mode = PathMode::StopAtEnd;
                Chat(robot, "Hostile sighted!");
                ChangeState(robot, ai, AIState::Path);
            }
            else if (ai.Awareness <= 0 && ai.State != AIState::Idle) {
                Chat(robot, "All quiet");
                ChangeState(robot, ai, AIState::Idle);
            }
        }
    }

    void MineLayerBehavior(AIRuntime& ai, Object& robot, const RobotInfo& /*robotInfo*/, float /*dt*/) {
        if (!Game::EnableAi()) return;

        ScanForTarget(robot, ai);
        ai.path.interruptable = false;

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

            PathTowardsGoal(robot, ai);

            if (ai.FireDelay <= 0) {
                auto weapon = robot.Control.AI.SmartMineFlag() ? WeaponID::SmartMine : WeaponID::ProxMine;
                Game::FireWeaponInfo info = { .id = weapon, .gun = 0, .showFlash = false };
                Game::FireWeapon(robot, info);
                Game::PlayWeaponSound(weapon, 1, robot, 0);
                ai.FireDelay = AI_MINE_LAYER_DELAY * (1 + Random() * 0.5f);
            }

            PlayCombatNoise(robot, ai);
        }
        else if (ai.Awareness > 0 && !ai.HasPath()) {
            // Keep pathing until awareness fully decays
            Chat(robot, "Someone is nearby! I'm going to mine the area");
            auto target = Game::GetObject(ai.TargetObject);
            ai.path.nodes = GenerateRandomPath(Game::Level, robot.Segment, 6, NavigationFlag::None, target ? target->Segment : SegID::None);

            // If path is short, it might be due to being cornered by the player. Try again ignoring the player.
            if (ai.path.nodes.size() < 2)
                ai.path.nodes = GenerateRandomPath(Game::Level, robot.Segment, 6, NavigationFlag::None);

            ai.path.mode = PathMode::StopAtEnd;
            ai.AlertTimer = 1 + Random() * 2;
            ai.FireDelay = AI_MINE_LAYER_DELAY * Random();
            ai.ActiveTime = AI_MINE_LAYER_AWAKE_TIME * (1 + Random() * 0.25f);
            ChangeState(robot, ai, AIState::Path);
        }
        else if (ai.ActiveTime <= 0 && ai.State != AIState::Idle) {
            // Go to sleep
            ai.ClearPath();
            PlayRobotAnimation(robot, Animation::Rest);
            Chat(robot, "I haven't heard an enemy recently, I'll stop dropping bombs");
            ChangeState(robot, ai, AIState::Idle);
        }
    }

    void PathRoutine(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        //bool fromMatcen = robot.SourceMatcen != MatcenID::None;
        // Check if reached goal

        if (ai.path.nodes.empty()) {
            Chat(robot, "I don't know where to go");
            ChangeState(robot, ai, AIState::Alert);
            return;
        }

        // todo: Stop chasing once robot can see source of sound, otherwise move to the location.
        // This is so a fleeing player is pursued around corners

        if (ai.Target && ai.Target->Segment == robot.Segment) {
            // Clear target if pathing towards it discovers the target isn't there.
            // This is so the robot doesn't turn around while chasing
            ai.Target = {};
        }

        // Saw an enemy
        bool throughWall = false;
        float distance = 0;
        //if (ai.path.interruptable && ScanForTarget(robot, ai, &throughWall) && (!throughWall || robotInfo.Attack == AttackType::Ranged)) {
        if (ai.path.interruptable && ScanForTarget(robot, ai, &throughWall, &distance)) {
            // don't stop pathing for melee robots unless target is close to a wall so they can swing through it
            if (robotInfo.Attack == AttackType::Ranged || (!throughWall || distance < MELEE_RANGE)) {
                ai.ClearPath(); // Stop chasing if robot finds a target
                Chat(robot, "You can't hide from me!");
                ChangeState(robot, ai, AIState::Combat);
            }
        }

        if (!PathTowardsGoal(robot, ai))
            ChangeState(robot, ai, AIState::Alert);


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
                IdleRoutine(robot, ai, robotInfo);
                break;
            case AIState::Alert:
                DodgeProjectiles(robot, ai, robotInfo, Game::Level);
                AlertRoutine(robot, ai, robotInfo, dt);
                MaybeFlee(robot, ai, robotInfo);
                break;
            case AIState::Combat:
                DodgeProjectiles(robot, ai, robotInfo, Game::Level);
                CombatRoutine(robot, ai, robotInfo, dt);
                MaybeFlee(robot, ai, robotInfo);
                break;
            case AIState::Roam:
                break;
            case AIState::BlindFire:
                DodgeProjectiles(robot, ai, robotInfo, Game::Level);
                BlindFireRoutine(robot, ai, robotInfo, dt);
                MaybeFlee(robot, ai, robotInfo);
                break;
            case AIState::Path:
                DodgeProjectiles(robot, ai, robotInfo, Game::Level);
                PathRoutine(robot, ai, robotInfo);
                break;
            case AIState::FindHelp:
                DodgeProjectiles(robot, ai, robotInfo, Game::Level);
                FindHelpRoutine(robot, ai, robotInfo);
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
                // explode object, create sound
                AlertAlliesOfDeath(robot);
                ExplodeObject(robot);

                if (Game::LevelNumber < 0) {
                    // todo: respawn thief on secret levels
                }
            }
            return; // Can't act while dying
        }

        //else if (HasFlag(robot.Physics.Flags, PhysicsFlag::Gravity))
        //    ClearFlag(robot.Physics.Flags, PhysicsFlag::Gravity); // Unstunned
        ai.ActiveTime -= dt;
        ai.ActiveTime = std::max(ai.ActiveTime, 0.0f);

        if (ai.RemainingStun > 0) {
            if (ai.AnimationState == Animation::Flinch)
                AnimateRobot(robot, ai, dt); // animate robots getting flinched by the stun

            return; // Can't act while stunned
        }

        AnimateRobot(robot, ai, dt);

        if (robot.NextThinkTime == NEVER_THINK || robot.NextThinkTime > Game::Time)
            return;

        if (robotInfo.IsBoss && Game::Level.IsDescent1())
            Game::BossBehaviorD1(ai, robot, robotInfo, dt);
        else if (robotInfo.Script == SUPERVISOR_SCRIPT)
            SupervisorBehavior(ai, robot, robotInfo, dt);
        else if (robot.Control.AI.Behavior == AIBehavior::RunFrom)
            MineLayerBehavior(ai, robot, robotInfo, dt);
        else
            DefaultBehavior(ai, robot, robotInfo, dt);

        if (ai.DodgeTime > 0 && ai.DodgeVelocity != Vector3::Zero && Game::EnableAi())
            ai.Velocity += ai.DodgeVelocity;

        DecayAwareness(ai);
        ai.Awareness = std::clamp(ai.Awareness, 0.0f, 1.0f);

        // Force aware robots to always update
        SetFlag(robot.Flags, ObjectFlag::AlwaysUpdate, ai.State != AIState::Idle);

        //ClampThrust(robot, ai);
        ApplyVelocity(robot, ai, dt);
        ai.LastUpdate = Game::Time;

        if (ai.State == AIState::Combat || ai.State == AIState::FindHelp || ai.State == AIState::BlindFire)
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
