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
}