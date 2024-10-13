#pragma once
#include "Types.h"
//#include "Level.h"

namespace Inferno {
    struct TerrainInfo;
    struct Level;
}

// Boundary between game and graphics code
namespace Inferno::Graphics {
    void LoadLevel(const Level& level);
    void LoadTerrain(const TerrainInfo& info);

    void LoadLevelTextures(const Level& level, bool force = false);
    void LoadTextures(span<const string> names);
    void LoadEnvironmentMap(string_view name);

    // Rename to async
    void LoadModelDynamic(ModelID);
    void LoadTextureDynamic(TexID);
    void LoadTextureDynamic(LevelTexID);
    void LoadTextureDynamic(VClipID);
    void LoadMaterials(span<const TexID> ids, bool forceLoad = false, bool keepLoaded = false);
    void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false, bool keepLoaded = false);

    // Locates and loads an OOF by path. Returns -1 if not found.
    ModelID LoadOutrageModel(const string& path);

    void SetExposure(float exposure, float bloom);

    void PrintMemoryUsage();

    // Returns the diffuse GPU material pointer
    uint64 GetMaterialGpuPtr(TexID id);

    // Returns the diffuse GPU material pointer
    uint64 GetMaterialGpuPtr(LevelTexID ltid);

    // Returns the rooms visible from the player
    // todo: should be based on player camera?
    span<RoomID> GetVisibleRooms();

    void CreateWindowSizeDependentResources(bool forceSwapChainRebuild);

    void ReloadResources();

    void ReloadTextures();
    void UnloadTextures();
    void PruneTextures();

    void NotifyLevelChanged();
}
