#include "pch.h"
#include "Graphics.h"
#include "FileSystem.h"
#include "Graphics/Render.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.Editor.h"
#include "Graphics/Render.Level.h"
#include "Resources.h"

namespace Inferno {
    Vector2 ApplyOverlayRotation(const SegmentSide& side, Vector2 uv);
}

namespace Inferno::Graphics {
    void LoadLevel(const Level& level) {
        Render::LoadLevel(level);
    }

    void LoadLevelTextures(const Level& level, bool force) {
        Render::Materials->LoadLevelTextures(level, force);
    }

    void LoadTextures(span<const string> names) {
        Render::Materials->LoadTextures(names);
    }

    void LoadEnvironmentMap(string_view name) {
        if (auto path = FileSystem::TryFindFile(name)) {
            DirectX::ResourceUploadBatch batch(Render::Device);
            batch.Begin();
            Render::Materials->EnvironmentCube.LoadDDS(batch, *path);
            Render::Materials->EnvironmentCube.CreateCubeSRV();
            batch.End(Render::Adapter->BatchUploadQueue->Get());
        }
    }

    void PrintMemoryUsage() {
        Render::Adapter->PrintMemoryUsage();
    }

    uint64 GetMaterialGpuPtr(TexID id) {
        return Render::Materials->Get(id).Pointer();
    }

    uint64 GetMaterialGpuPtr(LevelTexID ltid) {
        auto id = Resources::LookupTexID(ltid);
        return GetMaterialGpuPtr(id);
    }

    void LoadTerrain(const TerrainInfo& info) {
        Array textures = { info.SatelliteTexture, info.SurfaceTexture };
        Render::Materials->LoadTextures(textures);

        Set<TexID> ids;
        Render::GetTexturesForModel(Resources::GameData.ExitModel, ids);
        Render::GetTexturesForModel(Resources::GameData.DestroyedExitModel, ids);
        LoadMaterials(Seq::ofSet(ids));

        auto& resources = Render::LevelResources;
        resources.TerrainMesh = make_unique<Render::TerrainMesh>();
        resources.TerrainMesh->AddTerrain(info.Vertices, info.Indices, info.SurfaceTexture);

        {
            //Matrix::CreateFromAxisAngle(Vector3::UnitY, info.SatelliteDir);
            auto satPosition = info.SatelliteDir * 1000 + Vector3(0, info.SatelliteHeight, 0);

            //const float planetRadius = 200; // Add a planet radius
            auto normal = /*Vector3(0, planetRadius, 0)*/ -satPosition;
            normal.Normalize();
            auto tangent = normal.Cross(Vector3::UnitY);
            tangent.Normalize();
            auto bitangent = tangent.Cross(normal);
            tangent = bitangent.Cross(normal);

            List<ObjectVertex> satVerts;

            auto addVertex = [&](const Vector3& position, const Vector2& uv) {
                ObjectVertex vertex{
                    .Position = position,
                    .UV = uv,
                    .Color = info.SatelliteColor,
                    .Normal = normal,
                    .Tangent = tangent,
                    .Bitangent = bitangent,
                    .TexID = (int)TexID::None // Rely on override
                };

                satVerts.push_back(vertex);
            };

            //auto delta = Vector3(-info.SatelliteSize, -info.SatelliteSize, 0);
            //auto transform = Matrix::CreateRotationZ(DirectX::XM_PIDIV2);
            //auto transform = Matrix::CreateFromAxisAngle(normal, DirectX::XM_PIDIV2);
            auto radius = info.SatelliteSize;
            auto ratio = info.SatelliteAspectRatio;

            addVertex(satPosition - tangent * radius - bitangent * radius * ratio, Vector2(1, 1)); // bl
            addVertex(satPosition + tangent * radius - bitangent * radius * ratio, Vector2(0, 1)); // br
            addVertex(satPosition + tangent * radius + bitangent * radius * ratio, Vector2(0, 0)); // tr
            addVertex(satPosition - tangent * radius + bitangent * radius * ratio, Vector2(1, 0)); // tl

            List<uint16> satIndices = { 0, 1, 2, 0, 2, 3 };

            resources.TerrainMesh->AddSatellite(satVerts, satIndices, info.SatelliteTexture);

            //for (int i = 0; i < 4; i++) {
            //    ObjectVertex vertex{
            //        .Position = satPosition + delta,
            //        .UV = Vector2(0, 0),
            //        .Color = Color(1, 1, 1),
            //        .Normal = normal,
            //        .Tangent = tangent,
            //        .Bitangent = bitangent,
            //        .TexID = (int)TexID::None // Rely on override
            //    };

            //    vertices.push_back(vertex);

            //    delta = delta.Transform(delta, transform);
            //}
        }
    }


    // Loads a single model at runtime
    void LoadModelDynamic(ModelID id) {
        if (!Render::LevelResources.ObjectMeshes) return;
        Render::LevelResources.ObjectMeshes->LoadModel(id);
        Set<TexID> ids;
        Render::GetTexturesForModel(id, ids);
        auto tids = Seq::ofSet(ids);
        Render::Materials->LoadMaterials(tids, false);
    }

    void LoadTextureDynamic(LevelTexID id) {
        List list = { Resources::LookupTexID(id) };
        auto& eclip = Resources::GetEffectClip(id);
        Seq::append(list, eclip.VClip.GetFrames());
        Render::Materials->LoadMaterials(list, false);
    }

    void LoadTextureDynamic(TexID id) {
        if (id <= TexID::None) return;
        List list{ id };
        auto& eclip = Resources::GetEffectClip(id);
        Seq::append(list, eclip.VClip.GetFrames());
        Render::Materials->LoadMaterials(list, false);
    }

    void LoadTextureDynamic(VClipID id) {
        auto& vclip = Resources::GetVideoClip(id);
        Render::Materials->LoadMaterials(vclip.GetFrames(), false);
    }

    void LoadMaterials(span<const TexID> ids, bool forceLoad, bool keepLoaded) {
        Render::Materials->LoadMaterials(ids, forceLoad, keepLoaded);
    }

    void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad, bool keepLoaded) {
        Render::Materials->LoadMaterialsAsync(ids, forceLoad, keepLoaded);
    }

    ModelID LoadOutrageModel(const string& path) {
        if (!Render::LevelResources.ObjectMeshes) return ModelID::None;

        auto id = Resources::LoadOutrageModel(path);
        if (auto model = Resources::GetOutrageModel(id)) {
            Render::LevelResources.ObjectMeshes->LoadOutrageModel(*model, id);
            Render::Materials->LoadTextures(model->Textures);

            //Materials->LoadOutrageModel(*model);
            //NewTextureCache->MakeResident();
        }

        return id;
    }

    void SetExposure(float exposure, float bloom) {
        Render::ToneMapping->ToneMap.BloomStrength = bloom;
        Render::ToneMapping->ToneMap.Exposure = exposure;
    }

    span<RoomID> GetVisibleRooms() {
        return Render::GetVisibleRooms();
    }

    void CreateWindowSizeDependentResources(bool forceSwapChainRebuild) {
        if (Render::Adapter)
            Render::Adapter->CreateWindowSizeDependentResources(forceSwapChainRebuild);
    }

    void ReloadResources() {
        if (Render::Adapter)
            Render::Adapter->ReloadResources();
    }

    void ReloadTextures() {
        Render::Materials->Reload();
    }

    void UnloadTextures() {
        Render::Materials->Unload();
    }

    void PruneTextures() {
        Render::Materials->Prune();
    }

    void NotifyLevelChanged() {
        Render::LevelChanged = true;
    }

    struct AutomapMesh {
        List<LevelVertex> Vertices;
        List<uint32> Indices;

        int32 AddSide(const Level& level, Segment& seg, SideID sideId) {
            auto startIndex = (int32)Vertices.size();
            auto& side = seg.GetSide(sideId);
            auto& uv = side.UVs;

            Indices.push_back(startIndex);
            Indices.push_back(startIndex + 1);
            Indices.push_back(startIndex + 2);

            Indices.push_back(startIndex + 0);
            Indices.push_back(startIndex + 2);
            Indices.push_back(startIndex + 3);

            auto& sideVerts = SIDE_INDICES[(int)sideId];

            for (int i = 0; i < 4; i++) {
                auto& vert = level.Vertices[seg.Indices[sideVerts[i]]];
                Vector2 uv2 = side.HasOverlay() ? ApplyOverlayRotation(side, uv[i]) : Vector2();
                Vertices.push_back({ vert, uv[i], side.Light[i], uv2, side.AverageNormal });
            }

            return startIndex;
        }
    };


    void UpdateAutomap() {
        using Render::AutomapType;

        AutomapMesh solidWalls;
        AutomapMesh connections; // non-visited connections
        AutomapMesh wallMesh;
        AutomapMesh fullmap;
        AutomapMesh fuelcen, matcen, reactor;
        AutomapMesh transparent;

        auto& level = Game::Level;

        Render::LevelResources.AutomapMeshes = make_unique<Render::AutomapMeshes>();
        auto meshes = Render::LevelResources.AutomapMeshes.get();

        const auto packMesh = [&meshes](const AutomapMesh& mesh) {
            return Render::PackedMesh{
                .VertexBuffer = meshes->Buffer.PackVertices(span{ mesh.Vertices }),
                .IndexBuffer = meshes->Buffer.PackIndices(span{ mesh.Indices }),
                .IndexCount = (uint)mesh.Indices.size()
            };
        };

        for (size_t segIndex = 0; segIndex < Game::AutomapSegments.size(); segIndex++) {
            auto state = Game::AutomapSegments[segIndex];
            if (state == Game::AutomapState::Hidden) continue;

            if (auto seg = level.TryGetSegment((SegID)segIndex)) {
                auto type = AutomapType::Wall;

                if (seg->Type == SegmentType::Energy)
                    type = AutomapType::Fuelcen;
                else if (seg->Type == SegmentType::Matcen)
                    type = AutomapType::Matcen;
                else if (seg->Type == SegmentType::Reactor)
                    type = AutomapType::Reactor;

                for (auto& sideId : SIDE_IDS) {
                    bool unrevealed = false; // does this touch an unrevealed side?

                    if (auto connState = Seq::tryItem(Game::AutomapSegments, (int)seg->GetConnection(sideId))) {
                        unrevealed = *connState != Game::AutomapState::Visible;
                    }

                    auto& side = seg->GetSide(sideId);
                    auto wall = level.TryGetWall(side.Wall);
                    bool isDoor = false;
                    bool isSecretDoor = false;

                    if (wall) {
                        if (wall->Type == WallType::Door) {
                            isDoor = true;
                            isSecretDoor = HasFlag(Resources::GetDoorClip(wall->Clip).Flags, DoorClipFlag::Secret);

                            if (HasFlag(wall->Keys, WallKey::Blue))
                                type = AutomapType::BlueDoor;
                            else if (HasFlag(wall->Keys, WallKey::Gold))
                                type = AutomapType::GoldDoor;
                            else if (HasFlag(wall->Keys, WallKey::Red))
                                type = AutomapType::RedDoor;
                            else if (isSecretDoor)
                                type = AutomapType::Wall;
                            else if (HasFlag(wall->Flags, WallFlag::DoorLocked))
                                type = AutomapType::LockedDoor;
                            else
                                type = AutomapType::Door;
                        }
                    }

                    // Add verts to a mesh
                    if (unrevealed && !isDoor && !isSecretDoor) {
                        connections.AddSide(level, *seg, sideId);
                    }
                    else if (wall) {
                        if (wall->Type == WallType::Door || wall->Type == WallType::Closed || wall->Type == WallType::Destroyable) {
                            AutomapMesh mesh;
                            mesh.AddSide(level, *seg, sideId);

                            Render::AutomapMeshInstance instance{
                                .Texture = Resources::LookupTexID(side.TMap),
                                .Decal = side.TMap2 > LevelTexID::Unset ? Resources::LookupTexID(side.TMap2) : TexID::None,
                                .Mesh = packMesh(mesh),
                                .Type = type
                            };

                            meshes->Walls.push_back(instance);
                        }
                    }
                    else if (seg->SideIsSolid(sideId, level)) {
                        if (state == Game::AutomapState::Visible) {
                            if (seg->Type == SegmentType::Energy)
                                fuelcen.AddSide(level, *seg, sideId);
                            else if (seg->Type == SegmentType::Matcen)
                                matcen.AddSide(level, *seg, sideId);
                            else if (seg->Type == SegmentType::Reactor)
                                reactor.AddSide(level, *seg, sideId);
                            else
                                solidWalls.AddSide(level, *seg, sideId);
                        }
                        else if (state == Game::AutomapState::FullMap) {
                            fullmap.AddSide(level, *seg, sideId);
                        }
                    }

                    if (auto conn = level.TryGetSegment(seg->GetConnection(sideId))) {
                        const auto addTransparent = [&](AutomapType connType) {
                            AutomapMesh mesh;
                            mesh.AddSide(level, *seg, sideId);

                            Render::AutomapMeshInstance instance{
                                .Texture = Resources::LookupTexID(side.TMap),
                                .Decal = side.TMap2 > LevelTexID::Unset ? Resources::LookupTexID(side.TMap2) : TexID::None,
                                .Mesh = packMesh(mesh),
                                .Type = connType
                            };

                            meshes->TransparentWalls.push_back(instance);
                        };

                        if (conn->Type != seg->Type) {
                            // Segment type changing
                            if (seg->Type == SegmentType::Energy)
                                addTransparent(AutomapType::Fuelcen);
                            else if (seg->Type == SegmentType::Reactor)
                                addTransparent(AutomapType::Reactor);
                            else if (seg->Type == SegmentType::Matcen)
                                addTransparent(AutomapType::Matcen);
                            else if (seg->Type == SegmentType::None) {
                                // Normal segment facing a special type
                                if (conn->Type == SegmentType::Energy)
                                    addTransparent(AutomapType::Fuelcen);
                                else if (conn->Type == SegmentType::Reactor)
                                    addTransparent(AutomapType::Reactor);
                                else if (conn->Type == SegmentType::Matcen)
                                    addTransparent(AutomapType::Matcen);
                            }
                        }
                    }
                }
            }
        }

        // add solid walls as a single mesh
        meshes->Walls.push_back({
            .Mesh = packMesh(solidWalls),
            .Type = AutomapType::Wall
        });

        meshes->Walls.push_back({
            .Mesh = packMesh(fuelcen),
            .Type = AutomapType::Fuelcen
        });

        meshes->Walls.push_back({
            .Mesh = packMesh(matcen),
            .Type = AutomapType::Matcen
        });

        meshes->Walls.push_back({
            .Mesh = packMesh(reactor),
            .Type = AutomapType::Reactor
        });

        meshes->Walls.push_back({
            .Mesh = packMesh(connections),
            .Type = AutomapType::Unrevealed
        });
    }
}
