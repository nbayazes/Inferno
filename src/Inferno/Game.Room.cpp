#include "pch.h"
#include <spdlog/spdlog.h>
#include "Game.Room.h"
#include "Face.h"
#include "Game.Segment.h"
#include "Game.Visibility.h"
#include "Graphics.Debug.h"
#include "Physics.Capsule.h"
#include "Physics.h"
#include "ScopedTimer.h"

namespace Inferno::Game {
    constexpr auto NAV_OBJECT_RADIUS = 4.0f; // expected object radius to follow a navigation path

    Room* GetRoom(List<Room>& rooms, RoomID id) {
        if (!Seq::inRange(rooms, (int)id)) return nullptr;
        return &rooms[(int)id];
    }

    const Room* GetRoom(const List<Room>& rooms, RoomID id) {
        if (!Seq::inRange(rooms, (int)id)) return nullptr;
        return &rooms[(int)id];
    }

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

        Room room{};
        if (level.Segments.empty()) return room;
        auto& startSeg = level.GetSegment(start);

        while (!search.empty()) {
            auto segId = search.top();
            search.pop();

            auto& seg = level.GetSegment(segId);
            if (seg.Type == SegmentType::Energy || seg.Type == SegmentType::Repair)
                room.Type = seg.Type; // Mark energy centers

            segments.insert(segId);

            for (auto& side : SIDE_IDS) {
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

                if (auto wall = level.GetConnectedWall({ segId, side }))
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

            for (auto& side : SIDE_IDS) {
                if (!seg.SideHasConnection(side)) continue; // nothing to do here

                auto connId = seg.GetConnection(side);
                if (segments.contains(connId)) continue;
                if (visited.contains(connId)) continue; // Another room is already using this

                //if (Seq::contains(room.Portals, Tag{ segId, side })) {
                //    SPDLOG_WARN("Tried adding a duplicate portal");
                //    continue;
                //}

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

    RoomID FindRoomBySegment(span<Room> rooms, SegID seg) {
        for (int i = 0; i < rooms.size(); i++) {
            if (Seq::contains(rooms[i].Segments, seg)) return RoomID(i);
        }

        return RoomID::None;
    }

    void UpdatePortalLinks(const Level& level, List<Room>& rooms) {
        int id = 0;

        for (auto& room : rooms) {
            for (auto& portal : room.Portals) {
                ASSERT(Seq::inRange(rooms, (int)portal.RoomLink));
                auto& croom = rooms[(int)portal.RoomLink];
                auto conn = level.GetConnectedSide(portal.Tag);
                portal.PortalLink = croom.GetPortalIndex(conn);
                auto cportal = croom.GetPortal(conn);
                if (cportal) {
                    if (cportal->Id == -1 && portal.Id == -1)
                        cportal->Id = portal.Id = id++;

                    ASSERT(cportal->Id == portal.Id);
                }

                ASSERT(portal.PortalLink != -1);
                ASSERT(portal.Id != -1);
            }
        }
    }

    void AddPortalsToRoom(Level& level, span<Room> rooms, Room& room) {
        room.Portals.clear();

        for (auto& segId : room.Segments) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SIDE_IDS) {
                auto conn = seg.GetConnection(sideId);
                if (conn <= SegID::None) continue;

                if (room.Contains(conn)) continue;
                auto roomId = FindRoomBySegment(rooms, conn);
                ASSERT(roomId != RoomID::None);
                room.AddPortal({ segId, sideId, roomId });
                //auto roomId = FindRoomBySegment(rooms, conn.Segment);
                //if (roomId != RoomID::None) {
                //    auto link = rooms[(int)roomId].GetPortalIndex(conn);
                //    ASSERT(link != -1);
                //    room.AddPortal({ segId, sideId, roomId, link });
                //}
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

            for (auto& sideId : SIDE_IDS) {
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
                if (!newRoom.Segments.empty()) {
                    for (auto& s : newRoom.Segments)
                        Seq::remove(room.Segments, s);

                    //AddPortalsToRoom(level, rooms, newRoom);
                    rooms.push_back(newRoom);
                    newRoom = {};
                }

                Tag tag = splits.top();
                splits.pop();
                auto conn = level.GetConnectedSide(tag);
                search.push_front(conn.Segment);
            }
        }

        //AddPortalsToRoom(level, rooms, newRoom);
        room = newRoom; // copy remaining segs back to room

        //SPDLOG_INFO("Split room into {} rooms", rooms.size());
        return rooms;
    }

    // Splits isolated segments into separate lists
    List<List<SegID>> SplitIsolatedSegments(const Level& level, span<SegID> source) {
        if (source.empty()) return {};
        List<SegID> visited;
        List<List<SegID>> results;

        while (visited.size() != source.size()) {
            Set<SegID> segments;
            Stack<SegID> search;
            auto start = SegID::None;
            for (auto& segid : source) {
                if (!Seq::contains(visited, segid)) {
                    start = segid;
                    break;
                }
            }

            ASSERT(start != SegID::None);
            search.push(start);
            visited.push_back(start);

            while (!search.empty()) {
                auto segId = search.top();
                search.pop();

                auto seg = level.TryGetSegment(segId);
                if (!seg) continue;
                segments.insert(segId);

                for (auto& side : SIDE_IDS) {
                    auto connId = seg->GetConnection(side);
                    if (connId != SegID::None &&
                        //!segments.contains(connId) &&
                        !Seq::contains(visited, connId) &&
                        Seq::contains(source, connId)) {
                        ASSERT(!Seq::contains(visited, connId));
                        search.push(connId);
                        visited.push_back(connId);
                    }
                }
            }

            results.push_back(Seq::ofSet(segments));
        }

        return results;
    }

    // Splits a large room in half
    List<Room> SubdivideLargeRoom(Level& level, Room& room, int maxSegs) {
        if (room.Segments.size() < maxSegs || room.Type != SegmentType::None)
            return {};

        auto bounds = room.GetBounds(level);
        int axis = 0;
        float maxValue = -FLT_MAX;
        Array<float, 3> extents = { bounds.Extents.x, bounds.Extents.y, bounds.Extents.z };
        for (int i = 0; i < 3; i++) {
            if (extents[i] > maxValue) {
                maxValue = extents[i];
                axis = i;
            }
        }

        Vector3 normal = Vector3::UnitX;
        if (axis == 1) normal = Vector3::UnitY;
        if (axis == 2) normal = Vector3::UnitZ;
        Plane plane(bounds.Center, normal);

        List<SegID> roomSegments, otherSegments;

        for (auto& segid : room.Segments) {
            if (auto seg = level.TryGetSegment(segid)) {
                if (plane.DotCoordinate(seg->Center) > 0) {
                    otherSegments.push_back(segid);
                }
                else {
                    roomSegments.push_back(segid);
                }
            }
        }

        List<Room> rooms;

        auto splitSegs = SplitIsolatedSegments(level, roomSegments);
        Seq::append(splitSegs, SplitIsolatedSegments(level, otherSegments));

        size_t segCheck = 0;

        for (auto& segs : splitSegs) {
            Room newRoom{};
            newRoom.Segments = segs;
            rooms.push_back(newRoom);
            segCheck += segs.size();
        }

        ASSERT(segCheck == room.Segments.size());
        room.Segments = {}; // empty the original room
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
        float dist{};

        for (int i = 0; i < 2; i++) {
            auto poly = face.GetPoly(i);
            if (capsule.Intersects(poly[0], poly[1], poly[2], face.Side.Normals[i], ref, normal, dist))
                return true;
        }

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
            for (auto& sideId : SIDE_IDS) {
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

    void UpdatePortalDistances(Level& level, Room& room) {
        room.PortalDistances.resize(room.Portals.size());

        for (int i = 0; i < room.Portals.size(); i++) {
            room.PortalDistances[i].resize(room.Portals.size());

            auto& a = level.GetSide(room.Portals[i].Tag);
            for (int j = 0; j < room.Portals.size(); j++) {
                auto& b = level.GetSide(room.Portals[j].Tag);
                room.PortalDistances[i][j] = Vector3::Distance(a.Center, b.Center);
            }
        }
    }

    void UpdateNavNodes(Level& level, Room& room) {
        room.NavNodes.clear();

        if (room.Segments.empty()) return; // Nothing here!

        //auto insertOrFindNode = [&level, &room](Tag tag) {
        //    auto conn = level.GetConnectedSide(tag);

        //    for (size_t i = 0; i < room.NavNodes.size(); i++) {
        //        auto& node = room.NavNodes[i];
        //        if (node.Tag == tag || node.Tag == conn)
        //            return (int)i;
        //    }

        //    // Node wasn't in list, insert a new one
        //    auto& node = room.NavNodes.emplace_back();
        //    node.Position = level.GetSide(tag).Center;
        //    node.Tag = tag;
        //    return int(room.NavNodes.size() - 1);
        //};

        auto findNode = [&room](SegID seg) {
            for (size_t i = 0; i < room.NavNodes.size(); i++) {
                auto& node = room.NavNodes[i];
                if (node.Segment == seg)
                    return (int)i;
            }

            return -1;
        };

        //auto insertNode = [&level, &room](SegID seg) {
        //    auto& node = room.NavNodes.emplace_back();
        //    node.Position = level.GetSegment(seg).Center;
        //    node.Segment = seg;
        //    //node.Tag = tag;
        //    return int(room.NavNodes.size() - 1);
        //};

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

        for (auto& segId : room.Segments) {
            if (!level.SegmentExists(segId)) continue;
            auto& seg = level.GetSegment(segId);

            auto& node = room.NavNodes.emplace_back();
            node.Position = seg.Center;
            node.Segment = segId;
        }

        List<NavigationNode> intermediateNodes;

        for (int i = 0; i < room.NavNodes.size(); i++) {
            auto& node = room.NavNodes[i];
            auto& seg = level.GetSegment(node.Segment);
            for (auto& sideId : SIDE_IDS) {
                auto connId = seg.GetConnection(sideId);
                auto connection = findNode(connId);
                if (connection == -1) continue;

                auto& connSeg = level.GetSegment(connId);
                BoundingCapsule capsule(node.Position, connSeg.Center, NAV_OBJECT_RADIUS);

                // Check if connection to node intersects
                bool intersect = false;
                for (auto& sideId2 : SIDE_IDS) {
                    if (seg.SideHasConnection(sideId2)) continue;
                    if (IntersectCapsuleSide(level, capsule, { node.Segment, sideId2 })) {
                        intersect = true;
                        break;
                    }
                }

                if (intersect) {
                    auto intermediateIndex = room.NavNodes.size() + intermediateNodes.size();
                    // insert an intermediate node on the joining side
                    NavigationNode intermediate{ .Position = seg.GetSide(sideId).Center };
                    intermediate.Connections.push_back(i);
                    intermediate.Connections.push_back(connection);
                    intermediateNodes.push_back(intermediate);

                    node.Connections.push_back((int)intermediateIndex);
                    room.NavNodes[connection].Connections.push_back((int)intermediateIndex);
                }
                else {
                    node.Connections.push_back(connection);
                }
            }
        }

        Seq::append(room.NavNodes, intermediateNodes);

        // todo: maybe add new nodes at segment centers? or split long connections?
        //return;

        // Add new connections between visible nodes
        //for (int i = 0; i < room.NavNodes.size(); i++) {
        //    auto& node = room.NavNodes[i];
        //    //bool isPortal = IsPortal(node.Tag);
        //    if (node.Segment == SegID::None) continue; // don't insert connections to intermediates

        //    for (int j = 0; j < room.NavNodes.size(); j++) {
        //        if (i == j) continue; // skip self
        //        if (Seq::contains(node.Connections, j)) continue; // Already has connection

        //        auto& other = room.NavNodes[j];
        //        auto otherIsPortal = room.IsPortal(other.Tag);
        //        auto dir = other.Position - node.Position;
        //        //auto maxDist = dir.Length();
        //        dir.Normalize();
        //        //Ray ray = { node.Position, dir };

        //        if (/*isPortal ||*/ otherIsPortal) {
        //            // if this node is a portal, don't connect to nodes behind because other portals might be facing it

        //            //if (isPortal && dir.Dot(level.GetSide(node.Tag).AverageNormal) <= 0)
        //            //    continue; // direction towards portal face, skip it

        //            if (otherIsPortal && dir.Dot(level.GetSide(other.Tag).AverageNormal) <= 0)
        //                continue; // direction towards portal face, skip it
        //        }

        //        //bool blocked = false;

        //        BoundingCapsule capsule(node.Position, other.Position, NAV_OBJECT_RADIUS);

        //        auto blocked = FloodFill(level, room, node.Segment, [&room, &level, &capsule, &node](Tag tag) {
        //            auto& seg = level.GetSegment(tag);

        //            if (node.Tag == tag) return false; // don't hit test self
        //            if (seg.SideHasConnection(tag.Side) && !room.IsPortal(tag))
        //                return false; // skip open sides, but only if they aren't portals

        //            return IntersectCapsuleSide(level, capsule, tag);
        //        });

        //        //for (auto& segId : Segments) {
        //        //    if (!level.SegmentExists(segId)) continue;
        //        //    auto& seg = level.GetSegment(segId);
        //        //    for (auto& sideId : SideIDs) {
        //        //        if (seg.SideHasConnection(sideId) && !IsPortal({ segId, sideId }))
        //        //            continue; // skip open sides, but only if they aren't portals

        //        //        if (IntersectCapsuleSide(level, capsule, { segId, sideId })) {
        //        //            //if (IntersectRaySegment(level, ray, segId, maxDist, false, false, nullptr)) {
        //        //            blocked = true;
        //        //            break;
        //        //        }

        //        //        //auto face = Face::FromSide(level, seg, sideId);

        //        //        //// Check 4 additional rays, 1 in each edge direction
        //        //        //for (int edge = 0; edge < 4; edge++) {
        //        //        //    auto edgeDir = face.GetEdgeMidpoint(edge) - face.Center();
        //        //        //    edgeDir.Normalize();
        //        //        //    Ray offsetRay = { node.Position + edgeDir * NAV_OBJECT_RADIUS, dir };

        //        //        //    if (IntersectRaySegment(level, offsetRay, segId, maxDist, false, false, nullptr)) {
        //        //        //        blocked = true;
        //        //        //        break;
        //        //        //    }
        //        //        //}
        //        //    }
        //        //}

        //        if (blocked) continue;
        //        node.Connections.push_back(j);
        //    }
        //}
    }

    void MergeSmallRoom(Level& level, List<Room>& rooms, Room& room, int minSize) {
        if (room.Segments.size() > minSize) return;
        if (room.Type == SegmentType::Energy || room.Type == SegmentType::Repair)
            return; // Don't merge energy centers

        Room* mergedNeighbor = nullptr;

        AddPortalsToRoom(level, rooms, room); // Refresh portals

        for (auto& portal : room.Portals) {
            if (level.TryGetWall(portal.Tag))
                continue; // Don't merge a wall

            // Wasn't a wall, find the owning room and merge into it
            auto connection = level.GetConnectedSide(portal.Tag);
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

    // 
    using PortalProjection = Array<Ray, 4>;

    void SegmentPlaneIntersection(const Plane& plane, const Vector3& p1, const Vector3& p2, List<Vector3>& points) {
        constexpr float EPSILON = 0.01f;
        float d1 = plane.DotCoordinate(p1); // Distance from plane
        float d2 = plane.DotCoordinate(p2); // Distance from plane

        float p1OnPlane = abs(d1) < EPSILON;
        float p2OnPlane = abs(d2) < EPSILON;

        if (p1OnPlane) points.push_back(p1);
        if (p2OnPlane) points.push_back(p2);
        if (p1OnPlane && p2OnPlane) return;

        // Logic for only plane intersection
        //if (d1 * d2 > EPSILON) return; // points on the same side of plane
        //float t = d1 / (d1 - d2); // position of intersection on segment
        //points.push_back(p1 + t * (p2 - p1));

        if (d1 * d2 <= EPSILON) {
            // points on opposite sides of plane
            float t = d1 / (d1 - d2); // position of intersection on segment
            points.push_back(p1 + t * (p2 - p1));
        }
        else {
            // include points that are in front of the plane
            if (d1 > 0) points.push_back(p1);
            if (d2 > 0) points.push_back(p2);
        }
    }

    List<Vector3> FacePlaneIntersection(const Face& face, const Plane& plane) {
        List<Vector3> points;
        SegmentPlaneIntersection(plane, face[0], face[1], points);
        SegmentPlaneIntersection(plane, face[1], face[2], points);
        SegmentPlaneIntersection(plane, face[2], face[3], points);
        SegmentPlaneIntersection(plane, face[3], face[0], points);
        //Seq::distinct(points); // todo: distinct

        return points;
    }

    // Clips a convex polygon's points behind a plane
    List<Vector3> ClipConvexPolygon(const List<Vector3>& points, const Plane& plane) {
        // clip each segment and update the available points
        List<Vector3> result;

        for (int i = 0; i < points.size(); i++) {
            //constexpr float EPSILON = 0.01f;
            auto& p1 = points[i];
            auto& p2 = points[(i + 1) % points.size()];
            float d1 = plane.DotCoordinate(p1); // Distance from plane
            float d2 = plane.DotCoordinate(p2); // Distance from plane
            //d1 = DistanceFromPlane(p1, plane.Normal() * plane.D(), plane.Normal());
            //d2 = DistanceFromPlane(p2, plane.Normal() * plane.D(), plane.Normal());

            //bool p1OnPlane = abs(d1) < EPSILON;
            //bool p2OnPlane = abs(d2) < EPSILON;

            //if (p1OnPlane) result.push_back(p1);
            //if (p2OnPlane) result.push_back(p2);
            //if (p1OnPlane && p2OnPlane) continue;

            //if (d1 > 0) result.push_back(p1); // first point in front of plane

            if (d1 * d2 < 0 /*EPSILON*/) {
                // points on opposite sides of plane
                float t = d1 / (d1 - d2); // position of intersection on segment
                result.push_back(p1 + t * (p2 - p1));
            }

            if (d2 >= 0)
                result.push_back(p2); // second point in front of plane
        }

        return result;
    }

    FaceInfo GetFaceBounds(span<Vector3> faceVerts, const Vector3& normal) {
        // unrotate face verts to xy plane
        auto transform = Matrix(VectorToRotation(normal));
        transform = transform.Transpose(); // invert rotation
        Vector3 center;
        Array<Vector3, 4> verts; // Max of 4 verts per face
        auto vertCount = std::min(4, (int)faceVerts.size());

        for (int i = 0; i < vertCount; i++) {
            verts[i] = faceVerts[i];
            center += verts[i];
        }

        center /= (float)faceVerts.size();

        for (int i = 0; i < vertCount; i++) {
            auto& v = verts[i];
            v = Vector3::Transform(v - center, transform);
        }

        // Find left most point
        int xMinPoint = -1;
        float xMin = FLT_MAX;

        for (int i = 0; i < verts.size(); i++) {
            if (verts[i].x < xMin) {
                xMinPoint = i;
                xMin = verts[i].x;
            }
        }

        ASSERT(xMinPoint != -1);

        // Find top most point
        int yMaxPoint = -1;
        float yMax = -FLT_MAX;

        for (int i = 0; i < verts.size(); i++) {
            if (verts[i].y > yMax) {
                yMaxPoint = i;
                yMax = verts[i].y;
            }
        }

        ASSERT(yMaxPoint != -1);

        // Find right most point
        int xMaxPoint = -1;
        float xMax = -FLT_MAX;

        for (int i = 0; i < verts.size(); i++) {
            if (verts[i].x > xMax) {
                xMaxPoint = i;
                xMax = verts[i].x;
            }
        }

        ASSERT(xMaxPoint != -1);

        // Find bottom most point
        int yMinPoint = -1;
        float yMin = FLT_MAX;

        for (int i = 0; i < verts.size(); i++) {
            if (verts[i].y < yMin) {
                yMinPoint = i;
                yMin = verts[i].y;
            }
        }

        ASSERT(yMinPoint != -1);

        // now set the base vertex, which is where we base uv 0,0 on
        Vector3 baseVert;

        baseVert.x = verts[xMinPoint].x;
        baseVert.y = verts[yMaxPoint].y;
        baseVert.z = 0;

        // Figure out grid resolution
        auto xdiff = verts[xMaxPoint].x - verts[xMinPoint].x;
        auto ydiff = verts[yMaxPoint].y - verts[yMinPoint].y;

        // Find upper left corner
        transform = transform.Transpose(); // invert rotation
        auto upperLeft = Vector3::Transform(baseVert, transform) + center;

        return { .Width = xdiff, .Height = ydiff, .UpperLeft = upperLeft };
    }

    constexpr float PADDING = 2.5f;

    // project a ray from a point to the portals in another room
    bool PortalVisibleFromPoint(IntersectContext& intersect, SegID srcSegment, const Vector3& srcPoint, const Vector3& srcNormal,
                                const Array<Vector3, 3>& destTri, const Vector3& destNormal, const FaceInfo& destBounds,
                                int steps) {
        auto transform = VectorToRotation(destNormal);
        auto xstep = (destBounds.Width - PADDING * 2) / (steps - 1);
        auto ystep = -(destBounds.Height - PADDING * 2) / (steps - 1);

        // Check the source point against the portal grid
        for (int x = 0; x < steps; x++) {
            for (int y = 0; y < steps; y++) {
                auto pt = destBounds.UpperLeft + transform.Right() * PADDING + transform.Right() * xstep * (float)x
                    + transform.Up() * -PADDING + transform.Up() * ystep * (float)y;

                auto [dir, dist] = GetDirectionAndDistance(pt, srcPoint);
                if (!TriangleContainsPoint(destTri, pt)) continue;

                if (abs(dir.Dot(srcNormal)) <= 0.01f)
                    return false; // ray is perpendicular to portal

                //Inferno::Render::Debug::DebugPoints2.push_back(pt);
                Ray ray(srcPoint, dir);
                LevelHit hit;
                RayQuery query{ .MaxDistance = dist, .Start = srcSegment, .Mode = RayQueryMode::IgnoreWalls };

                if (!intersect.RayLevel(ray, query, hit)) {
                    //Render::Debug::DebugLines.push_back(pt);
                    //Render::Debug::DebugLines.push_back(srcPoint);
                    return true; // At least one ray can reach the portal
                }
            }
        }

        return false; // Wasn't visible
    }

    // Determines the potentially visible rooms from this room.
    // Creates a grid of points across each face based on `steps`.
    void ComputeRoomVisibility(const Level& level, const List<Room>& rooms, Room& room,
                               List<Tuple<int, int>>& visiblePortalLinks, int steps) {
        ASSERT(steps >= 2);
        auto roomId = RoomID(&room - &rooms[0]);
        //SPDLOG_INFO("Room Visibility: {}", roomId);
        room.NearbyRooms.clear();
        room.NearbyRooms.push_back(roomId); // Can see self

        IntersectContext intersect(level);

        for (auto& srcPortal : room.Portals) {
            room.NearbyRooms.push_back(srcPortal.RoomLink); // all adjacent rooms are visible
            auto& srcSeg = level.GetSegment(srcPortal.Tag);
            auto srcFace = ConstFace::FromSide(level, srcSeg, srcPortal.Tag.Side);
            auto connectedSide = level.GetConnectedSide(srcPortal.Tag);

            // Check each triangle in the src portal face
            for (int i = 0; i < 2; i++) {
                auto destRoom = GetRoom(rooms, srcPortal.RoomLink);
                if (!destRoom) continue;

                auto srcPoly = srcFace.GetPoly(i);
                auto srcBounds = GetFaceBounds(srcPoly, srcFace.Side.Normals[i]);
                auto srcTransform = VectorToRotation(srcFace.Side.Normals[i]);
                auto xstep = (srcBounds.Width - PADDING * 2) / (steps - 1);
                auto ystep = -(srcBounds.Height - PADDING * 2) / (steps - 1);

                // Flip the source portal plane to look towards the opening
                Plane srcPortalPlane(srcFace.Center(), -srcFace.AverageNormal());
                //SPDLOG_INFO("Base portal: {}", srcPortal.Tag);

                Stack<const Portal*> stack;
                Set<RoomID> visited;
                visited.insert(srcPortal.RoomLink);

                for (auto& p : destRoom->Portals)
                    stack.push(&p);

                // Adds all portals in the room this portal links to
                auto addLinkedRooms = [&visited, &room, &rooms, &stack](const Portal& portal) {
                    if (!visited.contains(portal.RoomLink)) {
                        room.NearbyRooms.push_back(portal.RoomLink);
                        visited.insert(portal.RoomLink);

                        if (auto nextRoom = GetRoom(rooms, portal.RoomLink))
                            for (auto& p : nextRoom->Portals)
                                stack.push(&p);
                    }
                };

                while (!stack.empty()) {
                    auto destPortal = stack.top();
                    stack.pop();

                    if (destPortal->Tag == connectedSide) continue; // Don't test connected portal
                    if (Seq::contains(visiblePortalLinks, Tuple{ destPortal->Id, srcPortal.Id })) {
                        // Portal is known to be visible, no need to recalculate it
                        addLinkedRooms(*destPortal);
                        continue;
                    }

                    auto& destSeg = level.GetSegment(destPortal->Tag);
                    auto destFace = ConstFace::FromSide(level, destSeg, destPortal->Tag.Side);

                    auto addLeafRoom = [&srcFace, &destFace, &room, &destPortal] {
                        // Add the final leaf room without recursion if it is nearby
                        constexpr float NEARBY_DIST = 120; // max dist for final leaf rooms
                        if (Vector3::Distance(srcFace.Center(), destFace.Center()) < NEARBY_DIST)
                            room.NearbyRooms.push_back(destPortal->RoomLink);
                    };

                    // Check if the portals are in front of each other (note that src plane is flipped)
                    Plane destPlane(destFace.Center(), destFace.AverageNormal());
                    if (!srcFace.InFrontOfPlane(destPlane, 0.1f) || !destFace.InFrontOfPlane(srcPortalPlane, -0.1f)) {
                        addLeafRoom();
                        continue; // portals were behind each other
                    }

                    auto destPoly0 = destFace.GetPoly(0);
                    auto destPoly1 = destFace.GetPoly(1);
                    auto destBounds0 = GetFaceBounds(destPoly0, destFace.Side.Normals[0]);
                    auto destBounds1 = GetFaceBounds(destPoly1, destFace.Side.Normals[1]);
                    bool foundPortal = false;

                    // Compare each point on the src portal grid to the dest portal
                    for (int x = 0; x < steps && !foundPortal; x++) {
                        for (int y = 0; y < steps && !foundPortal; y++) {
                            auto pt = srcBounds.UpperLeft
                                + srcTransform.Right() * PADDING + srcTransform.Right() * xstep * (float)x
                                + srcTransform.Up() * -PADDING + srcTransform.Up() * ystep * (float)y;

                            if (!TriangleContainsPoint(srcPoly, pt)) continue; // Grid point wasn't inside triangle

                            pt += srcTransform.Forward() * 0.2f; // shift the point inside the start seg so parallel portals aren't marked as visible

                            if (!SegmentContainsPoint(level, connectedSide.Segment, pt))
                                continue; // Shifting the point rarely pushes it outside the expected segment. Discard it if this happens.

                            // Check the source triangle against both dest triangles
                            if (PortalVisibleFromPoint(intersect, connectedSide.Segment, pt, srcFace.Side.Normals[i], destPoly0, destFace.Side.Normals[0], destBounds0, steps) ||
                                PortalVisibleFromPoint(intersect, connectedSide.Segment, pt, srcFace.Side.Normals[i], destPoly1, destFace.Side.Normals[1], destBounds1, steps)) {
                                // Add both pairs to simplify searching
                                visiblePortalLinks.push_back({ destPortal->Id, srcPortal.Id });
                                visiblePortalLinks.push_back({ srcPortal.Id, destPortal->Id });
                                addLinkedRooms(*destPortal);
                                foundPortal = true;
                            }
                        }
                    }

                    if (!foundPortal)
                        addLeafRoom();
                }
            }
        }

        Seq::distinct(room.NearbyRooms); // Clean up duplicates

        // Store visible segments
        for (auto& rid : room.NearbyRooms) {
            if (auto pRoom = GetRoom(rooms, rid)) {
                Seq::append(room.VisibleSegments, pRoom->Segments);
            }
        }

        Seq::distinct(room.VisibleSegments); // Clean up duplicates
    }

    // Splits a large room in half along its longest axis.
    // Can create multiple rooms from one is several leaf rooms are formed.
    void SplitLargeRooms(Level& level, List<Room>& rooms, int maxSize) {
        bool maybeBigRoom = true;
        int iterations = 0;

        while (maybeBigRoom && iterations < 1000) {
            maybeBigRoom = false;
            List<Room> roomBuffer;

            for (auto& room : rooms) {
                auto subdivisions = SubdivideLargeRoom(level, room, maxSize);
                for (auto& sub : subdivisions) {
                    if (sub.Segments.size() > maxSize)
                        maybeBigRoom = true;
                }
                Seq::append(roomBuffer, subdivisions);
            }

            Seq::append(rooms, roomBuffer);
            RemoveEmptyRooms(rooms);
            iterations++;
        }
    }

    void PrepassSolidEdges(Level& level) {
        struct Edge {
            uint16 A, B;
            auto operator==(const Edge& e) const { return A == e.A && B == e.B; }
        };

        List<Edge> solidEdges;

        // Find all solid edges
        for (auto& seg : level.Segments) {
            for (auto& sideid : SIDE_IDS) {
                auto& side = seg.GetSide(sideid);
                if (seg.SideHasConnection(sideid)) {
                    ranges::fill(side.SolidEdges, false);
                    continue;
                }

                auto indices = seg.GetVertexIndices(sideid);
                ranges::fill(side.SolidEdges, true);

                for (int i = 0; i < 4; i++) {
                    solidEdges.push_back({ indices[i], indices[(i + 1) % 4] });
                }
            }
        }

        int segid = 0;
        for (auto& seg : level.Segments) {
            for (auto& sideid : SIDE_IDS) {
                if (!seg.SideHasConnection(sideid)) continue; // already marked solid sides earlier

                auto indices = seg.GetVertexIndices(sideid);
                auto& side = seg.GetSide(sideid);

                for (int i = 0; i < 4; i++) {
                    Edge edge = { indices[i], indices[(i + 1) % 4] };
                    if (Seq::contains(solidEdges, edge)) {
                        side.SolidEdges[i] = true;
                        //SPDLOG_INFO("Marking seg {}:{}:{} as solid", segid, sideid, i);
                    }
                }
            }

            segid++;
        }
    }

    List<Room> CreateRooms(Level& level, SegID start, int preferredSegCount) {
        Set<SegID> visited;
        List<Room> rooms;

        Stopwatch timer;

        Stack<SegID> search;
        search.push(start);

        while (!search.empty()) {
            auto id = search.top();
            search.pop();
            if (visited.contains(id)) continue; // already visited

            auto room = CreateRoom(level, id);

            // Add connections
            for (auto& portal : room.Portals) {
                auto& seg = level.GetSegment(portal.Tag);
                auto conn = seg.GetConnection(portal.Tag.Side);
                assert(conn != SegID::None);
                search.push(conn);
            }

            Seq::insert(visited, room.Segments);
            rooms.push_back(std::move(room));
        }

        List<Room> newRooms;
        for (auto& room : rooms) {
            auto subdivisions = SubdivideRoom(level, room, preferredSegCount);
            Seq::append(newRooms, subdivisions);
        }

        Seq::append(rooms, newRooms);

        RemoveEmptyRooms(rooms);

        // Merge small rooms into adjacent rooms
        for (auto& room : rooms)
            MergeSmallRoom(level, rooms, room, 2);
        RemoveEmptyRooms(rooms);

        // Split big rooms in half until they are no longer big
        SplitLargeRooms(level, rooms, 90);

        Set<SegID> usedSegments;
        for (int roomId = 0; roomId < rooms.size(); roomId++) {
            auto& room = rooms[roomId];
            AddPortalsToRoom(level, rooms, room);

            for (auto& segID : room.Segments) {
                assert(!usedSegments.contains(segID));
                usedSegments.insert(segID);
                room.Center += level.GetSegment(segID).Center;

                // Update object rooms
                auto& seg = level.GetSegment(segID);
                seg.Room = (RoomID)roomId;
            }

            room.Center /= (float)room.Segments.size();
            UpdatePortalDistances(level, room);
        }

        Graphics::ResetDebug();
        UpdatePortalLinks(level, rooms);
        //ComputeRoomVisibility(level, rooms, rooms[4]);

        PrepassSolidEdges(level);
        SPDLOG_INFO("Room generation time {}", timer.GetElapsedSeconds());

        //timer = {};
        //List<Tuple<int, int>> visiblePortalLinks;
        //visiblePortalLinks.reserve(rooms.size() * 3);

        //int visibilitySteps = 4;

        //for (auto& room : rooms) {
        //    ComputeRoomVisibility(level, rooms, room, visiblePortalLinks, visibilitySteps);
        //    //UpdateNavNodes(level, room);
        //}

        //SPDLOG_INFO("Room visibility time {}", timer.GetElapsedSeconds());

        constexpr float PORTAL_DEPTH = 200.0f;

        // Use all nearby connected rooms up to a maximum distance as 'nearby'
        for (int i = 0; i < rooms.size(); i++) {
            Seq::append(rooms[i].NearbyRooms, GetRoomsByDepth(rooms, RoomID(i), PORTAL_DEPTH, TraversalFlag::None));
        }

        return rooms;
    }
}
