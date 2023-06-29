#pragma once

#include "Level.h"
#include "Types.h"

namespace Inferno {
    namespace Debug {
        inline List<Vector3> NavigationPath;
    }

    enum class RoomID : int16 { None = -1 };

    struct Portal : Tag {
        RoomID Room = RoomID::None;
    };

    // A room is a group of segments divided by walls

    struct Room {
        List<SegID> Segments;
        List<Portal> Portals; // Which tags of this room have connections to other rooms

        int Reverb = 0;
        Color Fog;
        float FogDepth = -1;
        SegmentType Type = SegmentType::None;

        int Meshes{}; // Meshes for each material
        //float EstimatedSize;
        //bool ContainsLava{}, ContainsWater{}; // Ambient noise
        SoundID AmbientSound = SoundID::None;
        DirectX::BoundingOrientedBox Bounds;
        Vector3 Center;

        List<List<float>> PortalDistances; // List of distances from portal A to ABCD

        bool Contains(SegID id) {
            return Seq::contains(Segments, id);
        }

        void AddPortal(Portal tag) {
            if (!Seq::contains(Portals, tag))
                Portals.push_back(tag);
        }

        void AddSegment(SegID seg) {
            if (!Seq::contains(Segments, seg))
                Segments.push_back(seg);
        }

        Portal* GetPortal(Tag tag) {
            for (auto& portal : Portals) {
                if (portal == tag) return &portal;
            }

            return nullptr;
        }

        void UpdatePortalDistances(Level& level) {
            PortalDistances.resize(Portals.size());

            for (int i = 0; i < Portals.size(); i++) {
                PortalDistances[i].resize(Portals.size());

                auto& a = level.GetSide(Portals[i]);
                for (int j = 0; j < Portals.size(); j++) {
                    auto& b = level.GetSide(Portals[j]);
                    PortalDistances[i][j] = Vector3::Distance(a.Center, b.Center);
                }
            }
        }
    };

    RoomID FindRoomBySegment(span<Room> rooms, SegID seg);
    List<Room> CreateRooms(Level& level);

    class LevelRooms {
    public:
        LevelRooms() { }

        LevelRooms(Level& level) {
            Rooms = CreateRooms(level);
        }

        List<Room> Rooms;

        Room* GetRoom(RoomID id) {
            if (!Seq::inRange(Rooms, (int)id)) return nullptr;
            return &Rooms[(int)id];
        }

        Room* GetRoom(SegID id) {
            auto roomId = FindBySegment(id);
            if (roomId == RoomID::None) return nullptr;
            return &Rooms[(int)roomId];
        }

        RoomID FindBySegment(SegID seg) {
            for (int i = 0; i < Rooms.size(); i++) {
                if (Seq::contains(Rooms[i].Segments, seg)) return RoomID(i);
            }

            return RoomID::None;
        }

        Room* GetConnectedRoom(Tag tag) {
            if (auto room = GetRoom(tag.Segment)) {
                if (auto portal = room->GetPortal(tag)) {
                    return GetRoom(portal->Room);
                }
            }

            return nullptr;
        }
    };

    class NavigationNetwork {
        struct SegmentSideNode {
            float Distance = -1;
            SegID Connection = SegID::None;
            // Needs to be updated when doors are unlocked or walls are removed
            // todo: blocked is conditional. thief can open key doors but no other bots can.
            bool Blocked = false;
        };

        struct SegmentNode {
            SegmentSideNode Sides[6];
            Vector3 Position;
        };

        //struct PortalNode {
        //    //RoomID Room;
        //    // distance between this portal and other portals
        //    List<float> Distances;
        //};

        struct RoomNode {
            //RoomID Room; // Room this is associated with (use index?)
            // Matches the portal ind
            //List<PortalNode> Portals;

            List<List<float>> PortalDistances; // List of distances from portal A to ABCD
            Vector3 Center;
        };

        // State for A* traversal
        struct TraversalNode {
            int Index = -1;
            int Parent = -1;
            float GoalDistance = FLT_MAX; // global goal
            float LocalGoal = FLT_MAX;
            bool Visited = false;
        };

        List<SegmentNode> _segmentNodes;
        //List<RoomNode> _roomNodes;
        List<TraversalNode> _traversalBuffer;

    public:
        NavigationNetwork() { }

        NavigationNetwork(Level& level, const LevelRooms& rooms) {
            _segmentNodes.resize(level.Segments.size());
            //_roomNodes.resize(rooms.Rooms.size());
            _traversalBuffer.resize(level.Segments.size());

            for (int id = 0; id < level.Segments.size(); id++) {
                UpdateNode(level, (SegID)id);
            }

            for (int id = 0; id < rooms.Rooms.size(); id++) {
                //_roomNodes[id].Center = rooms.Rooms[id].Center;
                //UpdatePortalDistances(level, rooms.Rooms[id], _roomNodes[id]);
            }
        }

        List<SegID> NavigateTo(SegID start, SegID goal, LevelRooms& rooms, Level& level);

    private:
        void UpdateNode(Level& level, SegID segId) {
            auto& node = _segmentNodes[(int)segId];
            auto& seg = level.GetSegment(segId);
            node.Position = seg.Center;

            for (int side = 0; side < 6; side++) {
                auto& nodeSide = node.Sides[side];
                if (auto cseg = level.TryGetSegment(seg.Connections[side])) {
                    nodeSide.Distance = Vector3::Distance(seg.Center, cseg->Center);
                    nodeSide.Connection = seg.Connections[side];
                }

                if (auto wall = level.TryGetWall({ segId, (SideID)side })) {
                    if (wall->Type == WallType::Door && wall->HasFlag(WallFlag::DoorLocked))
                        nodeSide.Blocked = true;

                    if (wall->Type == WallType::Closed || wall->Type == WallType::Cloaked)
                        nodeSide.Blocked = true;
                }
            }
        }

        static float Heuristic(const SegmentNode& a, const SegmentNode& b) {
            return Vector3::DistanceSquared(a.Position, b.Position);
        }

        List<RoomID> NavigateAcrossRooms(RoomID start, RoomID goal, LevelRooms& rooms, Level& level);

        List<SegID> NavigateWithinRoom(SegID start, SegID goal, Room& room);
    };
}
