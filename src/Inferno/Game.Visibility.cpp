#include "pch.h"
#include "Game.Visibility.h"
#include "Game.Automap.h"
#include "Game.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.h"
#include "logging.h"

namespace Inferno {
    
    Window ToScreenWindow(const Window& src) {
        Window window;

        auto size = Render::Adapter->GetOutputSize();
        window.Left = (src.Left + 1) * size.x * 0.5f;
        window.Right = (src.Right + 1) * size.x * 0.5f;
        window.Top = (1 - src.Top) * size.y * 0.5f;
        window.Bottom = (1 - src.Top) * size.y * 0.5f;
        return window;
    }

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
            auto& info = stack[index++];
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

    Vector3 ProjectPoint(const Vector3& pointWorld, const Matrix& viewProj, bool& crossesViewPlane) {
        auto clip = Vector4::Transform(Vector4(pointWorld.x, pointWorld.y, pointWorld.z, 1), viewProj);
        if (clip.w < 0) crossesViewPlane = true;
        return { Vector3(clip) / abs(clip.w) };
    }
}
