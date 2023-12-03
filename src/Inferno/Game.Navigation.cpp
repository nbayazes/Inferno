#include "pch.h"
#include "Game.Navigation.h"
#include "Game.h"
#include "Resources.h"
#include "logging.h"
#include <numeric>

namespace Inferno {
    class ScopedBool {
        std::atomic_bool* _b;

    public:
        ScopedBool(std::atomic_bool& b) : _b(&b) { *_b = true; }
        ~ScopedBool() { *_b = false; }
        ScopedBool(const ScopedBool&) = delete;
        ScopedBool(ScopedBool&&) = default;
        ScopedBool& operator=(const ScopedBool&) = delete;
        ScopedBool& operator=(ScopedBool&&) = default;
    };

    // Asserts single threaded access of this scope
#define ASSERT_STA() static std::atomic staGuard = false; ASSERT(!staGuard); ScopedBool staScope(staGuard);

    List<SegID> NavigateWithinRoomBfs(Level& level, SegID start, SegID goal, Room& room) {
        ASSERT_STA();

        struct Visited {
            SegID id = SegID::None, parent = SegID::None;
        };

        static Queue<Visited> queue;
        static List<Visited> visited;

        visited.resize(level.Segments.size());

        for (int i = 0; i < visited.size(); i++) {
            visited[i].id = SegID(i);
            visited[i].parent = SegID::None;
        }

        queue = {};

        List<Visited> maybePath;
        maybePath.resize(room.Segments.size());

        queue.push({ start, SegID::None });
        while (!queue.empty()) {
            Visited value = queue.front();
            queue.pop();
            maybePath.push_back(value);

            if (value.id == goal)
                break;

            auto seg = level.TryGetSegment(value.id);
            for (auto& sid : SIDE_IDS) {
                auto conn = seg->GetConnection(sid);
                if (conn == SegID::None) continue; // solid side
                auto& side = seg->GetSide(sid);
                if (auto wall = level.TryGetWall(side.Wall)) {
                    if (wall->IsSolid()) continue; // can't go through this wall
                }

                auto& node = visited[(int)conn];
                if (node.parent != SegID::None) continue; // already visited
                if (!Seq::contains(room.Segments, conn)) continue; // not in room

                node.parent = value.id;
                queue.push({ conn, value.id });
            }
        }

        List<SegID> path;

        if (start == goal) {
            path.push_back(goal);
        }
        else {
            auto pathNode = &visited[(int)goal];
            if (pathNode->parent == SegID::None)
                return path; // didn't reach

            while (pathNode->parent != SegID::None && pathNode->id != start) {
                ASSERT(!Seq::contains(path, pathNode->id));
                path.push_back(pathNode->id);
                pathNode = &visited[(int)pathNode->parent];
            }

            path.push_back(start);
            ranges::reverse(path);
        }

        return path;
    }

    List<SegID> NavigationNetwork::NavigateTo(SegID start, SegID goal, NavigationFlags flags, Level& level, float maxDistance) {
        auto startRoom = level.GetRoom(start);
        auto endRoom = level.GetRoom(goal);
        if (!startRoom || !endRoom)
            return {}; // Rooms don't exist

        if (startRoom == endRoom)
            return NavigateWithinRoomBfs(level, start, goal, *endRoom);

        List<SegID> path;
        auto roomStartSeg = start;
        auto roomPath = NavigateAcrossRooms(level.GetRoomID(start), level.GetRoomID(goal), flags, level);
        float totalDistance = 0;

        // starting at the first room, use the closest portal that matches the next room
        for (int i = 0; i < roomPath.size(); i++) {
            auto room = level.GetRoom(roomStartSeg);

            if (room == endRoom || i + 1 >= roomPath.size()) {
                auto localPath = NavigateWithinRoomBfs(level, roomStartSeg, goal, *endRoom);
                Seq::append(path, localPath);
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

        //SPDLOG_INFO("Room path distance {}", totalDistance);
        return path;
    }

    bool CanNavigateWall(const Wall& wall, NavigationFlags flags) {
        if (wall.Type == WallType::Destroyable)
            return false;

        if (wall.Type == WallType::Door) {
            if (wall.HasFlag(WallFlag::DoorLocked))
                return false;

            auto& clip = Resources::GetDoorClip(wall.Clip);

            if (HasFlag(clip.Flags, DoorClipFlag::Secret) && !HasFlag(flags, NavigationFlags::OpenSecretDoors))
                return false;

            if (wall.IsKeyDoor() && !HasFlag(flags, NavigationFlags::OpenKeyDoors))
                return false;
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

    List<SegID> GenerateRandomPath(SegID start, uint depth, NavigationFlags flags, SegID avoid) {
        List<SegID> path;
        if (!Game::Level.SegmentExists(start)) return path;


        Tag portalTag;
        auto& startSeg = Game::Level.GetSegment(start);

        {
            // Pick a portal that is not close to the start segment
            if (auto room = Game::Level.GetRoom(startSeg.Room)) {
                if (!room->Portals.empty()) {
                    auto portalCount = room->Portals.size();
                    auto index = RandomInt((int)portalCount - 1);
                    auto& portal = room->Portals[index];
                    int bestPortal = 0;
                    for (int i = 0; i < portalCount; i++, index++) {
                        // Skip sides we can't navigate through
                        if (CanNavigateSide(Game::Level, portal.Tag, flags)) {
                            bestPortal = index % portalCount;
                            break;
                        }

                        //    //auto& p = room->Portals[indices[i]];
                        //    auto& portal = room->Portals[index % portalCount];
                        //    auto& side = Game::Level.GetSide(portal.Tag);
                        //    auto dist = Vector3::DistanceSquared(side.Center, startSeg.Center);

                        //    if (dist > 60 * 60) {
                        //        bestPortal = index % portalCount;
                        //        break;
                        //    }
                    }

                    //path = NavigateWithinRoomBfs(Game::Level, start, room->Portals[bestPortal].Tag.Segment, *room);

                    portalTag = room->Portals[bestPortal].Tag;

                    // Pick a random portal in the room to run to
                    path = NavigateWithinRoomBfs(Game::Level, start, portalTag.Segment, *room);
                }
            }
        }

        if (path.size() > depth) {
            path.resize(depth);
            return path; // in-room path was long enough!
        }

        //return path; // in-room path was long enough!

        // do a random search outside of the room because the path was too short

        //static List<SegID> visited;
        //visited.clear();
        //Seq::append(visited, path);

        //if (avoid != SegID::None)
        //    visited.push_back(avoid);

        path.reserve(depth);

        std::array lookup = SIDE_IDS;
        //Vector3 startDir;
        Vector3 startPoint;
        float furthestDist = 0;
        auto portalConn = Game::Level.GetConnectedSide(portalTag);
        SegID segid = portalConn ? portalConn.Segment : start;

        uint d = 0;
        while (d <= depth) {
            auto& seg = Game::Level.GetSegment(segid);
            //visited.push_back(segid);
            if (path.back() != segid)
                path.push_back(segid);

            if (d == 0)
                startPoint = seg.Center;

            //if (d == 1) {
            //    startDir = seg.Center - startPoint;
            //    startDir.Normalize();
            //}

            float bestDot = -2;
            auto bestSide = SideID::None;
            bool biasFromStart = true;
            float bestDist = 0;

            Shuffle(lookup); // Randomize where to go

            for (auto& side : lookup) {
                auto conn = seg.GetConnection(side);
                if (conn == avoid || Seq::contains(path, conn)) continue;
                if (!CanNavigateSide(Game::Level, { segid, side }, flags)) continue;


                if (biasFromStart && d > 0) {
                    //bestSide = side;
                    auto dist = Vector3::DistanceSquared(seg.GetSide(side).Center, startPoint);

                    // Try to move away from the start in the most optimal way possible to start with
                    if (dist > furthestDist /*&& dist > 60 * 60*/) {
                        // pick the first side that is further than the previous overall best, so the same route isn't always taken
                        bestSide = side;
                        furthestDist = dist;
                        break;
                    }
                    //else if (dist > bestDist) {
                    //    // pick the furthest side from the start. Always chooses same path.
                    //    bestSide = side;
                    //    bestDist = dist;
                    //}

                    //auto dir = seg.GetSide(side).Center - seg.Center;
                    //dir.Normalize();
                    //auto dot = dir.Dot(startDir);
                    //if (dot > bestDot) {
                    //    bestSide = side;
                    //    bestDot = dot;
                    //}

                    //if (bestDot >= 0)
                    //    break; // Found a good enough solution
                }
                else {
                    // Randomly pick a side
                    bestSide = side;
                    break;
                }
            }

            if (bestSide == SideID::None)
                return path; // Unable to path further

            segid = seg.GetConnection(bestSide);
            d++;
        }

        return path;
    }
}
