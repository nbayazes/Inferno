#include "pch.h"

#include "Room.h"

#include "Game.h"
#include "Physics.h"
#include "ScopedTimer.h"

namespace Inferno {
    constexpr auto NAV_OBJECT_RADIUS = 4.0f; // expected object radius to follow a navigation path

    bool SegmentIsTunnel(Segment& seg) {
        int connections = 0;
        for (int i = 0; i < 6; i++) {
            if (seg.Connections[i] != SegID::None) connections++;
        }

        if (connections != 2) return false;

        return
            (seg.GetConnection(SideID::Front) != SegID::None && seg.GetConnection(SideID::Back) != SegID::None) ||
            (seg.GetConnection(SideID::Top) != SegID::None && seg.GetConnection(SideID::Bottom) != SegID::None) ||
            (seg.GetConnection(SideID::Left) != SegID::None && seg.GetConnection(SideID::Right) != SegID::None);
    }

    bool WallIsPortal(const Wall& wall) {
        if (wall.Type == WallType::Open)
            return false; // invisible walls

        if (wall.Type == WallType::Illusion)
            return false; // don't split energy centers into separate rooms

        return true;
    }

    Room CreateRoom(Level& level, SegID start) {
        Set<SegID> segments;
        Stack<SegID> search;
        search.push(start);

        Room room;
        auto& startSeg = level.GetSegment(start);

        while (!search.empty()) {
            auto segId = search.top();
            search.pop();

            auto& seg = level.GetSegment(segId);
            if (seg.Type == SegmentType::Energy || seg.Type == SegmentType::Repair)
                room.Type = seg.Type; // Mark energy centers

            segments.insert(segId);

            for (auto& side : SideIDs) {
                if (!seg.SideHasConnection(side)) continue; // nothing to do here

                auto conn = seg.GetConnection(side);
                auto& cseg = level.GetSegment(conn);

                bool addPortal = false;
                auto shouldAddPortal = [&seg, &startSeg](const Wall& wall) {
                    if (!WallIsPortal(wall)) return false;
                    if (seg.Type == SegmentType::Energy && startSeg.Type == SegmentType::Energy)
                        return false; // don't split energy centers into separate rooms

                    return true;
                };

                if (auto wall = level.TryGetWall({ segId, side }))
                    addPortal |= shouldAddPortal(*wall);

                if (auto wall = level.TryGetConnectedWall({ segId, side }))
                    addPortal |= shouldAddPortal(*wall);

                addPortal |= cseg.Type != startSeg.Type; // new room if seg type changes

                if (addPortal) {
                    room.AddPortal({ segId, side });
                    continue;
                }

                if (conn > SegID::None && !segments.contains(conn)) {
                    search.push(conn);
                }
            }
        }

        room.Segments = Seq::ofSet(segments);
        return room;
    }

    Room CreateRoom(Level& level, SegID start, const Set<SegID>& visited, float maxSegments) {
        Set<SegID> segments;
        Stack<SegID> search;

        // tunnels are tracked before adding to the room. If max segments is exceeded, the tunnel
        // is added as a separate room.
        Set<SegID> tunnel;
        search.push(start);
        Tag tunnelStart, tunnelEnd;

        Room room;
        auto& startSeg = level.GetSegment(start);

        while (!search.empty()) {
            auto segId = search.top();
            search.pop();

            auto& seg = level.GetSegment(segId);
            if (!SegmentIsTunnel(seg))
                segments.insert(segId);

            for (auto& side : SideIDs) {
                if (!seg.SideHasConnection(side)) continue; // nothing to do here

                auto connId = seg.GetConnection(side);
                if (segments.contains(connId)) continue;
                if (visited.contains(connId)) continue; // Another room is already using this

                if (Seq::contains(room.Portals, Tag{ segId, side })) {
                    SPDLOG_WARN("Tried adding a duplicate portal");
                    continue;
                }

                auto& cseg = level.GetSegment(connId);

                bool addPortal = false;
                if (auto wall = level.TryGetWall({ segId, side })) {
                    addPortal = true;
                    if (wall->Type == WallType::Open)
                        addPortal = false; // invisible walls

                    if (wall->Type == WallType::Illusion &&
                        seg.Type == SegmentType::Energy && startSeg.Type == SegmentType::Energy) {
                        addPortal = false; // don't split energy centers into separate rooms
                    }
                }

                addPortal |= cseg.Type != startSeg.Type; // new room if seg type changes

                // delay adding tunnels to the room
                if (SegmentIsTunnel(cseg)) {
                    if (tunnel.empty()) {
                        if (SegmentIsTunnel(seg)) tunnel.insert(segId);
                        tunnelStart = { segId, GetOppositeSide(side) };
                    }

                    // if the room size gets exceeded from a tunnel
                    if (segments.size() + tunnel.size() >= maxSegments) {
                        // Use the tunnel as the room
                        if (segments.size() < maxSegments / 3) {
                            Seq::insert(segments, tunnel);
                            //room.Portals.push_back({ segId, GetOppositeSide(side) });
                            room.AddPortal({ tunnelEnd });
                        }
                        else {
                            room.AddPortal({ tunnelStart });
                        }

                        room.Segments = Seq::ofSet(segments);
                        return room; // stop
                    }
                    tunnel.insert(connId);
                    tunnelEnd = { connId, side };
                }
                else {
                    // segment wasn't a tunnel, continue adding unless it was a portal
                    if (addPortal) {
                        room.AddPortal({ segId, side });
                        continue; // stop
                    }
                    else {
                        segments.insert(segId);
                    }
                }

                if (connId > SegID::None)
                    search.push(connId);
            }
        }

        room.Segments = Seq::ofSet(segments);
        return room;
    }


    struct SegmentNode {
        SegID Seg;
        int Connections;
        int Delta[6];
    };

    void AddPortalsToRoom(Level& level, span<Room> rooms, Room& room) {
        room.Portals.clear();

        for (auto& segId : room.Segments) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                auto conn = seg.GetConnection(sideId);
                if (conn <= SegID::None) continue;

                if (room.Contains(conn)) continue;
                auto roomId = FindRoomBySegment(rooms, conn);
                room.AddPortal({ segId, sideId, roomId });
            }
        }
    }

    List<Room> SubdivideRoom(Level& level, Room& room, int maxSegs) {
        if (room.Segments.size() < maxSegs) return {};

        List<SegmentNode> nodes(room.Segments.size());

        for (int i = 0; i < room.Segments.size(); i++) {
            auto segId = room.Segments[i];
            auto& seg = level.GetSegment(segId);
            int connections = 0;
            for (auto& conn : seg.Connections) {
                if (conn != SegID::None) connections++;
            }

            auto& node = nodes[i];
            node.Seg = segId;
            node.Connections = connections;
        }

        for (int i = 0; i < room.Segments.size(); i++) {
            auto& node = nodes[i];

            for (auto& sideId : SideIDs) {
                auto conn = level.GetConnectedSide({ node.Seg, sideId });
                if (!conn) continue;

                if (auto other = Seq::find(nodes, [conn](const SegmentNode& x) { return x.Seg == conn.Segment; })) {
                    node.Delta[(int)sideId] = other->Connections - node.Connections;
                }
            }
        }

        Room newRoom;
        SegID start = room.Segments[0];

        // starting at a portal, walk until max seg is reached and a large delta is found
        std::deque<SegID> search;
        Stack<Tag> splits; // After room is split, store segs on other side separately
        search.push_back(start);
        Set<SegID> visited;

        List<Room> rooms;

        auto canSearchSegment = [&visited, &room, &search](SegID segId) {
            if (segId <= SegID::None) return false;
            if (visited.contains(segId)) return false; // already visited
            if (Seq::contains(search, segId)) return false;
            if (!Seq::contains(room.Segments, segId)) return false; // only visit segs in this room
            return true;
        };

        while (!search.empty()) {
            auto segId = *search.begin();
            Seq::remove(search, segId);

            if (visited.contains(segId)) {
                if (!splits.empty()) {
                    Tag tag = splits.top();
                    splits.pop();
                    auto conn = level.GetConnectedSide(tag);
                    //newRoom.AddPortal({ conn });
                    search.push_front(conn.Segment);
                }

                continue; // already visited
            }

            assert(canSearchSegment(segId));
            if (!Seq::contains(room.Segments, segId)) continue; // only visit segs in this room

            // Update segment tracking
            newRoom.AddSegment(segId);
            visited.insert(segId);

            auto& seg = level.GetSegment(segId);

            if (Seq::find(nodes, [segId](const SegmentNode& x) { return x.Seg == segId; })) {
                for (int side = 0; side < 6; side++) {
                    auto conn = seg.GetConnection(SideID(side));
                    Tag tag = { segId, SideID(side) };

                    if (conn > SegID::None && !Seq::contains(room.Segments, conn) && !Seq::contains(newRoom.Portals, tag)) {
                        //newRoom.AddPortal({ tag }); // Connection to outside the room is a portal
                    }

                    if (conn > SegID::None && newRoom.Segments.size() + search.size() >= maxSegs /*&& std::abs(node->Delta[side]) == 0*//* && node->Connections == 2*/) {
                        auto& cseg = level.GetSegment(conn);
                        if (canSearchSegment(conn)) {
                            if (SegmentIsTunnel(cseg)) {
                                //newRoom.AddPortal({ tag }); // Insert a portal inside the room
                                splits.push(tag);
                            }
                            else {
                                search.push_front(conn);
                            }
                        }
                    }
                    else if (canSearchSegment(conn)) {
                        auto wall = level.TryGetWall(tag);
                        if (wall && WallIsPortal(*wall)) {
                            //newRoom.AddPortal({ tag }); // Insert a portal inside the room
                            splits.push(tag);
                        }
                        else {
                            search.push_front(conn);
                        }
                    }
                }
            }

            if (search.empty() && !splits.empty()) {
                if (/*!newRoom.Portals.empty() &&*/ !newRoom.Segments.empty()) {
                    for (auto& s : newRoom.Segments)
                        Seq::remove(room.Segments, s);

                    AddPortalsToRoom(level, rooms, newRoom);
                    rooms.push_back(newRoom);
                    newRoom = {};
                }

                Tag tag = splits.top();
                splits.pop();
                auto conn = level.GetConnectedSide(tag);
                //newRoom.AddPortal({ conn });
                search.push_front(conn.Segment);
            }
        }

        AddPortalsToRoom(level, rooms, newRoom);
        room = newRoom; // copy remaining segs back to room

        //SPDLOG_INFO("Split room into {} rooms", rooms.size());
        return rooms;
    }

    //bool CanNavigateThroughSide(Level& level, Tag tag) {
    //    if(!level.SegmentExists(tag)) return false;
    //    auto& seg = level.GetSegment(tag);
    //    if(!seg.SideHasConnection(tag.Side)) return false;
    //}

    bool IntersectCapsuleSide(Level& level, const BoundingCapsule& capsule, Tag tag) {
        auto face = Face::FromSide(level, tag);

        Vector3 ref, normal;
        float dist;

        auto poly0 = face.VerticesForPoly0();
        if (capsule.Intersects(poly0[0], poly0[1], poly0[2], face.Side.Normals[0], ref, normal, dist))
            return true;

        auto poly1 = face.VerticesForPoly1();
        if (capsule.Intersects(poly1[0], poly1[1], poly1[2], face.Side.Normals[1], ref, normal, dist))
            return true;

        return false;
    }

    // Breadth first execution. Execution stops if action returns true.
    bool FloodFill(Level& level, Room& room, SegID start, const std::function<bool(Tag)>& action) {
        Set<SegID> visited;
        Stack<SegID> search;
        assert(room.Contains(start));
        search.push(start);

        while (!search.empty()) {
            auto segId = search.top();
            search.pop();
            visited.insert(segId);

            auto& seg = level.GetSegment(segId);
            for (auto& sideId : SideIDs) {
                //auto& side = seg.GetSide(sideId);
                if (action({ segId, sideId }))
                    return true;

                auto conn = seg.GetConnection(sideId);
                if (!visited.contains(conn) && room.Contains(conn)) {
                    search.push(conn);
                }
            }
        }

        return false;
    }

    void Room::UpdateNavNodes(Level& level) {
        NavNodes.clear();

        if (Segments.empty()) return; // Nothing here!

        auto insertOrFindNode = [&level, this](Tag tag) {
            auto conn = level.GetConnectedSide(tag);

            for (size_t i = 0; i < NavNodes.size(); i++) {
                auto& node = NavNodes[i];
                if (node.Tag == tag || node.Tag == conn)
                    return (int)i;
            }

            // Node wasn't in list, insert a new one
            auto& node = NavNodes.emplace_back();
            node.Position = level.GetSide(tag).Center;
            node.Tag = tag;
            return int(NavNodes.size() - 1);
        };

        auto findNode = [this](SegID seg) {
            for (size_t i = 0; i < NavNodes.size(); i++) {
                auto& node = NavNodes[i];
                if (node.Segment == seg)
                    return (int)i;
            }

            return -1;
        };

        auto insertNode = [&level, this](SegID seg) {
            auto& node = NavNodes.emplace_back();
            node.Position = level.GetSegment(seg).Center;
            node.Segment = seg;
            //node.Tag = tag;
            return int(NavNodes.size() - 1);
        };

        // Add nodes and connections between segment sides
        //for (auto& segId : Segments) {
        //    if (!level.SegmentExists(segId)) continue;
        //    auto& seg = level.GetSegment(segId);

        //    auto& node = NavNodes.emplace_back();
        //    node.Position = seg.Center;
        //    node.Segment = segId;


        //    node.Tag = tag;
        //    return int(NavNodes.size() - 1);

        //    for (auto& sideId : SideIDs) {
        //        if (!seg.SideHasConnection(sideId)) continue; // skip solid walls

        //        Tag tag = { segId, sideId };
        //        auto newIndex = Seq::indexOf(Segments, segId);
        //        auto newIndex = insertNode(segId);

        //        for (auto& otherSideId : SideIDs) {
        //            if (otherSideId == sideId) continue; // skip self
        //            if (!seg.SideHasConnection(otherSideId)) continue; // skip solid walls


        //            //auto connNodeIndex = insertOrFindNode({ segId, otherSideId }); // will get from other side
        //            // Be sure to use array index here, otherwise adding the above node will invalidate the reference
        //            NavNodes[newIndex].Connections.push_back(connNodeIndex);
        //        }
        //    }
        //}

        for (auto& segId : Segments) {
            if (!level.SegmentExists(segId)) continue;
            auto& seg = level.GetSegment(segId);

            auto& node = NavNodes.emplace_back();
            node.Position = seg.Center;
            node.Segment = segId;
        }

        List<NavigationNode> intermediateNodes;

        for (int i = 0; i < NavNodes.size(); i++) {
            auto& node = NavNodes[i];
            auto& seg = level.GetSegment(node.Segment);
            for (auto& sideId : SideIDs) {
                auto connId = seg.GetConnection(sideId);
                auto connection = findNode(connId);
                if (connection == -1) continue;

                auto& connSeg = level.GetSegment(connId);
                BoundingCapsule capsule(node.Position, connSeg.Center, NAV_OBJECT_RADIUS);

                // Check if connection to node intersects
                bool intersect = false;
                for (auto& sideId2 : SideIDs) {
                    if (seg.SideHasConnection(sideId2)) continue;
                    if (IntersectCapsuleSide(level, capsule, { node.Segment, sideId2 })) {
                        intersect = true;
                        break;
                    }
                }

                if (intersect) {
                    auto intermediateIndex = NavNodes.size() + intermediateNodes.size();
                    // insert an intermediate node on the joining side
                    NavigationNode intermediate{ .Position = seg.GetSide(sideId).Center };
                    intermediate.Connections.push_back(i);
                    intermediate.Connections.push_back(connection);
                    intermediateNodes.push_back(intermediate);

                    node.Connections.push_back(intermediateIndex);
                    NavNodes[connection].Connections.push_back(intermediateIndex);

                } else {
                    node.Connections.push_back(connection);
                }
            }
        }

        Seq::append(NavNodes, intermediateNodes);

        // todo: maybe add new nodes at segment centers? or split long connections?
        //return;

        // Add new connections between visible nodes
        for (int i = 0; i < NavNodes.size(); i++) {
            auto& node = NavNodes[i];
            //bool isPortal = IsPortal(node.Tag);
            if(node.Segment == SegID::None) continue; // don't insert connections to intermediates

            for (int j = 0; j < NavNodes.size(); j++) {
                if (i == j) continue; // skip self
                if (Seq::contains(node.Connections, j)) continue; // Already has connection

                auto& other = NavNodes[j];
                auto otherIsPortal = IsPortal(other.Tag);
                auto dir = other.Position - node.Position;
                //auto maxDist = dir.Length();
                dir.Normalize();
                //Ray ray = { node.Position, dir };

                if (/*isPortal ||*/ otherIsPortal) {
                    // if this node is a portal, don't connect to nodes behind because other portals might be facing it

                    //if (isPortal && dir.Dot(level.GetSide(node.Tag).AverageNormal) <= 0)
                    //    continue; // direction towards portal face, skip it

                    if (otherIsPortal && dir.Dot(level.GetSide(other.Tag).AverageNormal) <= 0)
                        continue; // direction towards portal face, skip it
                }

                //bool blocked = false;

                BoundingCapsule capsule(node.Position, other.Position, NAV_OBJECT_RADIUS);

                auto blocked = FloodFill(level, *this, node.Segment, [this, &level, &capsule, &node](Tag tag) {
                    auto& seg = level.GetSegment(tag);

                    if (node.Tag == tag) return false; // don't hit test self
                    if (seg.SideHasConnection(tag.Side) && !IsPortal(tag))
                        return false; // skip open sides, but only if they aren't portals

                    return IntersectCapsuleSide(level, capsule, tag);
                });

                //for (auto& segId : Segments) {
                //    if (!level.SegmentExists(segId)) continue;
                //    auto& seg = level.GetSegment(segId);
                //    for (auto& sideId : SideIDs) {
                //        if (seg.SideHasConnection(sideId) && !IsPortal({ segId, sideId }))
                //            continue; // skip open sides, but only if they aren't portals

                //        if (IntersectCapsuleSide(level, capsule, { segId, sideId })) {
                //            //if (IntersectRaySegment(level, ray, segId, maxDist, false, false, nullptr)) {
                //            blocked = true;
                //            break;
                //        }

                //        //auto face = Face::FromSide(level, seg, sideId);

                //        //// Check 4 additional rays, 1 in each edge direction
                //        //for (int edge = 0; edge < 4; edge++) {
                //        //    auto edgeDir = face.GetEdgeMidpoint(edge) - face.Center();
                //        //    edgeDir.Normalize();
                //        //    Ray offsetRay = { node.Position + edgeDir * NAV_OBJECT_RADIUS, dir };

                //        //    if (IntersectRaySegment(level, offsetRay, segId, maxDist, false, false, nullptr)) {
                //        //        blocked = true;
                //        //        break;
                //        //    }
                //        //}
                //    }
                //}

                if (blocked) continue;
                node.Connections.push_back(j);
            }
        }
    }

    RoomID FindRoomBySegment(span<Room> rooms, SegID seg) {
        for (int i = 0; i < rooms.size(); i++) {
            if (Seq::contains(rooms[i].Segments, seg)) return RoomID(i);
        }

        return RoomID::None;
    }

    void MergeSmallRoom(Level& level, List<Room>& rooms, Room& room, int minSize) {
        if (room.Segments.size() > minSize) return;
        if (room.Type == SegmentType::Energy || room.Type == SegmentType::Repair)
            return; // Don't merge energy centers

        Room* mergedNeighbor = nullptr;

        for (auto& portal : room.Portals) {
            if (level.TryGetWall(portal))
                continue; // Don't merge a wall

            // Wasn't a wall, find the owning room and merge into it
            auto connection = level.GetConnectedSide(portal);
            if (level.TryGetWall(connection))
                continue; // Other side had a wall (check for one-sided walls)

            auto roomId = FindRoomBySegment(rooms, connection.Segment);
            if (roomId != RoomID::None) {
                auto neighbor = &rooms[(int)roomId];
                // In rare cases a room can be surrounded by another room on multiple sides.
                // Check that we are merging into the same room.
                if (mergedNeighbor && neighbor != mergedNeighbor) continue;
                if (!mergedNeighbor) mergedNeighbor = neighbor;

                Seq::append(neighbor->Segments, room.Segments);
                break;
                // Move other portals to the merged room
                //for (auto& p : room.Portals) {
                //    if (!neighbor->Contains(p.Segment))
                //        neighbor->Portals.push_back(p);
                //    //if (p != portal) neighbor->Portals.push_back(p);
                //}

                // Check if any neighbor portals point to this room and remove them
                //List<Tag> portalsToRemove;
                //for (auto& p : neighbor->Portals) {
                //    auto conn = level.GetConnectedSide(p);
                //    if (room.Contains(conn.Segment))
                //        portalsToRemove.push_back(p);
                //}

                //for (auto& p : portalsToRemove) {
                //    Seq::remove(neighbor->Portals, p);
                //}
            }
        }

        if (mergedNeighbor) {
            room.Segments = {};
            AddPortalsToRoom(level, rooms, *mergedNeighbor);
        }
    }

    void RemoveEmptyRooms(List<Room>& rooms) {
        // Sort empty rooms to end and remove them. Rooms can be empty after splitting.
        Seq::sortBy(rooms, [](const Room& a, const Room& b) { return a.Segments.size() > b.Segments.size(); });
        if (auto index = Seq::findIndex(rooms, [](const Room& room) { return room.Segments.size() == 0; }))
            rooms.resize(*index);
    }

    List<Room> CreateRooms(Level& level) {
        Set<SegID> visited;
        List<Room> rooms;

        Stack<SegID> search;
        search.push(SegID(0));

        while (!search.empty()) {
            auto id = search.top();
            search.pop();
            if (visited.contains(id)) continue; // already visited

            auto room = CreateRoom(level, id);

            // Add connections
            for (auto& portal : room.Portals) {
                auto& seg = level.GetSegment(portal);
                auto conn = seg.GetConnection(portal.Side);
                assert(conn != SegID::None);
                search.push(conn);
            }

            Seq::insert(visited, room.Segments);
            rooms.push_back(std::move(room));
        }

        List<Room> newRooms;
        for (auto& room : rooms) {
            auto subdivisions = SubdivideRoom(level, room, 10);
            Seq::append(newRooms, subdivisions);
        }

        Seq::append(rooms, newRooms);

        RemoveEmptyRooms(rooms);

        // Merge small rooms into adjacent rooms
        for (auto& room : rooms)
            MergeSmallRoom(level, rooms, room, 2);

        // Do a second pass as circular tunnels can cause isolated rooms
        //for (auto& room : rooms)
        //    MergeSmallRoom(level, rooms, room, 2);

        RemoveEmptyRooms(rooms);


        Stopwatch timer;

        Set<SegID> usedSegments;
        for (auto& room : rooms) {
            AddPortalsToRoom(level, rooms, room);

            for (auto& segID : room.Segments) {
                assert(!usedSegments.contains(segID));
                usedSegments.insert(segID);
                room.Center += level.GetSegment(segID).Center;
            }

            room.Center /= (float)room.Segments.size();
            room.UpdatePortalDistances(level);
            //room.UpdateNavNodes(level);
        }

        SPDLOG_WARN("Update room nav nodes in {}", timer.GetElapsedSeconds());
        return rooms;
    }

    List<SegID> NavigationNetwork::NavigateTo(SegID start, SegID goal, LevelRooms& rooms, Level& level) {
        auto startRoom = rooms.GetRoom(start);
        auto endRoom = rooms.GetRoom(goal);
        if (!startRoom || !endRoom)
            return {}; // Rooms don't exist

        if (startRoom == endRoom)
            return NavigateWithinRoom(start, goal, *endRoom); // Start and goal are in same room

        List<SegID> path;
        auto roomStartSeg = start;
        auto roomPath = NavigateAcrossRooms(rooms.FindBySegment(start), rooms.FindBySegment(goal), rooms, level);

        // starting at the first room, use the closest portal that matches the next room
        for (int i = 0; i < roomPath.size(); i++) {
            auto room = rooms.GetRoom(roomStartSeg);

            if (room == endRoom || i + 1 >= roomPath.size()) {
                auto localPath = NavigateWithinRoom(roomStartSeg, goal, *endRoom);
                Seq::append(path, localPath);
            }
            else {
                // Not yet to final room
                float closestPortal = FLT_MAX;
                Tag bestPortal;
                auto& seg = level.GetSegment(roomStartSeg);

                for (int portalIndex = 0; portalIndex < room->Portals.size(); portalIndex++) {
                    auto& portal = room->Portals[portalIndex];
                    if (portal.Room != roomPath[i + 1])
                        continue; // Portal doesn't connect to next room in the path

                    auto& portalSide = level.GetSide(portal);
                    auto distance = Vector3::DistanceSquared(seg.Center, portalSide.Center);
                    if (distance < closestPortal) {
                        closestPortal = distance;
                        bestPortal = Tag(portal);
                    }
                }

                if (!bestPortal) break; // Pathfinding to next portal failed

                auto localPath = NavigateWithinRoom(roomStartSeg, bestPortal.Segment, *room);
                Seq::append(path, localPath);

                // Use the portal connection as the start for the next room
                roomStartSeg = level.GetConnectedSide(bestPortal).Segment;
            }
        }

        return path;
    }

    List<RoomID> NavigationNetwork::NavigateAcrossRooms(RoomID start, RoomID goal, LevelRooms& rooms, Level& level) {
        if (start == goal) return { start };

        // Reset traversal state
        //auto startRoom = rooms.GetRoom(start);
        auto goalRoom = rooms.GetRoom(goal);

        for (int i = 0; i < rooms.Rooms.size(); i++) {
            auto& room = rooms.Rooms[i];
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
            auto room = &rooms.Rooms[current->Index];

            for (auto& portal : room->Portals) {
                auto& segNode = _segmentNodes[(int)portal.Segment];
                auto& nodeSide = segNode.Sides[(int)portal.Side];
                if (nodeSide.Connection <= SegID::None) continue;
                if (nodeSide.Blocked) continue;

                auto& neighbor = _traversalBuffer[(int)portal.Room];

                if (!neighbor.Visited)
                    queue.push_back(&neighbor);

                auto& portalSide = level.GetSide(portal);

                // If portal connects to goal room use distance 0 and not distance between centers
                //
                // This heuristic could be improved by taking the distance between the entrance
                // and exit portals instead of the room centers.
                auto localDistance = Vector3::DistanceSquared(room->Center, portalSide.Center);
                float localGoal = portal.Room == goal ? current->LocalGoal : current->LocalGoal + localDistance;

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
                if (nodeSide.Blocked) continue;
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

    //List<Vector3> NavigationNetwork::NavigateWithinRoom(SegID start, SegID goal, Room& room) {
    //    if (!room.Contains(start) || !room.Contains(goal))
    //        return {}; // No direct solution. Programming error

    //    _traversalBuffer.resize(room.NavNodes.size());


    //    auto& goalSeg = Game::Level.GetSegment(goal);
    //    auto& startSeg = Game::Level.GetSegment(start);

    //    auto startIndex = room.FindClosestNode(startSeg.Center);
    //    auto endIndex = room.FindClosestNode(goalSeg.Center);

    //    // Reset traversal state
    //    for (int i = 0; i < _traversalBuffer.size(); i++) {
    //        _traversalBuffer[i] = {
    //            .Index = i,
    //            .GoalDistance = Vector3::DistanceSquared(room.NavNodes[i].Position, goalSeg.Center)
    //        };
    //    }

    //    std::list<TraversalNode*> queue;
    //    _traversalBuffer[startIndex].LocalGoal = 0;
    //    queue.push_back(&_traversalBuffer[startIndex]);

    //    List<TraversalNode*> navNodes;

    //    while (!queue.empty()) {
    //        queue.sort([](const TraversalNode* a, const TraversalNode* b) {
    //            return a->GoalDistance < b->GoalDistance;
    //        });

    //        if (!queue.empty() && queue.front()->Visited)
    //            queue.pop_front();

    //        if (queue.empty())
    //            break; // no nodes left

    //        auto& current = queue.front();
    //        current->Visited = true;
    //        auto& node = room.NavNodes[current->Index];

    //        for (auto& connection : node.Connections) {
    //            navNodes.push_back(&_traversalBuffer[connection]);
    //        }

    //        Seq::sortBy(navNodes, [](const TraversalNode* a, const TraversalNode* b) {
    //            return a->GoalDistance < b->GoalDistance;
    //        });

    //        // only scan the 6 closest nodes
    //        for (int i = 0; i < std::min((int)navNodes.size(), 6); i++) {
    //            auto& connection = navNodes[i];
    //            //}

    //            //for (auto& connection : node.Connections) {
    //            //auto& nodeSide = node.Tag;
    //            //auto& connId = nodeSide.Connection;
    //            //if (node.Tag.Segment <= SegID::None) continue;
    //            //if (nodeSide.Blocked) continue; // todo: query portals. but should we? should never even reach room
    //            //if (!room.Contains(node.Tag.Segment)) continue; // Only search segments in this room

    //            auto& neighborNode = room.NavNodes[connection->Index];
    //            auto& neighbor = _traversalBuffer[connection->Index];

    //            //if(!Seq::contains(queue, neighbor))
    //            //    queue.push_back(&neighbor);
    //            if (!neighbor.Visited)
    //                queue.push_back(&neighbor);

    //            float localGoal = current->LocalGoal + Vector3::DistanceSquared(node.Position, neighborNode.Position);

    //            if (localGoal < neighbor.LocalGoal) {
    //                neighbor.Parent = current->Index;
    //                neighbor.LocalGoal = localGoal;
    //                neighbor.GoalDistance = neighbor.LocalGoal + Vector3::DistanceSquared(neighborNode.Position, goalSeg.Center);
    //            }
    //        }
    //    }

    //    // add nodes along the path starting at the goal
    //    auto* trav = &_traversalBuffer[endIndex];
    //    List<Vector3> path;

    //    while (trav) {
    //        path.push_back(room.NavNodes[trav->Index].Position);
    //        trav = trav->Parent >= 0 ? &_traversalBuffer[trav->Parent] : nullptr;
    //    }
    //    //}

    //    ranges::reverse(path);

    //    // Walk backwards, using the parent
    //    return path;
    //}
}
