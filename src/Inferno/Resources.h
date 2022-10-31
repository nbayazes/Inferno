#pragma once
#include "Level.h"
#include "Pig.h"
#include "HamFile.h"
#include "Mission.h"
#include "HogFile.h"
#include "Hog2.h"
#include "OutrageBitmap.h"
#include "OutrageModel.h"
#include "OutrageTable.h"
#include "SoundSystem.h"
#include "StringTable.h"

// Abstraction for game resources
namespace Inferno::Resources {
    void Init();

    extern SoundFile SoundsD1, SoundsD2;
    int GetSoundCount();
    int GetSoundIndex(SoundID id);
    SoundResource GetSoundResource(SoundID id);

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

    Weapon& GetWeapon(WeaponID);

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

    inline bool HasGameData() { return !GameData.Robots.empty() && !GameData.LevelTexIdx.empty(); }

    bool FoundDescent1();
    bool FoundDescent2();
    bool FoundVertigo();
    bool HasCustomTextures();

    inline Hog2 Descent3Hog, Mercenary;
    inline Outrage::GameTable GameTable;
    inline List<Outrage::VClip> VClips; // Expanded from OAF headers

    void MountDescent3();

    Option<StreamReader> OpenFile(const string& name);

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& name);
    Option<Outrage::Model> ReadOutrageModel(const string& name);

    Outrage::Model const* GetOutrageModel(const string& name);

    // Loads D1 and D2 sounds
    void LoadSounds();

    const string_view GetString(GameString);
    inline const string_view GetPrimaryName(PrimaryWeaponIndex id) {
        return GetString(GameString{ (int)GameString::Laser + (int)id });
    }

    inline const string_view GetSecondaryName(SecondaryWeaponIndex id) {
        return GetString(GameString{ (int)GameString::Concussion + (int)id });
    }

    inline const string_view GetPrimaryNameShort(PrimaryWeaponIndex id) {
        return GetString(GameString{ (int)GameString::LaserShort + (int)id });
    }

    inline const string_view GetSecondaryNameShort(SecondaryWeaponIndex id) {
        return GetString(GameString{ (int)GameString::ConcussionShort + (int)id });
    }
}