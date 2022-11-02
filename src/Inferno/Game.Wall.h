#pragma once

#include "Level.h"
#include "DataPool.h"
#include "Resources.h"
#include "Editor/Events.h"
#include "Types.h"
#include "Level.h"

namespace Inferno {
    // Tries to open a door
    void OpenDoor(Level& level, Tag tag);
    
    // Updates opened doors
    void UpdateDoors(Level& level, float dt);

    void ActivateTriggerD1(Level& level, Trigger& trigger);
    void ActivateTriggerD2(Level& level, Trigger& trigger);

    // Returns if the wall has transparent or supertransparent textures
    bool WallIsTransparent(Level& level, Tag tag);
    void DamageWall(Level& level, Tag tag, float damage);

    void UpdateExplodingWalls(Level& level, float dt);
}