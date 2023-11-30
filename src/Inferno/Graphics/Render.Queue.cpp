#include "pch.h"
#include "Render.Queue.h"
#include "Game.h"
#include "Game.Wall.h"
#include "LegitProfiler.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Render.Particles.h"
#include "Render.h"

namespace Inferno::Render {
    bool ShouldDrawObject(const Object& obj) {
        if (!obj.IsAlive()) return false;
        bool gameModeHidden = obj.Type == ObjectType::Coop || obj.Type == ObjectType::SecretExitReturn;
        if (Game::GetState() != GameState::Editor && gameModeHidden) return false;
        return true;
    }

    void RenderQueue::Update(Level& level, LevelMeshBuilder& meshBuilder, bool drawObjects) {
        LegitProfiler::ProfilerTask task("Render queue", LegitProfiler::Colors::ALIZARIN);
        _transparentQueue.clear();
        _opaqueQueue.clear();
        _decalQueue.clear();
        _visited.clear();
        _distortionQueue.clear();
        _roomQueue.clear();

        if (Settings::Editor.RenderMode == RenderMode::None) return;

        // Queue commands for level meshes
        for (auto& mesh : meshBuilder.GetMeshes()) {
            if (!Render::CameraFrustum.Contains(mesh.Chunk->Bounds)) continue;
            _opaqueQueue.push_back({ &mesh, 0 });
        }

        for (auto& mesh : meshBuilder.GetDecals()) {
            if (!Render::CameraFrustum.Contains(mesh.Chunk->Bounds)) continue;
            _decalQueue.push_back({ &mesh, 0 });
        }

        if (Game::GetState() == GameState::Editor) {
            UpdateAllEffects(Game::FrameTime);

            for (auto& mesh : meshBuilder.GetWallMeshes()) {
                if (!Render::CameraFrustum.Contains(mesh.Chunk->Bounds)) continue;
                float depth = Vector3::DistanceSquared(Camera.Position, mesh.Chunk->Center);
                _transparentQueue.push_back({ &mesh, depth });
            }

            if (drawObjects) {
                for (auto& obj : level.Objects) {
                    if (!ShouldDrawObject(obj)) continue;
                    DirectX::BoundingSphere bounds(obj.GetPosition(Game::LerpAmount), obj.Radius);
                    if (CameraFrustum.Contains(bounds))
                        QueueEditorObject(obj, Game::LerpAmount);
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
                        _transparentQueue.push_back({ effect, GetRenderDepth(effect->Position) });
                    }
                }
            }

            // Mark all rooms as visible in editor mode
            for (int i = 0; i < level.Rooms.size(); i++) {
                _roomQueue.push_back((RoomID)i);
            }
        }
        else if (!level.Objects.empty()) {
            // todo: should start at camera position
            //TraverseLevel(level.Objects[0].Segment, level, wallMeshes);

            auto roomId = level.GetRoomID(Game::GetPlayerObject());
            TraverseLevelRooms(roomId, level, meshBuilder.GetWallMeshes());
        }

        LegitProfiler::AddCpuTask(std::move(task));
    }

    void RenderQueue::QueueEditorObject(Object& obj, float lerp) {
        auto position = obj.GetPosition(lerp);

        DirectX::BoundingSphere bounds(position, obj.Radius);
        if (!CameraFrustum.Contains(bounds))
            return;

        float depth = GetRenderDepth(position);
        const float maxDistSquared = Settings::Editor.ObjectRenderDistance * Settings::Editor.ObjectRenderDistance;

        if (depth > maxDistSquared && Game::GetState() == GameState::Editor) {
            DrawObjectOutline(obj);
        }
        else if (obj.Render.Model.Outrage) {
            // d3 has transparent model materials mixed with opaque ones. they should be registered with both queues?
            _transparentQueue.push_back({ &obj, depth });
        }
        else if (obj.Render.Type == RenderType::Model && obj.Render.Model.ID != ModelID::None) {
            if (obj.IsCloaked() && Game::GetState() != GameState::Editor) {
                _distortionQueue.push_back({ &obj, depth });
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

    //void RenderQueue::QueueSegmentObjects(Level& level, const Segment& seg) {
    //    _objects.clear();

    //    // queue objects in segment
    //    for (auto oid : seg.Objects) {
    //        if (auto obj = level.TryGetObject(oid)) {
    //            if (!ShouldDrawObject(*obj)) continue;
    //            _objects.push_back({ obj, GetRenderDepth(obj->Position) });
    //        }
    //    }

    //    for (auto& effectId : seg.Effects) {
    //        if (auto effect = GetEffect(effectId)) {
    //            Stats::EffectDraws++;
    //            _objects.push_back({ nullptr, GetRenderDepth(effect->Position), effect });
    //        }
    //    }

    //    // Sort objects in segment by depth
    //    Seq::sortBy(_objects, [](const ObjDepth& a, const ObjDepth& b) {
    //        return a.Depth < b.Depth;
    //    });

    //    // Queue objects in seg
    //    for (auto& obj : _objects) {
    //        if (obj.Obj) {
    //            if (obj.Obj->Render.Type == RenderType::Model &&
    //                obj.Obj->Render.Model.ID != ModelID::None) {
    //                if (obj.Obj->IsCloaked() && Game::GetState() != GameState::Editor) {
    //                    // Cloaked objects render using a different queue
    //                    _distortionQueue.push_back({ obj.Obj, obj.Depth });
    //                }
    //                else {
    //                    // always submit objects to opaque queue, as the renderer will skip
    //                    // non-transparent submeshes
    //                    _opaqueQueue.push_back({ obj.Obj, obj.Depth });

    //                    if (obj.Obj->Render.Model.Outrage) {
    //                        //auto& mesh = GetOutrageMeshHandle(obj.Obj->Render.Model.ID);
    //                        //if (mesh.HasTransparentTexture)
    //                        // outrage models do not set transparent texture flag, but many contain transparent faces
    //                        _transparentQueue.push_back({ obj.Obj, obj.Depth });
    //                    }
    //                    else {
    //                        auto& mesh = GetMeshHandle(obj.Obj->Render.Model.ID);
    //                        if (mesh.IsTransparent)
    //                            _transparentQueue.push_back({ obj.Obj, obj.Depth });
    //                    }
    //                }
    //            }
    //            else {
    //                if(obj.Obj->Render.Type == RenderType::Hostage || obj.Obj->Render.Type == RenderType::Powerup) {
    //                    // Assume all powerups are opaque for now
    //                    _opaqueQueue.push_back({ obj.Obj, obj.Depth });
    //                } else {
    //                    _transparentQueue.push_back({ obj.Obj, obj.Depth });
    //                }
    //            }
    //        }
    //        else if (obj.Effect) {
    //            auto depth = GetRenderDepth(obj.Effect->Position);

    //            if (obj.Effect->Queue == RenderQueueType::Transparent)
    //                _transparentQueue.push_back({ obj.Effect, depth });
    //            else if (obj.Effect->Queue == RenderQueueType::Opaque)
    //                _opaqueQueue.push_back({ obj.Effect, depth });
    //        }
    //    }
    //}

    void RenderQueue::QueueRoomObjects(Level& level, const Room& room) {
        _objects.clear();

        for (auto& segId : room.Segments) {
            auto pseg = level.TryGetSegment(segId);
            if (!pseg) continue;
            auto& seg = *pseg;

            // queue objects in segment
            for (auto oid : seg.Objects) {
                if (auto obj = level.TryGetObject(oid)) {
                    if (!ShouldDrawObject(*obj)) continue;
                    _objects.push_back({ obj, GetRenderDepth(obj->Position) });
                }
            }

            for (auto& effectId : seg.Effects) {
                if (auto effect = GetEffect(effectId)) {
                    Stats::EffectDraws++;
                    _objects.push_back({ nullptr, GetRenderDepth(effect->Position), effect });
                }
            }
        }

        // Sort objects in room by depth
        Seq::sortBy(_objects, [](const ObjDepth& a, const ObjDepth& b) {
            return a.Depth > b.Depth;
        });

        // Add objects to queue
        for (auto& obj : _objects) {
            if (obj.Obj) {
                if (obj.Obj->Render.Type == RenderType::Model &&
                    obj.Obj->Render.Model.ID != ModelID::None) {
                    if (obj.Obj->IsCloaked() && Game::GetState() != GameState::Editor) {
                        _distortionQueue.push_back({ obj.Obj, obj.Depth });
                    }
                    else {
                        // always submit objects to opaque queue, as the renderer will skip
                        // non-transparent submeshes
                        _opaqueQueue.push_back({ obj.Obj, obj.Depth });

                        if (obj.Obj->Render.Model.Outrage) {
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
                auto depth = GetRenderDepth(obj.Effect->Position);
                if (obj.Effect->Queue == RenderQueueType::Transparent)
                    _transparentQueue.push_back({ obj.Effect, depth });
                else if (obj.Effect->Queue == RenderQueueType::Opaque)
                    _opaqueQueue.push_back({ obj.Effect, depth });
            }
        }
    }

    //void RenderQueue::TraverseLevel(SegID startId, Level& level, span<LevelMesh> wallMeshes) {
    //    ScopedTimer levelTimer(&Render::Metrics::QueueLevel);

    //    _objects.clear();
    //    _visited.clear();
    //    _search.push({ startId, 0 });
    //    Stats::EffectDraws = 0;

    //    // todo: add visible lights. Graphics::Lights.AddLight(light);

    //    while (!_search.empty()) {
    //        SegDepth item = _search.front();
    //        _search.pop();

    //        // must check if visited because multiple segs can connect to the same seg before being it is visited
    //        if (_visited.contains(item.Seg)) continue;
    //        _visited.insert(item.Seg);
    //        auto* seg = &level.GetSegment(item.Seg);

    //        Array<SegDepth, 6> children{};

    //        // Find open sides
    //        for (auto& sideId : SideIDs) {
    //            if (!WallIsTransparent(level, { item.Seg, sideId }))
    //                continue; // Can't see through wall

    //            bool culled = false;
    //            // always add adjacent segments to start
    //            if (item.Seg != startId) {
    //                auto vec = seg->Sides[(int)sideId].Center - Camera.Position;
    //                vec.Normalize();

    //                // todo: draw objects in adjacent segments, as objects on the boundary can overlap
    //                if (vec.Dot(seg->Sides[(int)sideId].AverageNormal) >= 0)
    //                    culled = true;
    //            }

    //            auto cid = seg->GetConnection(sideId);
    //            auto cseg = level.TryGetSegment(cid);
    //            if (cseg && !_visited.contains(cid)) {
    //                children[(int)sideId] = {
    //                    .Seg = cid,
    //                    .Depth = GetRenderDepth(cseg->Center),
    //                    .Culled = culled
    //                };
    //            }
    //        }

    //        // Sort connected segments by depth
    //        Seq::sortBy(children, [](const SegDepth& a, const SegDepth& b) {
    //            if (a.Seg == SegID::None) return false;
    //            if (b.Seg == SegID::None) return true;
    //            return a.Depth < b.Depth;
    //        });

    //        if (!item.Culled) {
    //            for (auto& c : children) {
    //                if (c.Seg != SegID::None)
    //                    _search.push(c);
    //            }
    //        }

    //        QueueSegmentObjects(level, *seg);

    //        // queue visible walls (this does not scale well)
    //        // todo: track walls as iterating
    //        for (auto& mesh : wallMeshes) {
    //            if (mesh.Chunk->Tag.Segment == item.Seg)
    //                _transparentQueue.push_back({ &mesh, 0 });
    //        }
    //    }

    //    Stats::VisitedSegments = (uint16)_visited.size();
    //}


    Bounds2D GetBounds(const Array<Vector3, 4>& points) {
        Vector2 min(FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX);

        for (auto& p : points) {
            if (p.x < min.x)
                min.x = p.x;
            if (p.y < min.y)
                min.y = p.y;
            if (p.x > max.x)
                max.x = p.x;
            if (p.y > max.y)
                max.y = p.y;
        }

        return { min, max };
    }


    //Vector3 GetNdc(const Face2& face, int i) {
    //    auto clip = Vector4::Transform(Vector4(face[i].x, face[i].y, face[i].z, 1), Render::ViewProjection);
    //    // Take abs of w, otherwise points behind the plane cause their coords to flip
    //    return Vector3(clip / abs(clip.w));
    //};

    constexpr int MAX_PORTAL_DEPTH = 50;

    // Returns the points of a face in NDC. Returns empty if all points are behind the face.
    Option<Array<Vector3, 4>> GetNdc(const Face2& face, const Matrix& viewProj) {
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

    void RenderQueue::CheckRoomVisibility(Level& level, Room& room, const Bounds2D& srcBounds, int depth) {
        if (depth > MAX_PORTAL_DEPTH) return; // Prevent stack overflow

        for (auto& portal : room.Portals) {
            if (!WallIsTransparent(level, portal.Tag))
                continue; // stop at opaque walls

            auto face = Face2::FromSide(level, portal.Tag);
            auto ndc = GetNdc(face, Render::ViewProjection);
            if (!ndc) continue;
            auto bounds = GetBounds(*ndc);

            if (srcBounds.Overlaps(bounds) && !Seq::contains(_roomQueue, portal.RoomLink)) {
                _roomQueue.push_back(portal.RoomLink);
                if (auto linkedRoom = level.GetRoom(portal.RoomLink))
                    CheckRoomVisibility(level, *linkedRoom, bounds, depth++);
            }
        }
    }

    void RenderQueue::TraverseLevelRooms(RoomID startRoomId, Level& level, span<LevelMesh> wallMeshes) {
        _roomQueue.clear();
        _objects.clear();
        _roomQueue.push_back(startRoomId);

        auto startRoom = level.GetRoom(startRoomId);
        if (!startRoom) return;

        for (auto& basePortal : startRoom->Portals) {
            if (!WallIsTransparent(level, basePortal.Tag))
                continue; // stop at opaque walls like closed doors

            auto baseFace = Face2::FromSide(level, basePortal.Tag);
            auto basePoints = GetNdc(baseFace, Render::ViewProjection);

            if (auto linkedRoom = level.GetRoom(basePortal.RoomLink)) {
                // Search next room if portal is on screen
                if (basePoints) {
                    auto bounds = GetBounds(*basePoints);
                    CheckRoomVisibility(level, *linkedRoom, bounds, 0);
                }

                // always add immediately connected rooms to prevent flickering
                // when lights or effects are slightly out of view in another room
                if (!Seq::contains(_roomQueue, basePortal.RoomLink))
                    _roomQueue.push_back(basePortal.RoomLink);
            }
        }

        //SPDLOG_INFO("Update effects");

        // Reverse the room queue so distant room objects are drawn first
        for (auto rid : _roomQueue | views::reverse) {
            if (auto room = level.GetRoom(rid)) {
                // queue wall meshes
                for (auto& index : room->WallMeshes) {
                    if (!Seq::inRange(wallMeshes, index)) continue;
                    auto& mesh = wallMeshes[index];
                    float depth = Vector3::DistanceSquared(Camera.Position, mesh.Chunk->Center);
                    _transparentQueue.push_back({ &mesh, depth });
                }

                QueueRoomObjects(level, *room);

                // Update effects in the room
                for (auto& sid : room->Segments) {
                    if (auto seg = level.TryGetSegment(sid)) {
                        for (int i = 0; i < seg->Effects.size(); i++) {
                            UpdateEffect(Game::FrameTime, seg->Effects[i]);
                        }
                    }
                }
            }
        }
    }
}
