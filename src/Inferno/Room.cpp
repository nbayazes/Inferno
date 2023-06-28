#include "pch.h"

#include "Room.h"

namespace Inferno {
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
            if (seg.Type != SegmentType::GoalBlue && seg.Type != SegmentType::GoalRed)
                room.Type = seg.Type; // segment type should be consistent

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
                            room.AddPortal(tunnelEnd);
                        }
                        else {
                            room.AddPortal(tunnelStart);
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


    struct Portal {
        Tag Tag;
        int Nearby;
    };

    struct SegmentNode {
        //Portal Portals[6];
        SegID Seg;
        int Connections;
        int Delta[6];
    };

    void AddPortalsToRoom(Level& level, Room& room) {
        room.Portals.clear();

        for (auto& segId : room.Segments) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                auto conn = seg.GetConnection(sideId);
                if (conn <= SegID::None) continue;

                if (room.Contains(conn)) continue;
                room.AddPortal({ segId, sideId });
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
                //auto& other = nodes[];

                if (auto other = Seq::find(nodes, [conn](const SegmentNode& x) { return x.Seg == conn.Segment; })) {
                    node.Delta[(int)sideId] = other->Connections - node.Connections;
                }
            }
        }

        Room newRoom;
        SegID start = room.Segments[0];

        // starting at a portal, walk until max seg is reached and a large delta is found
        std::deque<SegID> search;
        Stack<Tag> searchPortals; // After room is split, store segs on other side separately
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
            /*auto segId = search.top();
            search.pop();*/
            auto segId = *search.begin();
            Seq::remove(search, segId);
            //search.erase(segId);

            if (visited.contains(segId)) {
                if (!searchPortals.empty()) {
                    Tag tag = searchPortals.top();
                    searchPortals.pop();
                    auto conn = level.GetConnectedSide(tag);
                    newRoom.AddPortal(conn);
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

            if (auto node = Seq::find(nodes, [segId](const SegmentNode& x) { return x.Seg == segId; })) {
                for (int side = 0; side < 6; side++) {
                    auto conn = seg.GetConnection(SideID(side));
                    Tag tag = { segId, SideID(side) };

                    if (conn > SegID::None && !Seq::contains(room.Segments, conn) && !Seq::contains(newRoom.Portals, tag)) {
                        newRoom.Portals.push_back(tag); // Connection to outside the room is a portal
                    }

                    if (conn > SegID::None && newRoom.Segments.size() + search.size() >= maxSegs /*&& std::abs(node->Delta[side]) == 0*//* && node->Connections == 2*/) {
                        auto& cseg = level.GetSegment(conn);
                        if (canSearchSegment(conn)) {
                            if (SegmentIsTunnel(cseg)) {
                                newRoom.AddPortal(tag); // Insert a portal inside the room
                                searchPortals.push(tag);
                            }
                            else {
                                search.push_front(conn);
                            }
                        }
                    }
                    else if (canSearchSegment(conn)) {
                        auto wall = level.TryGetWall(tag);
                        if (wall && WallIsPortal(*wall)) {
                            newRoom.AddPortal(tag); // Insert a portal inside the room
                            searchPortals.push(tag);
                        }
                        else {
                            search.push_front(conn);
                        }
                    }
                }
            }

            if (search.empty() && !searchPortals.empty()) {
                if (!newRoom.Portals.empty() && !newRoom.Segments.empty()) {
                    for (auto& s : newRoom.Segments)
                        Seq::remove(room.Segments, s);

                    AddPortalsToRoom(level, newRoom);
                    rooms.push_back(newRoom);
                    newRoom = {};
                }

                Tag tag = searchPortals.top();
                searchPortals.pop();
                auto conn = level.GetConnectedSide(tag);
                newRoom.AddPortal(conn);
                search.push_front(conn.Segment);
            }
        }

        AddPortalsToRoom(level, newRoom);
        room = newRoom; // copy remaining segs back to room

        //SPDLOG_INFO("Split room into {} rooms", rooms.size());
        return rooms;
    }

    Room* FindRoomBySegment(List<Room>& rooms, SegID seg) {
        for (auto& room : rooms) {
            if (Seq::contains(room.Segments, seg)) return &room;
        }

        return nullptr;
    }

    void MergeSmallRoom(Level& level, List<Room>& rooms, Room& room, int minSize) {
        if (room.Segments.size() > minSize) return;
        if (room.Type != SegmentType::None && room.Type != SegmentType::GoalBlue && room.Type != SegmentType::GoalRed)
            return; // Don't merge special segments

        Room* mergedNeighbor = nullptr;

        for (auto& portal : room.Portals) {
            if (level.TryGetWall(portal))
                continue; // Don't merge a wall

            // Wasn't a wall, find the owning room and merge into it
            auto connection = level.GetConnectedSide(portal);
            if (level.TryGetWall(connection))
                continue; // Other side had a wall (check for one-sided walls)

            if (auto neighbor = FindRoomBySegment(rooms, connection.Segment)) {
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
            AddPortalsToRoom(level, *mergedNeighbor);
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

        Set<SegID> usedSegments;
        for (auto& room : rooms) {
            for (auto& seg : room.Segments) {
                assert(!usedSegments.contains(seg));
                usedSegments.insert(seg);
            }
        }
        return rooms;
    }
}
