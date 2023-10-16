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
#include "Physics.Math.h"
#include "SoundSystem.h"
#include "Editor/Editor.Selection.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.Particles.h"

namespace Inferno {
    namespace {
        List<AIRuntime> RuntimeState;
        IntersectContext Intersect(Game::Level);
    }

    void ResetAI() {
        for (auto& ai : RuntimeState)
            ai = {};

        Game::InitBoss();
    }

    void ResizeAI(size_t size) {
        if (size > RuntimeState.capacity()) {
            size = size + 50;
            SPDLOG_INFO("Resizing AI state");
        }

        RuntimeState.resize(size);
    }

    AIRuntime& GetAI(const Object& obj) {
        assert(obj.IsRobot());
        return RuntimeState[(int)Game::GetObjectRef(obj).Id];
    }

    constexpr float AWARENESS_INVESTIGATE = 0.5f; // when a robot exceeds this threshold it will investigate the point of interest
    constexpr float MAX_SLOW_TIME = 2.0f; // Max duration of slow
    constexpr float MAX_SLOW_EFFECT = 0.9f; // Max percentage of slow to apply to a robot
    constexpr float MAX_SLOW_THRESHOLD = 0.4f; // Percentage of life dealt to reach max slow

    constexpr float STUN_THRESHOLD = 27.5; // Minimum damage to stun a robot. Concussion is 30 damage.
    constexpr float MAX_STUN_PERCENT = 0.6f; // Percentage of life required in one hit to reach max stun time
    constexpr float MAX_STUN_TIME = 1.5f; // max stun in seconds
    constexpr float MIN_STUN_TIME = 0.25f; // min stun in seconds. Stuns under this duration are discarded.

    const RobotDifficultyInfo& Difficulty(const RobotInfo& info) {
        return info.Difficulty[Game::Difficulty];
    }

    void AddAwareness(AIRuntime& ai, float awareness) {
        ai.Awareness += awareness;
        if (ai.Awareness > 1) ai.Awareness = 1;
    }

    void AlertEnemiesInRoom(Level& level, const Room& room, SegID soundSeg, const Vector3& position, float soundRadius, float awareness) {
        for (auto& segId : room.Segments) {
            auto pseg = level.TryGetSegment(segId);
            if (!pseg) continue;
            auto& seg = *pseg;

            for (auto& objId : seg.Objects) {
                if (auto obj = level.TryGetObject(objId)) {
                    if (!obj->IsRobot()) continue;

                    auto dist = Vector3::Distance(obj->Position, position);
                    if (dist > soundRadius) continue;

                    //auto falloff = std::clamp(std::lerp(awareness, 0.0f, (soundRadius - dist) / soundRadiusSq), 0.0f, 1.0f);
                    auto falloff = std::powf(1 - dist / soundRadius, 2); // inverse falloff 
                    //auto falloff = Saturate(InvLerp(soundRadius, 0, dist));
                    auto& ai = GetAI(*obj);

                    auto prevAwareness = ai.Awareness;
                    ai.Awareness += awareness * falloff;
                    //SPDLOG_INFO("Alerted enemy {} by {} from sound", obj->Signature, awareness * falloff);

                    //Render::Debug::DrawPoint(obj->Position, Color(1, 1, 0));

                    if (prevAwareness < AWARENESS_INVESTIGATE && ai.Awareness > AWARENESS_INVESTIGATE) {
                        SPDLOG_INFO("Enemy {}:{} investigating sound at {}, {}, {}!", objId, obj->Signature, position.x, position.y, position.z);

                        auto& robotInfo = Resources::GetRobotInfo(*obj);
                        auto path = Game::Navigation.NavigateTo(obj->Segment, soundSeg, !robotInfo.IsThief, Game::Level);
                        ai.PathDelay = AI_PATH_DELAY;
                        ai.GoalSegment = soundSeg;
                        ai.GoalPosition = position;
                        ai.GoalRoom = level.GetRoomID(soundSeg);
                        ai.GoalPath = path;
                        ai.GoalPathIndex = 0;
                        obj->NextThinkTime = 0;
                    }
                }
            }
        }
    }

    // adds awareness to robots in nearby rooms
    void AlertEnemiesOfNoise(const Object& source, float soundRadius, float awareness) {
        auto& level = Game::Level;
        auto room = level.GetRoomID(source);
        if (room == RoomID::None) return;

        auto action = [&](const Room& r) {
            AlertEnemiesInRoom(level, r, source.Segment, source.Position, soundRadius, awareness);
        };

        Game::TraverseRoomsByDistance(level, room, source.Position, soundRadius, true, action);
    }

    void PlayAlertSound(const Object& obj, const RobotInfo& robot) {
        if (robot.IsBoss) return; // Bosses handle sound differently
        auto id = Game::GetObjectRef(obj);
        Sound3D sound({ robot.SeeSound }, id);
        sound.AttachToSource = true;
        Sound::Play(sound);
    }

    bool PointInFOV(const Object& robot, const Vector3& pointDir, const RobotInfo& robotInfo) {
        auto dot = robot.Rotation.Forward().Dot(pointDir);
        auto& diff = robotInfo.Difficulty[Game::Difficulty];
        return dot >= diff.FieldOfView;
    }

    bool CanSeeObject(const Object& obj, const Vector3& objDir, float objDist, AIRuntime& ai) {
        if (obj.IsCloaked()) return false; // Can't see cloaked object

        LevelHit hit{};
        Ray ray = { obj.Position, objDir };
        RayQuery query{ .MaxDistance = objDist, .Start = obj.Segment, .PassTransparent = true };
        bool visible = !Game::Intersect.RayLevel(ray, query, hit);
        if (visible) ai.LastSeenPlayer = 0;
        return visible;
    }

    // Player visibility doesn't account for direct line of sight like weapon fire does (other robots, walls)
    bool CanSeePlayer(const Object& robot, const RobotInfo& robotInfo) {
        auto& player = Game::GetPlayerObject();
        auto [playerDir, dist] = GetDirectionAndDistance(player.Position, robot.Position);
        auto& ai = GetAI(robot);
        if (!CanSeeObject(robot, playerDir, dist, ai))
            return false;

        if (!PointInFOV(robot, playerDir, robotInfo))
            return false;

        auto prevAwareness = ai.Awareness;
        AddAwareness(ai, 1);

        // only play sound when robot was asleep
        if (prevAwareness < 0.3f) {
            PlayAlertSound(robot, robotInfo);
            PlayRobotAnimation(robot, AnimState::Alert, 0.5f);
            // Delay firing after waking up
            float wakeTime = (5 - Game::Difficulty) * 0.3f;
            ai.FireDelay = std::min(Difficulty(robotInfo).FireDelay, wakeTime);
            ai.FireDelay2 = Difficulty(robotInfo).FireDelay2;
        }

        return true;
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
            // Going critical!
            if (!dyingSoundPlaying) {
                Sound3D sound(resource, Game::GetObjectRef(obj));
                sound.Volume = volume;
                sound.Radius = 400; // Should be a global radius for bosses
                sound.AttachToSource = true;
                Sound::Play(sound);
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

    void MoveTowardsPoint(Object& obj, const Vector3& point, float thrust) {
        auto dir = point - obj.Position;
        dir.Normalize();
        obj.Physics.Thrust += dir * thrust;
    }

    void ClampThrust(Object& robot, const AIRuntime& ai) {
        if (ai.RemainingStun > 0) {
            robot.Physics.Thrust = Vector3::Zero;
            robot.Physics.AngularThrust = Vector3::Zero;
            return;
        }

        auto& robotInfo = Resources::GetRobotInfo(robot.ID);

        auto slow = ai.RemainingSlow;
        float slowScale = slow > 0 ? 1 - MAX_SLOW_EFFECT * slow / MAX_SLOW_TIME : 1;

        auto maxSpeed = Difficulty(robotInfo).Speed / 8 * slowScale;
        Vector3 maxThrust(maxSpeed, maxSpeed, maxSpeed);
        robot.Physics.Thrust.Clamp(-maxThrust, maxThrust);

        auto maxAngle = slowScale * 1 / Difficulty(robotInfo).TurnTime;
        Vector3 maxAngVel(maxAngle, maxAngle, maxAngle);
        robot.Physics.AngularThrust.Clamp(-maxAngVel, maxAngVel);
    }

    float GetRotationSpeed(const RobotInfo& ri) {
        auto turnTime = Difficulty(ri).TurnTime;
        if (turnTime <= 0) turnTime = 1.0f;
        return 1 / turnTime / 8;
    }

    struct AiExtended {
        float AwarenessDecay = 0.2f; // Awareness decay per second
        float Fear = 0.2f; // Taking damage increases flee state
        float Curiosity = 0.2f; // How much awareness from noise / likeliness to investigate
    };

    AiExtended DefaultAi{};

    void FireWeaponAtPoint(const Object& obj, const RobotInfo& robot, uint8 gun, const Vector3& point, WeaponID weapon) {
        auto aim = 8.0f - 7.0f * FixToFloat(robot.Aim << 8);

        // todo: seismic disturbance inaccuracy

        // Randomize target based on difficulty
        Vector3 target = {
            point.x + RandomN11() * (5 - Game::Difficulty - 1) * aim,
            point.y + RandomN11() * (5 - Game::Difficulty - 1) * aim,
            point.z + RandomN11() * (5 - Game::Difficulty - 1) * aim
        };

        // this duplicates position/direction calculation in FireWeapon...
        auto gunOffset = GetSubmodelOffset(obj, { robot.GunSubmodels[gun], robot.GunPoints[gun] });
        auto position = Vector3::Transform(gunOffset, obj.GetTransform());
        auto direction = NormalizeDirection(target, position);
        auto id = Game::GetObjectRef(obj);
        Game::FireWeapon(id, weapon, gun, &direction);
    }

    constexpr float FAST_WEAPON_SPEED = 200;

    // Returns a vector to lead the target by
    Vector3 LeadTarget(const Vector3& targetDir, float targetDist, const AITarget& target, float projectileSpeed) {
        constexpr float MAX_LEAD_DISTANCE = 200;
        constexpr float MIN_LEAD_SPEED = 4;
        constexpr float LEAD_ANGLE = 45 * DegToRad;

        if (projectileSpeed > FAST_WEAPON_SPEED) {
            if (Game::Difficulty <= 1)
                return Vector3::Zero; // Don't lead with fast weapons on rookie and below

            projectileSpeed *= 5 - Game::Difficulty; // Scale speed based on difficulty
        }

        if (projectileSpeed <= 5)
            return Vector3::Zero; // if projectile is too slow leading is pointless

        // don't lead distant targets
        if (targetDist > MAX_LEAD_DISTANCE)
            return Vector3::Zero;

        auto targetSpeed = target.Velocity.Length();
        if (targetSpeed < MIN_LEAD_SPEED)
            return Vector3::Zero; // don't lead slow targets

        Vector3 velDir;
        target.Velocity.Normalize(velDir);
        auto dot = targetDir.Dot(velDir);
        if (dot < -LEAD_ANGLE || dot > LEAD_ANGLE)
            return Vector3::Zero; // outside of reasonable lead angle

        float expectedTravelTime = targetDist / projectileSpeed;
        return target.Velocity * expectedTravelTime;
    }

    void DecayAwareness(AIRuntime& ai) {
        auto deltaTime = float(Game::Time - ai.LastUpdate);
        ai.Awareness -= DefaultAi.AwarenessDecay * deltaTime;
        if (ai.Awareness < 0) ai.Awareness = 0;
    }

    // Vectors must have same origin and be on same plane
    float SignedAngleBetweenVectors(const Vector3& a, const Vector3& b, const Vector3& normal) {
        return std::atan2(a.Cross(b).Dot(normal), a.Dot(b));
    }

    // Returns the max amount of aim assist a weapon can have when fired by a robot
    float GetAimAssistAngle(const Weapon& weapon) {
        // Fast weapons get less assistance for balance reasons
        return weapon.Speed[Game::Difficulty] > FAST_WEAPON_SPEED ? 12.5f * DegToRad : 30.0f * DegToRad;
    }

    void CycleGunpoint(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        ai.GunIndex = robotInfo.Guns > 0 ? (ai.GunIndex + 1) % robotInfo.Guns : 0;
        if (Game::Level.IsDescent1() && robot.ID == 23 && ai.GunIndex == 2)
            ai.GunIndex = 3; // HACK: skip to 3 due to gunpoint 2 being zero-filled on the D1 final boss

        if (robotInfo.WeaponType2 != WeaponID::None && ai.GunIndex == 0)
            ai.GunIndex = 1; // Reserve gun 0 for secondary weapon if present
    }

    void FireRobotWeapon(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, Vector3 target, bool primary) {
        if (!primary && robotInfo.WeaponType2 == WeaponID::None) return; // no secondary set

        auto& weapon = Resources::GetWeapon(primary ? robotInfo.WeaponType : robotInfo.WeaponType2);

        uint8 gunIndex = primary ? ai.GunIndex : 0;
        auto [aimDir, aimDist] = GetDirectionAndDistance(target, robot.Position);

        float aimAssist = GetAimAssistAngle(weapon);
        auto forward = robot.Rotation.Forward();

        if (AngleBetweenVectors(aimDir, forward) > aimAssist) {
            // clamp the angle if target it outside of the max aim assist
            auto normal = forward.Cross(aimDir);
            if (normal.Dot(robot.Rotation.Up()) < 0) normal *= -1;

            auto angle = SignedAngleBetweenVectors(forward, aimDir, normal);
            auto aimAngle = aimAssist;
            if (angle < 0) aimAngle *= -1;

            auto transform = Matrix::CreateFromAxisAngle(normal, aimAngle);
            target = robot.Position + Vector3::Transform(forward, transform) * aimDist;
        }

        FireWeaponAtPoint(robot, robotInfo, gunIndex, target, robotInfo.WeaponType);

        if (primary)
            CycleGunpoint(robot, ai, robotInfo);
    }

    void DodgeProjectile(const Object& robot, AIRuntime& ai, const Object& projectile, const RobotInfo& robotInfo) {
        if (projectile.Physics.Velocity.LengthSquared() < 5 * 5) return; // Don't dodge slow projectiles. also prevents crash at 0 velocity.

        auto [projDir, projDist] = GetDirectionAndDistance(projectile.Position, robot.Position);
        // Looks weird to dodge distant projectiles. also they might hit another target
        // Consider increasing this for massive robots?
        if (projDist > AI_MAX_DODGE_DISTANCE) return;
        if (!PointInFOV(robot, projDir, robotInfo)) return;

        Vector3 projTravelDir;
        projectile.Physics.Velocity.Normalize(projTravelDir);
        Ray projRay = { projectile.Position, projTravelDir };
        auto dodgePoint = ProjectRayOntoPlane(projRay, robot.Position, -projTravelDir);
        if (!dodgePoint) return;
        auto dodgeDir = robot.Position - *dodgePoint;
        if (dodgeDir.Length() > robot.Radius * 1.25f) return; // Don't dodge projectiles that won't hit us
        ai.DodgeDirection = dodgeDir;
        ai.DodgeDelay = (5 - Game::Difficulty) / 2.0f * 2.0f * Random(); // (2.5 to 0.5) * 2 delay
        ai.DodgeTime = AI_DODGE_TIME * 0.5f + AI_DODGE_TIME * 0.5f * Random();
    }

    float EstimateDodgeDistance(const RobotInfo& robot) {
        return (4 / robot.Mass) * Difficulty(robot).Speed;
    }

    void CheckProjectiles(Level& level, const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        auto room = level.GetRoom(robot);
        if (ai.DodgeDelay > 0) return; // not ready to dodge again

        for (auto& segId : room->Segments) {
            if (!level.SegmentExists(segId)) continue;
            auto& seg = level.GetSegment(segId);
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
        }
    }

    // Tries to path towards the player or move directly to it if in the same room
    void MoveTowardsObject(Level& level, const Object& object, Object& robot,
                           AIRuntime& ai, const Vector3& objDir, float objDist) {
        if (CanSeeObject(robot, objDir, objDist, ai)) {
            Ray ray(robot.Position, objDir);
            //AvoidConnectionEdges(level, ray, desiredIndex, obj, thrust);
            Vector3 playerPosition = object.Position;
            AvoidRoomEdges(level, ray, robot, playerPosition);
            //auto& seg = level.GetSegment(robot.Segment);
            //AvoidSideEdges(level, ray, seg, side, robot, 0, player.Position);
            MoveTowardsPoint(robot, playerPosition, 100); // todo: thrust from difficulty
        }
        else {
            SetPathGoal(level, robot, ai, object.Segment, object.Position);
        }
    }

    // Moves towards a random segment further away from the player. Prefers room portals.
    void MoveAwayFromPlayer(Level& /*level*/, const Object& player, Object& robot) {
        auto playerDir = player.Position - robot.Position;
        playerDir.Normalize();
        Ray ray(robot.Position, -playerDir);
        LevelHit hit;
        RayQuery query{ .MaxDistance = 10, .Start = robot.Segment };
        if (Intersect.RayLevel(ray, query, hit))
            return; // no room to move backwards

        // todo: try escaping through portals if there are any in the player's FOV
        MoveTowardsPoint(robot, robot.Position - playerDir * 10, 10);
    }

    void MoveToCircleDistance(Level& level, const Object& player, Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        auto circleDistance = Difficulty(robotInfo).CircleDistance;
        auto [dir, dist] = GetDirectionAndDistance(player.Position, robot.Position);
        auto distOffset = dist - circleDistance;
        if (abs(distOffset) < 20 && circleDistance > 10 && robotInfo.Attack == AttackType::Ranged)
            return; // already close enough

        if (distOffset > 0)
            MoveTowardsObject(level, player, robot, ai, dir, dist);
        else
            MoveAwayFromPlayer(level, player, robot);
    }

    void PlayRobotAnimation(const Object& robot, AnimState state, float time, float moveMult) {
        auto& robotInfo = Resources::GetRobotInfo(robot);
        auto& angles = robot.Render.Model.Angles;

        //float remaining = 1;
        // if a new animation is requested before the previous one finishes, speed up the new one as it has less distance
        //if (ail.AnimationTime < ail.AnimationDuration)
        //    remaining = (ail.AnimationDuration - ail.AnimationTime) / ail.AnimationDuration;

        auto& ai = GetAI(robot);
        ai.AnimationDuration = time /** remaining*/;
        ai.AnimationTime = 0;
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

        ai.AnimationTime += dt;
        if (ai.AnimationTime > ai.AnimationDuration) return;

        for (int joint = 1; joint < model.Submodels.size(); joint++) {
            auto& curAngle = robot.Render.Model.Angles[joint];
            curAngle += ai.DeltaAngles[joint] / ai.AnimationDuration * dt;
        }
    }

    void DamageRobot(const Vector3& source, bool sourceIsPlayer, Object& robot, float damage, float stunMult) {
        auto& info = Resources::GetRobotInfo(robot);
        auto& ai = GetAI(robot);

        // Wake up a robot if it gets hit
        if (ai.Awareness < .30f) {
            ai.Awareness = .30f;
            ai.Target = source; // Ok to look at ally if they woke this robot up
        }

        if (sourceIsPlayer)
            ai.LastHitByPlayer = 0;

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

            if (auto beam = Render::EffectLibrary.GetBeamInfo("stunned_object_arcs")) {
                auto startObj = Game::GetObjectRef(robot);
                Render::AddBeam(*beam, stunTime, startObj);
                Render::AddBeam(*beam, stunTime, startObj);
            }
        }

        if (Settings::Cheats.DisableWeaponDamage) return;

        robot.HitPoints -= damage;
        if (info.IsBoss) return;
        if (robot.HitPoints <= 0 && info.DeathRoll == 0)
            ExplodeObject(robot); // Explode normal robots immediately
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

    void FireRobotPrimary(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const Vector3& target) {
        ai.FireDelay = 0;
        // multishot: consume as many projectiles as possible based on burst count
        // A multishot of 1 and a burst of 3 would fire 2 projectiles then 1 projectile
        // Multishot incurs extra fire delay per projectile
        auto burstDelay = std::min(1 / 8.0f, Difficulty(robotInfo).FireDelay / 2);
        for (int i = 0; i < robotInfo.Multishot; i++) {
            ai.FireDelay += burstDelay;

            FireRobotWeapon(robot, ai, robotInfo, target, true);
            ai.BurstShots++;
            if (ai.BurstShots >= Difficulty(robotInfo).ShotCount) {
                ai.BurstShots = 0;
                ai.FireDelay += Difficulty(robotInfo).FireDelay;
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
                auto id = Game::GetObjectRef(robot);
                fx->Parent = id;

                Sound3D sound({ SoundID::FusionWarmup }, id);
                sound.AttachToSource = true;
                ai.SoundHandle = Sound::Play(sound);

                for (uint8 i = 0; i < robotInfo.Guns; i++) {
                    fx->ParentSubmodel.Offset = GetGunpointOffset(robot, i);
                    Render::AddSparkEmitter(*fx, robot.Segment);
                }
            }
        }

        //if (ai.WeaponCharge >= Difficulty(info).FireDelay * 2) {
        if (ai.WeaponCharge >= 1) {
            Sound::Stop(ai.SoundHandle);
            auto target = ai.Target ? *ai.Target : robot.Position + robot.Rotation.Forward() * 40;
            FireRobotPrimary(robot, ai, robotInfo, target);
            ai.WeaponCharge = 0;
        }
    }

    // Returns true if a point has line of sight to a target
    bool HasLineOfSight(const Object& obj, int8 gun, const Vector3& target, ObjectMask mask) {
        auto gunPosition = GetGunpointWorldPosition(obj, gun);
        // todo: check if segment contains gunpoint. it's possible an adjacent segment contains it instead.
        auto [dir, distance] = GetDirectionAndDistance(target, gunPosition);
        LevelHit hit{};
        RayQuery query{ .MaxDistance = distance, .Start = obj.Segment, .TestTextures = true };

        bool visible = !Game::Intersect.RayLevel({ gunPosition, dir }, query, hit, mask, Game::GetObjectRef(obj).Id);
        Render::Debug::DrawLine(gunPosition, target, visible ? Color(0, 1, 0) : Color(1, 0, 0));
        return visible;
    }

    // Wiggles a robot along its x/y plane
    void WiggleRobot(const Object& robot, AIRuntime& ai, float time) {
        if (ai.WiggleTime > 0) return; // Don't wiggle if already doing so
        // dir is a random vector on the xy/plane of the robot
        Vector3 dir(RandomN11(), RandomN11(), 0);
        dir.Normalize();
        ai.DodgeDirection = Vector3::Transform(dir * 0.5f, robot.Rotation);
        ai.WiggleTime = time;
    }


    // Tries to circle strafe the target.
    // Checks level geometry. Returns false if strafing isn't possible.
    void CircleStrafe(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float dt) {
        ai.StrafeTime -= dt;

        if (!ai.Target)
            ai.StrafeTime = 0;

        if (ai.StrafeTime <= 0)
            return;

        auto transform = Matrix::CreateFromAxisAngle(robot.Rotation.Forward(), ai.StrafeAngle);
        auto dir = Vector3::Transform(robot.Rotation.Right(), transform);
        robot.Physics.Thrust += dir * Difficulty(robotInfo).Speed;
    }

    void TryStartCircleStrafe(const Object& robot, AIRuntime& ai, float time) {
        if (ai.StrafeTime > 0) return;

        ai.StrafeAngle = Random() * DirectX::XM_2PI;

        // Check if the new direction intersects level
        LevelHit hit{};
        RayQuery query{ .MaxDistance = 20, .Start = robot.Segment };

        auto transform = Matrix::CreateFromAxisAngle(robot.Rotation.Forward(), ai.StrafeAngle);
        auto dir = Vector3::Transform(robot.Rotation.Right(), transform);
        Ray ray(robot.Position, dir);
        if (Game::Intersect.RayLevel(ray, query, hit))
            return; // Try again

        ai.StrafeTime = time;
    }

    void UpdateRangedAI(const Object& robot, const RobotInfo& robotInfo, AIRuntime& ai, float dt) {
        if (robotInfo.WeaponType2 != WeaponID::None && ai.FireDelay2 < 0) {
            if (!HasLineOfSight(robot, 0, *ai.Target, ObjectMask::Robot)) {
                //WiggleRobot(robot, ai, 0.5f);
                TryStartCircleStrafe(robot, ai, 2);
                return;
            }

            // Secondary weapons have no animations or wind up
            FireRobotWeapon(robot, ai, robotInfo, *ai.Target, false);
            ai.FireDelay2 = Difficulty(robotInfo).FireDelay2;
        }
        else {
            if (ai.AnimationState != AnimState::Fire && !ai.PlayingAnimation()) {
                PlayRobotAnimation(robot, AnimState::Alert, 1.0f);
            }

            auto& weapon = Resources::GetWeapon(robotInfo.WeaponType);

            if (ai.AnimationState != AnimState::Fire && ai.FireDelay < 0.25f) {
                // Can fire a weapon soon, try to do so.
                // But only fire if there is nothing blocking LOS to the target
                if (!HasLineOfSight(robot, ai.GunIndex, *ai.Target, ObjectMask::Robot)) {
                    //WiggleRobot(robot, ai, 0.5f);
                    TryStartCircleStrafe(robot, ai, 2);
                    CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                    ai.FireDelay = 0.25f + 1 / 8.0f; // Try again in 1/8th of a second
                    return;
                }

                //ai.DodgeTime = 0; // Stop dodging when firing (hack used to stop wiggle, use different timer?)

                auto aimDir = *ai.Target - robot.Position;
                aimDir.Normalize();
                float aimAssist = GetAimAssistAngle(weapon);
                if (AngleBetweenVectors(aimDir, robot.Rotation.Forward()) <= aimAssist) {
                    // Target is within the cone of the weapon, start firing
                    PlayRobotAnimation(robot, AnimState::Fire, ai.FireDelay * 0.8f);
                }
            }
            else if (ai.AnimationState == AnimState::Fire && weapon.Extended.Chargable) {
                WeaponChargeBehavior(robot, ai, robotInfo, dt); // Charge up during fire animation
            }
            else if (ai.FireDelay <= 0 && !ai.PlayingAnimation()) {
                // Check that the target hasn't gone out of LOS when using explosive weapons. as
                // Robots can easily blow themselves up in this case.
                if (weapon.SplashRadius > 0 && !HasLineOfSight(robot, ai.GunIndex, *ai.Target, ObjectMask::None)) {
                    CycleGunpoint(robot, ai, robotInfo); // Cycle gun in case a different one isn't blocked
                    //WiggleRobot(robot, ai, 0.5f);
                    return;
                }

                // Fire animation finished, release a projectile
                FireRobotPrimary(robot, ai, robotInfo, *ai.Target);
            }
        }
    }

    void UpdateMeleeAI(const Object& robot, const RobotInfo& robotInfo, AIRuntime& ai, float dist,
                       Object& player, const Vector3& playerDir, float dt) {
        constexpr float MELEE_RANGE = 10; // how close to actually deal damage
        constexpr float MELEE_SWING_TIME = 0.175f;
        constexpr float BACKSWING_TIME = 0.45f;
        constexpr float BACKSWING_RANGE = MELEE_RANGE * 3; // When to prepare a swing
        constexpr float MELEE_GIVE_UP = 2.0f;

        //PlayRobotAnimation(robot, AnimState::Alert, 1.0f);
        if (ai.ChargingWeapon)
            ai.WeaponCharge += dt; // Raising arms to swing counts as "charging"

        if (!ai.PlayingAnimation()) {
            if (ai.ChargingWeapon) {
                if (ai.AnimationState == AnimState::Fire) {
                    // Arms are raised
                    if (dist < robot.Radius + MELEE_RANGE) {
                        // Player moved close enough, swing
                        PlayRobotAnimation(robot, AnimState::Recoil, MELEE_SWING_TIME);
                        ai.MeleeHitDelay = MELEE_SWING_TIME / 2;
                    }
                    else if (ai.WeaponCharge > MELEE_GIVE_UP) {
                        // Player moved out of range for too long, give up
                        PlayRobotAnimation(robot, AnimState::Alert, BACKSWING_TIME);
                        ai.ChargingWeapon = false;
                        ai.FireDelay = Difficulty(robotInfo).FireDelay;
                    }
                }
            }
            else {
                PlayRobotAnimation(robot, AnimState::Alert, 0.5f);
            }
        }

        if (ai.AnimationState == AnimState::Recoil) {
            if (ai.ChargingWeapon && ai.MeleeHitDelay <= 0) {
                ai.ChargingWeapon = false;
                // todo: multishot can swing multiple times instead of using full fire delay
                ai.FireDelay = Difficulty(robotInfo).FireDelay;

                // check that object is in front?
                // damage objects in a cone?
                if (dist < robot.Radius + MELEE_RANGE) {
                    // Still in range
                    auto soundId = Game::Level.IsDescent1() ? (RandomInt(1) ? SoundID::TearD1_01 : SoundID::TearD1_02) : SoundID::TearD1_01;
                    auto id = Game::GetObjectRef(robot);
                    Sound3D sound({ soundId }, id);
                    sound.Position = robot.Position;
                    Sound::Play(sound);
                    Game::Player.ApplyDamage(Difficulty(robotInfo).MeleeDamage, false);

                    player.Physics.Velocity += playerDir * 20; // shove the player backwards

                    if (auto sparks = Render::EffectLibrary.GetSparks("melee hit")) {
                        auto position = robot.Position + playerDir * robot.Radius;
                        Render::AddSparkEmitter(*sparks, robot.Segment, position);

                        Render::DynamicLight light{};
                        light.LightColor = sparks->Color;
                        light.Radius = 15;
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
        }
    }

    void UpdateRobotAI(Object& robot, float dt) {
        auto& ai = GetAI(robot);
        auto& robotInfo = Resources::GetRobotInfo(robot.ID);
        auto& player = Game::GetPlayerObject();

        // Reset thrust accumulation
        robot.Physics.Thrust = Vector3::Zero;
        robot.Physics.AngularThrust = Vector3::Zero;

        auto decr = [&dt](float& value) {
            value -= dt;
            if (value < 0) value = 0;
        };

        decr(ai.FireDelay);
        decr(ai.FireDelay2);
        decr(ai.RemainingSlow);
        decr(ai.RemainingStun);
        decr(ai.DodgeDelay);
        decr(ai.DodgeTime);
        decr(ai.MeleeHitDelay);
        decr(ai.PathDelay);
        ai.LastSeenPlayer += dt;

        //if (HasFlag(robot.Flags, ObjectFlag::Exploding)) {
        //    if (ai.DyingTimer == -1) ai.DyingTimer = 0;
        //    ai.DyingTimer += dt;
        //}

        if (robotInfo.IsBoss)
            if (!Game::UpdateBoss(robot, dt))
                return; // UpdateBoss returns false when dying

        if (robot.HitPoints <= 0 && robotInfo.DeathRoll > 0) {
            ai.DeathRollTimer += dt;
            auto duration = (float)std::min(robotInfo.DeathRoll / 2 + 1, 6);
            auto volume = robotInfo.IsBoss ? 2 : robotInfo.DeathRoll / 4.0f;
            bool explode = DeathRoll(robot, duration, ai.DeathRollTimer, robotInfo.DeathRollSound,
                                     ai.DyingSoundPlaying, volume, dt);

            if (explode) {
                ExplodeObject(robot);

                // explode object, create sound
                if (Game::LevelNumber < 0) {
                    // todo: respawn thief on secret levels
                }
            }
            return; // Can't act while dying
        }

        if (Settings::Cheats.DisableAI) return;

        if (ai.Awareness <= 0) {
            ai.Target = {}; // Clear target if robot loses interest.
            ai.KnownPlayerSegment = SegID::None;
        }

        //PlayRobotAnimation(robot, AnimState::Fire);
        AnimateRobot(robot, ai, dt);

        if (ai.Target) {
            //TurnTowardsVector(robot, playerDir, Difficulty(robotInfo).TurnTime / 2);
            float turnTime = 1 / Difficulty(robotInfo).TurnTime / 8;
            RotateTowards(robot, *ai.Target, turnTime);
            CircleStrafe(robot, ai, robotInfo, dt);
        }

        if (robot.NextThinkTime == NEVER_THINK || robot.NextThinkTime > Game::Time)
            return;

        if (ai.LastSeenPlayer > Difficulty(robotInfo).FireDelay)
            ai.BurstShots = 0; // Reset burst fire if player hasn't been seen recently

        if (ai.RemainingStun > 0)
            return; // Can't act while stunned

        CheckProjectiles(Game::Level, robot, ai, robotInfo);

        if (ai.DodgeTime > 0 || ai.WiggleTime > 0) {
            robot.Physics.Thrust += ai.DodgeDirection * Difficulty(robotInfo).EvadeSpeed * 32;
        }

        if (ai.GoalSegment != SegID::None) {
            // goal pathing takes priority over other behaviors
            PathTowardsGoal(Game::Level, robot, ai, dt);

            if (CanSeePlayer(robot, robotInfo))
                ai.ClearPath(); // Stop pathing if robot sees the player
        }
        else if (ai.Awareness >= AI_COMBAT_AWARENESS) {
            // in combat

            // this causes the robot to pursue the player if out of sight as well
            MoveToCircleDistance(Game::Level, player, robot, ai, robotInfo);

            auto [playerDir, dist] = GetDirectionAndDistance(player.Position, robot.Position);
            if (CanSeeObject(robot, playerDir, dist, ai)) {
                ai.Target = player.Position;
                ai.KnownPlayerSegment = player.Segment;
            }
            else {
                DecayAwareness(ai);
            }

            // Prevent attacking during phasing (matcens and teleports)
            if (ai.Target && !robot.IsPhasing()) {
                if (robotInfo.Attack == AttackType::Ranged) {
                    UpdateRangedAI(robot, robotInfo, ai, dt);
                }
                else if (robotInfo.Attack == AttackType::Melee) {
                    UpdateMeleeAI(robot, robotInfo, ai, dist, player, playerDir, dt);
                }
            }
        }
        else {
            if (CanSeePlayer(robot, robotInfo)) { }
            else {
                // Nothing nearby, sleep for longer
                DecayAwareness(ai);
                robot.NextThinkTime = Game::Time + Game::TICK_RATE * 16;
            }
        }

        if (ai.Awareness > 1) ai.Awareness = 1;
        ClampThrust(robot, ai);
        ai.LastUpdate = Game::Time;
    }

    void UpdateAI(Object& obj, float dt) {
        if (obj.Type == ObjectType::Robot) {
            Debug::ActiveRobots++;
            UpdateRobotAI(obj, dt);
        }
        else if (obj.Type == ObjectType::Reactor) {
            Game::UpdateReactorAI(obj, dt);
        }
    }
}
