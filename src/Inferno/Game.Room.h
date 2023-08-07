#pragma once
#include "Room.h"
#include "Level.h"

namespace Inferno::Game {
    class NavigationNetwork {
        struct SegmentSideNode {
            float Distance = -1; // Distance between the segment centers on this side
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
        //    struct PortalLink {
        //        float Distance;
        //        int Index;
        //    };

        //    Portal Side1, Side2;
        //    // distance between this portal and other portals
        //    List<PortalLink> Links;
        //    WallID Wall = WallID::None;
        //};

        // State for A* traversal. Reused between seg and rooms.
        struct TraversalNode {
            int Index = -1;
            int Parent = -1;
            float GoalDistance = FLT_MAX; // global goal
            float LocalGoal = FLT_MAX;
            bool Visited = false;
        };

        List<SegmentNode> _segmentNodes;
        List<TraversalNode> _traversalBuffer;

    public:
        NavigationNetwork() {}

        NavigationNetwork(struct Level& level) {
            _segmentNodes.resize(level.Segments.size());
            _traversalBuffer.resize(level.Segments.size());

            for (int id = 0; id < level.Segments.size(); id++) {
                UpdateNode(level, (SegID)id);
            }
        }

        List<SegID> NavigateTo(SegID start, SegID goal, struct Level& level);

    private:
        void UpdateNode(struct Level& level, SegID segId) {
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

        List<RoomID> NavigateAcrossRooms(RoomID start, RoomID goal, struct Level& level);

        List<SegID> NavigateWithinRoom(SegID start, SegID goal, Room& room);
    };
}
