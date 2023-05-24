#pragma once

#include "Level.h"
#include "Types.h"

namespace Inferno {
    struct Room {
        List<SegID> Segments;
        List<Tag> Portals;

        int Reverb = 0;
        Color Fog;
        float FogDepth = -1;

        int Meshes{}; // Meshes for each material
    };

    inline Room CreateRoom(Level& level, SegID start) {
        Set<SegID> segments;
        Stack<SegID> search;
        search.push(start);

        Room room;
        auto& startSeg = level.GetSegment(start);

        // todo: track segment type and insert portals if it changes
        // todo: ignore illusionary walls if they're inside an energy center
        while (!search.empty()) {
            auto tag = search.top();
            search.pop();

            auto& seg = level.GetSegment(tag);
            segments.insert(tag);

            for (auto& side : SideIDs) {
                if (!seg.SideHasConnection(side)) continue; // nothing to do here

                auto conn = seg.GetConnection(side);
                auto& cseg = level.GetSegment(conn);

                bool addPortal = false;
                if (auto wall = level.TryGetWall({ tag, side })) {
                    addPortal = true;
                    if (wall->Type == WallType::FlyThroughTrigger)
                        addPortal = false; // invisible walls

                    if (wall->Type == WallType::Illusion &&
                        seg.Type == SegmentType::Energy && startSeg.Type == SegmentType::Energy) {
                        addPortal = false; // don't split energy centers into separate rooms
                    }
                }

                addPortal |= cseg.Type != startSeg.Type; // new room if seg type changes

                if (addPortal) {
                    room.Portals.push_back({ tag, side });
                    continue;
                }

                if (conn > SegID::None && !segments.contains(conn)) {
                    search.push(conn);
                }
            }
        }

        room.Segments = Seq::ofSet(segments);
        return room;
    }

    inline List<Room> CreateRooms(Level& level) {
        Set<SegID> segments;
        List<Room> rooms;

        Stack<SegID> search;
        search.push(SegID(0));

        while (!search.empty()) {
            auto id = search.top();
            search.pop();
            if (segments.contains(id)) continue; // already visited

            auto room = CreateRoom(level, id);

            // Add connections
            for (auto& portal : room.Portals) {
                auto& seg = level.GetSegment(portal);
                auto conn = seg.GetConnection(portal.Side);
                search.push(conn);
            }

            Seq::insert(segments, room.Segments);
            rooms.push_back(std::move(room));
        }
        // todo: split large rooms in two (or more). track nodes and prefer single width tunnels

        return rooms;
    }
}
