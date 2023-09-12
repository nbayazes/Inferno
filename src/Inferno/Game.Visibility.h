#pragma once
#include "Level.h"

namespace Inferno {
    constexpr float ACTIVE_ROOM_DEPTH = 1000; // Portal depth for active rooms

    span<RoomID> GetActiveRooms();

    // Updates the active rooms based on a segment
    void UpdateActiveRooms(Level& level, RoomID startRoom, float maxDistance = ACTIVE_ROOM_DEPTH);
}
