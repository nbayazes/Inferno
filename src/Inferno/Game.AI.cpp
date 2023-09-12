#include "pch.h"

#include "Types.h"
#include "Game.AI.h"
#include "Game.h"
#include "Game.Object.h"
#include "Resources.h"
#include "Physics.h"
#include "logging.h"
#include "Physics.Math.h"
#include "SoundSystem.h"
#include "Editor/Editor.Selection.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.Particles.h"

namespace Inferno {
    List<AIRuntime> AI;
    constexpr float AI_PATH_DELAY = 5; // Default delay for trying to path to the player
    constexpr float AI_DODGE_TIME = 0.5f; // Time to dodge a projectile. Should probably scale based on mass.
    constexpr float AI_COMBAT_AWARENESS = 0.6f; // Robot is engaged in combat
    constexpr float AI_MAX_DODGE_DISTANCE = 60; // Range at which projectiles are dodged

    void ResetAI() {
        for (auto& ai : AI)
            ai = {};
    }

    void ResizeAI(size_t size) {
        AI.resize(size);
    }

    AIRuntime& GetAI(const Object& obj) {
        assert(obj.IsRobot());
        return AI[(int)Game::GetObjectID(obj)];
    }

    constexpr float AWARENESS_INVESTIGATE = 0.5f; // when a robot exceeds this threshold it will investigate the point of interest
    constexpr float MAX_SLOW_TIME = 2.0f; // Max duration of slow
    constexpr float MAX_SLOW_EFFECT = 0.9f; // Max percentage of slow to apply to a robot
    constexpr float MAX_SLOW_THRESHOLD = 0.4f; // Percentage of life dealt to reach max slow

    constexpr float STUN_THRESHOLD = 27.5; // Minimum damage to stun a robot. Concussion is 30 damage.
    constexpr float MAX_STUN_PERCENT = 0.6f; // Percentage of life required in one hit to reach max stun time
    constexpr float MAX_STUN_TIME = 1.5f; // max stun in seconds
    constexpr float MIN_STUN_TIME = 0.25f; // min stun in seconds

    const RobotDifficultyInfo& Difficulty(const RobotInfo& info) {
        return info.Difficulty[Game::Difficulty];
    }

    bool CanSeePlayer(const Object& obj, const Vector3& playerDir, float playerDist) {
        if (Game::Player.HasPowerup(PowerupFlag::Cloak)) return false; // Can't see cloaked player

        LevelHit hit{};
        Ray ray = { obj.Position, playerDir };
        RayQuery query{ .MaxDistance = playerDist, .Start = obj.Segment, .PassTransparent = true };
        return !IntersectRayLevel(Game::Level, ray, query, hit);
    }

    void AddAwareness(AIRuntime& ai, float awareness) {
        ai.Awareness += awareness;
        if (ai.Awareness > 1) ai.Awareness = 1;
    }

    void AlertEnemiesInRoom(Level& level, const Room& room, SegID soundSeg, const Vector3& position, float soundRadius, float awareness) {
        for (auto& segId : room.Segments) {
            auto& seg = level.GetSegment(segId);
            for (auto& objId : seg.Objects) {
                if (auto obj = level.TryGetObject(objId)) {
                    if (!obj->IsRobot()) continue;

                    auto dist = Vector3::Distance(obj->Position, position);
                    if (dist > soundRadius) return;

                    //auto falloff = std::clamp(std::lerp(awareness, 0.0f, (soundRadius - dist) / soundRadiusSq), 0.0f, 1.0f);
                    auto falloff = std::powf(1 - dist / soundRadius, 2); // inverse falloff 
                    //auto falloff = Saturate(InvLerp(soundRadius, 0, dist));
                    auto& ai = GetAI(*obj);

                    auto prevAwareness = ai.Awareness;
                    ai.Awareness += awareness * falloff;
                    //SPDLOG_INFO("Alerted enemy {} by {} from sound", obj->Signature, awareness * falloff);

                    if (prevAwareness < AWARENESS_INVESTIGATE && ai.Awareness > AWARENESS_INVESTIGATE) {
                        SPDLOG_INFO("Enemy {}:{} investigating sound at {}, {}, {}!", objId, obj->Signature, position.x, position.y, position.z);

                        auto& robotInfo = Resources::GetRobotInfo(*obj);
                        auto path = Game::Navigation.NavigateTo(obj->Segment, soundSeg, !robotInfo.IsThief, Game::Level);
                        ai.PathDelay = AI_PATH_DELAY;
                        ai.GoalSegment = soundSeg;
                        ai.GoalPosition = position;
                        ai.GoalRoom = level.FindRoomBySegment(soundSeg);
                        obj->NextThinkTime = 0;
                        obj->GoalPath = path;
                        obj->GoalPathIndex = 0;
                    }
                }
            }
        }
    }

    bool SoundPassesThroughSide(Level& level, const SegmentSide& side) {
        auto wall = level.TryGetWall(side.Wall);
        if (!wall) return true; // open side
        if (!wall->IsSolid()) return true; // wall is destroyed or open

        // Check if the textures are transparent
        auto& tmap1 = Resources::GetTextureInfo(side.TMap);
        bool transparent = tmap1.Transparent;

        if (side.HasOverlay()) {
            auto& tmap2 = Resources::GetTextureInfo(side.TMap2);
            transparent |= tmap2.SuperTransparent;
        }

        return transparent;
    }

    // adds awareness to robots in nearby rooms
    void AlertEnemiesOfNoise(const Object& source, float soundRadius, float awareness) {
        auto& level = Game::Level;
        auto room = level.GetRoomID(source);
        if (room == RoomID::None) return;

        auto action = [&](const Room& r) {
            AlertEnemiesInRoom(level, r, source.Segment, source.Position, soundRadius, awareness);
        };

        Game::TraverseRoomsByDistance(level, room, source.Position, soundRadius, action);
    }

    void PlayAlertSound(const Object& obj, const RobotInfo& robot) {
        auto id = Game::GetObjectRef(obj);
        Sound3D sound(id);
        sound.AttachToSource = true;
        sound.Resource = Resources::GetSoundResource(robot.SeeSound);
        Sound::Play(sound);
    }

    bool InRobotFOV(const Object& robot, const Vector3& pointDir, const RobotInfo& robotInfo) {
        auto dot = robot.Rotation.Forward().Dot(pointDir);
        auto& diff = robotInfo.Difficulty[Game::Difficulty];
        return dot >= diff.FieldOfView;
    }

    bool CheckPlayerVisibility(const Object& robot, const RobotInfo& robotInfo) {
        auto& player = Game::Level.Objects[0];
        auto [playerDir, dist] = GetDirectionAndDistance(player.Position, robot.Position);
        if (!CanSeePlayer(robot, playerDir, dist)) return false;

        //auto angle = AngleBetweenVectors(playerDir, obj.Rotation.Forward());
        if (!InRobotFOV(robot, playerDir, robotInfo))
            return false;

        auto& ai = GetAI(robot);
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

    //SegID GetGoalPathRoom(const AIRuntime& ai) {
    //    if (ai.GoalPath.empty()) return SegID::None;
    //    return *ai.GoalPath.end();
    //}

    bool PathIsValid(Object& obj, const AIRuntime& ai) {
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

    // Similar to TurnTowardsVector but adds angular thrust
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

    Tag GetNextConnection(span<SegID> path, Level& level, SegID segId) {
        if (segId == SegID::None) return {};

        for (int i = 0; i < path.size() - 1; i++) {
            if (path[i] == segId) {
                auto& seg = level.GetSegment(segId);

                // Find the connection to the next segment in the path
                for (auto& sideId : SideIDs) {
                    auto connId = seg.GetConnection(sideId);
                    if (connId == path[i + 1]) {
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

    // Returns true if the ray is within the radius of a face edge. Intended for edge avoidance.
    bool CheckLevelEdges(Level& level, const Ray& ray, span<SegID> segments, float radius) {
        for (auto& segId : segments) {
            auto seg = level.TryGetSegment(segId);
            if (!seg) continue;

            for (auto& side : SideIDs) {
                if (!seg->SideIsSolid(side, level)) continue;
                auto face = Face::FromSide(level, *seg, side);
                if (face.AverageNormal().Dot(ray.direction) > 0)
                    continue; // don't hit test faces pointing away

                auto planeNormal = face.AverageNormal();
                auto planeOrigin = face.Center();
                auto length = planeNormal.Dot(ray.position - planeOrigin) / planeNormal.Dot(-ray.direction);
                if (std::isinf(length)) continue;
                auto point = ray.position + ray.direction * length;

                //auto maybePoint = ProjectRayOntoPlane(ray, face.Center(), face.AverageNormal());
                //if (!maybePoint) continue;
                //auto& point = *maybePoint;
                auto edge = face.GetClosestEdge(point);
                auto closest = ClosestPointOnLine(face[edge], face[edge + 1], point);

                if (Vector3::Distance(closest, point) < radius) {
                    //Render::Debug::DrawPoint(point, Color(1, 0, 0));
                    //Render::Debug::DrawPoint(closest, Color(1, 0, 0));
                    //Render::Debug::DrawLine(closest, point, Color(1, 0, 0));
                    return true;
                }
            }
        }

        return false;
    }

    // Returns the tag of the 'parallel' side of the adjacent side to an edge
    Tag GetConnectedAdjacentSide(Level& level, Tag tag, int edge) {
        if (!level.SegmentExists(tag)) return {};
        auto& seg = level.GetSegment(tag);
        auto indices = seg.GetVertexIndicesRef(tag.Side);
        PointID edgeIndices[] = { *indices[edge], *indices[(edge + 1) % 4] };

        auto adjacent = GetAdjacentSide(tag.Side, edge);
        auto connSide = level.GetConnectedSide({ tag.Segment, adjacent });
        if (!connSide) return {};
        auto& connSeg = level.GetSegment(connSide.Segment);

        for (auto& sideId : SideIDs) {
            auto otherIndices = connSeg.GetVertexIndicesRef(sideId);
            int matches = 0;

            for (auto& i : edgeIndices) {
                for (auto& other : otherIndices) {
                    if (i == *other) matches++;
                }
            }

            if (matches == 2)
                return { connSide.Segment, sideId };
        }

        return {};
    }


    void AvoidSideEdges(Level& level, const Ray& ray, Segment& seg, SideID sideId, const Object& obj, float /*thrust*/, Vector3& target) {
        if (!seg.SideIsSolid(sideId, level)) return;

        // project ray onto side
        auto& side = seg.GetSide(sideId);
        if (side.AverageNormal.Dot(ray.direction) >= 0) return; // ignore sides pointing away
        auto offset = side.AverageNormal * obj.Radius;
        offset = Vector3::Zero;
        auto point = ProjectRayOntoPlane(ray, side.Center + offset, side.AverageNormal);
        if (!point) return;
        auto dist = Vector3::Distance(*point, obj.Position);
        if (dist > 20) return;

        auto pointDir = *point - obj.Position;
        pointDir.Normalize();
        if (pointDir.Dot(ray.direction) <= 0)
            return; // facing away (why did the above not catch it?)

        auto face = Face::FromSide(level, seg, sideId);

        // check point vs each edge
        for (int edge = 0; edge < 4; edge++) {
            // check if edge's adjacent side is closed
            //auto adjacent = GetAdjacentSide(tag.Side, edge);
            //if (!seg.SideIsSolid(adjacent, level)) continue;
            //auto adjacentFace = Face::FromSide(level, seg, adjacent);


            // check distance
            auto edgePoint = ClosestPointOnLine(face[edge] + offset, face[edge + 1] + offset, *point);
            if (Vector3::Distance(edgePoint, *point) < obj.Radius) {
                //auto edges = Editor::FindSharedEdges(level, tag, { tag.Segment, adjacent });
                //auto vec = face.GetEdgeMidpoint(edge) - face.Center();
                auto adjacent = GetAdjacentSide(sideId, edge);
                Vector3 vec;
                auto edgeMidpoint = face.GetEdgeMidpoint(edge);

                if (!seg.SideIsSolid(adjacent, level)) {
                    // if adjacent side isn't solid, shift goal point forward into next segment
                    auto adjacentFace = Face::FromSide(level, seg, adjacent);
                    vec = adjacentFace.Center() - face.Center();
                }
                else {
                    vec = edgeMidpoint - face.Center();
                }

                vec.Normalize();

                //auto vec2 = face.Center() + face.AverageNormal()
                //vec += face.Center() - adjacentFace.Center();
                //vec.Normalize();
                //auto target = adjacentFace.Center() + vec * 2;
                target += edgeMidpoint + vec * 25;
                target /= 2;
                //auto target = edgeMidpoint + vec * 20;
                Render::Debug::DrawLine(edgeMidpoint + vec * 20, edgeMidpoint, Color(1, 0, 1));
                Render::Debug::DrawPoint(target, Color(1, 0, 1));
                Render::Debug::DrawPoint(side.Center, Color(1, 0, 1));
                //MoveTowardsPoint(obj, target, thrust);

                // avoid this edge
                return;
            }
        }
    }

    //bool AvoidConnectionEdges(Level& level, const Ray& ray, Tag tag, Object& obj, float thrust) {
    //    auto& seg = level.GetSegment(tag);
    //    // project ray onto side
    //    auto& side = seg.GetSide(tag.Side);
    //    auto point = ProjectRayOntoPlane(ray, side.Center, side.AverageNormal);
    //    if (!point) return false;
    //    auto face = Face::FromSide(level, seg, tag.Side);

    //    // check point vs each edge
    //    for (int edge = 0; edge < 4; edge++) {
    //        // check if edge's adjacent side is closed
    //        // todo: this can fail if there is a connection with a solid adjacent side
    //        auto adjacent = GetAdjacentSide(tag.Side, edge);
    //        if (!seg.SideIsSolid(adjacent, level)) continue;
    //        auto adjacentFace = Face::FromSide(level, seg, adjacent);
    //        

    //        // check distance
    //        auto edgePoint = ClosestPointOnLine(face[edge], face[edge + 1], *point);
    //        if (Vector3::Distance(edgePoint, *point) < obj.Radius * 1.5f) {
    //            //auto edges = Editor::FindSharedEdges(level, tag, { tag.Segment, adjacent });
    //            auto vec = face.GetEdgeMidpoint(edge) - adjacentFace.Center();
    //            //auto vec2 = face.Center() + face.AverageNormal()
    //            vec += face.Center() - adjacentFace.Center();
    //            //vec.Normalize();
    //            auto target = adjacentFace.Center() + vec * 2;
    //            Render::Debug::DrawLine(target, adjacentFace.Center(), Color(1, 0, 1));
    //            MoveTowardsPoint(obj, target, thrust);

    //            // avoid this edge
    //            return true;
    //        }
    //    }

    //    return false;
    //}

    //void AvoidConnectionEdges(Level& level, const Ray& ray, int length, Object& obj, float thrust) {
    //    auto index = Seq::indexOf(obj.GoalPath, obj.Segment);
    //    if (!index) return;

    //    auto startConn = GetNextConnection(obj.GoalPath, level, obj.Segment);
    //    auto& startSide = level.GetSide(startConn);

    //    //auto room = Game::Rooms.GetRoom(obj.Room);
    //    //for (auto& segId : room) { }

    //    for (int i = 0; i < length; i++) {
    //        if (i >= obj.GoalPath.size()) return;

    //        auto conn = GetNextConnection(obj.GoalPath, level, obj.GoalPath[*index + i]);

    //        bool avoided = AvoidConnectionEdges(level, ray, conn, obj, thrust);

    //        if (!avoided) {
    //            // check the adjacent side connections as well
    //            for (int edge = 0; edge < 4; edge++) {
    //                if (auto adj = GetConnectedAdjacentSide(level, conn, edge)) {
    //                    if (AvoidConnectionEdges(level, ray, adj, obj, thrust)) {
    //                        avoided = true;
    //                        break;
    //                    }
    //                }
    //            }
    //        }

    //        //if (avoided) {
    //        //    Render::Debug::DrawLine(obj.Position, startSide.Center, Color(1, 0, 1));
    //        //    MoveTowardsPoint(obj, startSide.Center, thrust);
    //        //    break;
    //        //}
    //    }
    //}

    void AvoidRoomEdges(Level& level, const Ray& ray, const Object& obj, float thrust, Vector3& target) {
        auto room = level.GetRoom(obj);
        if (!room) return;

        for (auto& segId : room->Segments) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                AvoidSideEdges(level, ray, seg, sideId, obj, thrust, target);
            }
        }
    }

    void PathTowardsGoal(Level& level, Object& obj, AIRuntime& ai, float /*dt*/) {
        auto checkGoalReached = [&obj, &ai] {
            if (Vector3::Distance(obj.Position, ai.GoalPosition) <= std::max(obj.Radius, 5.0f)) {
                SPDLOG_INFO("Robot {} reached the goal!", obj.Signature);
                ai.GoalSegment = SegID::None; // Reached the goal!
            }
        };

        if (!PathIsValid(obj, ai)) {
            if (ai.PathDelay > 0) return; // Don't spam trying to path to a goal
            // Calculate a new path
            SPDLOG_INFO("Robot {} updating goal path", obj.Signature);
            auto& robotInfo = Resources::GetRobotInfo(obj);
            obj.GoalPath = Game::Navigation.NavigateTo(obj.Segment, ai.GoalSegment, !robotInfo.IsThief, level);
            ai.PathDelay = AI_PATH_DELAY;
            if (obj.GoalPath.empty()) {
                // Unable to find a valid path, clear the goal and give up
                ai.GoalSegment = SegID::None;
                ai.GoalRoom = RoomID::None;
                return;
            }
        }

        auto& robot = Resources::GetRobotInfo(obj.ID);
        auto thrust = Difficulty(robot).Speed / 8;
        auto turnTime = Difficulty(robot).TurnTime;
        if (turnTime <= 0) turnTime = 1.0f;
        auto angThrust = 1 / turnTime / 8;

        if (ai.GoalSegment == obj.Segment) {
            // Reached the goal segment
            MoveTowardsPoint(obj, ai.GoalPosition, thrust);
            RotateTowards(obj, ai.GoalPosition, angThrust);
            checkGoalReached();
        }
        else {
            auto getPathSeg = [&obj](size_t index) {
                //if (!Seq::inRange(obj.GoalPath, index)) return obj.GoalPath.back();
                if (!Seq::inRange(obj.GoalPath, index)) return SegID::None;
                return obj.GoalPath[index];
            };

            auto pathIndex = Seq::indexOf(obj.GoalPath, obj.Segment);
            if (!pathIndex) {
                SPDLOG_ERROR("Invalid path index for obj {}", obj.Signature);
            }

            //auto next1 =  GetNextPathSegment(obj.GoalPath, obj.Segment);
            //auto next2 = GetNextPathSegment(obj.GoalPath, next1);
            auto next1 = getPathSeg(*pathIndex + 1);
            auto next2 = getPathSeg(*pathIndex + 2);
            auto next3 = getPathSeg(*pathIndex + 3);

            SegID segs[] = { obj.Segment, next1, next2, next3 };

            auto nextSideTag = GetNextConnection(obj.GoalPath, level, obj.Segment);
            auto& nextSide = level.GetSide(nextSideTag);
            Vector3 targetPosition = nextSide.Center; // default to the next side


            int desiredIndex = 0;

            Vector3 desiredPosition;
            for (int i = (int)std::size(segs) - 1; i > 0; i--) {
                if (auto nextSeg = level.TryGetSegment(segs[i])) {
                    desiredIndex = i;
                    desiredPosition = nextSeg->Center;
                    break;
                }
            }

            auto findVisibleTarget = [&] {
                auto [dir, maxDist] = GetDirectionAndDistance(desiredPosition, obj.Position);
                Ray ray(obj.Position, dir);

                // Try pathing directly across multiple segments
                for (int i = 0; i < std::size(segs); i++) {
                    if (auto nextSeg = level.TryGetSegment(segs[i])) {
                        if (i == 0) {
                            // check the surrounding segments of the start location
                            for (auto& conn : nextSeg->Connections) {
                                if (IntersectRaySegment(level, ray, conn, maxDist, false, false, nullptr)) {
                                    //Render::Debug::DrawLine(obj.Position, desiredPosition, Color(1, 0, 0));
                                    //return; // wall in the way, don't try going any further

                                    // try a shorter path
                                    while (desiredIndex > 1) {
                                        desiredIndex--;
                                        if (!IntersectRaySegment(level, ray, segs[desiredIndex], maxDist, false, false, nullptr)) {
                                            nextSeg = level.TryGetSegment(segs[desiredIndex]);
                                            targetPosition = nextSeg->Center;
                                            return;
                                        }
                                    }

                                    if (desiredIndex == 0)
                                        return; // wall in the way, don't try going any further
                                }
                            }
                        }

                        if (IntersectRaySegment(level, ray, segs[i], maxDist, false, false, nullptr)) {
                            //Render::Debug::DrawLine(obj.Position, desiredPosition, Color(1, 0, 0));
                            break; // wall in the way, don't try going any further
                        }

                        if (i > 0)
                            targetPosition = nextSeg->Center;
                    }
                }
            };

            findVisibleTarget();


            //if (next2 == SegID::None) {
            //    // Target segment is adjacent, try pathing directly to it.
            //    auto [dir, maxDist] = GetDirectionAndDistance(ai.GoalPosition, obj.Position);
            //    Ray ray(obj.Position, dir);
            //    if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //        targetPosition = ai.GoalPosition;

            //    checkGoalReached(); // it's possible for the final seg to be small, so check completion if we're adjacent to it
            //}

            //auto [dir, maxDist] = GetDirectionAndDistance(side2Center, obj.Position);
            //Ray ray(obj.Position, dir);
            //if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //    targetPosition = side2Center;

            //if (next2 == SegID::None) {
            //    // Target segment is adjacent, try pathing directly to it.
            //    auto [dir, maxDist] = GetDirectionAndDistance(ai.GoalPosition, obj.Position);
            //    Ray ray(obj.Position, dir);
            //    if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //        targetPosition = ai.GoalPosition;

            //    checkGoalReached(); // it's possible for the final seg to be small, so check completion if we're adjacent to it
            //}
            //else {
            //    // Try pathing directly across multiple segments
            //    if (auto nextSideTag2 = GetNextConnection(obj.GoalPath, level, next1)) {
            //        // nothing between current seg and target so use a direct path
            //        auto side2Center = level.GetSide(nextSideTag2).Center;

            //        auto [dir, maxDist] = GetDirectionAndDistance(side2Center, obj.Position);
            //        Ray ray(obj.Position, dir);
            //        if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //            targetPosition = side2Center;
            //    }
            //}

            // Check for edge collisions and dodge
            //auto [dir, maxDist] = GetDirectionAndDistance(targetPosition, obj.Position);
            //Ray ray(obj.Position, dir);
            //AvoidConnectionEdges(level, ray, desiredIndex, obj, thrust);
            //Render::Debug::DrawLine(ray.position, ray.position + ray.direction * 20, Color(1, .5f, 0));
            //AvoidRoomEdges(level, ray, obj, thrust, targetPosition);

            Render::Debug::DrawLine(obj.Position, targetPosition, Color(0, 1, 0));

            //auto& seg1 = Game::Level.GetSegment(next1);
            //auto& seg2 = Game::Level.GetSegment(next2);
            targetPosition = (targetPosition * 2 + nextSide.Center) / 3;
            MoveTowardsPoint(obj, targetPosition, thrust);
            RotateTowards(obj, targetPosition, angThrust);


            //if (CheckLevelEdges(level, ray, segs, obj.Radius)) {
            //    // MoveTowardsPoint(obj, nextSide.Center, thrust);
            //    Render::Debug::DrawLine(obj.Position, nextSide.Center, Color(1, 0, 0));
            //}
        }
    }

    //void PathTowardsGoal(Level& level, Object& obj, float /*dt*/) {
    //    if (obj.GoalPath.empty()) return;
    //    if (!Seq::inRange(obj.GoalPath, obj.GoalPathIndex)) return;
    //    //auto& ai = obj.Control.AI.ail;

    //    auto& robot = Resources::GetRobotInfo(obj.ID);
    //    auto thrust = Difficulty(robot).MaxSpeed / 8;
    //    auto angularThrust = 1 / Difficulty(robot).TurnTime / 8;
    //    const auto& nextPoint = obj.GoalPath[obj.GoalPathIndex];

    //    MoveTowardsPoint(obj, nextPoint, thrust);
    //    RotateTowards(obj, nextPoint, angularThrust);

    //    if (Vector3::DistanceSquared(obj.Position, nextPoint) < 5 * 5.0f) {
    //        // got close to node, move to next
    //        obj.GoalPathIndex++;
    //    }

    //    if (obj.GoalPathIndex >= obj.GoalPath.size()) {
    //        // reached the end
    //        obj.GoalPath.clear();
    //        obj.GoalPathIndex = -1;
    //        SPDLOG_INFO("Robot {} reached the goal!", obj.Signature);
    //    }
    //}

    struct AiExtended {
        float AwarenessDecay = 0.2f; // Awareness decay per second
        float Fear = 0.2f; // Taking damage increases flee state
        float Curiosity = 0.2f; // How much awareness from noise / likeliness to investigate
    };

    AiExtended DefaultAi{};

    void FireWeaponAtPoint(const Object& obj, const RobotInfo& robot, uint8 gun, const Vector3& point, WeaponID weapon) {
        // for melee robots...
        // dist_to_player < obj->size + ConsoleObject->size + F1_0 * 2
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

    void FireRobotWeapon(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const AITarget& target, bool primary) {
        if (!primary && robotInfo.WeaponType2 == WeaponID::None) return; // no secondary set

        auto [targetDir, targetDist] = GetDirectionAndDistance(target.Position, robot.Position);
        auto& weapon = Resources::GetWeapon(primary ? robotInfo.WeaponType : robotInfo.WeaponType2);
        auto weaponSpeed = weapon.Speed[Game::Difficulty];

        // only fire if target is within certain angle. for fast require a more precise alignment
        if (primary) {
            ai.GunIndex = robotInfo.Guns > 0 ? (ai.GunIndex + 1) % robotInfo.Guns : 0;
            if (robotInfo.WeaponType2 != WeaponID::None && ai.GunIndex == 0)
                ai.GunIndex = 1; // Reserve gun 0 for secondary weapon if present
        }

        uint8 gunIndex = primary ? ai.GunIndex : 0;
        auto aimTarget = target.Position + LeadTarget(targetDir, targetDist, target, weaponSpeed);
        aimTarget = target.Position;
        auto aimDir = aimTarget - robot.Position;
        aimDir.Normalize();
        float maxAimAngle = weaponSpeed > FAST_WEAPON_SPEED ? 7.5f * DegToRad : 15.0f * DegToRad;

        if (AngleBetweenVectors(aimDir, robot.Rotation.Forward()) > maxAimAngle) {
            aimDir = (aimDir + robot.Rotation.Forward()) / 2.0f;
            aimDir.Normalize();
            if (AngleBetweenVectors(aimDir, robot.Rotation.Forward()) > maxAimAngle) {
                // todo: if robot wants to fire but can't, reset rapidfire if fire delay passes
                return; // couldn't aim to the target close enough
            }
        }

        // todo: fire at target if within facing angle regardless of aim assist

        //aimTarget += RandomVector((5 - Game::Difficulty) * 2); // Randomize aim based on difficulty
        FireWeaponAtPoint(robot, robotInfo, gunIndex, aimTarget, robotInfo.WeaponType);
    }

    void StopPathing(Object& robot, AIRuntime& ai) {
        robot.GoalPath.clear();
        robot.GoalPathIndex = -1;
        ai.GoalSegment = SegID::None;
    }

    void DodgeProjectile(const Object& robot, AIRuntime& ai, const Object& projectile, const RobotInfo& robotInfo) {
        if (projectile.Physics.Velocity.LengthSquared() < 5 * 5) return; // Don't dodge slow projectiles. also prevents crash at 0 velocity.

        auto [projDir, projDist] = GetDirectionAndDistance(projectile.Position, robot.Position);
        // Looks weird to dodge distant projectiles. also they might hit another target
        // Consider increasing this for massive robots?
        if (projDist > AI_MAX_DODGE_DISTANCE) return;
        if (!InRobotFOV(robot, projDir, robotInfo)) return;

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
    void MoveTowardsPlayer(Level& level, const Object& player, Object& robot, AIRuntime& ai) {
        auto playerRoom = level.GetRoomID(player);
        auto robotRoom = level.GetRoomID(robot);
        if (playerRoom == robotRoom) {
            MoveTowardsPoint(robot, player.Position, 100);
        }
        else {
            if (robot.GoalPath.empty() || robot.GoalPath.back() != player.Segment) {
                auto& robotInfo = Resources::GetRobotInfo(robot);
                robot.GoalPath = Game::Navigation.NavigateTo(robot.Segment, ai.GoalSegment, !robotInfo.IsThief, level);
            }

            PathTowardsGoal(level, robot, ai, 0);
        }
    }

    // Moves towards a random segment further away from the player. Prefers room portals.
    void MoveAwayFromPlayer(Level& level, const Object& player, Object& robot) {
        auto playerDir = player.Position - robot.Position;
        playerDir.Normalize();
        Ray ray(robot.Position, -playerDir);
        LevelHit hit;
        RayQuery query{ .MaxDistance = 10, .Start = robot.Segment };
        if (IntersectRayLevel(level, ray, query, hit))
            return; // no room to move backwards

        // todo: try escaping through portals if there are any in the player's FOV
        MoveTowardsPoint(robot, robot.Position - playerDir * 10, 10);
    }

    void MoveToCircleDistance(Level& level, const Object& player, Object& robot, AIRuntime& ai, const RobotInfo& robotInfo) {
        auto circleDistance = Difficulty(robotInfo).CircleDistance;
        auto distOffset = Vector3::Distance(player.Position, robot.Position) - circleDistance;
        if (abs(distOffset) < 20 && circleDistance > 10)
            return; // already close enough. Melee robots should always go to zero.

        if (distOffset > 0)
            MoveTowardsPlayer(level, player, robot, ai);
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
        // todo: fix goal angle not being exactly reached?

        for (int joint = 1; joint < model.Submodels.size(); joint++) {
            auto& curAngle = robot.Render.Model.Angles[joint];
            curAngle += ai.DeltaAngles[joint] / ai.AnimationDuration * dt;
        }
    }

    void DamageRobot(Object& robot, float damage, float stunMult) {
        auto& info = Resources::GetRobotInfo(robot);
        auto& ai = GetAI(robot);

        // Apply slow
        float damageScale = 1 - (info.HitPoints - damage * stunMult) / info.HitPoints; // percentage of life dealt
        float slowTime = std::lerp(0.0f, 1.0f, damageScale / MAX_SLOW_THRESHOLD);
        if (ai.RemainingSlow > 0) slowTime += ai.RemainingSlow;
        ai.RemainingSlow = std::clamp(slowTime, 0.1f, MAX_SLOW_TIME);

        // Apply stun
        if (damage * stunMult > STUN_THRESHOLD) {
            float stunTime = damageScale / MAX_STUN_PERCENT * MAX_STUN_TIME;
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

        // todo: boss invulnerability
        if (!Settings::Cheats.DisableWeaponDamage)
            robot.HitPoints -= damage;
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

    void FireRobotPrimary(const Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, const AITarget& target) {
        ai.FireDelay = 0;
        // multishot: consume as many projectiles as possible based on burst count
        // A multishot of 1 and a burst of 3 would fire 2 projectiles then 1 projectile
        // Multishot incurs extra fire delay per projectile
        auto burstDelay = std::min(1 / 8.0f, Difficulty(robotInfo).FireDelay / 2);
        for (int i = 0; i < robotInfo.Multishot; i++) {
            ai.FireDelay += burstDelay;

            FireRobotWeapon(robot, ai, robotInfo, target, true);
            ai.Shots++;
            if (ai.Shots >= Difficulty(robotInfo).ShotCount) {
                ai.Shots = 0;
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

                Sound3D sound(id);
                sound.Resource = Resources::GetSoundResource(SoundID::FusionWarmup);
                sound.Position = robot.Position;
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
            FireRobotPrimary(robot, ai, robotInfo, Game::GetPlayer());
            ai.WeaponCharge = 0;
        }
    }

    void UpdateRobotAI(Object& robot, float dt) {
        auto& ai = GetAI(robot);
        auto& robotInfo = Resources::GetRobotInfo(robot.ID);
        auto& player = Game::GetPlayer();

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

        AnimateRobot(robot, ai, dt);

        if (robot.NextThinkTime == NEVER_THINK || robot.NextThinkTime > Game::Time || Settings::Cheats.DisableAI)
            return;

        if (ai.RemainingStun > 0)
            return; // Can't act while stunned

        CheckProjectiles(Game::Level, robot, ai, robotInfo);

        if (ai.DodgeTime > 0) {
            robot.Physics.Thrust += ai.DodgeDirection * Difficulty(robotInfo).EvadeSpeed * 32;
        }

        if (ai.GoalSegment != SegID::None) {
            // goal path takes priority over other behaviors
            PathTowardsGoal(Game::Level, robot, ai, dt);

            if (CheckPlayerVisibility(robot, robotInfo)) {
                StopPathing(robot, ai); // Stop pathing if robot sees the player
                PlayAlertSound(robot, robotInfo);
            }
        }
        else if (ai.Awareness > AI_COMBAT_AWARENESS) {
            // in combat

            MoveToCircleDistance(Game::Level, player, robot, ai, robotInfo);

            auto [playerDir, dist] = GetDirectionAndDistance(player.Position, robot.Position);
            // todo: rework this to aim at last seen location
            if (CanSeePlayer(robot, playerDir, dist)) {
                //TurnTowardsVector(robot, playerDir, Difficulty(robotInfo).TurnTime / 2);
                float turnTime = 1 / Difficulty(robotInfo).TurnTime / 8;
                RotateTowards(robot, player.Position, turnTime);

                if (!ai.PlayingAnimation()) {
                    PlayRobotAnimation(robot, AnimState::Alert, 1.0f);
                }

                if (robotInfo.Attack == AttackType::Ranged) {
                    if (robotInfo.WeaponType2 != WeaponID::None && ai.FireDelay2 < 0) {
                        FireRobotWeapon(robot, ai, robotInfo, player, false);
                        ai.FireDelay2 = Difficulty(robotInfo).FireDelay2;
                    }


                    if (ai.AnimationState != AnimState::Fire && ai.FireDelay < 0.25f) {
                        PlayRobotAnimation(robot, AnimState::Fire, ai.FireDelay * 0.8f);
                    }

                    auto& weapon = Resources::GetWeapon(robotInfo.WeaponType);

                    if (ai.FireDelay <= 0) {
                        if (weapon.Extended.Chargable) {
                            WeaponChargeBehavior(robot, ai, robotInfo, dt);
                        }
                        else {
                            FireRobotPrimary(robot, ai, robotInfo, player);
                        }
                    }
                }
            }
            else {
                // Lost sight of player, decay awareness based on AI
                DecayAwareness(ai);
                // todo: move towards last known location if curious
            }
        }
        else {
            if (CheckPlayerVisibility(robot, robotInfo)) { }
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
            // check facing, fire weapon from gunpoint
        }
    }

    void UpdateNearbyAI(Level& level, float dt) {
        auto room = level.GetRoom(Game::GetPlayer());

        // todo: also update adjacent rooms
        for (auto& segId : room->Segments) {
            if (!level.SegmentExists(segId)) continue;
            auto& seg = level.GetSegment(segId);
            for (auto& objId : seg.Objects) {
                if (auto obj = level.TryGetObject(objId)) {
                    UpdateAI(*obj, dt);
                }
            }
        }
    }
}
