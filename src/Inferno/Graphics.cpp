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

        int32 AddSide(const Level& level, Segment& seg, SideID sideId, bool addOffset = false) {
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

            auto offset = addOffset ? side.AverageNormal * 0.5f : Vector3::Zero;

            for (int i = 0; i < 4; i++) {
                auto& vert = level.Vertices[seg.Indices[sideVerts[i]]];
                Vector2 uv2 = side.HasOverlay() ? ApplyOverlayRotation(side, uv[i]) : Vector2();
                Vertices.push_back({ vert + offset, uv[i], side.Light[i], uv2, side.AverageNormal });
            }

            return startIndex;
        }
    };

    // Transforms level state into meshes to draw the automap
    void UpdateAutomap() {
        using Render::AutomapType;

        AutomapMesh unrevealed; // non-visited connections
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

        struct Meshes {
            AutomapMesh walls, solidWalls, fuelcen, matcen, reactor;
            List<Render::AutomapMeshInstance>* mesh;
            AutomapType type = AutomapType::Normal;
        };

        Meshes fullMap, revealed;
        fullMap.mesh = &meshes->FullmapWalls;
        fullMap.type = AutomapType::FullMap;
        revealed.mesh = &meshes->Walls;

        for (size_t segIndex = 0; segIndex < Game::Automap.Segments.size(); segIndex++) {
            auto state = Game::Automap.Segments[segIndex];
            auto& destMesh = state == Game::AutomapState::Visible ? revealed : fullMap;

            if (auto seg = level.TryGetSegment((SegID)segIndex)) {
                auto type = AutomapType::Normal;

                if (seg->Type == SegmentType::Energy)
                    type = AutomapType::Fuelcen;
                else if (seg->Type == SegmentType::Matcen)
                    type = AutomapType::Matcen;
                else if (seg->Type == SegmentType::Reactor)
                    type = AutomapType::Reactor;

                for (auto& sideId : SIDE_IDS) {
                    bool unrevealedBoundary = false; // does this touch an unrevealed side?

                    if (auto connState = Seq::tryItem(Game::Automap.Segments, (int)seg->GetConnection(sideId))) {
                        // Check if on unrevealed boundary
                        unrevealedBoundary =
                            (*connState != Game::AutomapState::Visible && state == Game::AutomapState::Visible) ||
                            (state != Game::AutomapState::Visible && *connState == Game::AutomapState::Visible);
                    }

                    auto& side = seg->GetSide(sideId);
                    auto wall = level.TryGetWall(side.Wall);
                    bool isSecretDoor = false;
                    bool openDoor = false;

                    if (wall) {
                        if (wall->Type == WallType::Door) {
                            isSecretDoor = HasFlag(Resources::GetDoorClip(wall->Clip).Flags, DoorClipFlag::Secret);
                            type = AutomapType::Door;
                            openDoor = wall->HasFlag(WallFlag::DoorOpened) || wall->State == WallState::DoorOpening || wall->State == WallState::DoorClosing;

                            // Use special door colors if possible
                            if (HasFlag(wall->Keys, WallKey::Blue))
                                type = AutomapType::BlueDoor;
                            else if (HasFlag(wall->Keys, WallKey::Gold))
                                type = AutomapType::GoldDoor;
                            else if (HasFlag(wall->Keys, WallKey::Red))
                                type = AutomapType::RedDoor;
                            else if (isSecretDoor) {
                                if (openDoor) {
                                    // Secret door is open but not revealed, keep it hidden
                                    type = unrevealedBoundary ? AutomapType::Normal : AutomapType::Door;
                                }
                                else {
                                    type = AutomapType::Normal; // Hide closed secret doors
                                }
                            }
                            else if (HasFlag(wall->Flags, WallFlag::DoorLocked))
                                type = AutomapType::LockedDoor;
                        }
                        else if (wall->Type == WallType::Destroyable) {
                            // Destroyable walls are also doors, mark them if they are transparent
                            if (SideIsTransparent(level, { (SegID)segIndex, sideId }))
                                type = AutomapType::Door;
                        }
                        else {
                            // Not a door
                            if (SideIsTransparent(level, { (SegID)segIndex, sideId }) && unrevealedBoundary)
                                type = AutomapType::Unrevealed; // Mark transparent walls as unrevealed
                        }
                    }
                    else if (unrevealedBoundary) {
                        type = AutomapType::Unrevealed;
                    }

                    if (state == Game::AutomapState::Hidden && (!unrevealedBoundary || isSecretDoor))
                        continue; // Skip hidden, non-boundary sides and the backs of secret doors

                    if (type == AutomapType::Unrevealed && unrevealedBoundary) {
                        unrevealed.AddSide(level, *seg, sideId);
                    }
                    else if (wall) {
                        // Add 'walls' as individual sides
                        if (wall->Type == WallType::Door ||
                            wall->Type == WallType::Closed ||
                            wall->Type == WallType::Destroyable) {
                            AutomapMesh mesh;
                            mesh.AddSide(level, *seg, sideId);

                            Render::AutomapMeshInstance instance{
                                .Texture = Resources::LookupTexID(side.TMap),
                                .Decal = side.TMap2 > LevelTexID::Unset ? Resources::LookupTexID(side.TMap2) : TexID::None,
                                .Mesh = packMesh(mesh),
                                .Type = type
                            };

                            // Remove textures from doors leading to unexplored areas
                            if (wall->Type == WallType::Door && unrevealedBoundary)
                                instance.Texture = instance.Decal = TexID::None;

                            if (state == Game::AutomapState::FullMap && type == AutomapType::Normal)
                                instance.Type = AutomapType::FullMap; // Draw walls as blue

                            // Make doors transparent when open, the outline shader looks odd on them
                            if (openDoor && !unrevealedBoundary)
                                meshes->TransparentWalls.push_back(instance);
                            else
                                destMesh.mesh->push_back(instance);
                        }
                    }
                    else if (seg->SideIsSolid(sideId, level)) {
                        // Add solid walls as their special types if possible
                        if (state == Game::AutomapState::Visible || state == Game::AutomapState::FullMap) {
                            if (seg->Type == SegmentType::Energy)
                                destMesh.fuelcen.AddSide(level, *seg, sideId);
                            else if (seg->Type == SegmentType::Matcen)
                                destMesh.matcen.AddSide(level, *seg, sideId);
                            else if (seg->Type == SegmentType::Reactor && !level.HasBoss)
                                destMesh.reactor.AddSide(level, *seg, sideId);
                            else
                                destMesh.solidWalls.AddSide(level, *seg, sideId);
                        }
                    }

                    // Add boundary faces between normal and special segments
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
                            // Special segment facing a normal segment
                            if (seg->Type == SegmentType::Energy)
                                addTransparent(AutomapType::Fuelcen);
                            else if (seg->Type == SegmentType::Reactor && !level.HasBoss)
                                addTransparent(AutomapType::Reactor);
                            else if (seg->Type == SegmentType::Matcen)
                                addTransparent(AutomapType::Matcen);
                            else if (seg->Type == SegmentType::None) {
                                // Normal segment facing a special segment
                                if (conn->Type == SegmentType::Energy)
                                    addTransparent(AutomapType::Fuelcen);
                                else if (conn->Type == SegmentType::Reactor && !level.HasBoss)
                                    addTransparent(AutomapType::Reactor);
                                else if (conn->Type == SegmentType::Matcen)
                                    addTransparent(AutomapType::Matcen);
                            }
                        }
                    }
                }
            }
        }

        auto submitMeshes = [&packMesh](const Meshes& src) {
            // add solid walls as a single mesh
            src.mesh->push_back({ .Mesh = packMesh(src.solidWalls), .Type = src.type });
            src.mesh->push_back({ .Mesh = packMesh(src.fuelcen), .Type = AutomapType::Fuelcen });
            src.mesh->push_back({ .Mesh = packMesh(src.matcen), .Type = AutomapType::Matcen });
            src.mesh->push_back({ .Mesh = packMesh(src.reactor), .Type = AutomapType::Reactor });
        };

        submitMeshes(revealed);
        submitMeshes(fullMap);

        // Glowing unrevealed portals
        meshes->TransparentWalls.push_back({ .Mesh = packMesh(unrevealed), .Type = AutomapType::Unrevealed });
    }
}
