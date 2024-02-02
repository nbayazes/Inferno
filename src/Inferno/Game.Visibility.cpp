#include "pch.h"
#include "Game.Visibility.h"
#include "Game.h"
#include "logging.h"

namespace Inferno {
    List<RoomID> GetRoomsByDepth(span<Room> rooms, RoomID startRoom, float maxDistance, TraversalFlag flags) {
        ASSERT_STA();

        struct TravelInfo {
            Portal Portal;
            float Distance;
        };

        static List<TravelInfo> stack;
        static List<RoomID> results;

        stack.clear();
        results.clear();

        {
            auto room = Seq::tryItem(rooms, (int)startRoom);
            if (!room) return {};
            results.push_back(startRoom);

            for (auto& portal : room->Portals) {
                if (!Seq::exists(stack, [&portal](const TravelInfo& ti) { return ti.Portal.RoomLink != portal.RoomLink; }))
                    stack.push_back({ portal, 0 });
            }
        }

        uint index = 0;

        while (index != stack.size()) {
            TravelInfo info = stack[index++];
            auto room = Seq::tryItem(rooms, (int)info.Portal.RoomLink);
            if (!room) continue;

            auto wall = Game::Level.TryGetWall(info.Portal.Tag);

            if (wall && StopAtWall(Game::Level, *wall, flags))
                continue;

            if (!Seq::contains(results, info.Portal.RoomLink))
                results.push_back(info.Portal.RoomLink);

            if (!Seq::inRange(room->PortalDistances, info.Portal.PortalLink)) {
                SPDLOG_ERROR("Problem in GetRoomsByDepth()");
                __debugbreak();
                return results;
            }

            auto& portalDistances = room->PortalDistances[info.Portal.PortalLink];

            // check room portal distances
            for (int i = 0; i < room->Portals.size(); i++) {
                if (i == info.Portal.PortalLink) continue;

                auto distance = info.Distance + portalDistances[i];
                auto& endPortal = room->Portals[i];

                
                if (distance < maxDistance && 
                    !Seq::contains(results, endPortal.RoomLink) &&
                    !Seq::exists(stack, [&endPortal](const TravelInfo& ti) { return ti.Portal.RoomLink != endPortal.RoomLink; })
                    ) {
                    stack.push_back({ endPortal, distance });
                    //SPDLOG_INFO("Checking portal {}:{} dist: {}", endPortal.Tag.Segment, endPortal.Tag.Side, distance);
                }
            }
        }

        return results;
    }
}
