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
        List<Tag> Portals; // Which tags of this room have connections to other rooms

        int Reverb = 0;
        Color Fog;
        float FogDepth = -1;
        SegmentType Type = SegmentType::None;

        int Meshes{}; // Meshes for each material
        //float EstimatedSize;
        //bool ContainsLava{}, ContainsWater{}; // Ambient noise
        SoundID AmbientSound = SoundID::None;
        DirectX::BoundingOrientedBox Bounds;

        bool Contains(SegID id) {
            return Seq::contains(Segments, id);
        }

        void AddPortal(Tag tag) {
            if (!Seq::contains(Portals, tag))
                Portals.push_back(tag);
        }

        void AddSegment(SegID seg) {
            if (!Seq::contains(Segments, seg))
                Segments.push_back(seg);
        }
    };


    Room* FindRoomBySegment(List<Room>& rooms, SegID seg);
    List<Room> CreateRooms(Level& level);
}
