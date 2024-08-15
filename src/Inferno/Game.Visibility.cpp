#include "pch.h"
#include "Game.Visibility.h"
#include "Game.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.Queue.h"
#include "logging.h"

namespace Inferno {
    struct Window {
        float Left = -1, Right = 1, Top = 1, Bottom = -1;
        //bool CrossesPlane = false;

        // Note: this isn't implemented robustly and order of operations matters
        //Window Intersection(const Window& bounds) const {
        //    auto min = Vector2::Max(bounds.Min, Min);
        //    auto max = Vector2::Min(bounds.Max, Max);
        //    if (max.x <= min.x || max.y <= min.y)
        //        return {}; // no intersection

        //    return { min, max, CrossesPlane };
        //}

        //constexpr bool Empty() const {
        //    return Left == Right || Top == Bottom;
        //}

        void Expand(const Vector2& point) {
            if (point.x < Left) Left = point.x;
            if (point.x > Right) Right = point.x;

            if (point.y > Top) Top = point.y;
            if (point.y < Bottom) Bottom = point.y;
        }


        //static Bounds2D FromPoints(const Array<Vector3, 4>& points) {
        //    Vector2 min(FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX);
        //    bool crossesPlane = false;

        //    for (auto& p : points) {
        //        if (p.x < min.x)
        //            min.x = p.x;
        //        if (p.y < min.y)
        //            min.y = p.y;
        //        if (p.x > max.x)
        //            max.x = p.x;
        //        if (p.y > max.y)
        //            max.y = p.y;

        //        if (p.z < 0)
        //            crossesPlane = true;
        //    }

        //    return { min, max, crossesPlane };
        //}
    };

    constexpr auto EMPTY_WINDOW = Window{ -1, -1, -1, -1 };


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
            SegID id;
            //Window window = {};
            Window window = { 0, 0, 0, 0};
            Window trimmedWindow = {};
            short depth = -1;
            bool visited = false;
            bool processed = false;
            short position = -1; // order to draw in? position in render list
            bool overlap = false;
        };

        static List<SegID> stack;
        //static List<RoomID> results;

        stack.clear();
        //results.clear();


        static List<SegmentInfo> segInfo;
        segInfo.resize(level.Segments.size());
        ranges::fill(segInfo, SegmentInfo{});

        //static List<SegID> renderList;
        //static Queue<SegID> segQueue;
        //static List<short> renderPos;

        //constexpr auto MAX_RENDER_SEGS = 500;
        //renderList.resize(MAX_RENDER_SEGS);
        //renderPos.resize(level.Segments.size());

        //ranges::fill(renderPos, -1);

        //segInfo[(int)startSeg].visited = true;

        //renderList[0] = startSeg;
        //int listIndex = 1;
        //int ecnt = listIndex;

        //constexpr int RENDER_DEPTH = 40;
        //int segid = 0;

        //segQueue.push(startSeg);

        //short depth = 0;

        //stack.push_back(SegmentInfo{.id = startSeg, .visited = true});
        stack.push_back(startSeg);
        segInfo[(int)startSeg].depth = 0;
        uint stackIndex = 0;

        const auto getBounds = [&camera, &level](const Segment& seg, SideID side) {
            Vector2 prx[4];
            int i = 0;
            auto indices = seg.GetVertexIndices(side);
            int behindCount = 0;
            Window bounds = { FLT_MAX, -FLT_MAX, -FLT_MAX, FLT_MAX };

            for (auto& index : indices) {
                // project point
                auto& p = level.Vertices[index];
                auto clip = Vector4::Transform(Vector4(p.x, p.y, p.z, 1), camera.ViewProjection);

                // point is behind camera plane
                if (clip.w < 0) {
                    behindCount++;
                    //bounds.Min = Vector2(-1, -1);
                    //bounds.Max = Vector2(1, 1);
                    //bounds = {};
                    //break;
                }

                auto projected = Vector2{ clip / abs(clip.w) };
                prx[i++] = projected;
                bounds.Expand(projected);
            }

            const auto cross = (prx[1] - prx[0]).Cross(prx[3] - prx[1]);

            if (behindCount == 4) bounds = EMPTY_WINDOW; // portal is behind camera
            if (behindCount > 0) bounds = {}; // a portal crosses view plane, use the whole screen
            if (cross.x > 0) bounds = EMPTY_WINDOW; // portal faces away from camera
            return bounds;
        };

        while (stackIndex < stack.size()) {
            auto segid = stack[stackIndex++];
            auto& info = segInfo[(int)segid];
            if (info.processed) continue;

            info.processed = true;
            //info.depth = depth;
            //auto segnum = renderList[(int)segid];

            //if(depth == 0) {
            //    info.window = {};
            //}

            const auto& window = info.window;
            const auto& seg = level.GetSegment(segid);

            // Determine open and visible connections
            SideID visibleSides[6];
            ranges::fill(visibleSides, SideID::None);

            for (auto& side : SIDE_IDS) {
                Tag tag{ segid, side };
                auto conn = seg.GetConnection(side);

                if (conn < SegID(0) /*|| segInfo[(int)conn].visited */ || !SideIsTransparent(level, tag))
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
                //auto& ri = seg.GetSide(side).GetRenderIndices();
                Vector2 prx[4];
                int i = 0;

                for (auto& index : indices) {
                    // project point
                    auto& p = level.Vertices[index];
                    auto clip = Vector4::Transform(Vector4(p.x, p.y, p.z, 1), camera.ViewProjection);

                    // point is behind camera plane
                    if (clip.w < 0) {
                        behindCount++;
                        //bounds.Min = Vector2(-1, -1);
                        //bounds.Max = Vector2(1, 1);
                        //bounds = {};
                        //break;
                    }

                    auto projected = Vector2{ clip / abs(clip.w) };
                    prx[i++] = projected;
                    //bounds.Expand(Vector2(projected.x, projected.y));
                    bounds.Expand(projected);
                }

                auto a = prx[1] - prx[0];
                auto b = prx[2] - prx[0];
                const auto cross = Vector3(a.x, a.y, 0).Cross(Vector3(b.x, b.y, 0));

                if (behindCount == 4) continue; // portal is behind camera

                //if (behindCount > 0) bounds = {}; // a portal crosses view plane, use the whole screen
                if (cross.z > 0) {
                    Render::Debug::DrawCanvasBox(bounds.Left, bounds.Right, bounds.Top, bounds.Bottom, Color(1, 0, 0, 0.25f));
                    auto& segside = seg.GetSide(side);
                    Render::Debug::DrawArrow(segside.Center, segside.Center + segside.AverageNormal * 4, Color(1,0,0, 1), camera);
                    continue; // portal faces away from camera
                }

                info.window = bounds;

                auto& connInfo = segInfo[(int)conn];
                auto& connWindow = connInfo.window;

                if (connInfo.depth == -1)
                    connInfo.depth = info.depth + 1;

                //auto updateConnectionBounds = [&connWindow](const Window& a, const Window& b) {
                //    connWindow.Left = std::max(a.Left, b.Left); // trim inwards
                //    connWindow.Right = std::min(a.Right, b.Right);
                //    connWindow.Top = std::min(a.Top, b.Top);
                //    connWindow.Bottom = std::max(a.Bottom, b.Bottom);

                //    //if(connWindow.Left > connWindow.Right) connWindow.Left = connWindow.Right;
                //    //connWindow.Left = std::min(connWindow.Left, connWindow.Right);
                //    //connWindow.Right = std::max(connWindow.Left, connWindow.Right);
                //    //connWindow.Top = std::min(connWindow.Top, connWindow.Bottom);
                //    //connWindow.Bottom = std::max(connWindow.Top, connWindow.Bottom);
                //};

                //bool overlap =
                //    connWindow.Left < bounds.Right && connWindow.Right > bounds.Left &&
                //    connWindow.Top > bounds.Bottom && connWindow.Bottom < bounds.Top;


                //if (overlap) {
                //    info.overlap = true;
                //    //updateConnectionBounds(window, bounds);

                //    if (connInfo.visited) {
                //        // Expanding existing window if seg was already visited
                //        if (connWindow.Left < bounds.Left ||
                //            connWindow.Top > bounds.Top ||
                //            connWindow.Right > bounds.Right ||
                //            connWindow.Bottom < bounds.Bottom) {
                //            //connWindow.Left = std::min(connWindow.Left, bounds.Left);
                //            //connWindow.Right = std::max(connWindow.Right, bounds.Right);
                //            //connWindow.Top = std::max(connWindow.Top, bounds.Top);
                //            //connWindow.Bottom = std::min(connWindow.Bottom, bounds.Bottom);
                //            //updateConnectionBounds(connWindow, bounds);
                //        }
                //    }
                //    else {
                //        //updateConnectionBounds(window, bounds);
                //        // Trim child window to the segment window
                //        connWindow.Left = std::max(window.Left, bounds.Left);
                //        connWindow.Right = std::min(window.Right, bounds.Right);
                //        connWindow.Top = std::min(window.Top, bounds.Top);
                //        connWindow.Bottom = std::max(window.Bottom, bounds.Bottom);
                //    }
                //}

                //info.window = bounds;

                //int renderPos = segInfo[(int)conn].position;

                // Expanding existing window if seg was already visited
                //if (connInfo.visited) {
                //    //auto& win = segInfo[connInfo.position].window;
                //    //auto& win = connInfo.window;

                //    if (connWindow.Left < bounds.Left ||
                //        connWindow.Top > bounds.Top ||
                //        connWindow.Right > bounds.Right ||
                //        connWindow.Bottom < bounds.Bottom) {
                //        updateWindowBounds(connWindow, bounds);
                //        //updateWindowBounds(connWindow, win);
                //    }
                //}
                //else {
                //    //if (overlap)
                //    //    Render::Debug::DrawCanvasBox(newWindow.Left, newWindow.Right, newWindow.Top, newWindow.Bottom, Color(0, 1, 0, 0.25f));
                //    //connInfo.position = listIndex;
                //    connInfo.visited = true;
                //    //renderList[listIndex] = conn;
                //    info.depth = depth;
                //    //listIndex++;
                //}

                info.visited = true;

                //if (!connInfo.visited)
                //if (overlap)
                stack.push_back(conn);
            }

            //depth++;
            //if (dbglcnt < windows.size()) {
            //    auto& newWindow = windows[dbglcnt];
            //    Render::Debug::DrawCanvasBox(newWindow.Left, newWindow.Right, newWindow.Top, newWindow.Bottom, Color(1, 0, 0, 0.5f));
            //}
        }

        for (auto& seg : segInfo) {
            //if (!seg.overlap /*|| seg.depth == 0*/) continue;
            if (seg.depth == 1) {
                auto& win = seg.window;
                //Render::Debug::DrawCanvasBox(win.Left, win.Right, win.Top, win.Bottom, Color(0, 1, 0, 0.25f));
            }
        }
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
        auto forward = camera.GetForward();
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

                bool crossesViewPlane = false;
                int dbglcnt = lcnt;

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
                            crossesViewPlane = true;
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
