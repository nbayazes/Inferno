#pragma once
#include "Room.h"
#include "Level.h"

namespace Inferno::Game {
    class NavigationNetwork {
        struct SegmentSideNode {
            float Distance = -1; // Distance between the segment centers on this side
            SegID Connection = SegID::None;
        };

        struct SegmentNode {
            SegmentSideNode Sides[6];
            Vector3 Position;
        };

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

        List<SegID> NavigateTo(SegID start, SegID goal, bool stopAtKeyDoors, struct Level& level);

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
            }
        }

        static float Heuristic(const SegmentNode& a, const SegmentNode& b) {
            return Vector3::DistanceSquared(a.Position, b.Position);
        }

        List<RoomID> NavigateAcrossRooms(RoomID start, RoomID goal, bool stopAtKeyDoors, struct Level& level);

        List<SegID> NavigateWithinRoom(SegID start, SegID goal, Room& room);
    };

    // Executes a function on each room based on portal distance from a point
    void TraverseRoomsByDistance(Inferno::Level& level, RoomID startRoom, const Vector3& position, float maxDistance, const std::function<void(Room&)>& action);
}
