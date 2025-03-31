#pragma once
#include "Room.h"
#include "Level.h"

namespace Inferno::Game {
    struct FaceInfo {
        float Width, Height;
        Vector3 UpperLeft;
    };

    constexpr int PREFERRED_ROOM_SIZE = 5; // Segments per room

    //FaceInfo GetFaceBounds(const Face& face);
    List<Room> CreateRooms(Inferno::Level&, SegID start = SegID(0), int preferredSegCount = PREFERRED_ROOM_SIZE);
    List<Vector3> ClipConvexPolygon(const List<Vector3>& points, const Plane& plane);
}
