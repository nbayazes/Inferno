#pragma once
#include "CustomTextureLibrary.h"
#include "HamFile.h"
#include "Hog2.h"
#include "Level.h"
#include "LightInfo.h"
#include "MaterialInfo.h"
#include "Mission.h"
#include "OutrageBitmap.h"
#include "OutrageModel.h"
#include "OutrageTable.h"
#include "Pig.h"
#include "StringTable.h"

namespace Inferno {
    constexpr auto METADATA_EXTENSION = ".ied"; // inferno engine data
    inline const filesystem::path D1_DEMO_PATH = "d1/demo"; // subdirectory containing the d1 demo hog and pig
}

// Abstraction for game resources
namespace Inferno::Resources {
    inline HamFile GameData = {};
    inline HamFile GameDataD1 = {};
    inline HamFile GameDataD2 = {};

    void LoadDescent1GameData();
    void LoadDescent2GameData();

    void Init();

    inline CustomTextureLibrary CustomTextures;

    extern SoundFile SoundsD1, SoundsD2;
    string_view GetSoundName(SoundID);
    const Palette& GetPalette();

    const Powerup& GetPowerup(PowerupID);
    DClipID GetDoorClipID(LevelTexID);
    const DoorClip& GetDoorClip(DClipID);

    const VClip& GetVideoClip(VClipID);
    const EffectClip& GetEffectClip(EClipID);
    const EffectClip& GetEffectClip(TexID);
    const EffectClip& GetEffectClip(LevelTexID);

    EClipID GetEffectClipID(LevelTexID);
    EClipID GetEffectClipID(TexID);

    const LevelTexture& GetLevelTextureInfo(LevelTexID);
    const LevelTexture& GetLevelTextureInfo(TexID);

    TexID FindTexture(string_view name);
    LevelTexID FindLevelTexture(string_view name);

    const PigEntry& GetTextureInfo(TexID);
    const PigEntry& GetTextureInfo(LevelTexID);
    LevelTexID GetDestroyedTexture(LevelTexID);

    const Model& GetModel(ModelID);
    const Model& GetModel(const Object&);

    ModelID GetDeadModelID(ModelID);
    ModelID GetDyingModelID(ModelID);
    ModelID GetCoopShipModel();

    const RobotInfo& GetRobotInfo(uint);

    inline const RobotInfo& GetRobotInfo(const Object& obj) {
        ASSERT(obj.IsRobot());
        return GetRobotInfo(obj.ID);
    }

    List<TexID> CopyLevelTextureLookup();
    TexID LookupTexID(LevelTexID);
    TexID LookupModelTexID(const Model&, int16);

    inline LevelTexID LookupLevelTexID(TexID id) {
        if (!Seq::inRange(GameData.LevelTexIdx, (int)id)) return LevelTexID::None;
        return GameData.LevelTexIdx[(int)id];
    }

    inline const char* GetMaterialTablePath(const Level& level) {
        return level.IsDescent1() ? "data/d1/material.yml" : "data/d2/material.yml";
    }

    inline const char* GetGameDataFolder(const Level& level) {
        return level.IsDescent1() ? "data/d1/" : "data/d2/";
    }

    // Returns true if the id corresponds to a level texture
    bool IsLevelTexture(TexID id);

    bool IsObjectTexture(TexID id);

    Weapon& GetWeapon(WeaponID);

    inline Weapon& GetWeapon(const Object& obj) {
        ASSERT(obj.IsWeapon());
        return GetWeapon(WeaponID(obj.ID));
    }

    string GetRobotName(uint id);
    // Can return none if the powerup is unused
    Option<string> GetPowerupName(uint id);

    bool FileExists(string_view name);

    // Tries to read a text file by checking the mission, the game specific directory, the shared directory, and finally the game HOG
    string ReadTextFile(const string& name);

    enum class LoadFlag {
        None = 0,
        PreferD1 = 1 << 0, // Load from D1 before D2 (if present)
        //PreferD2 = 1 << 1, // The default
        ReadD3 = 1 << 2,
        SkipMission = 1 << 3,
        ReadDxa = 1 << 4
    };

    // Tries to read a binary file by checking the mission, the game specific directory, the shared directory, and finally the game HOG
    List<byte> ReadBinaryFile(const string& name, LoadFlag flags = LoadFlag::ReadDxa);

    void LoadDataTables(const Level& level);

    // Loads the corresponding resources for a level
    void LoadLevel(Level&);

    int GetTextureCount();
    // Returns bitmap data for a TexID
    const PigBitmap& GetBitmap(TexID);

    const PigBitmap& GetBitmap(LevelTexID);

    // Returns a modifiable bitmap
    PigBitmap& AccessBitmap(TexID);

    inline bool HasGameData() { return !GameData.Robots.empty() && !GameData.LevelTexIdx.empty(); }

    bool FoundDescent1();
    bool FoundDescent1Demo();
    bool FoundDescent2();
    bool FoundVertigo();

    inline Hog2 Descent3Hog, Mercenary;
    inline Outrage::GameTable GameTable;
    inline List<Outrage::VClip> VClips; // Expanded from OAF headers
    inline List< TextureLightInfo> Lights;

    inline TextureLightInfo* GetLightInfo(string_view name) {
        for (auto& info : Lights) {
            if (info.Name == name) return &info;
        }

        return nullptr;
    }

    inline TextureLightInfo* GetLightInfo(LevelTexID id) {
        for (auto& info : Lights) {
            if (info.Id == id) return &info;
        }

        return nullptr;
    }

    void MountDescent3();

    Option<StreamReader> OpenFile(const string& name);

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& fileName);
    Option<Outrage::SoundInfo> ReadOutrageSoundInfo(const string& name);

    // Loads an outrage model by name and returns the ID
    ModelID LoadOutrageModel(const string& name);
    // Returns a previously loaded model by LoadOutrageModel()
    const Outrage::Model* GetOutrageModel(ModelID);

    List<Inferno::MissionInfo> ReadMissionDirectory(const filesystem::path& directory);

    // Loads D1 and D2 sounds
    void LoadSounds();

    const string_view GetString(GameString);
    const string_view GetPrimaryName(PrimaryWeaponIndex id);
    const string_view GetSecondaryName(SecondaryWeaponIndex id);
    const string_view GetPrimaryNameShort(PrimaryWeaponIndex id);
    const string_view GetSecondaryNameShort(SecondaryWeaponIndex id);

    void LoadGameTables(const Level& level);
    span<JointPos> GetRobotJoints(int robotId, int gun, Animation state);

    inline MaterialInfoLibrary Materials;

    inline MaterialInfo& GetMaterial(TexID id) {
        return Materials.GetMaterialInfo(id);
    }

    inline MaterialInfo& GetMaterial(LevelTexID id) {
        return Materials.GetMaterialInfo(id);
    }

    struct PaletteInfo {
        string Name;
        string FileName;
    };

    span<PaletteInfo> GetAvailablePalettes();
}
