#pragma once

#include "Level.h"
#include "Resources.h"
#include "Types.h"

namespace Inferno {
    // Tries to open a door
    void OpenDoor(Level& level, Tag tag);

    // Updates opened doors
    void UpdateDoors(Level& level, float dt);

    void ActivateTrigger(Level& level, Trigger& trigger);

    // Returns true if the wall has transparent or supertransparent textures, or is an open side.
    bool WallIsTransparent(Level& level, Tag tag);
    void UpdateExplodingWalls(Level& level, float dt);
    void HitWall(Level& level, const Vector3& point, const Object& src, const Wall& wall);

    void AddStuckObject(Tag tag, ObjID id);
    void RemoveStuckObject(Level& level, Tag tag);
    void ResetStuckObjects();
}
