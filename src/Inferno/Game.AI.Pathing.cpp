#include "pch.h"

#include "Game.AI.Pathing.h"
#include "Game.AI.h"
#include "Game.h"
#include "Game.Object.h"
#include "Graphics.Debug.h"
#include "Resources.h"
#include "Robot.h"
#include "Settings.h"

namespace Inferno {
    void AI::SetPath(Object& obj, const List<NavPoint>& path) {
        if (!obj.IsRobot() || path.empty()) {
            ASSERT(false);
            SPDLOG_WARN("Tried to set invalid path on object");
            return;
        }

        ASSERT(obj.IsRobot());
        auto& ai = GetAI(obj);
        auto endSeg = Game::Level.TryGetSegment(path.back().Segment);
        auto endRoom = Game::Level.GetRoomID(path.back().Segment);

        if (!endSeg || endRoom == RoomID::None) {
            SPDLOG_WARN("Path end isn't valid");
            return;
        }

        //auto path = Game::Navigation.NavigateTo(obj.Segment, soundSeg, !robotInfo.IsThief, Game::Level);
        ai.PathDelay = AI_PATH_DELAY;
        ai.Path = path;
        ai.PathIndex = 0;
        ai.State = AIState::Chase; // Stop pathing after seeing target
        obj.NextThinkTime = 0;
    }

    //bool PathIsValid(Object& obj, const AIRuntime& ai) {
    //    if (ai.GoalPath.empty()) return false;
    //    if (ai.GoalPath.back().Segment != ai.GoalSegment) return false; // Goal isn't this path anymore
    //    return Seq::exists(ai.GoalPath, [&](auto& p) { return p.Segment == obj.Segment; }); // Check if robot strayed from path
    //}

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


    Tag GetNextConnection(span<NavPoint> path, Level& level, SegID segId) {
        if (segId == SegID::None) return {};

        for (int i = 0; i < path.size() - 1; i++) {
            if (path[i].Segment == segId) {
                auto& seg = level.GetSegment(segId);

                // Find the connection to the next segment in the path
                for (auto& sideId : SIDE_IDS) {
                    auto connId = seg.GetConnection(sideId);
                    if (connId == path[i + 1].Segment) {
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
                    for (auto& sideId : SIDE_IDS) {
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

            for (auto& side : SIDE_IDS) {
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
        auto indices = seg.GetVertexIndices(tag.Side);
        PointID edgeIndices[] = { indices[edge], indices[(edge + 1) % 4] };

        auto adjacent = GetAdjacentSide(tag.Side, edge);
        auto connSide = level.GetConnectedSide({ tag.Segment, adjacent });
        if (!connSide) return {};
        auto& connSeg = level.GetSegment(connSide.Segment);

        for (auto& sideId : SIDE_IDS) {
            auto otherIndices = connSeg.GetVertexIndices(sideId);
            int matches = 0;

            for (auto& i : edgeIndices) {
                for (auto& other : otherIndices) {
                    if (i == other) matches++;
                }
            }

            if (matches == 2)
                return { connSide.Segment, sideId };
        }

        return {};
    }


    // Updates the target position after avoiding edges of the current segment
    void AvoidSideEdges(Level& level, const Ray& ray, Segment& seg, SideID sideId, const Object& obj, Vector3& target) {
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
                if (Settings::Cheats.ShowPathing) {
                    Graphics::DrawLine(edgeMidpoint + vec * 20, edgeMidpoint, Color(1, 0, 1));
                    Graphics::DrawPoint(target, Color(1, 0, 1));
                    Graphics::DrawPoint(side.Center, Color(1, 0, 1));
                }
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

    void AvoidRoomEdges(Level& level, const Ray& ray, const Object& obj, Vector3& target) {
        auto room = level.GetRoom(obj);
        if (!room) return;

        for (auto& segId : room->Segments) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SIDE_IDS) {
                AvoidSideEdges(level, ray, seg, sideId, obj, target);
            }
        }
    }

    bool SetPathGoal(Level& level, const Object& obj, AIRuntime& ai, const NavPoint& goal, float maxDistance) {
        // Calculate a new path
        auto& robotInfo = Resources::GetRobotInfo(obj);

        NavigationFlag flags{};
        if (robotInfo.IsThief)
            SetFlag(flags, NavigationFlag::OpenKeyDoors);

        ai.Path = Game::Navigation.NavigateTo(obj.Segment, goal, flags, level, maxDistance);
        ai.PathDelay = AI_PATH_DELAY;

        if (ai.Path.empty()) {
            ai.PathIndex = -1;
            return false; // Unable to find a valid path, give up
        }

        //SPDLOG_INFO("Robot {} updating path goal to {}", obj.Signature, ai.GoalSegment);
        ai.PathIndex = 0;
        return true;
    }

    bool PathTowardsGoal(Object& robot, AIRuntime& ai, bool alwaysFaceGoal, bool stopOnceVisible) {
        // Travel along a designated path, incrementing the node index as we go
        if (!Seq::inRange(ai.Path, ai.PathIndex))
            return false; // Empty or invalid index

        auto& node = ai.Path[ai.PathIndex];
        auto& goal = ai.Path.back();
        auto& robotInfo = Resources::GetRobotInfo(robot.ID);

        if (Settings::Cheats.ShowPathing) {
            Graphics::DrawLine(robot.Position, node.Position, Color(0, 1, 0));

            for (int i = 0; i < ai.Path.size() - 1; i++) {
                auto& a = ai.Path[i];
                auto& b = ai.Path[i + 1];
                Graphics::DrawLine(a.Position, b.Position, Color(0, .8, 1));
            }
        }

        // Try dodging if collided with something recently
        if (Game::Time - ai.LastCollision < 0.5 && ai.DodgeDelay <= 0) {
            ai.DodgeVelocity = RandomLateralDirection(robot) * 10;
            ai.DodgeDelay = 1.0f;
            ai.DodgeTime = 0.5f;
        }

        if (stopOnceVisible && robot.Segment != ai.Path.front().Segment && HasLineOfSight(robot, goal.Position)) {
            PlayAlertSound(robot, ai);
            SPDLOG_INFO("Robot {} can see the goal!", robot.Signature);
            ai.Path.clear();
            return false;
        }

        MoveTowardsPoint(robot, ai, node.Position, 1);

        Vector3 targetPosition = alwaysFaceGoal ? goal.Position : node.Position;
        auto goalDir = targetPosition - robot.Position;
        goalDir.Normalize();

        if (goalDir != Vector3::Zero) // Can be zero when object starts on top of the path
            TurnTowardsDirection(robot, goalDir, DifficultyInfo(robotInfo).TurnTime);

        // Move towards each path node until sufficiently close
        if (Vector3::Distance(robot.Position, node.Position) <= std::max(robot.Radius, 5.0f)) {
            ai.PathIndex++;
        }

        return true;
    }
}
