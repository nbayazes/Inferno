#pragma once

#include "Level.h"
#include "Object.h"

namespace Inferno {
    class NavNetwork {
        struct NavNode {
            float Distances[6]{}; // Per side
            SegID Connections[6]{
                SegID::None, SegID::None, SegID::None, SegID::None, SegID::None, SegID::None
            };
            // Needs to be updated when doors are unlocked or walls are removed
            // todo: blocked is conditional. thief can open key doors but no other bots can.
            bool Blocked[6]{};
            Vector3 Position;
        };

        // State for A* traversal
        struct TraversalNode {
            int Index = -1;
            int Parent = -1;
            float GoalDistance = FLT_MAX; // global goal
            float LocalGoal = FLT_MAX;
            bool Visited = false;
        };

        List<NavNode> _nodes;
        List<TraversalNode> _traversal;

    public:
        NavNetwork(Level& level) {
            _nodes.resize(level.Segments.size());
            _traversal.resize(level.Segments.size());

            for (int id = 0; id < level.Segments.size(); id++) {
                UpdateNode(level, (SegID)id);
            }
        }

        void UpdateNode(Level& level, SegID segId) {
            auto& node = _nodes[(int)segId];
            auto& seg = level.GetSegment(segId);
            node.Position = seg.Center;

            for (int side = 0; side < 6; side++) {
                if (auto cseg = level.TryGetSegment(seg.Connections[side])) {
                    node.Distances[side] = Vector3::Distance(seg.Center, cseg->Center);
                    node.Connections[side] = seg.Connections[side];
                }

                if (auto wall = level.TryGetWall({ segId, (SideID)side })) {
                    if (wall->Type == WallType::Door && wall->HasFlag(WallFlag::DoorLocked))
                        node.Blocked[side] = true;

                    if (wall->Type == WallType::Closed || wall->Type == WallType::Cloaked)
                        node.Blocked[side] = true;
                }
            }
        }

        Set<SegID> GetSegmentsByDistance(SegID start, float /*distance*/) {
            Set<SegID> segs;
            auto& node = _nodes[(int)start];

            for (int i = 0; i < 6; i++) {
                auto conn = node.Connections[i];
                if (conn != SegID::None && !node.Blocked[i]) {}
            }

            return segs;
        }

        static float Heuristic(const NavNode& a, const NavNode& b) {
            return Vector3::DistanceSquared(a.Position, b.Position);
        }

        List<SegID> NavigateTo(SegID start, SegID goal) {
            // Reset traversal state
            for (int i = 0; i < _traversal.size(); i++)
                _traversal[i] = {
                    .Index = i,
                    .GoalDistance = Heuristic(_nodes[(int)start], _nodes[(int)goal])
                };

            std::list<TraversalNode*> queue;
            _traversal[(int)start].LocalGoal = 0;
            queue.push_back(&_traversal[(int)start]);


            while (!queue.empty()) {
                queue.sort([](const TraversalNode* a, const TraversalNode* b) {
                    return a->GoalDistance < b->GoalDistance;
                });

                // todo: stop searching if a path is found and max iterations is exceeded

                if (!queue.empty() && queue.front()->Visited)
                    queue.pop_front();

                if (queue.empty())
                    break; // no nodes left

                auto& current = queue.front();
                current->Visited = true;
                auto& node = _nodes[current->Index];

                for (int side = 0; side < 6; side++) {
                    auto& connId = node.Connections[side];
                    if (connId == SegID::None) continue;
                    if (node.Blocked[side]) continue;

                    auto& neighborNode = _nodes[(int)connId];
                    //unvistedNodes.push_back({ .Index = (int)connId });

                    auto& neighbor = _traversal[(int)connId];

                    if (!neighbor.Visited)
                        queue.push_back(&neighbor);

                    float localGoal = current->LocalGoal + Vector3::DistanceSquared(node.Position, neighborNode.Position);

                    if (localGoal < neighbor.LocalGoal) {
                        neighbor.Parent = current->Index;
                        neighbor.LocalGoal = localGoal;
                        neighbor.GoalDistance = neighbor.LocalGoal + Heuristic(neighborNode, _nodes[(int)goal]);
                    }
                }
            }

            List<SegID> path;

            // add nodes along the path starting at the goal
            auto* trav = &_traversal[(int)goal];
            //if (trav->Parent >= 0) {
            //path.push_back(goal);

            while (trav) {
                path.push_back((SegID)trav->Index);
                trav = trav->Parent >= 0 ? &_traversal[trav->Parent] : nullptr;
            }
            //}

            ranges::reverse(path);

            // Walk backwards, using the parent
            return path;
        }
    };

    void UpdateAI(Object& obj, float dt);
    void AlertEnemiesOfNoise(const Object& source, float soundRadius, float awareness);
    void PlayRobotAnimation(Object& robot, AnimState state, float time = 0.4f, float moveMult = 5);

    namespace Debug {
        inline int ActiveRobots = 0;
    }
}
