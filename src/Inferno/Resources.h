#pragma once
#include "CustomTextureLibrary.h"
#include "Level.h"
#include "Pig.h"
#include "HamFile.h"
#include "Mission.h"
#include "Hog2.h"
#include "OutrageBitmap.h"
#include "OutrageModel.h"
#include "OutrageTable.h"
#include "StringTable.h"
#include "LightInfo.h"
#include "MaterialInfo.h"
#include "SoundTypes.h"

// Abstraction for game resources
namespace Inferno::Resources {
    inline HamFile GameData = {};

    void Init();

    inline CustomTextureLibrary CustomTextures;

    extern SoundFile SoundsD1, SoundsD2;
    SoundResource GetSoundResource(SoundID id);
    string_view GetSoundName(SoundID id);
    const Palette& GetPalette();

    DClipID GetDoorClipID(LevelTexID);
    const DoorClip& GetDoorClip(DClipID);

    const VClip& GetVideoClip(VClipID);
    const EffectClip& GetEffectClip(EClipID);
    const EffectClip& GetEffectClip(LevelTexID);
    const EffectClip& GetEffectClip(TexID);

    EClipID GetEffectClipID(LevelTexID);
    EClipID GetEffectClipID(TexID);

    const LevelTexture& GetLevelTextureInfo(LevelTexID);
    const LevelTexture& GetLevelTextureInfo(TexID);
    const PigEntry& GetTextureInfo(TexID);
    const PigEntry& GetTextureInfo(LevelTexID);
    LevelTexID GetDestroyedTexture(LevelTexID);

    const Model& GetModel(ModelID);
    const RobotInfo& GetRobotInfo(uint);
    inline const RobotInfo& GetRobotInfo(const Object& obj) {
        assert(obj.IsRobot());
        return GetRobotInfo(obj.ID);
    }

    List<TexID> CopyLevelTextureLookup();
    TexID LookupTexID(LevelTexID);
    TexID LookupModelTexID(const Model&, int16);
    inline LevelTexID LookupLevelTexID(TexID id) { return GameData.LevelTexIdx[(int)id]; }

    inline const char* GetMaterialFileName(const Level& level) {
        return level.IsDescent1() ? "materials.yml" : "materials2.yml";
    }

    inline const char* GetLightFileName(const Level& level) {
        return level.IsDescent1() ? "lights.yml" : "lights2.yml";
    }


    // Returns true if the id corresponds to a level texture
    bool IsLevelTexture(TexID id);

    Weapon& GetWeapon(WeaponID);
    inline Weapon& GetWeapon(const Object& obj) {
        assert(obj.IsWeapon());
        return GetWeapon(WeaponID(obj.ID));
    }

    string GetRobotName(uint id);
    // Can return none if the powerup is unused
    Option<string> GetPowerupName(uint id);
    Powerup& GetPowerup(uint id);

    void LoadDataTables(const Level& level);

    // Loads the corresponding resources for a level
    void LoadLevel(Level&);

    int GetTextureCount();
    // Returns bitmap data for a TexID
    const PigBitmap& GetBitmap(TexID);

    const PigBitmap& GetBitmap(LevelTexID);

    // Returns a modifiable bitmap
    PigBitmap& AccessBitmap(TexID);

    // Reads a file from the mission or game HOG

    // Reads a level from the mounted mission
    Level ReadLevel(string name);

    inline bool HasGameData() { return !GameData.Robots.empty() && !GameData.LevelTexIdx.empty(); }

    bool FoundDescent1();
    bool FoundDescent2();
    bool FoundVertigo();

    inline Hog2 Descent3Hog, Mercenary;
    inline Outrage::GameTable GameTable;
    inline List<Outrage::VClip> VClips; // Expanded from OAF headers
    inline Dictionary<LevelTexID, TextureLightInfo> LightInfoTable;

    void MountDescent3();

    Option<StreamReader> OpenFile(const string& name);

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& fileName);
    Option<Outrage::SoundInfo> ReadOutrageSoundInfo(const string& name);

    // Loads an outrage model by name and returns the ID
    ModelID LoadOutrageModel(const string& name);
    // Returns a previously loaded model by LoadOutrageModel()
    const Outrage::Model* GetOutrageModel(ModelID);

    // Loads D1 and D2 sounds
    void LoadSounds();

    const string_view GetString(GameString);
    const string_view GetPrimaryName(PrimaryWeaponIndex id);
    const string_view GetSecondaryName(SecondaryWeaponIndex id);
    const string_view GetPrimaryNameShort(PrimaryWeaponIndex id);
    const string_view GetSecondaryNameShort(SecondaryWeaponIndex id);

    void LoadGameTable();
    span<JointPos> GetRobotJoints(int robotId, int gun, AnimState state);

    inline MaterialInfoLibrary Materials;

    inline MaterialInfo& GetMaterial(TexID id) {
        return Materials.GetMaterialInfo(id);
    }

    inline MaterialInfo& GetMaterial(LevelTexID id) {
        return Materials.GetMaterialInfo(id);
    }
}
