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
#include "Resources.Common.h"


// Abstraction for game resources
namespace Inferno::Resources {
    inline SoundFile Sounds = {}; // sounds for the current level
    inline FullGameData Descent1, Descent1Demo, Descent2, Vertigo;
    inline FullGameData GameData; // Resources for the current level

    // Returns the game data for the particular type of game
    inline FullGameData& ResolveGameData(FullGameData::Source source) {
        if (source == FullGameData::Descent1 || source == FullGameData::Descent1Demo) {
            if (Resources::GameData.source == FullGameData::Descent1 || Resources::GameData.source == FullGameData::Descent1Demo)
                return Resources::GameData; // use sounds from the current level
            else
                return Resources::Descent1;
        }
        else {
            if (Resources::GameData.source == FullGameData::Descent2)
                return Resources::GameData; // use sounds from the current level
            else
                return Resources::Descent2;
        }
    }

    bool LoadDescent1Data();
    bool LoadDescent1DemoData();
    bool LoadDescent2Data();

    bool Init();

    inline CustomTextureLibrary CustomTextures;

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

    // Finds a texture by name from specific game data
    TexID FindTexture(string_view name, const FullGameData& data = GameData);

    LevelTexID FindLevelTexture(string_view name);

    const PigEntry& GetTextureInfo(TexID);
    const PigEntry& GetTextureInfo(LevelTexID);
    LevelTexID GetDestroyedTexture(LevelTexID);
    const TexID GetEffectTexture(EClipID id, double time, bool critical);

    const Model& GetModel(ModelID);
    const Model& GetModel(const Object&);

    ModelID GetDeadModelID(ModelID);
    ModelID GetDyingModelID(ModelID);
    ModelID GetCoopShipModel(const Level& level);

    const RobotInfo& GetRobotInfo(uint);

    inline const RobotInfo& GetRobotInfo(const Object& obj) {
        ASSERT(obj.IsRobot());
        return GetRobotInfo(obj.ID);
    }

    List<TexID> CopyLevelTextureLookup();
    TexID LookupTexIDFromData(LevelTexID tid, const FullGameData& data);

    inline TexID LookupTexID(LevelTexID tid) {
        return LookupTexIDFromData(tid, GameData);
    }

    TexID LookupModelTexID(const Model&, int16);

    inline LevelTexID LookupLevelTexID(TexID id) {
        if (!Seq::inRange(GameData.LevelTexIdx, (int)id)) return LevelTexID::None;
        return GameData.LevelTexIdx[(int)id];
    }

    inline const filesystem::path& GetMaterialTablePath(bool descent1) {
        return descent1 ? D1_MATERIAL_FILE : D2_MATERIAL_FILE;
    }

    inline const filesystem::path& GetGameDataFolder(bool descent1) {
        return descent1 ? D1_FOLDER : D2_FOLDER;
    }

    // Returns true if the id corresponds to a level texture
    bool IsLevelTexture(bool descent1, TexID id);

    bool IsObjectTexture(TexID id);

    Weapon& GetWeapon(WeaponID);

    inline Weapon& GetWeapon(const Object& obj) {
        ASSERT(obj.IsWeapon());
        return GetWeapon(WeaponID(obj.ID));
    }

    string GetRobotName(uint id);
    // Can return none if the powerup is unused
    Option<string> GetPowerupName(uint id);

    Option<ResourceHandle> Find(string_view fileName, LoadFlag flags = LoadFlag::Default);

    //bool FileExists(string_view fileName, LoadFlag flags = LoadFlag::Default);

    // Tries to read a text file by checking the mission, the game specific directory, the shared directory, and finally the game HOG
    Option<string> ReadTextFile(string_view name,LoadFlag flags = LoadFlag::Default);

    // Tries to read a binary file by checking the mission, the game specific directory, the shared directory, and finally the game HOG
    Option<List<byte>> ReadBinaryFile(string_view fileName, LoadFlag flags = LoadFlag::Default);

    Option<Image> ReadImage(const string& name, bool srgb);

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
    inline List<TextureLightInfo> Lights;

    inline TextureLightInfo* GetLightInfo(string_view name) {
        for (auto& info : Lights) {
            if (info.Name == name) return &info;
        }

        return nullptr;
    }

    void MountDescent3();

    void MountLevel(const Level& level, const filesystem::path& missionPath);

    // Mounts the addon data for a mission. Returns true if found.
    bool MountAddonData(filesystem::path path);
    void UnmountAddonData();

    Option<StreamReader> OpenFile(const string& name);

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& fileName);
    Option<Outrage::SoundInfo> ReadOutrageSoundInfo(const string& name);

    // Loads an outrage model by name and returns the ID
    ModelID LoadOutrageModel(const string& name);
    // Returns a previously loaded model by LoadOutrageModel()
    const Outrage::Model* GetOutrageModel(ModelID);

    List<Inferno::MissionInfo> ReadMissionDirectory(const filesystem::path& directory);

    const string_view GetString(GameString);
    const string_view GetPrimaryName(PrimaryWeaponIndex id);
    const string_view GetSecondaryName(bool descent1, SecondaryWeaponIndex id);
    const string_view GetPrimaryNameShort(bool descent1, PrimaryWeaponIndex id);
    const string_view GetSecondaryNameShort(bool descent1, SecondaryWeaponIndex id);

    bool LoadGameTables(const Level& level, FullGameData& dest = GameData);

    // Merges materials from individual tables. Must be called after adding or removing entries from a table.
    void MergeMaterials(const Level& level);
    void ExpandAnimatedFrames(TexID id = TexID::None);

    //bool LoadLightTables(LoadFlag flags);
    //bool LoadMaterialTables(LoadFlag flags);
    //void LoadDataTables(LoadFlag flags);
    void LoadDataTables(const Level& level);

    span<JointPos> GetRobotJoints(int robotId, uint gun, Animation state);

    //inline MaterialInfoLibrary Materials;

    // Returns a material from the merged materials
    MaterialInfo& GetMaterial(TexID id);

    // Returns a material from the merged materials
    MaterialInfo* TryGetMaterial(TexID id);

    // Returns all merged materials
    span<MaterialInfo> GetAllMaterials();

    struct PaletteInfo {
        string Name;
        string FileName;
    };

    span<PaletteInfo> GetAvailablePalettes();
}
