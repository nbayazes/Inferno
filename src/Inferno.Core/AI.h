#pragma once

#include "Types.h"
#include "Polymodel.h"

namespace Inferno {

    enum class AIBehavior : uint8 {
        Still = 0x80,
        Normal = 0x81,
        RunFrom = 0x83,
        // In D1 the robot will roam between the "Hide Segment" and the starting segment. 
        // In D2 this seems to be broken.
        Station = 0x85, 
        Hide = 0x82, // D1
        Behind = 0x82, // D2
        FollowPath = 0x84, // D1
        Snipe = 0x84, // D2
        Follow = 0x86,
    };

    enum class RobotAwareness : uint8 {
        None,
        NearbyRobotFired = 1,  // Nearby robot fired a weapon
        WeaponWallCollision = 2,  // Player weapon hit nearby wall
        PlayerCollision = 3,  // Player bumps into robot
        WeaponRobotCollision = 4,  // Player weapon hits nearby robot
    };

    enum class PlayerVisibility : int8 {
        NoLineOfSight,
        VisibleNotInFOV,
        VisibleInFOV,
    };

    enum class AIMode : uint8 {
        Still = 0,
        Wander = 1,
        FollowPath = 2,
        ChaseObject = 3,
        RunFromObject = 4,
        FollowPath2 = 6,
        OpenDoor = 7,
        Behind = 5, // Descent 2
        Hide = 5, // Descent 1
        GotoPlayer = 8, // Only for escort behavior
        GotoObject = 9, // Only for escort behavior

        SnipeAttack = 10,
        SnipeFire = 11,
        SnipeRetreat = 12,
        SnipeRetreatBackwards = 13,
        SnipeWait = 14,

        ThiefAttack = 15,
        ThiefRetreat = 16,
        ThiefWait = 17,
    };

    // Runtime AI data
    struct AIRuntime {
        RobotAwareness Awareness;
        uint8 PhysicsRetries; // number of retries in physics last time this object got moved.
        uint8 ConsecutiveRetries; // number of retries in consecutive frames without a count of 0
        PlayerVisibility PlayerVisibility;
        uint8 RapidfireCount; // number of shots fired rapidly
        AIMode Mode;
        SegID PathGoal;
        float NextActionTime;
        float FireDelay, FireDelay2; // Delay until firing for primary and secondary weapons
        float AwarenessTime; // How long to remain aware of the player, 0 for unaware
        float LastUpdate; // time since this robot was updated
        float LastSeenPlayer; // Time in seconds since player was seen
        float LastSeenAttackingPlayer; // Time in seconds since at least awareness level 2
        float MiscSoundTime; // Time in seconds since the robot made angry or lurking noises
        Array<Vector3, MAX_SUBMODELS> GoalAngles{}, DeltaAngles{};
        Array<sbyte, MAX_SUBMODELS> GoalState{}, AchievedState{};
    };
}