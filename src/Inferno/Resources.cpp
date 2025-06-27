#include "pch.h"
#include "Resources.h"
#include <fstream>
#include <mutex>
#include "BitmapTable.h"
#include "FileSystem.h"
#include "Game.h"
#include "GameTable.h"
#include "Graphics/MaterialLibrary.h"
#include "Hog.IO.h"
#include "logging.h"
#include "Pig.h"
#include "Settings.h"
#include "Sound.h"
#include "SoundSystem.h"
#include "MaterialInfo.h"

namespace Inferno {
    LoadFlag GetLevelLoadFlag(const Level& level) {
        return level.IsDescent1() ? LoadFlag::Descent1 : LoadFlag::Descent2;
    }
}

namespace Inferno::Resources {
    namespace {
        List<string> StringTable; // Text for the UI

        List<PaletteInfo> AvailablePalettes;

        constexpr VClip DEFAULT_VCLIP{};
        const Model DEFAULT_MODEL{};
        const LevelTexture DEFAULT_TEXTURE{};
        const Powerup DEFAULT_POWERUP{};
        const DoorClip DEFAULT_DOOR_CLIP{};
        const RobotInfo DEFAULT_ROBOT{};
        MaterialInfo DEFAULT_MATERIAL = {};

        struct ModelEntry {
            string Name;
            Outrage::Model Model;
        };

        List<ModelEntry> OutrageModels;
        IndexedMaterialTable IndexedMaterials;
    }

    int GetTextureCount() { return (int)GameData.pig.Entries.size(); } // Current.bitmaps.size()?;
    const Palette& GetPalette() { return GameData.palette; }

    string GetRobotName(uint id) {
        auto& info = GetRobotInfo(id);
        return info.Name.empty() ? "Unknown robot" : info.Name;
    }

    Option<string> GetPowerupName(uint id) {
        auto& info = GetPowerup((PowerupID)id);
        if (info.Name.empty()) return {};
        return info.Name;
    }

    const Powerup& GetPowerup(PowerupID id) {
        if (!Seq::inRange(GameData.Powerups, (int)id)) return DEFAULT_POWERUP;
        return GameData.Powerups[(int)id];
    }

    bool Init() {
        // Load some default resources.
        bool foundData = Resources::LoadDescent2Data();
        if (!foundData) foundData = Resources::LoadDescent1Data();

        if (!foundData) {
            foundData = Resources::LoadDescent1DemoData();
            Game::DemoMode = true;
        }

        //Game::DemoMode = true; // TESTING

        return foundData;
    }

    const DoorClip& GetDoorClip(DClipID id) {
        if (!Seq::inRange(GameData.DoorClips, (int)id)) return DEFAULT_DOOR_CLIP;
        return GameData.DoorClips[(int)id];
    }

    DClipID GetDoorClipID(LevelTexID id) {
        if (id == LevelTexID::None) return DClipID::None;

        for (int i = 0; i < GameData.DoorClips.size(); i++) {
            if (GameData.DoorClips[i].Frames[0] == id)
                return DClipID(i);
        }

        return DClipID::None;
    }

    EffectClip DEFAULT_EFFECT_CLIP = {};

    const EffectClip& GetEffectClip(EClipID id) {
        if (!Seq::inRange(GameData.Effects, (int)id)) return DEFAULT_EFFECT_CLIP;
        return GameData.Effects[(int)id];
    }

    const EffectClip& GetEffectClip(TexID id) {
        for (auto& clip : GameData.Effects) {
            if (clip.VClip.Frames[0] == id)
                return clip;
        }

        return DEFAULT_EFFECT_CLIP;
    }

    const EffectClip& GetEffectClip(LevelTexID id) {
        auto tid = LookupTexID(id);
        return GetEffectClip(tid);
    }

    EClipID GetEffectClipID(TexID tid) {
        if (tid == TexID::None) return EClipID::None;

        for (int i = 0; i < GameData.Effects.size(); i++) {
            if (GameData.Effects[i].VClip.Frames[0] == tid)
                return EClipID(i);
        }

        return EClipID::None;
    }

    EClipID GetEffectClipID(LevelTexID id) {
        auto tid = LookupTexID(id);
        return GetEffectClipID(tid);
    }

    // Some vclips have very fast speeds (like robot engine glows) that looks bad.
    // This slows them down
    constexpr void FixVClipTimes(span<EffectClip> clips) {
        for (auto& clip : clips) {
            auto& vclip = clip.VClip;
            if (vclip.FrameTime > 0 && vclip.FrameTime < 0.01f) {
                vclip.FrameTime *= 5;
                vclip.PlayTime *= 5;
            }
        }
    }

    const VClip& GetVideoClip(VClipID id) {
        if (GameData.VClips.size() <= (int)id) return DEFAULT_VCLIP;
        return GameData.VClips[(int)id];
    }


    const Inferno::Model& GetModel(ModelID id) {
        if (!Seq::inRange(GameData.Models, (int)id)) return DEFAULT_MODEL;
        return GameData.Models[(int)id];
    }

    const Model& GetModel(const Object& obj) {
        if (obj.Render.Type == RenderType::Model) {
            return GetModel(obj.Render.Model.ID);
        }
        return DEFAULT_MODEL;
    }

    ModelID GetDeadModelID(ModelID id) {
        if (!Seq::inRange(GameData.DeadModels, (int)id)) return ModelID::None;
        return GameData.DeadModels[(int)id];
    }

    ModelID GetDyingModelID(ModelID id) {
        if (!Seq::inRange(GameData.DyingModels, (int)id)) return ModelID::None;
        return GameData.DyingModels[(int)id];
    }

    ModelID GetCoopShipModel(const Level& level) {
        return level.IsDescent1() ? ModelID::D1Coop : ModelID::D2Player;
    }

    const RobotInfo& GetRobotInfo(uint id) {
        if (!Seq::inRange(GameData.Robots, id)) return DEFAULT_ROBOT;
        return GameData.Robots[id];
    }

    List<TexID> CopyLevelTextureLookup() {
        return GameData.AllTexIdx;
    }

    TexID LookupTexIDFromData(LevelTexID tid, const FullGameData& data) {
        auto id = (int)tid;
        if (!Seq::inRange(data.AllTexIdx, id)) return TexID::None;
        return TexID((int)data.AllTexIdx[id]);
    }

    const LevelTexture& GetLevelTextureInfo(LevelTexID id) {
        if (!Seq::inRange(GameData.LevelTextures, (int)id)) return DEFAULT_TEXTURE; // fix for invalid ids in some levels
        return GameData.LevelTextures[(int)id];
    }

    const LevelTexture& GetLevelTextureInfo(TexID id) {
        if (!Seq::inRange(GameData.LevelTexIdx, (int)id)) return DEFAULT_TEXTURE;
        auto ltid = GameData.LevelTexIdx[(int)id];
        return GetLevelTextureInfo(ltid);
    }

    LevelTexID GetDestroyedTexture(LevelTexID id) {
        if (id <= LevelTexID::Unset) return LevelTexID::None;

        auto& info = Resources::GetLevelTextureInfo(id);
        if (info.EffectClip != EClipID::None)
            return GetEffectClip(info.EffectClip).DestroyedTexture;
        else
            return info.DestroyedTexture;
    }

    const TexID GetEffectTexture(EClipID id, double time, bool critical) {
        auto& eclip = Resources::GetEffectClip(id);
        if (eclip.TimeLeft > 0) {
            time = eclip.VClip.PlayTime - eclip.TimeLeft;
        }

        TexID tex = eclip.VClip.GetFrame(time);
        if (critical && eclip.CritClip != EClipID::None) {
            auto& crit = Resources::GetEffectClip(eclip.CritClip);
            tex = crit.VClip.GetFrame(time);
        }

        return tex;
    }

    PigEntry DefaultPigEntry = { .Name = "Unknown", .Width = 64, .Height = 64 };

    TexID FindTexture(string_view name, const FullGameData& data) {
        auto index = Seq::findIndex(data.pig.Entries, [name](const PigEntry& entry) { return entry.Name == name; });
        return index ? TexID(*index) : TexID::None;
    }

    LevelTexID FindLevelTexture(string_view name) {
        //auto frameIndex = name.find('#');

        //if (frameIndex > 0) {
        //    return FindTexture(name.substr(0, frameIndex);
        //}

        if (auto tex = FindTexture(name); tex != TexID::None) {
            return LookupLevelTexID(tex);
        }

        return LevelTexID::None;
    }

    const PigEntry& GetTextureInfo(TexID id) {
        if (auto bmp = CustomTextures.Get(id)) return bmp->Info;
        return GameData.pig.Get(id);
    }

    const PigEntry* TryGetTextureInfo(TexID id) {
        if (id <= TexID::Invalid || (int)id >= GameData.pig.Entries.size()) return nullptr;
        if (auto bmp = CustomTextures.Get(id)) return &bmp->Info;
        return &GameData.pig.Get(id);
    }

    const PigEntry& GetTextureInfo(LevelTexID id) {
        return GameData.pig.Get(LookupTexID(id));
    }

    string_view GetSoundName(SoundID id) {
        if (!Seq::inRange(GameData.Sounds, (int)id)) return "None";
        auto index = GameData.Sounds[(int)id];
        if (index == 255) return "None";

        if (auto sound = Seq::tryItem(GameData.sounds.Sounds, index))
            return sound->Name;
        else
            return "Unknown";
    }

    TexID LookupModelTexID(const Model& m, int16 i) {
        if (i >= m.TextureCount || m.FirstTexture + i >= (int16)GameData.ObjectBitmapPointers.size()) return TexID::None;
        //if (i < 0 || i >= m.TextureCount || m.FirstTexture + i >= Current.ObjectBitmapPointers.size()) return TexID::None;
        auto ptr = GameData.ObjectBitmapPointers[m.FirstTexture + i];
        return GameData.ObjectBitmaps[ptr];
    }

    bool IsLevelTexture(bool descent1, TexID id) {
        auto tex255 = descent1 ? TexID(971) : TexID(1485);
        auto tid = Resources::LookupLevelTexID(id);

        // Default tid is 255, so check if the real 255 texid is passed in
        if (tid != LevelTexID(255) || id == tex255) return true;

        // Check if any wall clips contain this ID
        for (auto& effect : GameData.Effects) {
            for (auto& frame : effect.VClip.GetFrames()) {
                if (frame == id) return true;
            }
        }

        return tid != LevelTexID(255) || id == tex255;
    }

    bool IsObjectTexture(TexID id) {
        return Seq::contains(GameData.ObjectBitmaps, id);
    }

    Weapon DefaultWeapon{ .AmmoUsage = 1 };

    Weapon& GetWeapon(WeaponID id) {
        if (!Seq::inRange(GameData.Weapons, (int)id)) return DefaultWeapon;
        return GameData.Weapons[(int)id];
    }

    string ReplaceExtension(string_view source, string_view extension) {
        auto src = string(source);
        auto ext = string(extension);
        auto offset = src.find('.');
        if (!ext.starts_with('.')) ext = "." + ext;
        if (offset == string::npos) return src + ext;
        return src.substr(0, offset) + ext;
    }

    void UpdateAverageTextureColor() {
        SPDLOG_INFO("Update average texture color");

        for (auto& entry : GameData.pig.Entries) {
            auto& bmp = GetBitmap(entry.ID);
            entry.AverageColor = GetAverageColor(bmp.Data);
            entry.AverageColor.AdjustSaturation(2); // boost saturation to look nicer

            // Colors can go negative due to saturation
            entry.AverageColor.x = std::max(entry.AverageColor.x, 0.0f);
            entry.AverageColor.y = std::max(entry.AverageColor.y, 0.0f);
            entry.AverageColor.z = std::max(entry.AverageColor.z, 0.0f);
        }
    }

    // Try to reads a file from the current mission or the file system
    // Returns empty list if not found
    List<ubyte> TryReadMissionFile(const filesystem::path& path) {
        // Search mounted mission first
        auto fileName = path.filename().string();

        if (Game::Mission && Game::Mission->Exists(fileName)) {
            HogReader reader(Game::Mission->Path);
            return reader.ReadEntry(fileName);
        }

        // Then the filesystem
        if (filesystem::exists(path))
            return File::ReadAllBytes(path);

        return {};
    }

    // Reads a game resource file that must be present.
    // Searches the mounted mission, then the hog, then the filesystem
    List<ubyte> ReadGameResource(string file) {
        // Search mounted mission first
        if (Game::Mission && Game::Mission->Exists(file)) {
            HogReader reader(Game::Mission->Path);
            return reader.ReadEntry(file);
        }

        // Then main hog file
        if (GameData.hog.Exists(file)) {
            HogReader reader(GameData.hog.Path);
            return reader.ReadEntry(file);
        }

        // Then the filesystem
        if (auto path = FileSystem::TryFindFile(file))
            return File::ReadAllBytes(*path);

        auto msg = fmt::format("Required game resource file not found: {}", file);
        SPDLOG_ERROR(msg);
        throw Exception(msg);
    }

    List<PaletteInfo> FindAvailablePalettes(bool descent1) {
        if (descent1) return {};

        // Hard coded palettes
        List<PaletteInfo> palettes = {
            { "GroupA", "GROUPA.256" },
            { "Water", "WATER.256" },
            { "Fire", "FIRE.256" },
            { "Ice", "ICE.256" },
            { "Alien 1", "ALIEN1.256" },
            { "Alien 2", "ALIEN2.256" }
        };

        // Search game / data directories for matching pig and 256 files
        for (auto& dir : FileSystem::GetDirectories()) {
            for (auto& entry : filesystem::directory_iterator(dir)) {
                filesystem::path path = entry.path();
                if (path.extension() == ".256") {
                    auto file = String::ToUpper(path.filename().string());
                    filesystem::path pigPath = path;
                    pigPath.replace_extension(".PIG");

                    if (!FileSystem::TryFindFile(pigPath)) {
                        SPDLOG_WARN("Ignoring `{}` with no matching PIG", path.string());
                        continue; // 256 exists but the PIG doesn't
                    }

                    auto name = pigPath.filename().string();
                    if (!Seq::exists(palettes, [&file](auto entry) { return entry.FileName == file; }))
                        palettes.push_back({ name, file });
                }
            }
        }

        return palettes;
    }

    span<PaletteInfo> GetAvailablePalettes() {
        return AvailablePalettes;
    }

    const string UNKNOWN_STRING = "???";

    const string_view GetString(GameString i) {
        if (!Seq::inRange(StringTable, (int)i)) return UNKNOWN_STRING;
        return StringTable[(int)i];
    }

    const string_view GetPrimaryName(PrimaryWeaponIndex id) {
        return GetString(GameString{ 104 + (int)id }); // Same for d1 and d2
    }

    const string_view GetSecondaryName(bool descent1, SecondaryWeaponIndex id) {
        int index = descent1 ? 109 : 114;
        return GetString(GameString{ index + (int)id });
    }

    const string_view GetPrimaryNameShort(bool descent1, PrimaryWeaponIndex id) {
        if (id == PrimaryWeaponIndex::Spreadfire)
            return "spread"; // D1 has "spreadfire" in the string table, but it gets trimmed by the border

        int index = descent1 ? 114 : 124;
        return Resources::GetString(GameString{ index + (int)id });
    }

    const string_view GetSecondaryNameShort(bool descent1, SecondaryWeaponIndex id) {
        int index = descent1 ? 119 : 134;
        return Resources::GetString(GameString{ index + (int)id });
    }

    // Some levels don't have the D1 reactor model set
    void FixD1ReactorModel(Level& level) {
        for (auto& obj : level.Objects) {
            if (obj.Type == ObjectType::Reactor) {
                obj.ID = 0;
                obj.Render.Model.ID = ModelID::D1Reactor;
            }
        }
    }

    void LoadCustomModel(FullGameData& data, string_view fileName, LoadFlag flags) {
        auto modelData = ReadBinaryFile(fileName, flags);
        if (modelData) {
            auto model = ReadPof(*modelData, &data.palette);
            model.FileName = fileName;
            model.FirstTexture = (uint16)data.ObjectBitmaps.size();
            data.ObjectBitmapPointers.push_back(model.FirstTexture);

            for (auto& texture : model.Textures) {
                auto id = data.pig.Find(texture);
                data.ObjectBitmaps.push_back(id);
            }

            data.Models.push_back(model);
        }
    }

    // Load the custom exit models. Note this requires the D1 ham for proper texturing.
    void LoadCustomModels(FullGameData& data) {
        // Don't search the HOG files because it would find the original models
        auto flags = LoadFlag::Filesystem | LoadFlag::Descent1 | LoadFlag::Dxa;

        // todo: handle Descent 2. It does not define the exit models
        if (data.ExitModel != ModelID::None) {
            auto file = "exit01.pof";
            if (auto modelData = ReadBinaryFile(file, flags)) {
                auto model = ReadPof(*modelData, &data.palette);
                model.FileName = file;
                auto firstTexture = data.Models[(int)data.ExitModel].FirstTexture;
                data.Models[(int)data.ExitModel] = model;
                data.Models[(int)data.ExitModel].FirstTexture = firstTexture;
            }
        }

        if (data.DestroyedExitModel != ModelID::None) {
            auto file = "exit01d.pof";
            if (auto modelData = ReadBinaryFile(file, flags)) {
                auto model = ReadPof(*modelData, &data.palette);
                model.FileName = file;

                auto firstTexture = data.Models[(int)data.DestroyedExitModel].FirstTexture;
                data.Models[(int)data.DestroyedExitModel] = model;
                data.Models[(int)data.DestroyedExitModel].FirstTexture = firstTexture;
            }
        }

        if (data.PlayerShip.Model != ModelID::None) {
            auto file = "ship.pof";
            if (auto modelData = ReadBinaryFile(file, flags)) {
                auto model = ReadPof(*modelData, &data.palette);
                model.FileName = file;
                auto firstTexture = data.Models[(int)data.PlayerShip.Model].FirstTexture;
                data.Models[(int)data.PlayerShip.Model] = model;
                data.Models[(int)data.PlayerShip.Model].FirstTexture = firstTexture;
            }
        }

        if (data.PlayerShip.Model != ModelID::None) {
            auto file = "shipd.pof";
            if (auto modelData = ReadBinaryFile(file, flags)) {
                auto model = ReadPof(*modelData, &data.palette);
                model.FileName = file;
                auto id = (int)data.DyingModels[(int)data.PlayerShip.Model];
                auto firstTexture = data.Models[id].FirstTexture;
                data.Models[id] = model;
                data.Models[id].FirstTexture = firstTexture;
            }
        }

        // Append debris to the end of model list
        data.Debris = (ModelID)data.Models.size();
        LoadCustomModel(data, "debris.pof", flags);
        LoadCustomModel(data, "debris1.pof", flags);
        LoadCustomModel(data, "debris2.pof", flags);
        LoadCustomModel(data, "debris3.pof", flags);
    }

    bool LoadDescent1Data() {
        try {
            if (Descent1.source != FullGameData::Unknown) {
                SPDLOG_INFO("Descent 1 data already loaded");
                return true;
            }

            SPDLOG_INFO("Loading Descent 1 data");

            const auto hogPath = D1_FOLDER / "descent.hog";
            const auto pigPath = D1_FOLDER / "descent.pig";

            if (!filesystem::exists(hogPath)) {
                SPDLOG_WARN("descent.hog not found");
                return false;
            }

            if (!filesystem::exists(pigPath)) {
                SPDLOG_WARN("descent.pig not found");
                return false;
            }

            auto hog = HogFile::Read(hogPath);
            HogReader reader(hogPath);
            auto paletteData = reader.ReadEntry("palette.256");
            auto palette = ReadPalette(paletteData);
            auto pigData = File::ReadAllBytes(pigPath);

            PigFile pig;
            SoundFile sounds;

            auto ham = ReadDescent1GameData(pigData, palette, pig, sounds);
            sounds.Path = pig.Path = pigPath;

            if (Inferno::Settings::Inferno.UseTextureCaching) {
                WriteTextureCache(ham, pig, palette, D1_CACHE);
                D1TextureCache = TextureMapCache(D1_CACHE, 1800);
            }

            // Everything loaded okay, set data
            Descent1 = FullGameData(std::move(ham), FullGameData::Descent1);
            Descent1.bitmaps = ReadAllBitmaps(pig, palette);
            Descent1.palette = std::move(palette);
            Descent1.pig = std::move(pig);
            Descent1.hog = std::move(hog);
            Descent1.sounds = std::move(sounds);
            SPDLOG_INFO("Descent 1 data loaded");
            return true;
        }
        catch (...) {
            SPDLOG_WARN("Error reading Descent 1 data");
            return false;
        }
    }

    bool LoadDescent1DemoData() {
        try {
            if (Descent1Demo.source != FullGameData::Unknown) {
                SPDLOG_INFO("Descent 1 Demo data already loaded");
                return true;
            }

            SPDLOG_INFO("Loading Descent 1 Demo data");
            const auto hogPath = D1_DEMO_FOLDER / "descent.hog";
            const auto pigPath = D1_DEMO_FOLDER / "descent.pig";

            if (!filesystem::exists(hogPath)) {
                SPDLOG_WARN("{} not found", hogPath.string());
                return false;
            }

            if (!filesystem::exists(pigPath)) {
                SPDLOG_WARN("{} not found", pigPath.string());
                return false;
            }

            PigFile pig;
            SoundFile sounds;
            HamFile ham;

            auto pigData = File::ReadAllBytes(pigPath);
            auto hog = HogFile::Read(FileSystem::FindFile(hogPath));
            HogReader reader(hog.Path);
            ReadD1Pig(pigData, pig, sounds);
            sounds.Path = pig.Path = pigPath;
            sounds.Compressed = true;

            auto table = reader.ReadEntry("bitmaps.bin");
            auto paletteData = reader.ReadEntry("palette.256");
            auto palette = ReadPalette(paletteData);

            // Load and fix raw POF files from HOG
            for (auto& entry : hog.Entries) {
                if (entry.Name.ends_with(".pof")) {
                    auto modelData = reader.TryReadEntry(entry.Name);
                    if (!modelData) {
                        SPDLOG_WARN("No model data found for {}", entry.Name);
                        continue;
                    }

                    //auto modelData = ReadBinaryFile(entry.Name, LoadFlag::SkipMissionAndDxa);

                    //if (modelData.empty())
                    //    modelData = ReadBinaryFile(entry.Name);

                    auto model = ReadPof(*modelData, &palette);
                    model.FileName = entry.Name;

                    // Rest and fire animations are swapped on the green lifter in demo
                    if (entry.Name == "robot17.pof")
                        std::swap(model.Animation[(int)Animation::Rest], model.Animation[(int)Animation::Fire]);

                    // Shift the flare so it is centered better. Retail does not have this problem.
                    if (entry.Name == "flare.pof") {
                        for (auto& sm : model.Submodels) {
                            for (auto& v : sm.ExpandedPoints) {
                                v.Point.z -= 1.5f;
                            }
                        }
                    }

                    ham.Models.push_back(model);

                    // Workaround for red and brown hulk sharing the same model with different texture indices.
                    // Due to the way object meshes are generated we need separate models.
                    if (entry.Name == HULK_MODEL_NAME) {
                        model.FileName = RED_HULK_MODEL_NAME;
                        ham.Models.push_back(model);
                    }
                }
            }

            ham.DeadModels.resize(ham.Models.size());

            ReadBitmapTable(table, pig, sounds, ham);

            if (Inferno::Settings::Inferno.UseTextureCaching) {
                WriteTextureCache(ham, pig, palette, D1_DEMO_CACHE);
                D1DemoTextureCache = TextureMapCache(D1_DEMO_CACHE, 1800);
            }

            // Everything loaded okay, set data
            Descent1Demo = FullGameData(std::move(ham), FullGameData::Descent1Demo);
            Descent1Demo.bitmaps = ReadAllBitmaps(pig, palette);
            Descent1Demo.palette = std::move(palette);
            Descent1Demo.pig = std::move(pig);
            Descent1Demo.hog = std::move(hog);
            Descent1Demo.sounds = std::move(sounds);
            SPDLOG_INFO("Descent 1 demo data loaded");
            return true;
        }
        catch (...) {
            SPDLOG_WARN("Error reading Descent 1 demo data");
            return false;
        }
    }

    struct TextureSource {
        PigFile pig;
        Palette palette;
        filesystem::path path;
    };


    //class PigCache {
    //    Dictionary<filesystem::path, TextureSource> _sources;

    //public:
    //    void Load(const string& palette, const HogFile& hog) {
    //        // Find the 256 for the palette first. In most cases it is located inside of the d2 hog.
    //        // But for custom palettes it is on the filesystem
    //        auto paletteData = hog.TryReadEntry(palette);
    //        auto pigName = ReplaceExtension(palette, ".pig");
    //        auto pigPath = FileSystem::FindFile(pigName);

    //        if (paletteData.empty()) {
    //            // Wasn't in hog, find on filesystem
    //            if (auto path256 = FileSystem::TryFindFile(palette)) {
    //                paletteData = File::ReadAllBytes(*path256);
    //                pigPath = path256->replace_extension(".pig");
    //            }
    //            else {
    //                // Give up and load groupa
    //                paletteData = hog.ReadEntry("GROUPA.256");
    //            }
    //        }

    //        //_sources[];
    //    }
    //};

    void LoadPalette(FullGameData& data, string_view palette, HogReader& hog) {
        // Find the 256 for the palette first. In most cases it is located inside of the hog.
        // But for custom palettes it is on the filesystem
        auto paletteData = hog.TryReadEntry(palette);
        auto pigName = ReplaceExtension(palette, ".pig");
        auto pigPath = FileSystem::FindFile(pigName);

        if (!paletteData) {
            // Wasn't in hog, find on filesystem
            if (auto path256 = FileSystem::TryFindFile(palette)) {
                paletteData = File::ReadAllBytes(*path256);
                pigPath = path256->replace_extension(".pig");
            }
            else {
                // Give up and load groupa
                paletteData = hog.TryReadEntry("GROUPA.256");
            }
        }

        data.pig = ReadPigFile(pigPath);
        if (paletteData) data.palette = ReadPalette(*paletteData);
    }

    bool LoadDescent2Data() {
        try {
            if (Descent2.source != FullGameData::Unknown) {
                SPDLOG_INFO("Descent 2 data already loaded");
                return true;
            }

            auto hamPath = FileSystem::TryFindFile("descent2.ham");
            if (!hamPath) {
                SPDLOG_WARN("descent2.ham not found");
                return false;
            }

            auto hamData = File::ReadAllBytes(*hamPath);
            StreamReader reader(hamData);

            auto ham = ReadHam(reader);
            auto hog = HogFile::Read(FileSystem::FindFile("descent2.hog"));
            HogReader hogReader(hog.Path);

            // Everything loaded okay, set data
            Descent2 = FullGameData(ham, FullGameData::Descent2);
            Descent2.hog = std::move(hog);

            if (auto s22 = FileSystem::TryFindFile("descent2.s22")) {
                Descent2.sounds = ReadSoundFile(*s22);
            }

            LoadPalette(Descent2, "groupa.256", hogReader); // default to groupa
            Descent2.bitmaps = ReadAllBitmaps(Descent2.pig, Descent2.palette);

            // todo: write other caches?
            if (Inferno::Settings::Inferno.UseTextureCaching) {
                WriteTextureCache(ham, Descent2.pig, Descent2.palette, D2_CACHE);
                D2TextureCache = TextureMapCache(D2_CACHE, 2700);
            }

            //FixVClipTimes(Current.Effects);
            SPDLOG_INFO("Descent 2 data loaded");
            return true;
        }
        catch (...) {
            SPDLOG_WARN("Error reading descent2.ham");
            return false;
        }
    }

    // Loads DTX data (if present) onto the pig
    void LoadDtx(const Level& level, const Palette& palette, PigFile& pig) {
        filesystem::path folder = level.Path;
        folder.remove_filename();
        auto dtx = ReplaceExtension(level.FileName, ".dtx");
        auto dtxData = TryReadMissionFile(folder / dtx);
        if (!dtxData.empty()) {
            SPDLOG_INFO("DTX data found");
            CustomTextures.LoadDtx(pig.Entries, dtxData, palette);
        }
    }

    void LoadStringTable(const HogFile& hog) {
        StringTable.clear();
        StringTable.reserve(700);
        HogReader reader(hog.Path);
        auto data = reader.TryReadEntry("descent.txb");
        if (!data) {
            SPDLOG_WARN("Unable to load descent.txb");
            return;
        }

        auto text = DecodeText(*data);

        std::stringstream ss(text);
        std::string line;

        while (std::getline(ss, line, '\n')) {
            size_t i{};
            while ((i = line.find("\\n")) != std::string::npos)
                line.replace(i, 2, "\n");

            while ((i = line.find("\\t")) != std::string::npos)
                line.replace(i, 2, "\t");

            StringTable.push_back(line);
        }
    }

    void ResetResources() {
        AvailablePalettes = {};
        Lights = {};
        GameData = {};
        CustomTextures.Clear();
    }

    // Searches the mission HOG, then the game data folder, then the common data folder
    //string ReadTextFile(string_view file, bool descent1) {
    //    auto commonPath = DATA_FOLDER / file;
    //    auto gamePath = (descent1 ? D1_FOLDER : D2_FOLDER) / file;
    //    string data;

    //    if (Game::Mission) {
    //        data = Game::Mission->TryReadEntryAsString(file);
    //        if (!data.empty()) {
    //            SPDLOG_INFO("Reading {} from mission", file);
    //            return data;
    //        }
    //    }

    //    if (FileSystem::TryFindFile(gamePath)) {
    //        SPDLOG_INFO("Reading game table from `{}`", gamePath.string());
    //        data = File::ReadAllText(gamePath);
    //    }
    //    else if (FileSystem::TryFindFile(commonPath)) {
    //        SPDLOG_INFO("Reading game table from `{}`", commonPath.string());
    //        data = File::ReadAllText(commonPath);
    //    }

    //    if (data.empty())
    //        SPDLOG_WARN("Unable to find `{}`", file);

    //    return data;
    //}

    bool LoadGameTables(LoadFlag flags, FullGameData& dest) {
        if (auto data = ReadTextFile(GAME_TABLE_FILE, flags)) {
            LoadGameTable(*data, dest);
            return true;
        }

        return false;
    }

    List<TextureLightInfo> LoadLightTables(LoadFlag flags) {
        if (auto data = ReadTextFile(LIGHT_TABLE_FILE, flags)) {
            return LoadLightTable(*data);
        }

        return {};
    }

    Option<ResourceHandle> FindDxaEntryInFolder(const filesystem::path& folder, string_view fileName) {
        Option<ResourceHandle> handle;

        for (auto& item : filesystem::directory_iterator(folder)) {
            //auto name = string(fileName);
            auto& path = item.path();

            if (String::InvariantEquals(path.extension().string(), ".dxa")) {
                auto zip = File::OpenZip(path);
                if (zip->Contains(fileName))
                    return ResourceHandle(path, string(fileName));
            }

            if (handle) break; // found
        }

        return handle;
    }

    Option<ResourceHandle> Find(string_view fileName, LoadFlag flags) {
        if (fileName.empty()) return {};
        auto file = string(fileName);

        // current HOG file
        if (Game::Mission && HasFlag(flags, LoadFlag::Mission)) {
            //if (HasFlag(flags, LoadFlag::Filesystem)) {
            // Check unpacked data folder for mission
            auto path = Game::Mission->Path.parent_path();
            auto unpacked = path / Game::Mission->Path.stem() / fileName;
            if (filesystem::exists(unpacked))
                return ResourceHandle::FromFilesystem(unpacked);
            //}

            if (Game::Mission->Exists(file))
                return ResourceHandle::FromHog(file, fileName);
        }

        if (HasFlag(flags, LoadFlag::Dxa)) {
            // Check for addon (dxa) data
            if (HasFlag(flags, LoadFlag::Descent1))
                if (auto handle = FindDxaEntryInFolder(D1_FOLDER, fileName))
                    return handle;

            if (HasFlag(flags, LoadFlag::Descent2))
                if (auto handle = FindDxaEntryInFolder(D2_FOLDER, fileName))
                    return handle;

            if (HasFlag(flags, LoadFlag::Common))
                if (auto handle = FindDxaEntryInFolder(COMMON_FOLDER, fileName))
                    return handle;
        }

        if (HasFlag(flags, LoadFlag::Filesystem)) {
            if (HasFlag(flags, LoadFlag::Descent1) && filesystem::exists(D1_FOLDER / file))
                return ResourceHandle::FromFilesystem(D1_FOLDER / fileName);

            if (HasFlag(flags, LoadFlag::Descent2) && filesystem::exists(D2_FOLDER / file))
                return ResourceHandle::FromFilesystem(D2_FOLDER / fileName);

            if (HasFlag(flags, LoadFlag::Common) && filesystem::exists(COMMON_FOLDER / file))
                return ResourceHandle::FromFilesystem(COMMON_FOLDER / fileName);
        }

        // Base HOG file
        if (HasFlag(flags, LoadFlag::BaseHog)) {
            if (HasFlag(flags, LoadFlag::Descent1) && Descent1.hog.Exists(file))
                return ResourceHandle{ Descent1.hog.Path, string(fileName) };

            if (HasFlag(flags, LoadFlag::Descent2) && Descent2.hog.Exists(file))
                return ResourceHandle{ Descent2.hog.Path, string(fileName) };
        }

        return {}; // Wasn't found
    }

    Option<List<byte>> ReadFromDxaFolder(const filesystem::path& folder, string_view name) {
        for (auto& file : filesystem::directory_iterator(folder)) {
            if (!file.is_regular_file()) continue;

            auto& filePath = file.path();

            if (String::InvariantEquals(filePath.extension().string(), ".dxa")) {
                auto zip = File::OpenZip(filePath);
                if (auto data = zip->TryReadEntry(name))
                    return data;
            }
        }

        return {};
    }

    Option<List<byte>> ReadBinaryFile(string_view fileName, LoadFlag flags) {
        if (fileName.empty()) return {};
        auto file = string(fileName);

        // Check current mission
        if (Game::Mission && HasFlag(flags, LoadFlag::Mission)) {
            const auto& missionPath = Game::Mission->Path;

            // Check the unpacked development folder first
            auto unpacked = missionPath.parent_path() / missionPath.stem() / fileName;

            if (filesystem::exists(unpacked)) {
                SPDLOG_INFO("Reading from unpacked mission folder {}", unpacked.string());
                return File::ReadAllBytes(unpacked);
            }

            // Then check for packaged zips
            filesystem::path modZip = missionPath;
            modZip.replace_extension(".zip");

            if (filesystem::exists(modZip)) {
                auto zip = File::OpenZip(modZip);
                if (auto data = zip->TryReadEntry(fileName)) {
                    SPDLOG_INFO("Reading {}:{}", modZip.string(), fileName);
                    return data;
                }
            }

            // Finally check the original hog
            HogReader hog(missionPath);

            if (auto data = hog.TryReadEntry(file)) {
                SPDLOG_INFO("Reading from mission {}:{}", missionPath.string(), file);
                return data;
            }
        }

        // Check for DXA (zip) data
        if (HasFlag(flags, LoadFlag::Dxa)) {
            if (HasFlag(flags, LoadFlag::Descent1))
                if (auto data = ReadFromDxaFolder(D1_FOLDER, fileName))
                    return data;

            if (HasFlag(flags, LoadFlag::Descent2))
                if (auto data = ReadFromDxaFolder(D2_FOLDER, fileName))
                    return data;

            if (HasFlag(flags, LoadFlag::Common))
                if (auto data = ReadFromDxaFolder(COMMON_FOLDER, fileName))
                    return data;
        }

        if (HasFlag(flags, LoadFlag::Filesystem)) {
            if (HasFlag(flags, LoadFlag::Descent1) && filesystem::exists(D1_FOLDER / file)) {
                SPDLOG_INFO("Reading {}", (D1_FOLDER / fileName).string());
                return File::ReadAllBytes(D1_FOLDER / fileName);
            }

            if (HasFlag(flags, LoadFlag::Descent2) && filesystem::exists(D2_FOLDER / file)) {
                SPDLOG_INFO("Reading {}", (D2_FOLDER / fileName).string());
                return File::ReadAllBytes(D2_FOLDER / fileName);
            }
            if (HasFlag(flags, LoadFlag::Common) && filesystem::exists(COMMON_FOLDER / file)) {
                SPDLOG_INFO("Reading {}", (COMMON_FOLDER / fileName).string());
                return File::ReadAllBytes(COMMON_FOLDER / fileName);
            }
        }

        // Base HOG file
        if (HasFlag(flags, LoadFlag::BaseHog)) {
            if (HasFlag(flags, LoadFlag::Descent1) && Descent1.hog.Exists(file)) {
                SPDLOG_INFO("Reading {} from descent1.hog", file);
                HogReader hog(Descent1.hog.Path);
                return hog.TryReadEntry(file);
            }

            if (HasFlag(flags, LoadFlag::Descent2) && Descent2.hog.Exists(file)) {
                SPDLOG_INFO("Reading {} from descent2.hog", file);
                HogReader hog(Descent2.hog.Path);
                return hog.TryReadEntry(file);
            }
        }

        return {}; // Wasn't found
    }

    template <class T>
    string BytesToString(span<T> bytes) {
        string str((char*)bytes.data(), bytes.size());
        return str;
    }

    Option<string> ReadTextFile(string_view name, LoadFlag flags) {
        if (auto bytes = ReadBinaryFile(name, flags)) {
            string str((char*)bytes->data(), bytes->size());
            return str;
        }

        return {};
    }

    //void ExpandMaterialFrames(List<MaterialInfo>& materials) {
    //    for (auto& material : materials) {
    //        auto dclipId = Resources::GetDoorClipID(Resources::LookupLevelTexID((TexID)material.ID));
    //        auto& dclip = Resources::GetDoorClip(dclipId);

    //        // copy material from base frame to all frames of door
    //        material.ID = -1; // unset ID so it doesn't get saved later for individual frames

    //        for (int i = 1; i < dclip.NumFrames; i++) {
    //            auto frameId = Resources::LookupTexIDFromData(dclip.Frames[i], GameData);
    //            if (Seq::inRange(materials, (int)frameId)) {
    //                materials[(int)frameId] = material;
    //            }
    //        }
    //    }

    //    // Expand materials to all frames in effects
    //    for (auto& effect : Resources::GameData.Effects) {
    //        for (int i = 1; i < effect.VClip.NumFrames; i++) {
    //            auto src = effect.VClip.Frames[0];
    //            auto dest = effect.VClip.Frames[i];
    //            if (Seq::inRange(materials, (int)src) && Seq::inRange(materials, (int)dest))
    //                materials[(int)dest] = materials[(int)src];
    //        }
    //    }

    //    // Hard code special flat material
    //    if (materials.size() >= (int)Render::SHINY_FLAT_MATERIAL) {
    //        auto& flat = materials[(int)Render::SHINY_FLAT_MATERIAL];
    //        flat.ID = (int)Render::SHINY_FLAT_MATERIAL;
    //        flat.Metalness = 1.0f;
    //        flat.Roughness = 0.375f;
    //        flat.LightReceived = 0.5f;
    //        flat.SpecularStrength = 0.8f;
    //    }
    //}

    //void MergeMaterials(span<MaterialInfo> source, span<MaterialInfo> dest) {
    //    for (auto& material : source) {
    //        auto texId = Resources::FindTexture(material.Name);
    //        if (Seq::inRange(dest, (int)texId)) {
    //            material.ID = (int)texId;
    //            dest[(int)texId] = material;
    //        }
    //    }
    //}

    // Enables procedural textures for a level
    void EnableProcedurals(span<MaterialInfo> materials) {
        // todo: this should only add procedurals for textures used in the level
        for (size_t texId = 0; texId < materials.size(); texId++) {
            auto& material = materials[texId];

            // todo: reset all procedurals first
            // todo: if IsWater changes, recreate procedural

            if (!material.Procedural.Elements.empty()) {
                if (auto existing = GetProcedural(TexID(texId))) {
                    existing->Info.Procedural = material.Procedural;
                }
                else {
                    // Insert new procedural
                    Outrage::TextureInfo ti{};
                    ti.Procedural = material.Procedural;
                    ti.Name = Resources::GetTextureInfo(TexID(texId)).Name;
                    SetFlag(ti.Flags, Outrage::TextureFlag::Procedural);
                    if (material.Procedural.IsWater)
                        SetFlag(ti.Flags, Outrage::TextureFlag::WaterProcedural);

                    AddProcedural(ti, TexID(texId));
                }
            }
        }
    }

    // Merge all available materials for a level. Replaces the contents of dest.
    void MergeMaterials(const Level& level/*, List<MaterialInfo>& dest*/) {
        //auto& gameData = level.IsDescent1() ? Descent1 : Descent2;
        IndexedMaterials.Reset(Render::MATERIAL_COUNT);
        IndexedMaterials.Merge(Descent1Materials);
        //MergeMaterials(Descent1Materials, dest);

        // Merge D1 data for D2 levels
        if (level.IsDescent2())
            IndexedMaterials.Merge(Descent2Materials);
        //MergeMaterials(Descent2Materials, dest);

        IndexedMaterials.Merge(MissionMaterials);
        IndexedMaterials.Merge(LevelMaterials);

        //MergeMaterials(MissionMaterials, dest);
        //MergeMaterials(LevelMaterials, dest);

        //for (auto& material : Descent1Materials) {
        //    auto texId = Resources::FindTexture(material.Name);
        //    if (Seq::inRange(mergedMaterials, (int)texId)) {
        //        mergedMaterials[(int)texId] = material;
        //    }
        //}

        IndexedMaterials.ExpandAnimatedFrames();
        //ExpandMaterialFrames(dest);
    }

    void Resources::ExpandAnimatedFrames(TexID id) {
        IndexedMaterials.ExpandAnimatedFrames(id);
    }

    // Loads and merges material tables for the level
    void LoadMaterialTables(const Level& level) {
        // Clear existing tables
        Descent1Materials = {};
        Descent2Materials = {};
        MissionMaterials = {};
        LevelMaterials = {};

        // Load the base material tables from the d1 and d2 folders
        if (auto text = Resources::ReadTextFile("material.yml", LoadFlag::Filesystem | LoadFlag::Descent1)) {
            SPDLOG_INFO("Reading D1 material table");

            Descent1Materials = MaterialTable::Load(*text, TableSource::Descent1);
        }

        if (auto text = Resources::ReadTextFile("material.yml", LoadFlag::Filesystem | LoadFlag::Descent2)) {
            SPDLOG_INFO("Reading D2 material table");
            Descent2Materials = MaterialTable::Load(*text, TableSource::Descent2);
        }

        auto levelFile = String::NameWithoutExtension(level.FileName) + MATERIAL_TABLE_EXTENSION;

        if (Game::Mission) {
            if (auto text = Resources::ReadTextFile("material.yml", LoadFlag::Mission)) {
                SPDLOG_INFO("Reading mission material table");
                MissionMaterials = MaterialTable::Load(*text, TableSource::Mission);
            }

            if (auto text = Resources::ReadTextFile(levelFile, LoadFlag::Mission)) {
                SPDLOG_INFO("Reading level material table {}", levelFile);
                LevelMaterials = MaterialTable::Load(*text, TableSource::Level);
            }
        }
        else {
            // read table adjacent to level for standalone levels
            filesystem::path path = level.Path;
            path.replace_extension(MATERIAL_TABLE_EXTENSION);

            if (filesystem::exists(path)) {
                SPDLOG_INFO("Reading level material table {}", path.string());
                auto text = File::ReadAllText(path);
                LevelMaterials = MaterialTable::Load(text, TableSource::Level);
            }
        }
    }

    void MergeLights(List<TextureLightInfo>& dest, const List<TextureLightInfo>& source) {
        for (auto& light : source) {
            if (auto existing = Seq::find(dest, [&light](const TextureLightInfo& t) { return t.Name == light.Name; })) {
                // Replace existing lights
                *existing = light;
            }
            else {
                // Add new ones
                dest.push_back(light);
            }
        }
    }

    void LoadDataTables(const Level& level) {
        //LoadDataTables(level, LoadFlag::Filesystem | LoadFlag::Common);
        //LoadDataTables(level, LoadFlag::Filesystem | GetLevelLoadFlag(level));
        //LoadDataTables(level, LoadFlag::Mission);

        {
            // merge light tables
            Lights = LoadLightTables(LoadFlag::Filesystem | LoadFlag::Descent1);

            if (level.IsDescent2()) {
                auto d2Lights = LoadLightTables(LoadFlag::Filesystem | LoadFlag::Descent2);
                MergeLights(Lights, d2Lights);
            }

            auto missionLights = LoadLightTables(LoadFlag::Mission);
            MergeLights(Lights, missionLights);

            // reload lights on GPU
            Editor::Events::LevelChanged();
        }

        auto flags = LoadFlag::Filesystem | GetLevelLoadFlag(level);
        LoadGameTables(flags, GameData);
        LoadMaterialTables(level);
        MergeMaterials(level);

        EnableProcedurals(IndexedMaterials.Data());
    }

    span<JointPos> GetRobotJoints(int robotId, uint gun, Animation state) {
        ASSERT((int)state <= 4 && (int)state >= 0);
        auto& robotInfo = GetRobotInfo(robotId);
        ASSERT(gun <= robotInfo.Guns && gun >= 0);
        auto& animStates = robotInfo.Joints[gun][(int)state];
        if (GameData.RobotJoints.empty()) return {};
        auto& joints = GameData.RobotJoints[animStates.Offset];
        return span{ &joints, (uint)animStates.Count };
    }

    MaterialInfo& Resources::GetMaterial(TexID id) {
        if (!Seq::inRange(IndexedMaterials.Data(), (int)id)) return DEFAULT_MATERIAL;
        return IndexedMaterials.Data()[(int)id];
    }

    MaterialInfo* Resources::TryGetMaterial(TexID id) {
        if (!Seq::inRange(IndexedMaterials.Data(), (int)id)) return nullptr;
        return &IndexedMaterials.Data()[(int)id];
    }

    span<MaterialInfo> Resources::GetAllMaterials() {
        return IndexedMaterials.Data();
    }

    // Resets all object sizes to their resource defined values
    // NOTE: Hostage sizes should be left alone due to certain gimmick levels messing with their size
    //void ResetObjectSizes(Level& level) {
    //    for (auto& obj : level.Objects) {
    //        obj.Radius = Editor::GetObjectRadius(obj);
    //    }
    //}

    void LoadLevel(Level& level) {
        try {
            ResetResources();
            //Resources::LoadSounds();

            if (level.IsDescent2()) {
                SPDLOG_INFO("Loading Descent 2 level: '{}'\r\n Version: {} Segments: {} Vertices: {}", level.Name, level.Version, level.Segments.size(), level.Vertices.size());
                //LoadDescent2Resources(level);
                if (Descent2.source == FullGameData::Unknown) {
                    if (!LoadDescent2Data()) {
                        ShowErrorMessage("Unable to load level, Descent 2 data not found");
                        return;
                    }
                }

                AvailablePalettes = FindAvailablePalettes(level.IsDescent1());
                GameData = FullGameData(Descent2);
                // todo: switch palette based on level

                // todo: it is not ideal to reload palettes and their textures each time. Cache them.
                // Find the 256 for the palette first. In most cases it is located inside of the d2 hog.
                // But for custom palettes it is on the filesystem
                HogReader d2Hog(Descent2.hog.Path);
                auto paletteData = d2Hog.TryReadEntry(level.Palette);
                auto pigName = ReplaceExtension(level.Palette, ".pig");
                auto pigPath = FileSystem::FindFile(pigName);

                if (!paletteData) {
                    // Wasn't in hog, find on filesystem
                    if (auto path256 = FileSystem::TryFindFile(level.Palette)) {
                        paletteData = File::ReadAllBytes(*path256);
                        pigPath = path256->replace_extension(".pig");
                    }
                    else {
                        // Give up and load groupa, but fail if it's not found
                        paletteData = d2Hog.ReadEntry("GROUPA.256");
                    }
                }

                GameData.pig = ReadPigFile(pigPath); // todo: pick the correct pre-loaded pig
                auto palette = ReadPalette(*paletteData);
                auto bitmaps = ReadAllBitmaps(GameData.pig, palette); // todo: pick texture cache

                // Load VHAMs
                if (level.IsVertigo()) {
                    std::filesystem::path vhamPath = Game::Mission->Path;
                    vhamPath.replace_extension(".ham");
                    auto vham = TryReadMissionFile(vhamPath);
                    if (!vham.empty()) {
                        StreamReader vReader(vham);
                        AppendVHam(vReader, GameData);
                    }
                }

                {
                    // Load HXMs
                    auto hxm = ReplaceExtension(level.FileName, ".hxm");
                    filesystem::path folder = level.Path;
                    folder.remove_filename();
                    auto hxmData = TryReadMissionFile(folder / hxm);
                    if (!hxmData.empty()) {
                        SPDLOG_INFO("Loading HXM data");
                        StreamReader hxmReader(hxmData);
                        ReadHXM(hxmReader, GameData);
                    }
                }

                // Load custom textures
                {
                    filesystem::path folder = level.Path;
                    folder.remove_filename();

                    auto pog = ReplaceExtension(level.FileName, ".pog");
                    auto pogData = TryReadMissionFile(folder / pog);
                    if (!pogData.empty()) {
                        SPDLOG_INFO("Loading POG data");
                        CustomTextures.LoadPog(GameData.pig.Entries, pogData, GameData.palette);
                    }
                }
            }
            else if (level.IsDescent1()) {
                SPDLOG_INFO("Loading Descent 1 level: '{}'\r\n Version: {} Segments: {} Vertices: {}",
                            level.Name, level.Version, level.Segments.size(), level.Vertices.size());

                if (level.IsShareware) {
                    if (Descent1Demo.source == FullGameData::Unknown) {
                        if (!LoadDescent1DemoData()) {
                            ShowErrorMessage("Unable to load level, Descent 1 demo data not found");
                            return;
                        }
                    }

                    GameData = FullGameData(Descent1Demo);
                }
                else {
                    if (Descent1.source == FullGameData::Unknown) {
                        if (!LoadDescent1Data()) {
                            ShowErrorMessage("Unable to load level, Descent 1 data not found");
                            return;
                        }
                    }

                    GameData = FullGameData(Descent1);
                    LoadDtx(level, GameData.palette, GameData.pig);
                }
            }
            else {
                throw Exception("Unsupported level version");
            }

            // Read replacement models
            //for (auto& model : Current.Models) {
            //    if (model.FileName.empty()) continue;
            //    auto modelData = ReadBinaryFile(model.FileName, true);
            //    if (!modelData.empty()) {
            //        model = ReadPof(modelData, &LevelPalette);
            //    }
            //}

            // Doors that use TMap1 override their side's textures on level load
            for (auto& wall : level.Walls) {
                if (wall.Clip != DClipID::None) {
                    auto& clip = Resources::GetDoorClip(wall.Clip);
                    if (clip.HasFlag(DoorClipFlag::TMap1)) {
                        auto& side = level.GetSide(wall.Tag);
                        side.TMap = clip.Frames[0];
                        side.TMap2 = LevelTexID::Unset;
                    }
                }
            }


            for (auto& obj : level.Objects) {
                if (obj.Type == ObjectType::Hostage)
                    level.TotalHostages++;
            }

            LoadCustomModels(GameData); // Load models before tables, so the custom model gunpoints are used
            LoadDataTables(level);

            LoadStringTable(GameData.hog);
            UpdateAverageTextureColor();

            Inferno::Sound::CopySoundIds();
            //FixObjectModelIds(level);
            //ResetObjectSizes(level);
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
        }
    }

    const PigBitmap DEFAULT_BITMAP = { PigEntry{ .Name = "default", .Width = 64, .Height = 64 } };

    const PigBitmap& GetBitmap(TexID id) {
        if (GameData.bitmaps.empty())
            return DEFAULT_BITMAP;

        if (auto bmp = CustomTextures.Get(id)) return *bmp;
        if (!Seq::inRange(GameData.bitmaps, (int)id)) id = (TexID)0;
        return GameData.bitmaps[(int)id];
    }

    const PigBitmap& GetBitmap(LevelTexID tid) {
        return GetBitmap(LookupTexID(tid));
    }

    bool FoundDescent1() {
        return filesystem::exists(D1_FOLDER / "descent.hog");
    }

    bool FoundDescent1Demo() {
        return filesystem::exists(D1_DEMO_FOLDER / "descent.hog");
    }

    bool FoundDescent2() {
        return filesystem::exists(D2_FOLDER / "descent2.hog");
    }

    bool FoundDescent3() {
        return FileSystem::TryFindFile("d3.hog").has_value();
    }

    bool FoundVertigo() {
        return FileSystem::TryFindFile("d2x.hog").has_value();
    }

    bool FoundMercenary() {
        return FileSystem::TryFindFile("merc.hog").has_value();
    }

    // Opens a file stream from the data paths or the loaded hogs
    Option<StreamReader> OpenFile(const string& name) {
        // Check file system first, then hogs
        if (auto path = FileSystem::TryFindFile(name))
            return StreamReader(*path);
        else if (auto data = Descent3Hog.ReadEntry(name))
            return StreamReader(std::move(*data), name);

        return {};
    }

    void LoadVClips() {
        for (auto& tex : GameTable.Textures) {
            if (!tex.Animated()) continue;

            if (auto r = OpenFile(tex.FileName)) {
                auto vc = Outrage::VClip::Read(*r);
                if (vc.Frames.size() > 0)
                    vc.FrameTime = tex.Speed / vc.Frames.size();
                vc.FileName = tex.FileName;
                VClips.push_back(std::move(vc));
            }
        }
    }

    void MountDescent3() {
        try {
            if (auto path = FileSystem::TryFindFile("d3.hog")) {
                SPDLOG_INFO("Loading {} and Table.gam", path->string());
                Descent3Hog = Hog2::Read(*path);
                if (auto r = OpenFile("Table.gam"))
                    GameTable = Outrage::GameTable::Read(*r);

                LoadVClips();
            }

            //if (auto path = FileSystem::TryFindFile("merc.hog")) {
            //    Mercenary = Hog2::Read(*path);
            //}
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading Descent 3\n{}", e.what());
        }
    }

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& fileName) {
        for (auto& tex : GameTable.Textures) {
            string name;
            if (String::InvariantEquals(tex.FileName, fileName)) name = fileName;
            else if (String::InvariantEquals(tex.FileName, fileName + ".ogf")) name = fileName + ".ogf";
            else continue;

            if (auto data = Descent3Hog.ReadEntry(name)) {
                auto reader = StreamReader(std::move(*data), name);
                return Outrage::Bitmap::Read(reader);
            }
        }

        return {};
    }

    Option<Outrage::Model> TryReadOutrageModel(const string& name) {
        if (auto r = OpenFile(name))
            return Outrage::Model::Read(*r);

        return {};
    }

    Option<Outrage::SoundInfo> ReadOutrageSoundInfo(const string& name) {
        for (auto& sound : GameTable.Sounds) {
            if (sound.Name == name || sound.FileName == name)
                return sound;
        }

        return {};
    }

    ModelID LoadOutrageModel(const string& name) {
        if (name.empty()) return ModelID::None;

        for (int i = 0; i < OutrageModels.size(); i++) {
            if (OutrageModels[i].Name == name)
                return ModelID(i);
        }

        // todo: merge / rework texture caching
        //if (auto model = TryReadOutrageModel(name)) {
        //    for (auto& texture : model->Textures) {
        //        model->TextureHandles.push_back(Render::NewTextureCache->ResolveFileName(texture));
        //    }
        //    Render::Materials->LoadTextures(model->Textures);
        //    OutrageModels.push_back({ name, std::move(*model) });
        //    return ModelID(OutrageModels.size() - 1);
        //}

        return ModelID::None;
    }

    Outrage::Model const* GetOutrageModel(ModelID id) {
        auto i = (int)id;
        if (Seq::inRange(OutrageModels, i))
            return &OutrageModels[i].Model;

        return nullptr;
    }

    enum class MissionType { D1, D2 };

    List<Inferno::MissionInfo> ReadMissionDirectory(const filesystem::path& directory) {
        List<Inferno::MissionInfo> missions;

        try {
            for (auto& file : filesystem::directory_iterator(directory)) {
                if (String::InvariantEquals(file.path().extension().string(), ".msn") ||
                    String::InvariantEquals(file.path().extension().string(), ".mn2")) {
                    MissionInfo mission{};
                    std::ifstream missionFile(file.path());
                    if (mission.Read(missionFile)) {
                        mission.Path = file.path();
                        missions.push_back(mission);
                    }
                }
            }
        }
        catch (...) {
            SPDLOG_WARN("Unable to read mission directory`{}`", directory.string());
        }

        // Alphabetical sort
        Seq::sortBy(missions, [](const Inferno::MissionInfo& a, const Inferno::MissionInfo& b) {
            return lstrcmpiA(a.Name.c_str(), b.Name.c_str()) < 0;
        });

        return missions;
    }

    //int ResolveVClip(string frameName) {
    //    for (int id = 0; id < Resources::VClips.size(); id++) {
    //        auto& vclip = Resources::VClips[id];
    //        for (auto& frame : vclip.Frames) {
    //            if (String::InvariantEquals(frame.Name, frameName)) {
    //                RuntimeTextureInfo ti;
    //                ti.FileName = frame.Name;
    //                ti.VClip = id;
    //                return AllocTextureInfo(std::move(ti));
    //            }
    //        }
    //    }

    //    return -1;
    //}

    //const string& ResolveOutrageFileName(string_view fileName) {
    //    //for (int i = 0; i < _textures.size(); i++) {
    //    //    if (String::InvariantEquals(_textures[i].FileName, fileName))
    //    //        return i; // Already exists
    //    //}

    //    // Check the D3 game table
    //    for (auto& tex : GameTable.Textures) {
    //        if (String::InvariantEquals(tex.FileName, fileName))
    //            return tex.Name;
    //            //return AllocTextureInfo({ tex });
    //    }

    //    //if (auto id = ResolveVClip(fileName); id != -1)
    //    //    return id;

    //    //return -1;
    //}

    //const Outrage::Model* ReadOutrageModel(const string& name) {
    //    // name -> index -> 500 + model ID
    //    if (name.empty()) return nullptr;

    //    if (OutrageModels.contains(name))
    //        return &OutrageModels[name];

    //    if (auto model = TryReadOutrageModel(name)) {
    //        for (auto& texture : model->Textures) {
    //            model->TextureHandles.push_back(Render::NewTextureCache->ResolveFileName(texture));
    //        }
    //        OutrageModels[name] = std::move(*model);
    //        return &OutrageModels[name];
    //    }

    //    return nullptr;
    //}
};
