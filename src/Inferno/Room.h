#pragma once

#include "Level.h"
#include "Types.h"

namespace Inferno {
    enum class RoomID : int16 { None = -1 };

    struct PortalID : Tag {
        RoomID Room = RoomID::None;
    };

    // A room is a group of segments divided by walls

    struct Room {
        List<SegID> Segments;
        List<PortalID> Portals; // Connections to other rooms
        int Reverb = 0;
        Color FogColor;
        float FogDepth;
        //float EstimatedSize;
        //bool ContainsLava{}, ContainsWater{}; // Ambient noise
        SoundID AmbientSound = SoundID::None;
    };

    
}