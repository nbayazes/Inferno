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

        if (Settings::Editor.RenderMode != RenderMode::None) {
            // Queue commands for level meshes
            for (auto& mesh : levelMeshes)
                _opaqueQueue.push_back({ &mesh, 0 });

            if (Game::GetState() == GameState::Editor || level.Objects.empty()) {
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
            }
            else {
                TraverseLevel(level.Objects[0].Segment, level, wallMeshes);
            }
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

            auto& mesh = GetMeshHandle(obj.Render.Model.ID);
            if (mesh.HasTransparentTexture)
                _transparentQueue.push_back({ &obj, depth });
        }
        else {
            _transparentQueue.push_back({ &obj, depth });
        }
    }

    void RenderQueue::TraverseLevel(SegID startId, Level& level, span<LevelMesh> wallMeshes) {
        ScopedTimer levelTimer(&Render::Metrics::QueueLevel);

        _visited.clear();
        _search.push(startId);
        Stats::EffectDraws = 0;

        struct ObjDepth {
            Object* Obj = nullptr;
            float Depth = 0;
            EffectBase* Effect;
        };
        List<ObjDepth> objects;

        while (!_search.empty()) {
            auto id = _search.front();
            _search.pop();

            // must check if visited because multiple segs can connect to the same seg before being it is visited
            if (_visited.contains(id)) continue;
            _visited.insert(id);
            auto* seg = &level.GetSegment(id);

            struct SegDepth {
                SegID Seg = SegID::None;
                float Depth = 0;
            };
            Array<SegDepth, 6> children{};

            // Find open sides
            for (auto& sideId : SideIDs) {
                if (!WallIsTransparent(level, { id, sideId }))
                    continue; // Can't see through wall

                if (id != startId) {
                    // always add nearby segments
                    auto vec = seg->Sides[(int)sideId].Center - Camera.Position;
                    vec.Normalize();
                    if (vec.Dot(seg->Sides[(int)sideId].AverageNormal) >= 0)
                        continue; // Cull backfaces
                }

                auto cid = seg->GetConnection(sideId);
                auto cseg = level.TryGetSegment(cid);
                if (cseg && !_visited.contains(cid) /*&& CameraFrustum.Contains(cseg->Center)*/) {
                    children[(int)sideId] = {
                        .Seg = cid,
                        .Depth = GetRenderDepth(cseg->Center)
                    };
                }
            }

            // Sort connected segments by depth
            Seq::sortBy(children, [](const SegDepth& a, const SegDepth& b) {
                if (a.Seg == SegID::None) return false;
                if (b.Seg == SegID::None) return true;
                return a.Depth < b.Depth;
            });

            for (auto& c : children) {
                if (c.Seg != SegID::None)
                    _search.push(c.Seg);
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

            for (auto& effect : GetEffectsInSegment(id)) {
                if (effect && effect->IsAlive()) {
                    Stats::EffectDraws++;
                    objects.push_back({ nullptr, GetRenderDepth(effect->Position), effect.get() });
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
                            if (mesh.HasTransparentTexture)
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
            for (auto& mesh : wallMeshes) {
                if (mesh.Chunk->Tag.Segment == id)
                    _transparentQueue.push_back({ &mesh, 0 });
            }
        }

        Stats::VisitedSegments = (uint16)_visited.size();
    }
}
