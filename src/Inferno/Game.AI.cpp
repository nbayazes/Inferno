#include "pch.h"

#include "Types.h"
#include "Game.AI.h"
#include "Game.h"
#include "Resources.h"
#include "Physics.h"
#include "logging.h"

namespace Inferno {
    const RobotDifficultyInfo& Difficulty(const RobotInfo& info) {
        return info.Difficulty[Game::Difficulty];
    }

    Tuple<Vector3, float> GetDirectionAndDistance(const Vector3& target, const Vector3& point) {
        auto dir = target - point;
        float length = dir.Length();
        dir.Normalize();
        return { dir, length };
    }

    bool CanSeePlayer(const Object& obj, const Vector3& playerDir, float playerDist) {
        if (Game::Player.HasPowerup(PowerupFlag::Cloak)) return false; // Can't see cloaked player

        LevelHit hit{};
        Ray ray = { obj.Position, playerDir };
        return !IntersectRayLevel(Game::Level, ray, obj.Segment, playerDist, true, false, hit);
    }

    bool CheckPlayerVisibility(Object& obj, ObjID id, const RobotInfo& robot) {
        auto [playerDir, dist] = GetDirectionAndDistance(obj.Position, obj.Position);
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

        return SegID::None;
    }

    //Array<SegID, 3> GetNextPathSegments(SegID start, span<SegID> path) {
    //    Array<SegID, 3> result = { SegID::None, SegID::None, SegID::None };
    //    if (path.size() < 3) return result;

    //    for (int i = 0; i < path.size() - 2; i++) {
    //        if (path[i] == start) {
    //            result[0] = start;
    //            result[1] = path[i + 1];
    //            result[2] = path[i + 2];
    //            break;
    //        }
    //    }

    //    return result;
    //}

    void MoveTowardsPoint(Object& obj, const Vector3& point, float dt) {
        auto dir = point - obj.Position;
        dir.Normalize();

        auto& robot = Resources::GetRobotInfo(obj.ID);
        TurnTowardsVector(obj, dir, Difficulty(robot).TurnTime);
        obj.Physics.Velocity += dir * Difficulty(robot).MaxSpeed * 2 * dt;
    }

    Tag GetNextConnection(span<SegID> _path, Level& level, SegID segId) {
        for (int i = 0; i < _path.size() - 1; i++) {
            if (_path[i] == segId) {
                auto& seg = level.GetSegment(segId);

                // Find the connection to the next segment in the path
                for (auto& sideId : SideIDs) {
                    auto connId = seg.GetConnection(sideId);
                    if (connId == _path[i + 1]) {
                        return { segId, sideId };
                    }
                }
            }
        }

        return {};
    }

    class SegmentPath {
        List<SegID> _path;

    public:
        Tag GetNextConnection(Level& level, SegID segId) const {
            for (int i = 0; i < _path.size() - 1; i++) {
                if (_path[i] == segId) {
                    auto& seg = level.GetSegment(segId);

                    // Find the connection to the next segment in the path
                    for (auto& sideId : SideIDs) {
                        auto connId = seg.GetConnection(sideId);
                        if (connId == _path[i + 1]) {
                            return { connId, sideId };
                        }
                    }
                }
            }

            return {};
        }

        SegID GetNextPathSegment(SegID current) const {
            for (int i = 0; i < _path.size(); i++) {
                if (_path[i] == current) {
                    if (i + 1 >= _path.size()) break; // already at end
                    return _path[i + 1];
                }
            }

            return current;
        }
    };

    void PathTowardsGoal(Level& level, Object& obj, float dt) {
        auto& ai = obj.Control.AI.ail;
        auto& seg = level.GetSegment(obj.Segment);

        auto checkGoalReached = [&obj, &ai] {
            if (Vector3::Distance(obj.Position, ai.GoalPosition) <= std::max(obj.Radius, 5.0f)) {
                SPDLOG_INFO("Robot {} reached the goal!", obj.Signature);
                ai.GoalSegment = SegID::None; // Reached the goal!
            }
        };

        if (!PathIsValid(obj)) {
            // Calculate a new path
            SPDLOG_INFO("Robot {} updating goal path", obj.Signature);
            obj.GoalPath = Game::Navigation.NavigateTo(obj.Segment, ai.GoalSegment, Game::Rooms, level);
            if (obj.GoalPath.empty()) {
                // Unable to find a valid path, clear the goal and give up
                ai.GoalSegment = SegID::None;
                ai.GoalRoom = RoomID::None;
                return;
            }
        }

        if (ai.GoalSegment == obj.Segment) {
            // Reached the goal segment
            MoveTowardsPoint(obj, ai.GoalPosition, dt);
            checkGoalReached();
        }
        else {
            auto next1 = GetNextPathSegment(obj.GoalPath, obj.Segment);
            auto next2 = GetNextPathSegment(obj.GoalPath, next1);

            auto nextSideTag = GetNextConnection(obj.GoalPath, level, obj.Segment);
            auto& nextSide = level.GetSide(nextSideTag);
            Vector3 targetPosition = nextSide.Center;

            if (next2 == SegID::None) {
                // Target segment is adjacent, try pathing directly to it.
                auto [dir, maxDist] = GetDirectionAndDistance(ai.GoalPosition, obj.Position);
                Ray ray(obj.Position, dir);
                SegID segs[] = { obj.Segment, next1, next2 };
                if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
                    targetPosition = ai.GoalPosition;

                checkGoalReached(); // it's possible for the final seg to be small, so check completion if we're adjacent to it
            }
            else {
                // Try pathing directly across multiple segments
                if (auto nextSideTag2 = GetNextConnection(obj.GoalPath, level, next1)) {
                    // nothing between current seg and target so use a direct path
                    auto side2Center = level.GetSide(nextSideTag2).Center;

                    auto [dir, maxDist] = GetDirectionAndDistance(side2Center, obj.Position);
                    Ray ray(obj.Position, dir);
                    SegID segs[] = { obj.Segment, next1, next2 };
                    if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
                        targetPosition = side2Center;
                }
            }

            //auto& seg1 = Game::Level.GetSegment(next1);
            //auto& seg2 = Game::Level.GetSegment(next2);
            MoveTowardsPoint(obj, targetPosition, dt);
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
