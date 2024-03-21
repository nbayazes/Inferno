#pragma once

#include "Types.h"

namespace Inferno {

    enum class AIBehavior : uint8 {
        Still = 0x80, // Hold position and fire
        Normal = 0x81,
        Hide = 0x82, // D1: Similar to RunFrom, finds a segment to hide from the player. Probably unused.
        GetBehind = 0x82, // D2: Tries to get behind the player
        RunFrom = 0x83, // Runs away from the player. Brain (overseer), bots that drop mines. Can open doors in vanilla.
        Snipe = 0x84, // D2: Fires extra volleys, extra fast, open doors, falls back
        FollowPathD1 = 0x84, // D1: Similar to RunFrom. Probably unused.
        // In D1 the robot will roam between the "Hide Segment" and the starting segment. 
        // In D2 this seems to be broken.
        Station = 0x85, 
        FollowPathD2 = 0x86, // D2: Used internally by thief?
    };

    //enum class RobotAwareness : uint8 {
    //    None,
    //    NearbyRobotFired = 1,  // Nearby robot fired a weapon
    //    WeaponWallCollision = 2,  // Player weapon hit nearby wall
    //    PlayerCollision = 3,  // Player bumps into robot
    //    WeaponRobotCollision = 4,  // Player weapon hits nearby robot
    //};

    //enum class PlayerVisibility : int8 {
    //    NoLineOfSight,
    //    VisibleNotInFOV,
    //    VisibleInFOV,
    //};

    //enum class AIMode : uint8 {
    //    Still = 0,
    //    Wander = 1,
    //    FollowPath = 2,
    //    ChaseObject = 3,
    //    RunFromObject = 4,
    //    FollowPath2 = 6,
    //    OpenDoor = 7,
    //    Behind = 5, // Descent 2
    //    Hide = 5, // Descent 1
    //    GotoPlayer = 8, // Only for escort behavior
    //    GotoObject = 9, // Only for escort behavior

    //    SnipeAttack = 10,
    //    SnipeFire = 11,
    //    SnipeRetreat = 12,
    //    SnipeRetreatBackwards = 13,
    //    SnipeWait = 14,

    //    ThiefAttack = 15,
    //    ThiefRetreat = 16,
    //    ThiefWait = 17,
    //};

    //enum class AIState : int8 {
    //    None,
    //    Idle,
    //    Search,
    //    Lock,
    //    Flinch,
    //    Fire,
    //    Recoil,
    //    Error
    //};

    enum class Animation : int8 {
        Rest = 0,
        Alert = 1,
        Fire = 2,
        Recoil = 3,
        Flinch = 4
    };
}