#include "pch.h"
#include "Game.Visibility.h"
#include "Game.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.h"
#include "logging.h"

namespace Inferno {
    // Windows represent a 2D rectangle.
    // Comparisons are done with top being positive.
    struct Window {
        float Left = 0, Right = 0, Top = 0, Bottom = 0;

        // Clips the window by another window. Returns true if intersects.
        bool Clip(const Window& window) {
            if (!Intersects(window)) return false;
            Left = std::max(window.Left, Left);
            Top = std::min(window.Top, Top);
            Right = std::min(window.Right, Right);
            Bottom = std::max(window.Bottom, Bottom);
            return true;
        }

        // Returns true if the window intersects another window.
        bool Intersects(const Window& window) const {
            if (Left > window.Right || Top < window.Bottom ||
                Right < window.Left || Bottom > window.Top)
                return false;

            return true;
        }

        // Expands the window to another window. Returns true if changed.
        bool Expand(const Window& window) {
            if (window.Left < Left || window.Right > Right ||
                window.Top > Top || window.Bottom < Bottom) {
                Left = std::min(window.Left, Left);
                Top = std::max(window.Top, Top);
                Right = std::max(window.Right, Right);
                Bottom = std::min(window.Bottom, Bottom);
                return true;
            }

            return false;
        }

        // Expands the window to contain a point
        void Expand(const Vector2& point) {
            Left = std::min(point.x, Left);
            Top = std::max(point.y, Top);
            Right = std::max(point.x, Right);
            Bottom = std::min(point.y, Bottom);
        }

        bool IsEmpty() const { return Left == Right && Top == Bottom; }
    };

    constexpr auto EMPTY_WINDOW = Window{ -1, -1, -1, -1 };

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

    void TraverseSegments(const Camera& camera, SegID startSeg, TraversalFlag /*flags*/) {
        if (startSeg == SegID::Terrain) return;

        ASSERT_STA();

        auto& level = Game::Level;

        struct SegmentInfo {
            Window window;
            bool visited = false;
            bool processed = false;
        };

        static List<SegmentInfo> segInfo;
        static List<SegID> renderList;
        segInfo.resize(level.Segments.size());
        renderList.clear();
        renderList.reserve(500);
        ranges::fill(renderList, SegID::None);
        ranges::fill(segInfo, SegmentInfo{});
        Window screenWindow = { -1, 1, 1, -1 };

        const auto calcWindow = [&camera, &level](const Segment& seg, SideID side, const Window& parentWindow) {
            auto indices = seg.GetVertexIndices(side);
            int behindCount = 0;
            Window bounds = { FLT_MAX, -FLT_MAX, -FLT_MAX, FLT_MAX };

            for (auto& index : indices) {
                // project point
                auto& p = level.Vertices[index];
                auto clip = Vector4::Transform(Vector4(p.x, p.y, p.z, 1), camera.ViewProjection);

                if (clip.w < 0)
                    behindCount++; // point is behind camera plane

                auto projected = Vector2{ clip / abs(clip.w) };
                bounds.Expand(projected);
            }

            bool onScreen = bounds.Clip(parentWindow);

            if (behindCount == 4 || !onScreen)
                bounds = EMPTY_WINDOW; // portal is behind camera or offscreen
            else if (behindCount > 0)
                bounds = parentWindow; // a portal crosses view plane, use fallback

            return bounds;
        };

        const auto processSegment = [&](SegID segid, const Window& parentWindow) {
            const auto& adjSeg = level.GetSegment(segid);

            for (auto& sideid : SIDE_IDS) {
                auto connid = adjSeg.Connections[(int)sideid];
                if (connid < SegID(0))
                    continue;

                if (!SideIsTransparent(level, { segid, sideid }))
                    continue; // Opaque wall or no connection

                auto sideWindow = calcWindow(adjSeg, sideid, parentWindow);

                if (sideWindow.IsEmpty())
                    continue; // Side isn't visible from portal

                auto& conn = segInfo[(int)connid];

                if (conn.visited) {
                    if (conn.window.Expand(sideWindow))
                        conn.processed = false; // force reprocess due to window changing

                    continue; // Already visited, don't add it to the render list again
                }
                else {
                    conn.window = sideWindow;
                }

                conn.visited = true;
                Render::Debug::OutlineSegment(level, level.GetSegment(connid), Color(1, 1, 1));
                renderList.push_back(connid);
            }
        };

        // Add the first seg to populate the stack
        segInfo[(int)startSeg].window = screenWindow;
        segInfo[(int)startSeg].visited = true;
        renderList.push_back(startSeg);

        uint renderIndex = 0;

        Render::Debug::OutlineSegment(level, level.GetSegment(startSeg), Color(1, 1, 1));

        while (renderIndex++ < renderList.size()) {
            auto renderListSize = renderList.size();

            // iterate each segment in the render list for each pass in case the window changes
            // due to adjacent segments
            for (size_t i = 0; i < renderListSize; i++) {
                auto segid = renderList[i];
                if (segid == SegID::None) continue;
                Game::Automap.Segments[(int)segid] = Game::AutomapVisibility::Visible;
                auto& info = segInfo[(int)segid];
                if (info.processed) continue;

                info.processed = true;
                //Render::Debug::DrawCanvasBox(info.window.Left, info.window.Right, info.window.Top, info.window.Bottom, Color(0, 1, 0, 0.25f));
                processSegment(segid, info.window);
            }

            if (renderIndex > 1000) {
                SPDLOG_WARN("Maximum segment render count exceeded");
                __debugbreak();
                break;
            }
        }

        Game::Debug::VisibleSegments = (uint)renderList.size();


    }
}
