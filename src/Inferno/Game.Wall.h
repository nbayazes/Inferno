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

    // Returns if the wall has transparent or supertransparent textures
    bool WallIsTransparent(Level& level, Tag tag);
    void UpdateExplodingWalls(Level& level, float dt);
    void HitWall(Level& level, const Vector3& point, const Object& src, const Wall& wall);
}
