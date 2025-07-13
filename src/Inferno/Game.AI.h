#pragma once

#include "Game.Navigation.h"
#include "GameTimer.h"
#include "Intersect.h"
#include "Level.h"
#include "Object.h"
#include "Robot.h"
#include "SoundTypes.h"

namespace Inferno {
    constexpr float AI_PATH_DELAY = 1; // Default delay for trying to path to the player
    constexpr float AI_AWARENESS_MAX = 1.0f;
    constexpr float AI_COUNT_ALLY_RANGE = 160; // How far to search for allies
    constexpr uint AI_ALLY_FLEE_MIN = 2; // Will flee if fewer than this number of robots are in combat (counting self)
    constexpr float AI_HELP_SEARCH_RADIUS = 800;
    constexpr float AI_HELP_MIN_SEARCH_RADIUS = 175; // Distance that scouts call allies instead of running
    constexpr float AI_OBSERVER_HELP_RADIUS = 250; // Distance that observers call allies
    constexpr float AI_MINE_LAYER_DELAY = 6; // Seconds between robots dropping mines
    constexpr float AI_DOOR_AWARENESS_RADIUS = 160; // Alert distance of opening a door
    constexpr float AI_CURIOSITY_INTERVAL = 5.0f; // Interval to decide to investigate a noise
    constexpr float AI_VISION_FALLOFF_NEAR = 100.0f; // Vision starts getting worse at this range
    constexpr float AI_VISION_FALLOFF_FAR = 400.0f; // Max vision penalty distance
    constexpr float AI_VISION_MAX_PENALTY = 0.5f; // Max vision penalty
    constexpr float AI_REACTION_TIME = 0.05f; // Seconds per difficulty to react to the player in a lit room
    constexpr float AI_ANGER_SPEED = 0.6f; // Multiplier for attacks while angry
    constexpr float AI_GLOBAL_FLEE_DELAY = 20.0f; // how often a robot can run away

    struct AITarget {
        Vector3 Position;
        Vector3 Velocity;

        AITarget() = default;
        AITarget(const Object& obj) : Position(obj.Position), Velocity(obj.Physics.Velocity) {}
    };

    enum class AIState {
        Idle, // No awareness
        Alert, // Awake but not in combat
        Roam, // Wander around randomly
        Combat, // Saw or recently saw target
        BlindFire, // Shooting at the last known target location
        FindHelp, // Looking for help. Taking damage or seeing enemies does not interrupt this.
        Path, // Pathing to a location
    };

    constexpr auto AI_STATE_NAMES = std::to_array<string_view>({
        "Idle", "Alert", "Roam", "Combat", "Blind Fire", "Find Help", "Path"
    });


    //enum class PathFlags {
    //    None,
    //    HighPriority, // Don't stop pathing even if attacked
    //    Attack // Attacks while pathing
    //    FaceGoal // Always faces the final node in the path
    //    FaceTarget // Always faces the target (enemy) if possible
    //};


    //enum class AICombatFlags {
    //    None,
    //    BlindFire = 1 << 0,
    //    Chase = 1 << 1,
    //    GetBehind = 1 << 2,
    //    FallBack, // Retreat while firing at the player (sniper behavior)
    //    AlertAllies,
    //    Still, // ignores circle distance, won't chase
    //    //Dodge
    //};

    enum class PathMode {
        StopVisible, // Stops once the sound is visible
        StopAtEnd // Stops at the end of the path
    };

    struct PathingInfo {
        PathMode mode = PathMode::StopAtEnd;
        List<NavPoint> nodes; // For pathing to another segment
        int16 index = -1;
        bool interruptable = true; // Taking damage or seeing the player will interrupt pathing
        bool faceGoal = false; // Look towards goal instead of the next node
    };

    // Runtime AI data
    struct AIRuntime {
        AIState State = AIState::Idle;
        double LastUpdate = -1; // time when this robot was last updated
        float LastHitByPlayer = -1; // time in seconds since last hit by the player

        // How aware of the player this robot is. Ranges 0 to 1.
        // Only seeing the player can set awareness to 1.
        float Awareness = 0;

        // Increases when allies die or dodging attacks. Only robots with a FleeThreshold have fear.
        float Fear = 0;

        float LostSightDelay = 0; // Time that the target must be out of sight to be considered 'lost'

        uint8 BurstShots; // number of shots fired in the same volley. Resets to 0 when a firing pattern ends.
        uint8 GunIndex = 0; // Which gun to fire from next
        GameTimer FireDelay; // Delay for firing primary weapons
        GameTimer FireDelay2; // Delay for firing secondary weapons

        double LastSeenPlayer; // Time the player was last seen
        float AnimationTimer = 0; // How much of the animation has passed
        float AnimationDuration = 0; // Time in seconds to reach the goal angles
        float MeleeHitDelay = 0; // How long before a melee swing deals damage
        Animation AnimationState = {};

        // Last known target position or point of interest. Can have a target position without a target object.
        // This should be in line of sight if set from combat.
        Option<NavPoint> Target; 
        //Option<NavPoint> LastSeen; // Similar to target position, but persists shortly after the target goes out of sight to simulate the AI "guessing" where it went.
        ObjRef TargetObject; // Current thing we're fighting
        ObjRef Ally; // Robot to get help from

        bool TriedFindingHelp = false;

        Array<Vector3, MAX_SUBMODELS> GoalAngles{};
        Array<Vector3, MAX_SUBMODELS> DeltaAngles{};

        //SegID GoalSegment = SegID::None; // segment the robot wants to move to. Disables pathfinding when set to none.
        //RoomID GoalRoom = RoomID::None;
        //Vector3 GoalPosition; // position the robot wants to move to
        PathingInfo path;

        bool HasPath() const {
            return !path.nodes.empty();
        }

        GameTimer DodgeDelay = 0; // Delay before trying to dodge
        GameTimer DodgeTime = 0; // Remaining time to dodge for
        GameTimer PathDelay; // Delay before trying to path to a new location
        Vector3 DodgeVelocity; // Direction to dodge in, doesn't have to be normalized.

        Vector3 Velocity; // Desired velocity for this update. Clamped at end by max speed.

        float StrafeAngle = -1; // Angle relative to the right axis to strafe in
        Vector3 StrafeDir;
        GameTimer StrafeTimer; // When to stop strafing

        float WeaponCharge = 0; // For robots with charging weapons (fusion hulks)
        float NextChargeSoundDelay = 0; // Delay to play a sound when charging up
        bool ChargingWeapon = false; // Set to true when charging a weapon

        SoundUID SoundHandle = SoundUID::None; // Used to cancel a playing sound when the robot is destroyed
        float RemainingSlow = 0; // How long this robot is slowed (reduced movement and turn speed)
        float RemainingStun = 0; // How long this robot is stunned (unable to act)

        bool DyingSoundPlaying = false;
        float DeathRollTimer = 0; // time passed since dying
        float TeleportDelay = 0; // Delay before next teleport

        float ActiveTime = 0; // How long the robot will stay active before going idle

        GameTimer AlertTimer; // For alerting nearby robots of the player location
        GameTimer CombatSoundTimer; // For playing combat sounds
        GameTimer FleeTimer; // Finds help when this triggers
        GameTimer ChaseTimer; // Delay for chase attempts

        bool Angry = false; // Causes robot to fire faster
        SoundUID AmbientSound = SoundUID::None;

        double LastCollision{}; // Last time this robot collided with another object

        bool PlayingAnimation() const {
            return AnimationTimer < AnimationDuration;
        }

        void ClearPath() {
            path = {};
        }

        void AddAwareness(float awareness, float maxAwareness = AI_AWARENESS_MAX) {
            if (Awareness > maxAwareness)
                return; // Don't reduce existing awareness

            Awareness += awareness;

            if (Awareness > maxAwareness)
                Awareness = maxAwareness;
        }

        float GetDeltaTime() const;

        // Returns true if the robot is firing or about to fire
        bool IsFiring() const {
            return BurstShots > 0 || AnimationState == Animation::Fire || AnimationState == Animation::Recoil || ChargingWeapon;
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

        Set<SegID> GetSegmentsByDistance(SegID start, float /*distance*/) const {
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
    void RobotTouchObject(Object& robot, const Object& obj);
    void AlertRobotsOfNoise(const NavPoint& source, float soundRadius, float awareness, const Object* sourceObj = nullptr);
    void PlayRobotAnimation(const Object& robot, Animation state, float time = 0.4f, float moveMult = 5, float delay = 0);

    // Applies damage to a robot, applying stuns, slows, and waking it up if necessary.
    // Rotates towards source if asleep
    void DamageRobot(const NavPoint& sourcePos, Object& robot, float damage, float stunMult, Object* source);

    void ResetAI(); // Call on level start / load to reset AI state
    // Resizes the internal AI buffer. Keep in sync with the Level.Objects size.
    void ResizeAI(size_t size);

    // Clears all AI targets
    void ResetAITargets();

    AIRuntime& GetAI(const Object& obj);

    void PlayAlertSound(const Object& robot, AIRuntime& ai);

    void AlertEnemiesInSegment(Level& level, const Segment& seg, const NavPoint& source, float soundRadius, float awareness, const Object* sourceObj = nullptr);

    bool DeathRoll(Object& obj, float rollDuration, float elapsedTime, SoundID soundId, bool& dyingSoundPlaying, float volume, float dt);

    void MoveTowardsPoint(const Object& robot, AIRuntime& ai, const Vector3& point, float scale = 1);

    struct RobotInfo;
    void MoveTowardsDir(Object& robot, const Vector3& dir, float dt, float scale = 1);

    // Returns true if a target is in line of sight. Has an optional parameter to return if the target is through a solid wall.
    bool ScanForTarget(const Object& robot, AIRuntime& ai, bool* isThroughWall = nullptr, float* distance = nullptr);
    bool HasLineOfSight(const Object& obj, const Vector3& point, bool precise = false);
    IntersectResult HasFiringLineOfSight(const Object& obj, uint8 gun, const Vector3& target, ObjectMask mask);
    void CombatRoutine(Object& robot, AIRuntime& ai, const RobotInfo& robotInfo, float dt);
    void BeginAIFrame();
    bool IsAnimating(const Object& robot);

    // Helper to get robot info for the current difficulty
    const RobotDifficultyInfo& DifficultyInfo(const RobotInfo& info);
}
