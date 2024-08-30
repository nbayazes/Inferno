#include "pch.h"
#include "Game.Visibility.h"
#include "Game.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.h"
#include "logging.h"

namespace Inferno {
    struct Window {
        float Left = 0, Right = 0, Top = 0, Bottom = 0;

        // Clips this window by another window. Returns true if visible.
        bool Clip(const Window& window) {
            if (!Intersects(window)) return false;
            Left = std::max(window.Left, Left);
            Top = std::min(window.Top, Top);
            Right = std::min(window.Right, Right);
            Bottom = std::max(window.Bottom, Bottom);
            return true;
        }

        bool Intersects(const Window& window) const {
            if (Left > window.Right || Top < window.Bottom ||
                Right < window.Left || Bottom > window.Top)
                return false;

            return true;
        }

        // Expands this window to another window. Returns true if changed.
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

    void TraverseSegments(const Camera& camera, SegID startSeg, TraversalFlag flags) {
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

                auto sideWindow = calcWindow(adjSeg, sideid, parentWindow);

                if (sideWindow.IsEmpty())
                    continue; // Side isn't visible from portal

                if (!SideIsTransparent(level, { segid, sideid }))
                    continue; // Opaque wall or no connection

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
                Game::AutomapSegments[(int)segid] = Game::AutomapState::Visible;
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

        Game::Debug::VisibleSegments = renderList.size();
    }

    void TraverseSegmentsOld(const Camera& camera, SegID startSeg, TraversalFlag flags) {
        ASSERT_STA();

        //static List<TravelInfo> stack;
        static List<uint8> visited;
        static List<uint8> processed;
        static List<short> segDepth;
        static List<SegID> renderList;
        static List<short> renderPos;
        static List<Window> windows;

        auto& level = Game::Level;

        visited.resize(level.Segments.size());
        processed.resize(level.Segments.size());
        segDepth.resize(level.Segments.size());
        renderList.resize(level.Segments.size());
        windows.resize(level.Segments.size());
        renderPos.resize(level.Segments.size());

        //auto screenBounds = Bounds2D({ -1, -1 }, { 1, 1 });
        windows[0] = {};

        ranges::fill(visited, false);
        ranges::fill(processed, false);
        ranges::fill(segDepth, 0);
        ranges::fill(renderPos, -1);

        visited[(int)startSeg] = true;

        renderList[0] = startSeg;
        int lcnt = 1;
        int ecnt = lcnt;

        constexpr int RENDER_DEPTH = 40;
        int scnt = 0;

        for (short depth = 0; depth < RENDER_DEPTH; depth++) {
            for (; scnt < ecnt; scnt++) {
                if (processed[scnt]) continue;

                processed[scnt] = true;
                auto segnum = renderList[scnt];
                auto& checkWindow = windows[scnt];
                auto& seg = level.GetSegment(segnum);

                // Determine open and visible connections
                SideID visibleSides[6];
                ranges::fill(visibleSides, SideID::None);

                for (auto& side : SIDE_IDS) {
                    Tag tag{ segnum, side };
                    auto conn = seg.GetConnection(side);

                    if (conn < SegID(0) || visited[(int)conn] || !SideIsTransparent(level, tag))
                        continue; // skip opaque and visited sides

                    visibleSides[(int)side] = side;
                }

                // skip sorting, we have a depth buffer

                //auto basePoints = Render::GetNdc(face, camera.ViewProjection);
                // Expand the viewport to contain all open sides
                //Vector2 min(FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX);
                Window bounds = { FLT_MAX, -FLT_MAX, -FLT_MAX, FLT_MAX };

                for (auto& side : visibleSides) {
                    if (side == SideID::None) continue;
                    auto conn = seg.GetConnection(side);

                    //auto conn = seg.GetConnection(side);
                    //Tag tag{ segnum, side };
                    //auto face = ConstFace::FromSide(level, tag);
                    auto indices = seg.GetVertexIndices(side);
                    int behindCount = 0;

                    for (auto& index : indices) {
                        // project point
                        auto& p = level.Vertices[index];
                        //auto projected = ProjectPoint(p, camera.ViewProjection);

                        auto clip = Vector4::Transform(Vector4(p.x, p.y, p.z, 1), camera.ViewProjection);

                        // point is behind camera plane
                        if (clip.w < 0) {
                            behindCount++;
                            //bounds.Min = Vector2(-1, -1);
                            //bounds.Max = Vector2(1, 1);
                            bounds = {};
                            //break;
                        }

                        auto projected = Vector2{ clip / abs(clip.w) };
                        bounds.Expand(projected);
                    }

                    if (behindCount == 4) continue;
                    //if (crossesViewPlane) break;

                    //Render::Debug::DrawCanvasBox(bounds.Left, bounds.Right, bounds.Top, bounds.Bottom, Color(0, 1, 0, 0.5f));

                    auto& newWindow = windows[lcnt];

                    auto updateWindowBounds = [&newWindow](const Window& a, const Window& b) {
                        newWindow.Left = std::max(a.Left, b.Left); // trim inwards
                        newWindow.Right = std::min(a.Right, b.Right);
                        newWindow.Top = std::min(a.Top, b.Top);
                        newWindow.Bottom = std::max(a.Bottom, b.Bottom);
                    };

                    //bool overlapX = 
                    //    (bounds.Left > checkWindow.Left && bounds.Left < checkWindow.Right) ||
                    //    (bounds.Right > checkWindow.Left && bounds.Right < checkWindow.Right);

                    bool overlap = checkWindow.Left < bounds.Right && checkWindow.Right > bounds.Left &&
                        checkWindow.Top > bounds.Bottom && checkWindow.Bottom < bounds.Top;

                    updateWindowBounds(checkWindow, bounds);

                    int rp = renderPos[(int)conn];

                    // Expanding existing window if seg was already visited
                    if (rp != -1) {
                        auto& win = windows[rp];

                        if (newWindow.Left < win.Left ||
                            newWindow.Top > win.Top ||
                            newWindow.Right > win.Right ||
                            newWindow.Bottom < win.Bottom) {
                            updateWindowBounds(newWindow, win);
                        }
                    }
                    else {
                        if (overlap)
                            Render::Debug::DrawCanvasBox(newWindow.Left, newWindow.Right, newWindow.Top, newWindow.Bottom, Color(0, 1, 0, 0.25f));

                        renderPos[(int)conn] = lcnt;
                        renderList[lcnt] = conn;
                        segDepth[lcnt] = depth;
                        lcnt++;
                        visited[(int)conn] = true;
                    }
                }


                //if (dbglcnt < windows.size()) {
                //    auto& newWindow = windows[dbglcnt];
                //    Render::Debug::DrawCanvasBox(newWindow.Left, newWindow.Right, newWindow.Top, newWindow.Bottom, Color(1, 0, 0, 0.5f));
                //}
            }

            scnt = ecnt;
            ecnt = lcnt;
        }
    }
}
