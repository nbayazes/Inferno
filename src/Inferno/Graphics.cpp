#include "pch.h"
#include "Graphics.h"
#include "FileSystem.h"
#include "Graphics/Render.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.Editor.h"
#include "Graphics/Render.Level.h"
#include "Resources.h"

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
        List<AutomapVertex> Vertices;
        List<uint32> Indices;

        void AddQuad(const Array<Vector3, 4>& verts, const Color& color, const Vector3& normal) {
            auto startIndex = (int32)Vertices.size();

            Indices.push_back(startIndex);
            Indices.push_back(startIndex + 1);
            Indices.push_back(startIndex + 2);

            Indices.push_back(startIndex + 0);
            Indices.push_back(startIndex + 2);
            Indices.push_back(startIndex + 3);

            Vertices.push_back({ verts[0], color, normal });
            Vertices.push_back({ verts[1], color, normal });
            Vertices.push_back({ verts[2], color, normal });
            Vertices.push_back({ verts[3], color, normal });
        }
    };


    void UpdateAutomap() {
        AutomapMesh solidWalls;
        AutomapMesh connections; // non-visited connections
        //AutomapMesh transparentWalls;
        AutomapMesh doors;
        AutomapMesh fullmap;

        auto& level = Game::Level;

        for (size_t segIndex = 0; segIndex < Game::AutomapSegments.size(); segIndex++) {
            auto state = Game::AutomapSegments[segIndex];
            if (state == Game::AutomapState::Hidden) continue;

            if (auto seg = level.TryGetSegment((SegID)segIndex)) {
                for (auto& sideId : SIDE_IDS) {
                    bool unrevealed = false; // does this touch an unrevealed side?

                    //auto connId = seg->GetConnection(sideId);

                    if (auto connState = Seq::tryItem(Game::AutomapSegments, (int)seg->GetConnection(sideId))) {
                        unrevealed = *connState != Game::AutomapState::Visible;
                    }

                    auto& side = seg->GetSide(sideId);
                    //auto& sideIndices = side.GetRenderIndices();

                    //auto color = Render::Colors::AutomapWall;

                    // Determine color of side
                    auto color = Color(0, 1, 0);

                    if (seg->Type == SegmentType::Energy)
                        color = Render::Colors::Fuelcen;
                    else if (seg->Type == SegmentType::Matcen)
                        color = Render::Colors::Matcen;
                    else if (seg->Type == SegmentType::Reactor)
                        color = Render::Colors::Reactor;

                    auto wall = level.TryGetWall(side.Wall);

                    if (wall && !unrevealed) {
                        if (wall->Type == WallType::Door) {
                            if (HasFlag(wall->Keys, WallKey::Blue))
                                color = Render::Colors::DoorBlue;
                            else if (HasFlag(wall->Keys, WallKey::Gold))
                                color = Render::Colors::DoorGold;
                            else if (HasFlag(wall->Keys, WallKey::Red))
                                color = Render::Colors::DoorRed;
                            else
                                color = Render::Colors::Door;
                        }
                    }

                    // Add verts to a mesh
                    auto verts = Face::FromSide(level, *seg, sideId).CopyPoints();
                    auto& normal = side.AverageNormal;

                    if (unrevealed && !wall) {
                        color = Color(1, 1, 1);
                        connections.AddQuad(verts, color, normal);
                    }
                    else if (seg->SideIsSolid(sideId, level)) {
                        if (state == Game::AutomapState::Visible)
                            solidWalls.AddQuad(verts, color, normal);
                        else if (state == Game::AutomapState::FullMap)
                            fullmap.AddQuad(verts, color, normal);
                    }
                    else if (wall) {
                        if (wall->Type == WallType::Door || wall->Type == WallType::Closed) {
                            doors.AddQuad(verts, color, normal);
                        }
                    }

                    // create vertices for this face
                    //FlatVertex vertex{ .Position = pos, .Color = color };
                    //auto& pos = level.Vertices[sideIndices[i]];

                    //for (int i = 0; i < 6; i++) {
                    //    auto& pos = level.Vertices[sideIndices[i]];
                    //    FlatVertex vertex{ .Position = pos, .Color = color };

                    //    if (unrevealed && !wall) {
                    //        connections.AddQuad(verts, color);
                    //    }
                    //    else if (seg->SideIsSolid(sideId, level)) {
                    //        if (state == Game::AutomapState::Visible)
                    //            solidWalls.AddQuad(verts, color);
                    //        else if (state == Game::AutomapState::FullMap)
                    //            fullmapWalls.AddQuad(verts, color);
                    //    }
                    //    else if (wall) {
                    //        if (wall->Type == WallType::Door || wall->Type == WallType::Closed) {
                    //            doors.AddQuad(verts, color);
                    //        }
                    //    }
                    //}
                }
            }
        }

        Render::LevelResources.AutomapMeshes = make_unique<Render::AutomapMeshes>();
        auto meshes = Render::LevelResources.AutomapMeshes.get();

        const auto pack = [&meshes](const AutomapMesh& mesh, Render::PackedMesh& dest) {
            dest.VertexBuffer = meshes->Buffer.PackVertices(span{ mesh.Vertices });
            dest.IndexBuffer = meshes->Buffer.PackIndices(span{ mesh.Indices });
            dest.IndexCount = mesh.Indices.size();
        };

        //meshes.Buffer.ResetIndex();
        pack(solidWalls, meshes->SolidWalls);
        pack(fullmap, meshes->Fullmap);
        pack(doors, meshes->Doors);
        pack(connections, meshes->Connections);
    }
}
