#include "pch.h"
#include "Game.Visibility.h"

namespace Inferno {

    List<RoomID> GetRoomsByDepth(span<Room> rooms, RoomID startRoom, float maxDistance) {
        struct TravelInfo {
            Portal Portal;
            float Distance;
        };

        Stack<TravelInfo> stack;
        Set<RoomID> results;
        //SPDLOG_INFO("Traversing rooms");

        {
            auto room = Seq::tryItem(rooms, (int)startRoom);
            if (!room) return {};
            results.insert(startRoom);

            for (auto& portal : room->Portals) {
                stack.push({ portal, 0 });
            }
        }

        while (!stack.empty()) {
            TravelInfo info = stack.top();
            stack.pop();
            auto room = Seq::tryItem(rooms, (int)info.Portal.RoomLink);
            if (!room) continue;
            //SPDLOG_INFO("Executing on room {} Distance {}", (int)info.Portal.RoomLink, info.Distance);
            results.insert(info.Portal.RoomLink);

            auto& portalDistances = room->PortalDistances[info.Portal.PortalLink];

            // check room portal distances
            for (int i = 0; i < room->Portals.size(); i++) {
                if (i == info.Portal.PortalLink) continue;
                auto distance = info.Distance + portalDistances[i];
                auto& endPortal = room->Portals[i];

                if (distance < maxDistance && !results.contains(endPortal.RoomLink)) {
                    stack.push({ endPortal, distance });
                    //SPDLOG_INFO("Checking portal {}:{} dist: {}", endPortal.Tag.Segment, endPortal.Tag.Side, distance);
                }
            }
        }

        return Seq::ofSet(results);
    }

}