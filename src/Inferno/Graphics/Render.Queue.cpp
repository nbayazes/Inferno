#include "pch.h"
#include "Render.Queue.h"

#include "Game.h"
#include "Game.Wall.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Render.Particles.h"
#include "Render.h"
#include "ScopedTimer.h"

namespace Inferno::Render {
    bool ShouldDrawObject(const Object& obj) {
        if (!obj.IsAlive()) return false;
        bool gameModeHidden = obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop || obj.Type == ObjectType::SecretExitReturn;
        if (Game::GetState() != GameState::Editor && gameModeHidden) return false;
        return true;
    }

    void RenderQueue::Update(Level& level, span<LevelMesh> levelMeshes, span<LevelMesh> wallMeshes) {
        _transparentQueue.clear();
        _opaqueQueue.clear();
        _visited.clear();

        if (Settings::Editor.RenderMode == RenderMode::None) return;

        // Queue commands for level meshes
        for (auto& mesh : levelMeshes)
            _opaqueQueue.push_back({ &mesh, 0 });

        if (Game::GetState() == GameState::Editor) {
            for (auto& mesh : wallMeshes) {
                float depth = Vector3::DistanceSquared(Camera.Position, mesh.Chunk->Center);
                _transparentQueue.push_back({ &mesh, depth });
            }

            if (Settings::Editor.ShowObjects) {
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
                return l.Depth < r.Depth; // front to back, because the draw call flips it
            });

            for (int i = 0; i < level.Segments.size(); i++) {
                for (auto& effectID : level.Segments[i].Effects) {
                    if (auto effect = GetEffect(effectID)) {
                        _transparentQueue.push_back({ effect, GetRenderDepth(effect->Position) });
                    }
                }
            }
        }
        else if (!level.Objects.empty()) {
            // todo: should start at camera position
            TraverseLevel(level.Objects[0].Segment, level, wallMeshes);

            auto roomId = level.GetRoomID(Game::GetPlayer());
            TraverseLevelRooms(roomId, level, wallMeshes);
        }
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
        else {
            _transparentQueue.push_back({ &obj, depth });
        }
    }

    void RenderQueue::TraverseLevel(SegID startId, Level& level, span<LevelMesh> wallMeshes) {
        ScopedTimer levelTimer(&Render::Metrics::QueueLevel);

        _visited.clear();
        _search.push({ startId, 0 });
        Stats::EffectDraws = 0;

        struct ObjDepth {
            Object* Obj = nullptr;
            float Depth = 0;
            EffectBase* Effect;
        };
        List<ObjDepth> objects;

        // todo: add visible lights. Graphics::Lights.AddLight(light);

        while (!_search.empty()) {
            SegDepth item = _search.front();
            _search.pop();

            // must check if visited because multiple segs can connect to the same seg before being it is visited
            if (_visited.contains(item.Seg)) continue;
            _visited.insert(item.Seg);
            auto* seg = &level.GetSegment(item.Seg);

            Array<SegDepth, 6> children{};

            // Find open sides
            for (auto& sideId : SideIDs) {
                if (!WallIsTransparent(level, { item.Seg, sideId }))
                    continue; // Can't see through wall

                bool culled = false;
                // always add adjacent segments to start
                if (item.Seg != startId) {
                    auto vec = seg->Sides[(int)sideId].Center - Camera.Position;
                    vec.Normalize();

                    // todo: draw objects in adjacent segments, as objects on the boundary can overlap
                    if (vec.Dot(seg->Sides[(int)sideId].AverageNormal) >= 0)
                        culled = true;
                }

                auto cid = seg->GetConnection(sideId);
                auto cseg = level.TryGetSegment(cid);
                if (cseg && !_visited.contains(cid)) {
                    children[(int)sideId] = {
                        .Seg = cid,
                        .Depth = GetRenderDepth(cseg->Center),
                        .Culled = culled
                    };
                }
            }

            // Sort connected segments by depth
            Seq::sortBy(children, [](const SegDepth& a, const SegDepth& b) {
                if (a.Seg == SegID::None) return false;
                if (b.Seg == SegID::None) return true;
                return a.Depth < b.Depth;
            });

            if (!item.Culled) {
                for (auto& c : children) {
                    if (c.Seg != SegID::None)
                        _search.push(c);
                }
            }

            objects.clear();

            // queue objects in segment
            for (auto oid : seg->Objects) {
                if (auto obj = level.TryGetObject(oid)) {
                    if (!ShouldDrawObject(*obj)) continue;

                    //DirectX::BoundingSphere bounds(obj->Position, obj->Radius);
                    //if (CameraFrustum.Contains(bounds))
                    objects.push_back({ obj, GetRenderDepth(obj->Position) });
                }
            }

            for (auto& effectId : seg->Effects) {
                if (auto effect = GetEffect(effectId)) {
                    Stats::EffectDraws++;
                    objects.push_back({ nullptr, GetRenderDepth(effect->Position), effect });
                }
            }

            // Sort objects in segment by depth
            Seq::sortBy(objects, [](const ObjDepth& a, const ObjDepth& b) {
                return a.Depth < b.Depth;
            });

            // Queue objects in seg
            for (auto& obj : objects) {
                if (obj.Obj) {
                    if (obj.Obj->Render.Type == RenderType::Model &&
                        obj.Obj->Render.Model.ID != ModelID::None) {
                        // always submit objects to opaque queue, as the renderer will skip
                        // non-transparent submeshes
                        _opaqueQueue.push_back({ obj.Obj, obj.Depth });

                        if (obj.Obj->Render.Model.Outrage) {
                            //auto& mesh = GetOutrageMeshHandle(obj.Obj->Render.Model.ID);
                            //if (mesh.HasTransparentTexture)
                            // outrage models do not setting transparent texture flag, but many are
                            _transparentQueue.push_back({ obj.Obj, obj.Depth });
                        }
                        else {
                            auto& mesh = GetMeshHandle(obj.Obj->Render.Model.ID);
                            if (mesh.IsTransparent)
                                _transparentQueue.push_back({ obj.Obj, obj.Depth });
                        }
                    }
                    else {
                        _transparentQueue.push_back({ obj.Obj, obj.Depth });
                    }
                }
                else if (obj.Effect) {
                    auto depth = GetRenderDepth(obj.Effect->Position);

                    if (obj.Effect->IsTransparent)
                        _transparentQueue.push_back({ obj.Effect, depth });
                    else
                        _opaqueQueue.push_back({ obj.Effect, depth });
                }
            }

            // queue visible walls (this does not scale well)
            // todo: track walls as iterating
            for (auto& mesh : wallMeshes) {
                if (mesh.Chunk->Tag.Segment == item.Seg)
                    _transparentQueue.push_back({ &mesh, 0 });
            }
        }

        Stats::VisitedSegments = (uint16)_visited.size();
    }


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

    // Returns the points of a face in NDC. Returns empty if all points are behind the view plane.
    Option<Array<Vector3, 4>> GetNdc(const Face2& face, const Matrix& viewProj) {
        Array<Vector3, 4> points;
        int behind = 0;
        for (int i = 0; i < 4; i++) {
            auto clip = Vector4::Transform(Vector4(face[i].x, face[i].y, face[i].z, 1), viewProj);
            if (clip.w < 0) behind++; // return {};
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
        _roomQueue.push_back(startRoomId);
        int index = 0;
        //Plane cameraPlane(Camera.Position, Camera.GetForward());

        auto startRoom = level.GetRoom(startRoomId);
        if (!startRoom) return;

        //Bounds2D screenBounds = { { 0, 0 }, { (float)Adapter->GetWidth(), (float)Adapter->GetHeight() } };
        Bounds2D screenBounds = { { -1, -1 }, { 1, 1 } };

        for (auto& basePortal : startRoom->Portals) {
            if (!WallIsTransparent(level, basePortal.Tag))
                continue; // stop at opaque walls

            auto baseFace = Face2::FromSide(level, basePortal.Tag);
            //bool inFrontOfPlane = false;
            //bool behindPlane = false;
            //for (int i = 0; i < 4; i++) {
            //    if (cameraPlane.DotCoordinate(baseFace[i]) > 0)
            //        inFrontOfPlane = true;
            //    else
            //        behindPlane = true;
            //}
            //if (!baseFace.InFrontOfPlane(cameraPlane))
            //continue; // Portal behind camera plane

            //if (!inFrontOfPlane)
            //    continue; // Portal behind camera plane

            //if (!CameraFrustum.Contains(baseFace[0], baseFace[1], baseFace[2]) &&
            //    !CameraFrustum.Contains(baseFace[2], baseFace[3], baseFace[0]))
            //    continue; // Portal not in frustum


            auto basePoints = GetNdc(baseFace, Render::ViewProjection);
            if (!basePoints) continue;
            auto startBounds = GetBounds(*basePoints);

            // If the portal crosses the camera plane then treat it as visible
            /*if (inFrontOfPlane && behindPlane) {
                startBounds = screenBounds;
            }
            else */if (!screenBounds.Overlaps(startBounds)) {
                continue; // Portal not on screen
            }

            //Bounds2D startBounds{};

            //// Workaround for if the initial portal crosses the camera plane. Treat it as visible
            //if (inFrontOfPlane && behindPlane) {
            //    startBounds = screenBounds;
            //}
            //else {
            //    auto basePoints = GetNdc(baseFace);
            //    if (!basePoints) continue; // Portal behind camera

            //    if (!screenBounds.Overlaps(startBounds)) {
            //        continue; // Portal not on screen
            //    }
            //}

            if (auto linkedRoom = level.GetRoom(basePortal.RoomLink)) {
                CheckRoomVisibility(level, *linkedRoom, startBounds, 0);
                if (!Seq::contains(_roomQueue, basePortal.RoomLink))
                    _roomQueue.push_back(basePortal.RoomLink);
            }
        }
    }
}
