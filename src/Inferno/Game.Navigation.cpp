#include "pch.h"
#include "Game.h"
#include "Game.Navigation.h"
#include "Game.Segment.h"
#include "logging.h"
#include "Resources.h"
#include <numeric>

namespace Inferno {
    // Executes a function on each segment within range. Return false from action to stop iterating.
    void IterateNearbySegments(Level& level, SegID start, float distance, const std::function<void(Segment&, bool&)>& action) {
        ASSERT_STA();

        Vector3 startPosition;
        if (auto startSeg = level.TryGetSegment(start))
            startPosition = startSeg->Center;
        else
            return; // invalid segment

        static List<SegID> queue;
        static List<int8> visited;
        visited.resize(level.Segments.size());
        ranges::fill(visited, false);

        queue.clear();
        queue.push_back(start);

        float distSq = distance * distance;
        if (distSq < 0) distSq = FLT_MAX;
        int index = 0;

        while (index < queue.size()) {
            auto segid = queue[index++];
            auto seg = level.TryGetSegment(segid);
            if (!seg) continue;

            bool stop = false;
            action(*seg, stop);
            if (stop) break;

            for (auto& sideid : SIDE_IDS) {
                if (seg->SideIsSolid(sideid, level)) continue;

                if (Vector3::DistanceSquared(startPosition, seg->GetSide(sideid).Center) > distSq)
                    continue;

                auto connection = seg->GetConnection(sideid);
                auto& isVisited = visited[(int)connection];
                if (isVisited) continue; // already visited
                isVisited = true;
                queue.push_back(connection);
            }
        }
    }

    List<NavPoint> NavigateWithinRoomBfs(Level& level, SegID start, SegID goal, Room& room) {
        if (start == goal) return {};

        ASSERT_STA();

        struct Visited {
            SegID id = SegID::None, parent = SegID::None;
        };

        static List<Visited> queue;
        static List<Visited> visited;

        visited.resize(level.Segments.size());

        for (int i = 0; i < visited.size(); i++) {
            visited[i].id = SegID(i);
            visited[i].parent = SegID::None;
        }

        queue.clear();
        queue.push_back({ start, SegID::None });
        int index = 0;

        while (index < queue.size()) {
            Visited value = queue[index++];

            if (value.id == goal)
                break;

            auto seg = level.TryGetSegment(value.id);
            for (auto& sid : SIDE_IDS) {
                auto conn = seg->GetConnection(sid);
                if (!CanNavigateSide(level, { value.id, sid }, NavigationFlags::None))
                    continue;

                auto& node = visited[(int)conn];
                if (node.parent != SegID::None) continue; // already visited
                if (!Seq::contains(room.Segments, conn)) continue; // not in room

                node.parent = value.id;
                queue.push_back({ conn, value.id });
            }
        }

        List<NavPoint> path;

        {
            auto pathNode = &visited[(int)goal];
            if (pathNode->parent == SegID::None)
                return path; // didn't reach

            while (pathNode->parent != SegID::None && pathNode->id != start) {
                ASSERT(!Seq::exists(path, [&pathNode](auto& x) { return x.Segment == pathNode->id; }));
                auto& seg = level.GetSegment(pathNode->id);
                path.push_back({ pathNode->id, seg.Center });
                auto conn = level.GetConnectedSide(pathNode->parent, pathNode->id);
                if (conn != SideID::None)
                    path.push_back({ pathNode->id, seg.GetSide(conn).Center });

                pathNode = &visited[(int)pathNode->parent];
            }

            auto& seg = level.GetSegment(start);
            if (!path.empty()) {
                auto conn = level.GetConnectedSide(path.back().Segment, start);
                path.push_back({ start, seg.GetSide(conn).Center });
            }

            path.push_back({ start, seg.Center });
            ranges::reverse(path);
        }

        return path;
    }

    List<NavPoint> NavigationNetwork::NavigateTo(SegID start, const NavPoint& goal, NavigationFlags flags, Level& level, float maxDistance) {
        auto startRoom = level.GetRoom(start);
        auto endRoom = level.GetRoom(goal.Segment);
        if (!startRoom || !endRoom)
            return {}; // Rooms don't exist

        if (startRoom == endRoom) {
            auto path = NavigateWithinRoomBfs(level, start, goal.Segment, *endRoom);
            OptimizePath(path);
            return path;
        }

        List<NavPoint> path;
        auto roomStartSeg = start;
        auto roomPath = NavigateAcrossRooms(level.GetRoomID(start), level.GetRoomID(goal.Segment), flags, level);
        float totalDistance = 0;

        // starting at the first room, use the closest portal that matches the next room
        for (int i = 0; i < roomPath.size(); i++) {
            auto room = level.GetRoom(roomStartSeg);

            if (room == endRoom || i + 1 >= roomPath.size()) {
                auto localPath = NavigateWithinRoomBfs(level, roomStartSeg, goal.Segment, *endRoom);
                if (!localPath.empty()) {
                    localPath.back().Position = goal.Position;
                    Seq::append(path, localPath);
                }
            }
            else {
                // Not yet to final room
                float closestPortal = FLT_MAX;
                Tag bestPortal;
                auto& seg = level.GetSegment(roomStartSeg);

                for (int portalIndex = 0; portalIndex < room->Portals.size(); portalIndex++) {
                    auto& portal = room->Portals[portalIndex];
                    if (portal.RoomLink != roomPath[i + 1])
                        continue; // Portal doesn't connect to next room in the path

                    auto& portalSide = level.GetSide(portal.Tag);
                    auto distance = Vector3::DistanceSquared(seg.Center, portalSide.Center);
                    if (distance < closestPortal) {
                        closestPortal = distance;
                        bestPortal = portal.Tag;
                    }
                }

                if (!bestPortal) break; // Pathfinding to next portal failed

                totalDistance += sqrt(closestPortal);
                if (totalDistance > maxDistance) {
                    path.clear();
                    return path; // Max depth reached
                }

                auto localPath = NavigateWithinRoomBfs(level, roomStartSeg, bestPortal.Segment, *room);
                Seq::append(path, localPath);

                // Use the portal connection as the start for the next room
                roomStartSeg = level.GetConnectedSide(bestPortal).Segment;
            }
        }

        OptimizePath(path);

        //SPDLOG_INFO("Room path distance {}", totalDistance);
        return path;
    }

    bool CanNavigateWall(const Wall& wall, NavigationFlags flags) {
        if (wall.Type == WallType::Destroyable)
            return false;

        if (wall.Type == WallType::Door) {
            if (wall.HasFlag(WallFlag::DoorOpened))
                return true; // Regardless of whether the door is locked or keyed, navigate it if opened

            if (wall.HasFlag(WallFlag::DoorLocked))
                return false;

            auto& clip = Resources::GetDoorClip(wall.Clip);

            if (HasFlag(clip.Flags, DoorClipFlag::Secret) && !HasFlag(flags, NavigationFlags::OpenSecretDoors))
                return false;

            if (wall.IsKeyDoor()) {
                if (!HasFlag(flags, NavigationFlags::OpenKeyDoors)) return false;
                if (!Game::Player.CanOpenDoor(wall)) return false;
            }
        }

        if (wall.Type == WallType::Closed || wall.Type == WallType::Cloaked)
            return false;

        return true;
    }

    bool CanNavigateSide(Level& level, Tag tag, NavigationFlags flags) {
        auto seg = level.TryGetSegment(tag);
        if (!seg) return false;
        if (!seg->SideHasConnection(tag.Side)) return false;

        if (auto wall = level.TryGetWall(seg->GetSide(tag.Side).Wall))
            return CanNavigateWall(*wall, flags);

        return true;
    }

    List<RoomID> NavigationNetwork::NavigateAcrossRooms(RoomID start, RoomID goal, NavigationFlags flags, Level& level) {
        if (start == goal) return { start };

        // Reset traversal state
        auto goalRoom = level.GetRoom(goal);

        for (int i = 0; i < level.Rooms.size(); i++) {
            auto& room = level.Rooms[i];
            _traversalBuffer[i] = {
                .Index = i,
                .GoalDistance = Vector3::Distance(room.Center, goalRoom->Center)
            };
        }

        std::list<TraversalNode*> queue;
        _traversalBuffer[(int)start].LocalGoal = 0;
        queue.push_back(&_traversalBuffer[(int)start]);

        while (!queue.empty()) {
            queue.sort([](const TraversalNode* a, const TraversalNode* b) {
                return a->GoalDistance < b->GoalDistance;
            });

            if (!queue.empty() && queue.front()->Visited)
                queue.pop_front();

            if (queue.empty())
                break; // no nodes left

            auto& current = queue.front();
            current->Visited = true;
            auto room = &level.Rooms[current->Index];

            for (auto& portal : room->Portals) {
                auto& segNode = _segmentNodes[(int)portal.Tag.Segment];
                auto& nodeSide = segNode.Sides[(int)portal.Tag.Side];
                if (nodeSide.Connection <= SegID::None) continue;

                // Check if the portal is blocked
                if (auto wall = level.TryGetWall(portal.Tag)) {
                    if (!CanNavigateWall(*wall, flags))
                        continue;
                }

                auto& neighbor = _traversalBuffer[(int)portal.RoomLink];

                if (!neighbor.Visited)
                    queue.push_back(&neighbor);

                auto& portalSide = level.GetSide(portal.Tag);

                // If portal connects to goal room use distance 0 and not distance between centers
                //
                // This heuristic could be improved by taking the distance between the entrance
                // and exit portals instead of the room centers.
                auto localDistance = Vector3::DistanceSquared(room->Center, portalSide.Center);
                float localGoal = portal.RoomLink == goal ? current->LocalGoal : current->LocalGoal + localDistance;

                if (localGoal < neighbor.LocalGoal) {
                    neighbor.Parent = current->Index;
                    neighbor.LocalGoal = localGoal;
                    neighbor.GoalDistance = neighbor.LocalGoal + Vector3::DistanceSquared(portalSide.Center, goalRoom->Center);
                }
            }
        }

        List<RoomID> path;

        // add nodes along the path starting at the goal
        auto* trav = &_traversalBuffer[(int)goal];

        while (trav) {
            path.push_back((RoomID)trav->Index);
            trav = trav->Parent >= 0 ? &_traversalBuffer[trav->Parent] : nullptr;
        }

        ranges::reverse(path);

        // Walk backwards, using the parent
        return path;
    }

    List<SegID> NavigationNetwork::NavigateWithinRoom(SegID start, SegID goal, Room& room) {
        if (!room.Contains(start) || !room.Contains(goal))
            return {}; // No direct solution. Programming error

        // Reset traversal state
        for (int i = 0; i < _traversalBuffer.size(); i++)
            _traversalBuffer[i] = {
                .Index = i,
                .GoalDistance = Heuristic(_segmentNodes[(int)start], _segmentNodes[(int)goal])
            };

        std::list<TraversalNode*> queue;
        _traversalBuffer[(int)start].LocalGoal = 0;
        queue.push_back(&_traversalBuffer[(int)start]);

        while (!queue.empty()) {
            queue.sort([](const TraversalNode* a, const TraversalNode* b) {
                return a->GoalDistance < b->GoalDistance;
            });

            if (!queue.empty() && queue.front()->Visited)
                queue.pop_front();

            if (queue.empty())
                break; // no nodes left

            auto& current = queue.front();
            current->Visited = true;
            auto& node = _segmentNodes[current->Index];

            for (int side = 0; side < 6; side++) {
                auto& nodeSide = node.Sides[side];
                auto& connId = nodeSide.Connection;
                if (connId <= SegID::None) continue;
                if (!room.Contains(connId)) continue; // Only search segments in this room

                auto& neighborNode = _segmentNodes[(int)connId];
                auto& neighbor = _traversalBuffer[(int)connId];

                if (!neighbor.Visited)
                    queue.push_back(&neighbor);

                float localGoal = current->LocalGoal + Vector3::DistanceSquared(node.Position, neighborNode.Position);

                if (localGoal < neighbor.LocalGoal) {
                    neighbor.Parent = current->Index;
                    neighbor.LocalGoal = localGoal;
                    neighbor.GoalDistance = neighbor.LocalGoal + Heuristic(neighborNode, _segmentNodes[(int)goal]);
                }
            }
        }

        // add nodes along the path starting at the goal
        auto* trav = &_traversalBuffer[(int)goal];
        List<SegID> path;

        while (trav) {
            path.push_back((SegID)trav->Index);
            trav = trav->Parent >= 0 ? &_traversalBuffer[trav->Parent] : nullptr;
        }
        //}

        ranges::reverse(path);

        // Walk backwards, using the parent
        return path;
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

    void TraverseRoomsByDistance(Inferno::Level& level, RoomID startRoom, const Vector3& position,
                                 float maxDistance, bool soundMode, const std::function<bool(Room&)>& action) {
        struct TravelInfo {
            Portal Portal;
            float Distance;
        };

        //SPDLOG_INFO("Traversing rooms");
        static List<TravelInfo> stack;
        stack.clear();

        auto roomIsVisited = [](RoomID id) {
            for (auto& item : stack) {
                if (item.Portal.RoomLink == id) return true;
            }
            return false;
        };

        int stackIndex = 0;

        {
            auto room = level.GetRoom(startRoom);
            if (!room) return;
            if (action(*room)) // Execute on starting room
                return;

            //SPDLOG_INFO("Executing on room {}", (int)startRoom);

            // Check if any portals are in range of the start point
            for (auto& portal : room->Portals) {
                auto& side = level.GetSide(portal.Tag);
                if (soundMode && !SoundPassesThroughSide(level, side))
                    continue;

                auto dist = Vector3::Distance(side.Center, position);

                // Check projected distance in case point is on the portal face
                auto proj = ProjectPointOntoPlane(position, side.Center, side.AverageNormal);
                auto projDist = Vector3::Distance(proj, position);
                if (projDist < dist) dist = projDist;

                // todo: if multiple portals connect to the same room, pick the closer one
                if (dist < maxDistance && !roomIsVisited(portal.RoomLink)) {
                    stack.push_back({ portal, dist });
                    //SPDLOG_INFO("Checking adjacent portal {}:{} dist: {}", portal.Tag.Segment, portal.Tag.Side, dist);
                }
            }
        }

        while (stackIndex < stack.size()) {
            TravelInfo info = stack[stackIndex++]; // Intentional copy due to modifying stack
            auto room = level.GetRoom(info.Portal.RoomLink);
            if (!room) continue;
            //SPDLOG_INFO("Executing on room {} Distance {}", (int)info.Portal.RoomLink, info.Distance);
            if (action(*room)) // act on the room
                return;

            auto& portalDistances = room->PortalDistances[info.Portal.PortalLink];

            // check room portal distances
            for (int i = 0; i < room->Portals.size(); i++) {
                if (i == info.Portal.PortalLink) continue; // Don't backtrack
                auto& endPortal = room->Portals[i];

                auto& side = level.GetSide(endPortal.Tag);
                if (soundMode && !SoundPassesThroughSide(level, side))
                    continue;

                auto distance = info.Distance + portalDistances[i];
                if (endPortal.RoomLink == startRoom)
                    continue; // Start room already executed

                if (roomIsVisited(endPortal.RoomLink))
                    continue; // Linked room already visited

                if (distance < maxDistance) {
                    stack.push_back({ endPortal, distance });
                    //SPDLOG_INFO("Checking portal {}:{} dist: {}", endPortal.Tag.Segment, endPortal.Tag.Side, distance);
                }
            }
        }
    }

    List<NavPoint> GenerateRandomPath(SegID start, uint depth, NavigationFlags flags, SegID avoid) {
        List<NavPoint> path;
        if (!Game::Level.SegmentExists(start)) return path;

        //Tag portalTag;
        //auto& startSeg = Game::Level.GetSegment(start);

        auto& level = Game::Level;
        ASSERT_STA();

        struct Visited {
            SegID id = SegID::None, parent = SegID::None;
        };

        static List<SegID> queue;
        static List<Visited> visited;

        visited.resize(level.Segments.size());

        for (int i = 0; i < visited.size(); i++) {
            visited[i].id = SegID(i);
            visited[i].parent = SegID::None;
        }

        queue.clear();
        queue.reserve(depth);
        queue.push_back(start);

        uint index = 0;
        std::array sideLookup = SIDE_IDS;
        while (index < queue.size()) {
            SegID segid = queue[index++];
            if (index >= depth) break;

            auto seg = level.TryGetSegment(segid);
            if (!seg) continue;

            Shuffle(sideLookup); // Randomize where to go

            for (auto& sid : sideLookup) {
                auto conn = seg->GetConnection(sid);
                if (!CanNavigateSide(level, { segid, sid }, flags))
                    continue;

                if (conn == avoid) continue;

                auto& node = visited[(int)conn];
                if (node.parent != SegID::None || node.id == start)
                    continue; // already visited

                node.parent = segid;
                queue.push_back(conn);


                //pathNode = &visited[(int)pathNode->parent];
            }
        }

        SegID current = queue.back();

        while (current != SegID::None) {
            auto& seg = level.GetSegment(current);
            path.push_back({ current, seg.Center }); // Add seg center
            auto& node = visited[(int)current];
            current = node.parent;

            // Add side center
            auto connSide = level.GetConnectedSide(node.parent, node.id);
            if (connSide != SideID::None)
                path.push_back({ node.id, seg.GetSide(connSide).Center });
        }

        OptimizePath(path);
        ranges::reverse(path);

        //{
        //    auto pathNode = &visited[(int)goal];
        //    if (pathNode->parent == SegID::None)
        //        return path; // didn't reach

        //    while (pathNode->parent != SegID::None && pathNode->id != start) {
        //        ASSERT(!Seq::exists(path, [&pathNode](auto& x) { return x.Segment == pathNode->id; }));
        //        auto& seg = level.GetSegment(pathNode->id);
        //        path.push_back({ pathNode->id, seg.Center });
        //        auto conn = level.GetConnectedSide(pathNode->parent, pathNode->id);
        //        if (conn != SideID::None)
        //            path.push_back({ pathNode->id, seg.GetSide(conn).Center });

        //        pathNode = &visited[(int)pathNode->parent];
        //    }

        //    auto& seg = level.GetSegment(start);
        //    if (!path.empty()) {
        //        auto conn = level.GetConnectedSide(path.back().Segment, start);
        //        path.push_back({ start, seg.GetSide(conn).Center });
        //    }

        //    path.push_back({ start, seg.Center });
        //    ranges::reverse(path);
        //}

        return path;

        //OptimizePath(path);
        //return path;

        //
        //        {
        //            auto pathNode = &visited[(int)goal];
        //            if (pathNode->parent == SegID::None)
        //                return path; // didn't reach
        //
        //            while (pathNode->parent != SegID::None && pathNode->id != start) {
        //                ASSERT(!Seq::exists(path, [&pathNode](auto& x) { return x.Segment == pathNode->id; }));
        //                auto& seg = level.GetSegment(pathNode->id);
        //                path.push_back({ pathNode->id, seg.Center });
        //                auto conn = level.GetConnectedSide(pathNode->parent, pathNode->id);
        //                if (conn != SideID::None)
        //                    path.push_back({ pathNode->id, seg.GetSide(conn).Center });
        //
        //                pathNode = &visited[(int)pathNode->parent];
        //            }
        //
        //            auto& seg = level.GetSegment(start);
        //            if (!path.empty()) {
        //                auto conn = level.GetConnectedSide(path.back().Segment, start);
        //                path.push_back({ start, seg.GetSide(conn).Center });
        //            }
        //
        //            path.push_back({ start, seg.Center });
        //            ranges::reverse(path);
        //        }
        //
        //        return path;
        //
        //        {
        //            // Pick a portal that is not close to the start segment
        //            if (auto room = Game::Level.GetRoom(startSeg.Room)) {
        //                if (!room->Portals.empty()) {
        //                    auto portalCount = room->Portals.size();
        //                    auto index = RandomInt((int)portalCount - 1);
        //                    int bestPortal = 0;
        //                    for (int i = 0; i < portalCount; i++, index++) {
        //                        auto& portal = room->Portals[index % portalCount];
        //
        //                        // Skip sides we can't navigate through
        //                        if (CanNavigateSide(Game::Level, portal.Tag, flags)) {
        //                            bestPortal = index % portalCount;
        //                            break;
        //                        }
        //
        //                        //    //auto& p = room->Portals[indices[i]];
        //                        //    auto& portal = room->Portals[index % portalCount];
        //                        //    auto& side = Game::Level.GetSide(portal.Tag);
        //                        //    auto dist = Vector3::DistanceSquared(side.Center, startSeg.Center);
        //
        //                        //    if (dist > 60 * 60) {
        //                        //        bestPortal = index % portalCount;
        //                        //        break;
        //                        //    }
        //                    }
        //
        //                    //path = NavigateWithinRoomBfs(Game::Level, start, room->Portals[bestPortal].Tag.Segment, *room);
        //
        //                    portalTag = room->Portals[bestPortal].Tag;
        //
        //                    // Pick a random portal in the room to run to
        //                    path = NavigateWithinRoomBfs(Game::Level, start, portalTag.Segment, *room);
        //                }
        //            }
        //        }
        //
        //        if (path.size() > depth) {
        //            path.resize(depth);
        //            return path; // in-room path was long enough!
        //        }
        //
        //        //return path; // in-room path was long enough!
        //
        //        // do a random search outside the room because the path was too short
        //
        //        //static List<SegID> visited;
        //        //visited.clear();
        //        //Seq::append(visited, path);
        //
        //        //if (avoid != SegID::None)
        //        //    visited.push_back(avoid);
        //
        //        path.reserve(depth);
        //
        //        std::array sideLookup = SIDE_IDS;
        //        //Vector3 startDir;
        //        auto portalConn = Game::Level.GetConnectedSide(portalTag);
        //        SegID segid = portalConn ? portalConn.Segment : start;
        //
        //        // start seg was on edge of room
        //        if (path.empty() && portalTag) {
        //            auto& portalSeg = Game::Level.GetSegment(portalTag);
        //            auto& portalSide = portalSeg.GetSide(portalTag.Side);
        //            path.push_back({ portalTag.Segment, portalSeg.Center });
        //            path.push_back({ portalTag.Segment, portalSide.Center + portalSide.AverageNormal });
        //        }
        //
        //        uint d = 0;
        //        while (path.size() < depth) {
        //            auto& seg = Game::Level.GetSegment(segid);
        //
        //            //visited.push_back(segid);
        //            // todo: prevent overlap with existing path
        //            //if (!path.empty() && path.back().Segment != segid)
        //            path.push_back({ segid, seg.Center });
        //            d++;
        //
        //            //if (d == 1) {
        //            //    startDir = seg.Center - startPoint;
        //            //    startDir.Normalize();
        //            //}
        //
        //            auto bestSide = SideID::None;
        //            Shuffle(sideLookup); // Randomize where to go
        //
        //            for (auto& side : sideLookup) {
        //                auto conn = seg.GetConnection(side);
        //                if (conn == avoid || Seq::exists(path, [conn](auto& x) { return x.Segment == conn; })) continue;
        //                if (!CanNavigateSide(Game::Level, { segid, side }, flags)) continue;
        //                bestSide = side;
        //
        //#ifdef BIAS
        //                if (biasFromStart && d > 0) {
        //                    //bestSide = side;
        //                    auto dist = Vector3::DistanceSquared(seg.GetSide(side).Center, startPoint);
        //
        //                    // Try to move away from the start in the most optimal way possible to start with
        //                    if (dist > furthestDist /*&& dist > 60 * 60*/) {
        //                        // pick the first side that is further than the previous overall best, so the same route isn't always taken
        //                        bestSide = side;
        //                        furthestDist = dist;
        //                        break;
        //                    }
        //                    //else if (dist > bestDist) {
        //                    //    // pick the furthest side from the start. Always chooses same path.
        //                    //    bestSide = side;
        //                    //    bestDist = dist;
        //                    //}
        //
        //                    //auto dir = seg.GetSide(side).Center - seg.Center;
        //                    //dir.Normalize();
        //                    //auto dot = dir.Dot(startDir);
        //                    //if (dot > bestDot) {
        //                    //    bestSide = side;
        //                    //    bestDot = dot;
        //                    //}
        //
        //                    //if (bestDot >= 0)
        //                    //    break; // Found a good enough solution
        //                }
        //                else {
        //                    // Randomly pick a side
        //                    bestSide = side;
        //                    break;
        //                }
        //#endif
        //            }
        //
        //            if (bestSide == SideID::None)
        //                return path; // Unable to path further
        //
        //            auto& side = seg.GetSide(bestSide);
        //            path.push_back({ segid, side.Center + side.AverageNormal });
        //            segid = seg.GetConnection(bestSide);
        //        }
        //
        //        if (path.size() > 2)
        //            path.resize(path.size() - 1); // Discard the face connection on the last segment
        //
        //        OptimizePath(path);
        //        return path;
    }

    //float FaceEdgeDistanceOpen(const Face2& face, const Vector3& point) {
    //    // Check the four outside edges of the face
    //    auto mag1 = (point - ClosestPointOnLine(face[0], face[1], point)).Length();
    //    auto mag2 = (point - ClosestPointOnLine(face[1], face[2], point)).Length();
    //    auto mag3 = (point - ClosestPointOnLine(face[2], face[3], point)).Length();
    //    auto mag4 = (point - ClosestPointOnLine(face[3], face[0], point)).Length();
    //    return std::min({ mag1, mag2, mag3, mag4 });
    //}

    // Similar to FaceEdgeDistance() but checks for adjacent closed sides instead of open ones
    float FaceEdgeDistancePathing(const Segment& seg, SideID sideid, const Face2& face, const Vector3& point) {
        // Check the four outside edges of the face
        float mag1 = FLT_MAX, mag2 = FLT_MAX, mag3 = FLT_MAX, mag4 = FLT_MAX;
        auto& side = seg.GetSide(sideid);


        // If the edge doesn't have a connection it's safe to put a decal on it
        if (side.SolidEdges[0]) {
            auto c = ClosestPointOnLine(face[0], face[1], point);
            mag1 = (point - c).Length();
        }

        if (side.SolidEdges[1]) {
            auto c = ClosestPointOnLine(face[1], face[2], point);
            mag2 = (point - c).Length();
        }

        if (side.SolidEdges[2]) {
            auto c = ClosestPointOnLine(face[2], face[3], point);
            mag3 = (point - c).Length();
        }

        if (side.SolidEdges[3]) {
            auto c = ClosestPointOnLine(face[3], face[0], point);
            mag4 = (point - c).Length();
        }

        return std::min({ mag1, mag2, mag3, mag4 });
    }

    // Hit test against all sides in a segment, but ignores backfacing
    LevelHit IntersectSegmentPathing(Level& level, const Ray& ray, SegID segId) {
        LevelHit hit{};

        auto seg = level.TryGetSegment(segId);
        if (!seg) return hit;

        for (auto& side : SIDE_IDS) {
            auto face = Face2::FromSide(level, *seg, side);

            //if (ray.direction.Dot(face.AverageNormal()) >= 0)
            //    continue; // Don't hit backfaces

            float dist{};
            auto tri = face.Intersects(ray, dist);
            if (tri == -1) continue; // no hit on this side

            hit.Tag = { segId, side };
            hit.Distance = dist;
            hit.Normal = face.Side->Normals[tri];
            hit.Tangent = face.Side->Tangents[tri];
            hit.Point = ray.position + ray.direction * dist;
            hit.EdgeDistance = FaceEdgeDistancePathing(*seg, side, face, hit.Point);
            break;
        }

        return hit;
    }

    void OptimizePath(List<NavPoint>& path) {
        if (path.empty()) return;
        float objRadius = 8;

        List<NavPoint> buffer;
        buffer.reserve(path.size());
        buffer.push_back(path.front());

        for (int i = 0; i < path.size();) {
            uint offset = 1;

            // Keep casting rays to further nodes until one hits
            for (; offset + i < path.size(); offset++) {
                auto [dir, dist] = GetDirectionAndDistance(path[i + offset].Position, path[i].Position);
                Ray ray = { path[i].Position, dir };
                RayQuery query{ .MaxDistance = dist + 5.0f, .Start = path[i].Segment };
                LevelHit hit;
                ASSERT(dir != Vector3::Zero);
                // Checking for > 1 is in the case where the segments are too small for the radius even without splitting
                if (offset > 1 && Game::Intersect.RayLevel(ray, query, hit)) {
                    /*if(offset > 1) {
                        SPDLOG_WARN("Original path is too close to wall with requested radius.
                    }*/
                    offset--; // Back up one node
                    break;
                }
            }

            buffer.push_back(path[std::min(i + offset, (uint)path.size() - 1)]);
            i += offset;
        }

        path = buffer;
        buffer.clear();
        //return;

        //SPDLOG_INFO("Optimizing path");

        // now check if the remaining nodes are too close to segment edges
        for (uint i = 0; i + 1 < path.size(); i++) {
            auto& node = path[i];

            buffer.push_back(node);

            NavPoint curNode = node;
            //auto nodeSeg = node.Segment;
            //auto nodePoint = node.Position;

            //Vector3 prevHitPoint;

            // Check each segment along the path for being too close
            while (curNode.Segment != path[i + 1].Segment) {
                auto [dir, dist] = GetDirectionAndDistance(path[i + 1].Position, curNode.Position);

                //SPDLOG_INFO("Raytest seg: {}", curNode.Segment);

                Ray ray = { curNode.Position, dir };
                auto hit = IntersectSegmentPathing(Game::Level, ray, curNode.Segment);
                if (!hit) {
                    //__debugbreak();
                    SPDLOG_WARN("PATH FAILURE");
                    return; // return the original path
                }
                //auto edgeDistance = hit.EdgeDistance;

                //if (!hit.Tag) {
                //    // If a ray passes perfectly through the corner of segments it can skip the connected segment and instead go to an adjacent one.
                //    // Try nudging the point into a nearby seg from the previous hit
                //    __debugbreak();
                //    curNode.Segment = TraceSegment(Game::Level, curNode.Segment, prevHitPoint + ray.direction);
                //    hit = IntersectSegmentPathing(Game::Level, ray, curNode.Segment);
                //    if (!hit.Tag) {
                //        __debugbreak();
                //        SPDLOG_WARN("Nav Path didn't intersect with expected segment {}! unable to recover", curNode.Segment);
                //        break;
                //    }
                //}

                //if (!hit.Tag) {
                //    // Hit test against the current segment failed when it shouldn't. Verify the point position.
                //    curNode.Segment = TraceSegment(Game::Level, curNode.Segment, curNode.Position);
                //    auto retryHit = IntersectSegmentPathing(Game::Level, ray, curNode.Segment);

                //}

                if (hit.EdgeDistance < objRadius) {
                    // Path too close to edge, insert a new node towards the center
                    auto& side = Game::Level.GetSide(hit.Tag);
                    auto centerDir = GetDirection(side.Center, hit.Point);

                    //auto position = hit.Point + centerDir * (objRadius - hit.EdgeDistance);
                    curNode.Position = hit.Point + centerDir * (objRadius - hit.EdgeDistance);
                    curNode.Segment = TraceSegment(Game::Level, curNode.Segment, curNode.Position);
                    buffer.push_back(curNode/* { nodeSeg, position }*/);
                }

                //prevHitPoint = hit.Point;
                curNode.Segment = Game::Level.GetConnectedSide(hit.Tag).Segment;
                //nodeSeg = Game::Level.GetConnectedSide(hit.Tag).Segment; // Get the next segment in the path
                //nodePoint
            }
        }

        buffer.push_back(path.back());
        path = buffer;
        buffer.clear();

        // merge nearby nodes
        //for (uint i = 0; i + 1 < path.size(); i++) {
        //    if (Vector3::Distance(path[i].Position, path[i + 1].Position) < 5) {
        //        auto avg = (path[i].Position + path[i + 1].Position) * 0.5f;
        //        i++;
        //        auto newSeg = TraceSegment(Game::Level, path[i].Segment, avg);
        //        buffer.push_back({ newSeg, avg });
        //    }
        //    else {
        //        buffer.push_back(path[i]);
        //    }
        //}

        //path = buffer;
    }
}
