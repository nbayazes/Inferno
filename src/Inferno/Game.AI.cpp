#include "pch.h"

#include "Types.h"
#include "Game.AI.h"
#include "Game.h"
#include "Resources.h"
#include "Physics.h"

namespace Inferno {
    const RobotDifficultyInfo& Difficulty(const RobotInfo& info) {
        return info.Difficulty[Game::Difficulty];
    }


    Tuple<Vector3, float> GetPlayerDirection(const Object& obj) {
        auto& player = Game::Level.Objects[0];
        auto playerDir = player.Position - obj.Position;
        auto dist = playerDir.Length();
        playerDir.Normalize();
        return { playerDir, dist };
    }

    bool CanSeePlayer(const Object& obj, const Vector3& playerDir, float playerDist) {
        if (Game::Player.HasPowerup(PowerupFlag::Cloak)) return false; // Can't see cloaked player

        LevelHit hit{};
        Ray ray = { obj.Position, playerDir };
        return !IntersectRayLevel(Game::Level, ray, obj.Segment, playerDist, true, false, hit);
    }

    bool CheckPlayerVisibility(Object& obj, ObjID id, const RobotInfo& robot) {
        auto [playerDir, dist] = GetPlayerDirection(obj);
        if (!CanSeePlayer(obj, playerDir, dist)) return false;

        //auto angle = AngleBetweenVectors(playerDir, obj.Rotation.Forward());
        auto dot = playerDir.Dot(obj.Rotation.Forward());
        //dot = vm_vec_dot(vec_to_player, &objp->orient.fvec);

        auto& diff = robot.Difficulty[Game::Difficulty];
        if (dot < diff.FieldOfView) return false;

        auto prevAwareness = obj.Control.AI.ail.Awareness;
        obj.Control.AI.ail.Awareness = 1;

        // only play sound when robot was asleep
        if (prevAwareness < 0.3f) {
            Sound3D sound(id);
            sound.AttachToSource = true;
            sound.Resource = Resources::GetSoundResource(robot.SeeSound);
            Sound::Play(sound);
        }

        return true;
    }

    bool SegmentIsAdjacent(const Segment& src, SegID adjacent) {
        for (auto& conn : src.Connections) {
            if (conn == adjacent) return true;
        }
        return false;
    }

    //SegID GetGoalPathRoom(const AIRuntime& ai) {
    //    if (ai.GoalPath.empty()) return SegID::None;
    //    return *ai.GoalPath.end();
    //}

    bool PathIsValid(Object& obj) {
        auto& ai = obj.Control.AI.ail;

        if (obj.GoalPath.empty()) return false;
        if (obj.GoalPath.back() != ai.GoalSegment) return false; // Goal isn't this path anymore
        return Seq::contains(obj.GoalPath, obj.Segment); // Check if robot strayed from path
    }

    SegID GetNextPathSegment(span<SegID> path, SegID current) {
        for (int i = 0; i < path.size(); i++) {
            if (path[i] == current) {
                if (i + 1 >= path.size()) break; // already at end
                return path[i + 1];
            }
        }

        return current;
    }

    void MoveTowardsPoint(Object& obj, const Vector3& point, float dt) {
        auto dir = point - obj.Position;
        dir.Normalize();

        auto& robot = Resources::GetRobotInfo(obj.ID);
        TurnTowardsVector(obj, dir, Difficulty(robot).TurnTime);
        obj.Physics.Velocity += dir * Difficulty(robot).MaxSpeed * 2 * dt;
    }

    void PathTowardsGoal(Level& level, Object& obj, float dt) {
        auto& ai = obj.Control.AI.ail;

        auto& seg = level.GetSegment(obj.Segment);
        if (SegmentIsAdjacent(seg, ai.GoalSegment) || ai.GoalSegment == obj.Segment) {
            // move directly towards goal
            MoveTowardsPoint(obj, ai.GoalPosition, dt);

            if (Vector3::Distance(obj.Position, ai.GoalPosition) <= obj.Radius) {
                SPDLOG_INFO("Robot {} reached the goal!", obj.Signature);
                ai.GoalSegment = SegID::None; // Reached the goal!
            }
        }
        else {
            if (!PathIsValid(obj)) {
                // Calculate a new path
                SPDLOG_INFO("Robot {} updating goal path", obj.Signature);
                obj.GoalPath = Game::Navigation.NavigateTo(obj.Segment, ai.GoalSegment, Game::Rooms, level);
                if (obj.GoalPath.empty()) {
                    // Unable to find a valid path, clear the goal and give up
                    ai.GoalSegment = SegID::None;
                    return;
                }
            }

            auto nextSegId = GetNextPathSegment(obj.GoalPath, obj.Segment);
            auto& nextSeg = level.GetSegment(nextSegId);
            MoveTowardsPoint(obj, nextSeg.Center, dt);
        }
    }

    struct AiExtended {
        float AwarenessDecay = 0.2f; // Awareness decay per second
        float Fear = 0.2f; // Taking damage increases flee state
        float Curiosity = 0.2f; // How much awareness from noise / likeliness to investigate
    };

    AiExtended DefaultAi{};

    void UpdateAI(Object& obj, float dt) {
        if (obj.NextThinkTime == NEVER_THINK || obj.NextThinkTime > Game::Time)
            return;

        // todo: check if robot is in active set of segments (use rooms)

        if (obj.Type == ObjectType::Robot) {
            auto id = ObjID(&obj - Game::Level.Objects.data());

            // check fov

            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto& ai = obj.Control.AI.ail;
            auto& player = Game::Level.Objects[0];

            if (ai.Awareness > 0.5f) {
                // in combat?

                //auto [playerDir, dist] = GetPlayerDirection(obj);
                //if (CanSeePlayer(obj, playerDir, dist)) {
                //    TurnTowardsVector(obj, playerDir, Difficulty(robot).TurnTime);
                //    ai.AimTarget = player.Position;
                //}
                //else {
                //    // Lost sight of player, decay awareness based on AI
                //    auto deltaTime = float(Game::Time - ai.LastUpdate);
                //    ai.Awareness -= DefaultAi.AwarenessDecay * deltaTime;
                //    if (ai.Awareness < 0) ai.Awareness = 0;

                //    // todo: move towards last known location
                //}

                obj.NextThinkTime = Game::Time + Game::TICK_RATE;
            }
            else {
                if (ai.GoalSegment != SegID::None) {
                    PathTowardsGoal(Game::Level, obj, dt);
                }
                else {
                    //if (CheckPlayerVisibility(obj, id, robot)) {
                    //    obj.NextThinkTime = Game::Time + Game::TICK_RATE;
                    //}
                    //else {
                    //    // Nothing nearby
                    //    obj.NextThinkTime = Game::Time + 1.0f;
                    //}
                    obj.NextThinkTime = Game::Time + 1.0f;
                }
            }

            ai.LastUpdate = Game::Time;
        }
        else if (obj.Type == ObjectType::Reactor) {
            // check facing, fire weapon from gunpoint
        }
    }
}
