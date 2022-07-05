#pragma once
#include "Level.h"
#include "Pig.h"
#include "HamFile.h"
#include "Mission.h"
#include "HogFile.h"
#include "Hog2.h"
#include "OutrageBitmap.h"
#include "OutrageModel.h"

// Abstraction for game resources
namespace Inferno::Resources {
    void Init();

    WClipID GetWallClipID(LevelTexID);
    const WallClip& GetWallClip(WClipID);
    const WallClip* TryGetWallClip(WClipID);
    //const WallClip* TryGetWallClip(LevelTexID);

    const VClip& GetVideoClip(VClipID);
    const EffectClip& GetEffectClip(EClipID);
    const EffectClip* TryGetEffectClip(LevelTexID);
    const EffectClip* TryGetEffectClip(TexID);

    EClipID GetEffectClip(LevelTexID);
    EClipID GetEffectClip(TexID);

    const LevelTexture* TryGetLevelTextureInfo(LevelTexID);
    const LevelTexture& GetLevelTextureInfo(LevelTexID);
    const LevelTexture& GetLevelTextureInfo(TexID);
    LevelTexID GetDestroyedTexture(LevelTexID);
    const PigEntry& GetTextureInfo(TexID);
    const PigEntry* TryGetTextureInfo(TexID);
    const PigEntry* TryGetTextureInfo(LevelTexID);

    const PigEntry& GetTextureInfo(LevelTexID);
    const Model& GetModel(ModelID);
    const RobotInfo& GetRobotInfo(uint);

    List<TexID> CopyLevelTextureLookup();
    TexID LookupLevelTexID(LevelTexID);
    TexID LookupModelTexID(const Model&, int16);
    int GetSoundCount();

    string GetRobotName(uint id);
    // Can return none if the powerup is unused
    Option<string> GetPowerupName(uint id);

    // Loads the corresponding resources for a level
    void LoadLevel(Level&);

    const PigBitmap& ReadBitmap(TexID);

    inline HamFile GameData = {};

    // Reads a file from the mission or game HOG
    List<ubyte> ReadFile(string file);

    // Reads a level from the mounted mission
    Level ReadLevel(string name);
    List<ubyte> ReadSound(SoundID);

    List<string> GetSoundNames();

    inline bool HasGameData() { return !GameData.Robots.empty() && !GameData.LevelTexIdx.empty(); }

    bool FoundDescent1();
    bool FoundDescent2();
    bool FoundVertigo();
    bool HasCustomTextures();

    inline Ptr<Hog2> Descent3Hog;

    void MountD3Hog(std::filesystem::path);

    Option<OutrageBitmap> ReadOutrageBitmap(const string& name);
    Option<OutrageModel> ReadOutrageModel(const string& name);

    OutrageModel const* GetOutrageModel(const string& name);
}