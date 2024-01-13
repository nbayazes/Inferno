#pragma once
#include "Game.Navigation.h"
#include "Level.h"

namespace Inferno {
    constexpr float ACTIVE_ROOM_DEPTH = 1000; // Portal depth for active rooms

    List<RoomID> GetRoomsByDepth(span<Room> rooms, RoomID startRoom, float maxDistance, TraversalFlag flags);
}
