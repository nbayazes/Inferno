#include "pch.h"
#include "Game.Visibility.h"

namespace Inferno {
    namespace {
        List<RoomID> ActiveRooms;
        RoomID ActiveRoom = RoomID::None;
    }

    List<RoomID> GetRoomsByDepth(Inferno::Level& level, RoomID startRoom, float maxDistance) {
        struct TravelInfo {
            Portal Portal;
            float Distance;
        };

        Stack<TravelInfo> stack;
        Set<RoomID> rooms;
        //SPDLOG_INFO("Traversing rooms");

        {
            auto room = level.GetRoom(startRoom);
            if (!room) return {};
            rooms.insert(startRoom);

            for (auto& portal : room->Portals) {
                stack.push({ portal, 0 });
            }
        }

        while (!stack.empty()) {
            TravelInfo info = stack.top();
            stack.pop();
            auto room = level.GetRoom(info.Portal.RoomLink);
            if (!room) continue;
            //SPDLOG_INFO("Executing on room {} Distance {}", (int)info.Portal.RoomLink, info.Distance);
            rooms.insert(info.Portal.RoomLink);

            auto& startPortal = room->Portals[info.Portal.PortalLink];
            auto& portalDistances = room->PortalDistances[info.Portal.PortalLink];

            // check room portal distances
            for (int i = 0; i < room->Portals.size(); i++) {
                if (i == info.Portal.PortalLink) continue;
                auto distance = info.Distance + portalDistances[i];
                auto& endPortal = room->Portals[i];

                if (distance < maxDistance && !rooms.contains(startPortal.RoomLink)) {
                    stack.push({ endPortal, distance });
                    //SPDLOG_INFO("Checking portal {}:{} dist: {}", endPortal.Tag.Segment, endPortal.Tag.Side, distance);
                }
            }
        }

        return Seq::ofSet(rooms);
    }

    span<RoomID> GetActiveRooms() {
        return ActiveRooms;
    }

    void UpdateActiveRooms(Level& level, RoomID startRoom, float maxDistance) {
        if (ActiveRoom == startRoom) return;
        ActiveRoom = startRoom;
        ActiveRooms = GetRoomsByDepth(level, startRoom, maxDistance);
        // Also need to update active rooms if a player projectile flies through it
    }
}