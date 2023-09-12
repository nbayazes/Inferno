#pragma once
#include "Face.h"
#include "Room.h"
#include "Level.h"

namespace Inferno::Game {
    struct FaceInfo {
        float Width, Height;
        Vector3 UpperLeft;
    };

    //FaceInfo GetFaceBounds(const Face& face);
    List<Room> CreateRooms(Inferno::Level&);
    List<Vector3> ClipConvexPolygon(const List<Vector3>& points, const Plane& plane);
}
