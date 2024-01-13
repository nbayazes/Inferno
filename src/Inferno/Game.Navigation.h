#pragma once

#include "Types.h"
#include "Room.h"
#include "Level.h"

namespace Inferno {
    enum class NavigationFlag {
        None,
        OpenKeyDoors = 1 << 0, // Can open key doors. Player must have the key.
        OpenSecretDoors = 1 << 1, // Can open secret doors. Door must be unlocked.
        HighPriority = 1 << 2 // Keep pathing if possible
    };

    namespace Debug {
        inline List<NavPoint> Path, OptimizedPath;
    }

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

        List<NavPoint> NavigateTo(SegID start, const NavPoint& goal, NavigationFlag flags, struct Level& level, float maxDistance = FLT_MAX);

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

        List<RoomID> NavigateAcrossRooms(RoomID start, RoomID goal, NavigationFlag flags, struct Level& level);

        List<SegID> NavigateWithinRoom(SegID start, SegID goal, Room& room);
    };

    enum class TraversalFlag {
        None,
        StopWall = 1 << 1, // Stop at any wall (except fly-through triggers) regardless of other flags
        PassTransparent = 1 << 2, // Always traverse transparent walls regardless of type or other flags
        StopDoor = 1 << 3, // Stop at doors
        StopLockedDoor = 1 << 4, // Stop at locked doors
        StopKeyDoor = 1 << 5, // Stop at keyed doors
        StopSecretDoor = 1 << 6, // Stop at secret doors
        PassOpenDoors = 1 << 7, // Always traverse open doors regardless of lock or key state
    };

    bool StopAtWall(const Level& level, const Wall& wall, TraversalFlag flags);

    void IterateNearbySegments(Level& level, NavPoint start, float distance, TraversalFlag flags, const std::function<void(Segment&, bool&)>&);
    
    // Returns false if a side is blocked for navigation purposes
    bool CanNavigateSide(Level& level, Tag tag, NavigationFlag flags);

    // Executes a function on each room based on portal distance from a point. Action returns true to stop traversal.
    void TraverseRoomsByDistance(Inferno::Level& level, RoomID startRoom, const Vector3& position, 
                                 float maxDistance, bool soundMode, const std::function<bool(Room&)>& action);

    List<NavPoint> GenerateRandomPath(SegID start, uint depth, NavigationFlag flags = NavigationFlag::None, SegID avoid = SegID::None);
    void OptimizePath(List<NavPoint>& path);
}
