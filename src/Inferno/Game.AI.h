#pragma once

#include "Level.h"
#include "Object.h"
#include "SoundTypes.h"

namespace Inferno {
    constexpr float AI_PATH_DELAY = 5; // Default delay for trying to path to the player
    constexpr float AI_DODGE_TIME = 0.5f; // Time to dodge a projectile. Should probably scale based on mass.
    constexpr float AI_MAX_DODGE_DISTANCE = 60; // Range at which projectiles are dodged
    constexpr float DEATH_SOUND_DURATION = 2.68f;

    constexpr float AI_AWARENESS_MAX = 1.0f;
    constexpr float AI_AWARENESS_COMBAT = 0.6f; // Robot will fire at its last known target position
    constexpr float AI_AWARENESS_INVESTIGATE = 0.5f; // when a robot exceeds this threshold it will investigate the point of interest

    struct AITarget {
        Vector3 Position;
        Vector3 Velocity;

        AITarget()  = default;
        AITarget(const Object& obj) : Position(obj.Position), Velocity(obj.Physics.Velocity) {}
    };

    // Runtime AI data
    struct AIRuntime {
        double LastUpdate = -1; // time when this robot was last updated
        float LastHitByPlayer = -1; // time in seconds since last hit by the player

        // How aware of the player this robot is. Ranges 0 to 1.
        // Only seeing the player can set awareness to 1.
        float Awareness = 0;
        // How likely the robot is to flee. Increased by taking damage.
        float Fear = 0;

        //uint8 PhysicsRetries; // number of retries in physics last time this object got moved.
        //uint8 ConsecutiveRetries; // number of retries in consecutive frames without a count of 0
        PlayerVisibility PlayerVisibility;
        uint8 BurstShots; // number of shots fired rapidly
        uint8 GunIndex = 0; // Which gun to fire from next
        //AIMode Mode;
        //float NextActionTime;
        float FireDelay, FireDelay2; // Delay until firing for primary and secondary weapons
        //float AwarenessTime; // How long to remain aware of the player, 0 for unaware
        float LastSeenPlayer; // Time in seconds since player was seen
        float LastSeenAttackingPlayer; // Time in seconds since at least awareness level 2
        float MiscSoundTime; // Time in seconds since the robot made angry or lurking noises
        float AnimationTime = 0; // How much of the animation has passed
        float AnimationDuration = 0; // Time in seconds to reach the goal angles
        float MeleeHitDelay = 0; // How long before a melee swing deals damage
        AnimState AnimationState = {};

        SegID TargetSegment = SegID::None;
        Option<Vector3> Target;

        Array<Vector3, MAX_SUBMODELS> GoalAngles{}, DeltaAngles{};

        SegID GoalSegment = SegID::None; // segment the robot wants to move to. Disables pathfinding when set to none.
        RoomID GoalRoom = RoomID::None;
        Vector3 GoalPosition; // position the robot wants to move to

        float DodgeDelay = 0; // Delay before trying to dodge
        float PathDelay = 0; // Delay before trying to path to a new location
        float DodgeTime = 0; // Remaining time to dodge for
        float WiggleTime = 0; // Remaining wiggle time
        Vector3 DodgeDirection;

        float StrafeAngle = 0; // Angle relative to the right axis to strafe in
        float StrafeTime = 0; // How long to strafe for

        float WeaponCharge = 0; // For robots with charging weapons (fusion hulks)
        float NextChargeSoundDelay = 0; // Delay to play a sound when charging up
        bool ChargingWeapon = false; // Set to true when charging a weapon
        
        SoundUID SoundHandle = SoundUID::None; // Used to cancel a playing sound when the robot is destroyed
        float RemainingSlow = 0; // How long this robot is slowed (reduced movement and turn speed)
        float RemainingStun = 0; // How long this robot is stunned (unable to act)

        bool DyingSoundPlaying = false;
        float DeathRollTimer = 0; // time passed since dying
        float TeleportDelay = 0; // Delay before next teleport

        List<SegID> GoalPath; // For pathing to another segment
        int16 GoalPathIndex = -1;

        bool PlayingAnimation() const {
            return AnimationTime < AnimationDuration;
        }

        void ClearPath() {
            GoalPath.clear();
            GoalPathIndex = -1;
            GoalSegment = SegID::None;
            GoalRoom = RoomID::None;
        }
    };

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
    void AlertEnemiesOfNoise(const Object& source, float soundRadius, float awareness, float maxAwareness = AI_AWARENESS_MAX);
    void PlayRobotAnimation(const Object& robot, AnimState state, float time = 0.4f, float moveMult = 5);

    // Applies damage to a robot, applying stuns, slows, and waking it up if necessary.
    // Rotates towards source if asleep
    void DamageRobot(const Vector3& source, bool sourceIsPlayer, Object& robot, float damage, float stunMult);

    namespace Debug {
        inline int ActiveRobots = 0;
    }

    void ResetAI(); // Call on level start / load to reset AI state
    // Resizes the internal AI buffer. Keep in sync with the Level.Objects size.
    void ResizeAI(size_t size);

    AIRuntime& GetAI(const Object& obj);

    bool DeathRoll(Object& obj, float rollDuration, float elapsedTime, SoundID soundId, bool& dyingSoundPlaying, float volume, float dt);
    void MoveTowardsPoint(Object& obj, const Vector3& point, float thrust);

    struct RobotInfo;
    float GetRotationSpeed(const RobotInfo& ri);
}
