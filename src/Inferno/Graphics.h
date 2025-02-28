#pragma once
#include "Resources.Common.h"
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
    void LoadTextures(span<const string> names, LoadFlag loadFlags = LoadFlag::Default, bool force = false);
    void LoadTextures(span<TexID> ids);
    void LoadEnvironmentMap(string_view name);

    void LoadModel(ModelID);
    void LoadTexture(TexID);
    void LoadTexture(LevelTexID);
    void LoadTexture(VClipID);
    void LoadMaterials(span<const TexID> ids, bool forceLoad = false, bool keepLoaded = false);
    void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false, bool keepLoaded = false);

    // Locates and loads an OOF by path. Returns -1 if not found.
    ModelID LoadOutrageModel(const string& path);

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

    void TakeScoreScreenshot(float delay = 0);
    void UpdateTimers();
}
