#include "pch.h"
#include "Render.Queue.h"
#include "Game.Automap.h"
#include "Game.h"
#include "Game.Wall.h"
#include "LegitProfiler.h"
#include "MaterialLibrary.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Render.Particles.h"
#include "Render.h"
#include "Render.Level.h"
#include "Resources.h"

namespace Inferno::Render {
    bool ShouldDrawObject(const Object& obj) {
        if (!obj.IsAlive()) return false;
        bool gameModeHidden = obj.Type == ObjectType::Coop || obj.Type == ObjectType::SecretExitReturn;
        if (Game::GetState() != GameState::Editor && gameModeHidden) return false;
        return true;
    }

    void UpdateSegmentEffects(Level& level, SegID sid) {
        if (auto seg = level.TryGetSegment(sid)) {
            for (int i = 0; i < seg->Effects.size(); i++) {
                UpdateEffect(Game::FrameTime, seg->Effects[i]);
            }
        }
    }

    void RenderQueue::Update(Level& level, LevelMeshBuilder& meshBuilder, bool drawObjects, const Camera& camera) {
        LegitProfiler::ProfilerTask task("Render queue", LegitProfiler::Colors::ALIZARIN);
        _transparentQueue.clear();
        _opaqueQueue.clear();
        _decalQueue.clear();
        _visited.clear();
        //_distortionQueue.clear();
        _visibleRooms.clear();
        _roomStack.Reset();

        if (Settings::Editor.RenderMode == RenderMode::None) return;

        // Queue commands for level meshes
        for (auto& mesh : meshBuilder.GetMeshes()) {
            if (!camera.Frustum.Contains(mesh.Chunk->Bounds)) continue;
            _opaqueQueue.push_back({ &mesh, 0 });
        }

        for (auto& mesh : meshBuilder.GetDecals()) {
            if (!camera.Frustum.Contains(mesh.Chunk->Bounds)) continue;
            _decalQueue.push_back({ &mesh, 0 });
        }

        if (Game::GetState() == GameState::Editor) {
            UpdateAllEffects(Game::FrameTime);

            for (auto& mesh : meshBuilder.GetWallMeshes()) {
                if (!camera.Frustum.Contains(mesh.Chunk->Bounds)) continue;
                float depth = Vector3::DistanceSquared(camera.Position, mesh.Chunk->Center);
                _transparentQueue.push_back({ &mesh, depth });
            }

            if (drawObjects) {
                for (auto& obj : level.Objects) {
                    if (!ShouldDrawObject(obj)) continue;
                    DirectX::BoundingSphere bounds(obj.GetPosition(Game::LerpAmount), obj.Radius);
                    if (camera.Frustum.Contains(bounds))
                        QueueEditorObject(obj, Game::LerpAmount, camera);
                }
            }

            //QueueParticles();
            //QueueDebris();
            Seq::sortBy(_transparentQueue, [](const RenderCommand& l, const RenderCommand& r) {
                return l.Depth > r.Depth;
            });

            for (int i = 0; i < level.Segments.size(); i++) {
                for (auto& effectID : level.Segments[i].Effects) {
                    if (auto effect = GetEffect(effectID)) {
                        _transparentQueue.push_back({ effect, GetRenderDepth(effect->Position, camera) });
                    }
                }
            }

            for (auto& effectID : level.Terrain.Effects) {
                if (auto effect = GetEffect(effectID)) {
                    _transparentQueue.push_back({ effect, GetRenderDepth(effect->Position, camera) });
                }
            }

            // Mark all rooms as visible in editor mode
            for (int i = 0; i < level.Rooms.size(); i++) {
                _visibleRooms.push_back((RoomID)i);
            }
        }
        else if (!level.Objects.empty()) {
            TraverseSegments(level, camera, meshBuilder.GetWallMeshes(), Game::GetActiveCamera().Segment);
        }

        // Draw effects and objects on the terrain
        auto& player = Game::GetPlayerObject();
        if (player.Segment == SegID::Terrain || Game::GetState() == GameState::EscapeSequence) {
            UpdateSegmentEffects(level, SegID::Terrain);

            for (auto& oid : level.Terrain.Objects) {
                if (auto object = level.TryGetObject(oid)) {
                    float depth = GetRenderDepth(object->Position, camera);
                    _objects.push_back({ object, depth });
                }
            }

            for (auto& effectId : level.Terrain.Effects) {
                if (auto effect = GetEffect(effectId)) {
                    _objects.push_back({ nullptr, GetRenderDepth(effect->Position, camera), effect });
                }
            }

            QueueSegmentObjects(level, level.Terrain, camera);
        }

        LegitProfiler::AddCpuTask(std::move(task));
    }

    void RenderQueue::QueueEditorObject(Object& obj, float lerp, const Camera& camera) {
        auto position = obj.GetPosition(lerp);

        DirectX::BoundingSphere bounds(position, obj.Radius);
        if (!camera.Frustum.Contains(bounds))
            return;

        float depth = GetRenderDepth(position, camera);
        const float maxDistSquared = Settings::Editor.ObjectRenderDistance * Settings::Editor.ObjectRenderDistance;

        if (depth > maxDistSquared && Game::GetState() == GameState::Editor && !Settings::Editor.HideUI) {
            DrawObjectOutline(obj, camera);
        }
        else if (obj.Render.Model.Outrage) {
            // d3 has transparent model materials mixed with opaque ones. they should be registered with both queues?
            _transparentQueue.push_back({ &obj, depth });
        }
        else if (obj.Render.Type == RenderType::Model && obj.Render.Model.ID != ModelID::None) {
            if (obj.IsCloaked() && Game::GetState() != GameState::Editor) {
                _transparentQueue.push_back({ &obj, depth });
            }
            else {
                _opaqueQueue.push_back({ &obj, depth });

                bool transparentOverride = false;
                auto texOverride = TexID::None;

                if (obj.Render.Model.TextureOverride != LevelTexID::None) {
                    texOverride = Resources::LookupTexID(obj.Render.Model.TextureOverride);
                    if (texOverride != TexID::None)
                        transparentOverride = Resources::GetTextureInfo(texOverride).Transparent;
                }

                auto& mesh = GetMeshHandle(obj.Render.Model.ID);
                if (mesh.IsTransparent || transparentOverride)
                    _transparentQueue.push_back({ &obj, depth });
            }
        }
        else {
            _transparentQueue.push_back({ &obj, depth });
            //if (obj.Render.Type == RenderType::Hostage || obj.Render.Type == RenderType::Powerup) {
            //    // Assume all powerups are opaque for now
            //    _opaqueQueue.push_back({ &obj, depth });
            //}
            //else {
            //    _transparentQueue.push_back({ &obj, depth });
            //}
        }
    }

    void RenderQueue::QueueSegmentObjects(Level& level, const Segment& seg, const Camera& camera) {
        _objects.clear();
        auto state = Game::GetState();

        // queue objects in segment
        for (auto oid : seg.Objects) {
            if (oid == ObjID(0)) {
                if ((state == GameState::Game || state == GameState::PauseMenu) && !Game::Player.IsDead)
                    continue;

                if (GetEscapeScene() == EscapeScene::Start)
                    continue;
            }

            if (auto obj = level.TryGetObject(oid)) {
                if (!ShouldDrawObject(*obj)) continue;
                _objects.push_back({ obj, GetRenderDepth(obj->Position, camera) });
            }
        }

        for (auto& effectId : seg.Effects) {
            if (auto effect = GetEffect(effectId)) {
                Stats::EffectDraws++;
                _objects.push_back({ nullptr, GetRenderDepth(effect->Position, camera), effect });
            }
        }

        // Sort objects in segment by depth
        Seq::sortBy(_objects, [](const ObjDepth& a, const ObjDepth& b) {
            return a.Depth < b.Depth;
        });

        // Queue objects in seg
        for (auto& obj : _objects) {
            if (obj.Obj) {
                if (obj.Obj->Render.Type == RenderType::Model &&
                    obj.Obj->Render.Model.ID != ModelID::None) {
                    if (obj.Obj->IsCloaked() && Game::GetState() != GameState::Editor) {
                        _transparentQueue.push_back({ obj.Obj, obj.Depth });
                        //_distortionQueue.push_back({ obj.Obj, obj.Depth });
                    }
                    else {
                        // always submit objects to opaque queue, as the renderer will skip
                        // non-transparent submeshes
                        _opaqueQueue.push_back({ obj.Obj, obj.Depth });

                        if (obj.Obj->Render.Model.Outrage) {
                            //auto& mesh = GetOutrageMeshHandle(obj.Obj->Render.Model.ID);
                            //if (mesh.HasTransparentTexture)
                            // outrage models do not set transparent texture flag, but many contain transparent faces
                            _transparentQueue.push_back({ obj.Obj, obj.Depth });
                        }
                        else {
                            auto& mesh = GetMeshHandle(obj.Obj->Render.Model.ID);
                            if (mesh.IsTransparent)
                                _transparentQueue.push_back({ obj.Obj, obj.Depth });
                        }
                    }
                }
                else {
                    // Assume all powerups are transparent for now
                    _transparentQueue.push_back({ obj.Obj, obj.Depth });

                    //if (obj.Obj->Render.Type == RenderType::Hostage || obj.Obj->Render.Type == RenderType::Powerup) {
                    //    // Assume all powerups are opaque for now
                    //    _opaqueQueue.push_back({ obj.Obj, obj.Depth });
                    //}
                    //else {
                    //    _transparentQueue.push_back({ obj.Obj, obj.Depth });
                    //}
                }
            }
            else if (obj.Effect) {
                auto depth = GetRenderDepth(obj.Effect->Position, camera);

                if (obj.Effect->Queue == RenderQueueType::Transparent)
                    _transparentQueue.push_back({ obj.Effect, depth });
                else if (obj.Effect->Queue == RenderQueueType::Opaque)
                    _opaqueQueue.push_back({ obj.Effect, depth });
            }
        }
    }

    //void RenderQueue::QueueRoomObjects(Level& level, const Room& room, const Camera& camera) {
    //    _objects.clear();

    //    for (auto& segId : room.Segments) {
    //        auto pseg = level.TryGetSegment(segId);
    //        if (!pseg) continue;
    //        auto& seg = *pseg;

    //        // queue objects in segment
    //        for (auto oid : seg.Objects) {
    //            if (auto obj = level.TryGetObject(oid)) {
    //                if (!ShouldDrawObject(*obj)) continue;
    //                _objects.push_back({ obj, GetRenderDepth(obj->Position, camera) });
    //            }
    //        }

    //        for (auto& effectId : seg.Effects) {
    //            if (auto effect = GetEffect(effectId)) {
    //                Stats::EffectDraws++;
    //                _objects.push_back({ nullptr, GetRenderDepth(effect->Position, camera), effect });
    //            }
    //        }
    //    }

    //    // Sort objects in room by depth
    //    Seq::sortBy(_objects, [](const ObjDepth& a, const ObjDepth& b) {
    //        return a.Depth > b.Depth;
    //    });
    //}

    //Vector3 GetNdc(const Face2& face, int i) {
    //    auto clip = Vector4::Transform(Vector4(face[i].x, face[i].y, face[i].z, 1), Render::ViewProjection);
    //    // Take abs of w, otherwise points behind the plane cause their coords to flip
    //    return Vector3(clip / abs(clip.w));
    //};

    constexpr int MAX_PORTAL_DEPTH = 50;

    // Returns the points of a face in NDC. Returns empty if all points are behind the face.
    Option<Array<Vector3, 4>> GetNdc(const ConstFace& face, const Matrix& viewProj) {
        Array<Vector3, 4> points;
        int behind = 0;
        for (int i = 0; i < 4; i++) {
            auto clip = Vector4::Transform(Vector4(face[i].x, face[i].y, face[i].z, 1), viewProj);
            if (clip.w < 0) behind++;
            points[i] = Vector3(clip / abs(clip.w));
        }

        if (behind == 4) return {}; // all points behind plane
        return points;
    }

    void DrawBounds(const Bounds2D& bounds, const Color& color) {
        Vector2 pixels[4]{};
        auto size = Render::Adapter->GetOutputSize();

        pixels[0].x = (bounds.Min.x + 1) * size.x * 0.5f;
        pixels[0].y = (1 - bounds.Min.y) * size.y * 0.5f;
        //auto z0 = Render::Camera.NearClip + (*basePoints)[0].z * (Render::Camera.FarClip - Render::Camera.NearClip);

        pixels[1].x = (bounds.Max.x + 1) * size.x * 0.5f;
        pixels[1].y = (1 - bounds.Min.y) * size.y * 0.5f;
        //auto z1 = Render::Camera.NearClip + (*basePoints)[1].z * (Render::Camera.FarClip - Render::Camera.NearClip);

        pixels[2].x = (bounds.Max.x + 1) * size.x * 0.5f;
        pixels[2].y = (1 - bounds.Max.y) * size.y * 0.5f;
        //auto z2 = Render::Camera.NearClip + (*basePoints)[2].z * (Render::Camera.FarClip - Render::Camera.NearClip);

        pixels[3].x = (bounds.Min.x + 1) * size.x * 0.5f;
        pixels[3].y = (1 - bounds.Max.y) * size.y * 0.5f;
        //auto z3 = Render::Camera.NearClip + (*basePoints)[3].z * (Render::Camera.FarClip - Render::Camera.NearClip);

        CanvasPayload payload{};
        payload.Texture = Render::Materials->White().Handle();
        auto hex = color.RGBA().v;
        payload.V0 = CanvasVertex(pixels[0], {}, hex);
        payload.V1 = CanvasVertex(pixels[1], {}, hex);
        payload.V2 = CanvasVertex(pixels[2], {}, hex);
        payload.V3 = CanvasVertex(pixels[3], {}, hex);
        DebugCanvas->Draw(payload);
    }

    //void RenderQueue::CheckRoomVisibility(Level& level, const Portal& srcPortal, const Bounds2D& srcBounds, const Camera& camera) {
    //    auto room = level.GetRoom(srcPortal.RoomLink);
    //    if (!room) return;

    //    _roomStack.Push(srcPortal.RoomLink);

    //    for (auto& portal : room->Portals) {
    //        //if (Seq::contains(_roomQueue, portal.RoomLink))
    //        if (_roomStack.Contains(portal.RoomLink))
    //            continue; // Already visited linked room

    //        if (!SideIsTransparent(level, portal.Tag))
    //            continue; // stop at opaque walls

    //        auto face = ConstFace::FromSide(level, portal.Tag);
    //        if (!camera.Frustum.Contains(face[0], face[1], face[2])) continue;
    //        if (!camera.Frustum.Contains(face[1], face[2], face[3])) continue;

    //        //auto dot = face.AverageNormal().Dot(srcFace.AverageNormal());

    //        auto ndc = GetNdc(face, camera.ViewProjection);
    //        if (!ndc) continue;
    //        auto bounds = Bounds2D::FromPoints(*ndc);

    //        if (bounds.CrossesPlane)
    //            bounds = srcBounds; // Uncertain where the bounds of the portal are, use previous bounds
    //        else
    //            bounds = srcBounds.Intersection(bounds);

    //        if (!bounds.Empty()) {
    //            if (!Seq::contains(_visibleRooms, portal.RoomLink))
    //                _visibleRooms.push_back(portal.RoomLink);

    //            // Keep searching...
    //            if (Settings::Editor.ShowPortals)
    //                DrawBounds(bounds, Color(0, 1, 0, 0.2f));

    //            CheckRoomVisibility(level, portal, bounds, camera);
    //        }
    //    }

    //    _roomStack.Rewind(srcPortal.RoomLink);
    //}

    //void RenderQueue::TraverseLevelRooms(RoomID startRoomId, Level& level, span<LevelMesh> wallMeshes, const Camera& camera) {
    //    _objects.clear();
    //    _visibleRooms.clear();
    //    _visibleRooms.push_back(startRoomId);

    //    auto startRoom = level.GetRoom(startRoomId);
    //    if (!startRoom) return;
    //    //auto screenBounds = Bounds2D({ -.75f, -.75f }, { .75f, .75f });
    //    auto screenBounds = Bounds2D({ -1, -1 }, { 1, 1 });

    //    _roomStack.Push(startRoomId);

    //    for (auto& portal : startRoom->Portals) {
    //        if (!SideIsTransparent(level, portal.Tag))
    //            continue; // stop at opaque walls like closed doors

    //        auto face = ConstFace::FromSide(level, portal.Tag);
    //        auto basePoints = GetNdc(face, camera.ViewProjection);

    //        // Search next room if portal is on screen
    //        if (basePoints) {
    //            //if (!Seq::contains(_roomQueue, basePortal.RoomLink))
    //            //    _roomQueue.push_back(basePortal.RoomLink);

    //            if (!Seq::contains(_visibleRooms, portal.RoomLink))
    //                _visibleRooms.push_back(portal.RoomLink);

    //            // Check if the frustum contains the portal (can cross the view plane)
    //            //if (!Render::CameraFrustum.Contains(face[0], face[1], face[2])) continue;
    //            //if (!Render::CameraFrustum.Contains(face[1], face[2], face[3])) continue;

    //            auto bounds = Bounds2D::FromPoints(*basePoints);
    //            bounds = bounds.Intersection(screenBounds);
    //            if (bounds.Empty())
    //                continue;

    //            if (bounds.CrossesPlane)
    //                bounds = screenBounds; // Uncertain where the bounds of the portal are, use the whole screen

    //            if (Settings::Editor.ShowPortals)
    //                DrawBounds(bounds, Color(0, 0, 1, 0.2f));

    //            CheckRoomVisibility(level, portal, bounds, camera);
    //        }
    //    }

    //    // grow visible rooms by one to prevent flicker when lights are on room boundaries at the edge of vision or behind the view
    //    auto startSize = _visibleRooms.size();
    //    for (int i = 0; i < startSize; i++) {
    //        auto room = level.GetRoom(_visibleRooms[i]);
    //        if (!room) continue;

    //        for (auto& portal : room->Portals) {
    //            if (!SideIsTransparent(level, portal.Tag))
    //                continue; // Closed or opaque side

    //            if (!Seq::contains(_visibleRooms, portal.RoomLink))
    //                _visibleRooms.push_back(portal.RoomLink);
    //        }
    //    }

    //    //SPDLOG_INFO("Update effects");

    //    // Reverse the room queue so distant room objects are drawn first
    //    for (auto rid : _visibleRooms | views::reverse) {
    //        if (auto room = level.GetRoom(rid)) {
    //            // queue wall meshes
    //            for (auto& index : room->WallMeshes) {
    //                if (!Seq::inRange(wallMeshes, index)) continue;
    //                auto& mesh = wallMeshes[index];
    //                float depth = Vector3::DistanceSquared(camera.Position, mesh.Chunk->Center);
    //                _transparentQueue.push_back({ &mesh, depth });
    //            }

    //            QueueRoomObjects(level, *room, camera);
    //            SubmitObjects(camera);

    //            for (auto& sid : room->Segments) {
    //                UpdateSegmentEffects(level, sid);
    //            }
    //        }
    //    }
    //}

    void RenderQueue::TraverseSegments(Level& level, const Camera& camera, span<LevelMesh> wallMeshes, SegID startSeg) {
        _segInfo.resize(level.Segments.size());
        ranges::fill(_segInfo, SegmentInfo{});

        _renderList.clear();
        _renderList.reserve(500);
        ranges::fill(_renderList, SegID::None);

        _roomList.resize(level.Rooms.size());
        ranges::fill(_roomList, false);

        _visibleRooms.clear();

        if (startSeg == SegID::Terrain)
            startSeg = Game::Terrain.ExitTag.Segment; // Assume the exit tunnel is visible

        if (startSeg < SegID(0)) return;

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

                auto& conn = _segInfo[(int)connid];

                if (conn.visited) {
                    if (conn.window.Expand(sideWindow))
                        conn.processed = false; // force reprocess due to window changing

                    continue; // Already visited, don't add it to the render list again
                }
                else {
                    conn.window = sideWindow;
                }

                conn.visited = true;

                if (Settings::Graphics.OutlineVisibleRooms)
                    Render::Debug::OutlineSegment(level, level.GetSegment(connid), Color(1, 1, 1));

                _renderList.push_back(connid);
            }
        };

        // Add the first seg to populate the stack
        _segInfo[(int)startSeg].window = screenWindow;
        _segInfo[(int)startSeg].visited = true;
        _renderList.push_back(startSeg);

        uint renderIndex = 0;

        if (Settings::Graphics.OutlineVisibleRooms)
            Render::Debug::OutlineSegment(level, level.GetSegment(startSeg), Color(1, 1, 1, 0.25f));

        auto queueSegment = [&, this](SegID segid) {
            auto& seg = level.GetSegment(segid);
            QueueSegmentObjects(level, seg, camera);
            UpdateSegmentEffects(level, segid);

            // queue walls in segment
            for (auto& mesh : wallMeshes) {
                if (mesh.Chunk->Tag.Segment == segid)
                    _transparentQueue.push_back({ &mesh, 0 });
            }

            if (!UseRoomLighting)
                Render::DrawSegmentLights(segid);

            if (auto rl = Seq::tryItem(_roomList, (int)seg.Room))
                *rl = true;
        };

        while (renderIndex++ < _renderList.size()) {
            auto renderListSize = _renderList.size();

            // iterate each segment in the render list for each pass in case the window changes
            // due to adjacent segments
            for (size_t i = 0; i < renderListSize && i < Game::Automap.Segments.size(); i++) {
                auto segid = _renderList[i];
                if (segid == SegID::None) continue;
                Game::Automap.Segments[(int)segid] = AutomapVisibility::Visible;
                auto& info = _segInfo[(int)segid];
                if (info.processed) continue;

                info.processed = true;
                //Render::Debug::DrawCanvasBox(info.window.Left, info.window.Right, info.window.Top, info.window.Bottom, Color(0, 1, 0, 0.25f));
                processSegment(segid, info.window);

                if (!info.queued) {
                    info.queued = true;
                    queueSegment(segid);
                }
            }

            if (renderIndex > 1000) {
                SPDLOG_WARN("Maximum segment render count exceeded");
                __debugbreak();
                break;
            }
        }

        // extend past the visible segments so lights and objects don't get clipped
        auto growVisible = [&, this] {
            for (auto& segid : _renderList) {
                const auto& seg = level.GetSegment(segid);

                for (auto& sideid : SIDE_IDS) {
                    auto connid = seg.Connections[(int)sideid];
                    if (connid < SegID(0))
                        continue;

                    if (!SideIsTransparent(level, { segid, sideid }))
                        continue; // Opaque wall or no connection

                    auto& conn = _segInfo[(int)connid];
                    if (conn.visited)
                        continue;

                    queueSegment(connid);
                    conn.visited = true;

                    if (Settings::Graphics.OutlineVisibleRooms)
                        Render::Debug::OutlineSegment(level, level.GetSegment(connid), Color(0.65f, 0.65f, 1, 0.5f));

                    _renderList.push_back(connid);
                }
            }
        };

        // expand visible segments twice to reduce light and object popin
        growVisible();
        growVisible();

        // Mark the visible rooms for object updates
        for (int i = 0; i < _roomList.size(); i++) {
            if (_roomList[i] == false) continue;

            _visibleRooms.push_back((RoomID)i);

            // Draw lights using rooms
            if (UseRoomLighting) {
                if (auto room = level.GetRoom((RoomID)i)) {
                    for (auto& segid : room->Segments) {
                        Render::DrawSegmentLights(segid);
                    }
                }
            }
        }

        ranges::reverse(_transparentQueue); // reverse the queue so it draws back to front

        Game::Debug::VisibleSegments = (uint)_renderList.size();
    }
}
