#include "pch.h"
#include "Resources.h"
#include <fstream>
#include <mutex>
#include <zip/zip.h>
#include "BitmapTable.h"
#include "FileSystem.h"
#include "Game.h"
#include "GameTable.h"
#include "Graphics/MaterialLibrary.h"
#include "logging.h"
#include "Pig.h"
#include "Settings.h"
#include "Sound.h"
#include "SoundSystem.h"

namespace Inferno {
    LoadFlag GetLevelLoadFlag(const Level& level) {
        return level.IsDescent1() ? LoadFlag::Descent1 : LoadFlag::Descent2;
    }
}

namespace Inferno::Resources {
    namespace {
        List<string> RobotNames;
        List<string> PowerupNames;
        List<string> StringTable; // Text for the UI

        List<PaletteInfo> AvailablePalettes;

        constexpr VClip DEFAULT_VCLIP{};
        const Model DEFAULT_MODEL{};
        const LevelTexture DEFAULT_TEXTURE{};
        const Powerup DEFAULT_POWERUP{};
        const DoorClip DEFAULT_DOOR_CLIP{};
        const RobotInfo DEFAULT_ROBOT{};

        struct ModelEntry {
            string Name;
            Outrage::Model Model;
        };

        List<ModelEntry> OutrageModels;
    }

    int GetTextureCount() { return (int)GameData.pig.Entries.size(); } // Current.bitmaps.size()?;
    const Palette& GetPalette() { return GameData.palette; }

    void LoadRobotNames(const filesystem::path& path) {
        try {
            std::ifstream file(path);
            string line;
            while (std::getline(file, line))
                RobotNames.push_back(line);
        }
        catch (...) {
            SPDLOG_ERROR("Error reading robot names from `{}`", path.string());
        }
    }

    string GetRobotName(uint id) {
        if (id >= RobotNames.size()) return "Unknown";
        return RobotNames[id];
    }

    void LoadPowerupNames(const filesystem::path& path) {
        try {
            std::ifstream file(path);
            string line;
            while (std::getline(file, line))
                PowerupNames.push_back(line);
        }
        catch (...) {
            SPDLOG_ERROR("Error reading powerup names from `{}`", path.string());
        }
    }

    Option<string> GetPowerupName(uint id) {
        if (id >= PowerupNames.size() || PowerupNames[id] == "(not used)") return {};
        return { PowerupNames[id] };
    }

    const Powerup& GetPowerup(PowerupID id) {
        if (!Seq::inRange(GameData.Powerups, (int)id)) return DEFAULT_POWERUP;
        return GameData.Powerups[(int)id];
    }

    bool Init() {
        // Load some default resources.
        LoadPowerupNames("powerups.txt");
        LoadRobotNames("robots.txt");

        bool foundData = Resources::LoadDescent2Data();
        if (!foundData) foundData = Resources::LoadDescent1Data();
        if (!foundData) foundData = Resources::LoadDescent1DemoData();
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

    ModelID GetCoopShipModel() {
        return Game::Level.IsDescent1() ? ModelID::D1Coop : ModelID::D2Player;
    }

    const RobotInfo& GetRobotInfo(uint id) {
        if (!Seq::inRange(GameData.Robots, id)) return DEFAULT_ROBOT;
        return GameData.Robots[id];
    }

    List<TexID> CopyLevelTextureLookup() {
        return GameData.AllTexIdx;
    }

    TexID LookupTexID(LevelTexID tid) {
        auto id = (int)tid;
        if (!Seq::inRange(GameData.AllTexIdx, id)) return TexID::None;
        return TexID((int)GameData.AllTexIdx[id]);
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

    PigEntry DefaultPigEntry = { .Name = "Unknown", .Width = 64, .Height = 64 };

    TexID FindTexture(string_view name) {
        auto index = Seq::findIndex(GameData.pig.Entries, [name](const PigEntry& entry) { return entry.Name == name; });
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

    bool IsLevelTexture(TexID id) {
        auto tex255 = Game::Level.IsDescent1() ? TexID(971) : TexID(1485);
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
        }
    }

    // Try to reads a file from the current mission or the file system
    // Returns empty list if not found
    List<ubyte> TryReadMissionFile(const filesystem::path& path) {
        // Search mounted mission first
        auto fileName = path.filename().string();
        if (Game::Mission && Game::Mission->Exists(fileName))
            return Game::Mission->ReadEntry(fileName);

        // Then the filesystem
        if (filesystem::exists(path))
            return File::ReadAllBytes(path);

        return {};
    }

    // Reads a game resource file that must be present.
    // Searches the mounted mission, then the hog, then the filesystem
    List<ubyte> ReadGameResource(string file) {
        // Search mounted mission first
        if (Game::Mission && Game::Mission->Exists(file))
            return Game::Mission->ReadEntry(file);

        // Then main hog file
        if (GameData.hog.Exists(file))
            return GameData.hog.ReadEntry(file);

        // Then the filesystem
        if (auto path = FileSystem::TryFindFile(file))
            return File::ReadAllBytes(*path);

        auto msg = fmt::format("Required game resource file not found: {}", file);
        SPDLOG_ERROR(msg);
        throw Exception(msg);
    }

    List<PaletteInfo> FindAvailablePalettes() {
        if (Game::Level.IsDescent1()) return {};

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

    const string_view GetSecondaryName(SecondaryWeaponIndex id) {
        int index = Game::Level.IsDescent1() ? 109 : 114;
        return GetString(GameString{ index + (int)id });
    }

    const string_view GetPrimaryNameShort(PrimaryWeaponIndex id) {
        if (id == PrimaryWeaponIndex::Spreadfire)
            return "spread"; // D1 has "spreadfire" in the string table, but it gets trimmed by the border

        int index = Game::Level.IsDescent1() ? 114 : 124;
        return Resources::GetString(GameString{ index + (int)id });
    }

    const string_view GetSecondaryNameShort(SecondaryWeaponIndex id) {
        int index = Game::Level.IsDescent1() ? 119 : 134;
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

    // Load the custom exit models. Note this requires the D1 ham for proper texturing.
    void LoadCustomExitModels(HamFile& ham, const Palette& palette) {
        // Don't search the HOG files because it would find the original models
        auto flags = LoadFlag::Filesystem | LoadFlag::Descent1 | LoadFlag::Dxa;

        {
            auto exit = "exit01.pof";
            auto modelData = ReadBinaryFile(exit, flags);
            if (modelData) {
                //auto modelData = Game::Mission->ReadEntry(entry);
                auto model = ReadPof(*modelData, &palette);
                model.FileName = exit;
                auto firstTexture = ham.Models[(int)ham.ExitModel].FirstTexture;
                ham.Models[(int)ham.ExitModel] = model;
                ham.Models[(int)ham.ExitModel].FirstTexture = firstTexture;
            }
        }

        {
            auto exit = "exit01d.pof";
            auto modelData = ReadBinaryFile(exit, flags);
            if (modelData) {
                //auto modelData = Game::Mission->ReadEntry(entry);
                auto model = ReadPof(*modelData, &palette);
                model.FileName = exit;
                auto firstTexture = ham.Models[(int)ham.DestroyedExitModel].FirstTexture;
                ham.Models[(int)ham.DestroyedExitModel] = model;
                ham.Models[(int)ham.DestroyedExitModel].FirstTexture = firstTexture;
            }
        }
    }

    bool LoadDescent1Data() {
        try {
            if (Descent1.source != FullGameData::Unknown) {
                SPDLOG_INFO("Descent 1 data already loaded");
                return true;
            }

            SPDLOG_INFO("Loading Descent 1 data");
            auto hogPath = FileSystem::TryFindFile("descent.hog");
            auto pigPath = FileSystem::TryFindFile("descent.pig");

            if (!hogPath) {
                SPDLOG_WARN("descent.hog not found");
                return false;
            }

            if (!pigPath) {
                SPDLOG_WARN("descent.pig not found");
                return false;
            }

            auto hog = HogFile::Read(*hogPath);
            auto paletteData = hog.ReadEntry("palette.256");
            auto palette = ReadPalette(paletteData);
            auto pigData = File::ReadAllBytes(*pigPath);

            PigFile pig;
            SoundFile sounds;

            auto ham = ReadDescent1GameData(pigData, palette, pig, sounds);
            sounds.Path = pig.Path = *pigPath;

            LoadCustomExitModels(ham, palette);

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
            ReadD1Pig(pigData, pig, sounds);
            sounds.Path = pig.Path = pigPath;
            sounds.Compressed = true;

            auto table = hog.ReadEntry("bitmaps.bin");
            auto paletteData = hog.ReadEntry("palette.256");
            auto palette = ReadPalette(paletteData);

            // Load and fix raw POF files from HOG
            for (auto& entry : hog.Entries) {
                if (entry.Name.ends_with(".pof")) {
                    auto modelData = hog.TryReadEntry(entry.Name);
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

    void LoadPalette(FullGameData& data, string_view palette) {
        // Find the 256 for the palette first. In most cases it is located inside of the hog.
        // But for custom palettes it is on the filesystem
        auto paletteData = data.hog.TryReadEntry(palette);
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
                paletteData = data.hog.ReadEntry("GROUPA.256");
            }
        }

        data.pig = ReadPigFile(pigPath);
        data.palette = ReadPalette(*paletteData);
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

            // Everything loaded okay, set data
            Descent2 = FullGameData(ham, FullGameData::Descent2);
            Descent2.hog = std::move(hog);

            if (auto s22 = FileSystem::TryFindFile("descent2.s22")) {
                Descent2.sounds = ReadSoundFile(*s22);
            }

            LoadPalette(Descent2, "groupa.256"); // default to groupa
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
        auto data = hog.ReadEntry("descent.txb");
        auto text = DecodeText(data);

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

    bool LoadGameTables(LoadFlag flags) {
        // Load order matters. Changes get layered onto each other (root, game, mission)
        auto data = ReadTextFile(GAME_TABLE_FILE, flags);
        if (!data.empty()) {
            LoadGameTable(data, GameData);
            return true;
        }

        return false;
    }

    bool LoadLightTables(LoadFlag flags) {
        auto data = ReadTextFile(LIGHT_TABLE_FILE, flags);
        if (!data.empty()) {
            LoadLightTable(data, Lights);
            return true;
        }

        return false;
    }

    //class ZipFile {
    //    zip_t* _zip;

    //public:
    //    static Option<ZipFile> Open(const filesystem::path& path) {
    //        ZipFile file;

    //        file._zip = zip_open(path.string().c_str(), 0, 'r');
    //        if (!file._zip) return {};
    //        return file;
    //    }

    //    ~ZipFile() {
    //        zip_close(_zip);
    //    }

    //    ResourceHandle Find(string_view fileName) {
    //        
    //    }

    //    ZipFile(const ZipFile&) = delete;
    //    ZipFile(ZipFile&&) = default;
    //    ZipFile& operator=(const ZipFile&) = delete;
    //    ZipFile& operator=(ZipFile&&) = default;

    //private:
    //    ZipFile() = default;
    //};

    Option<ResourceHandle> FindDxaEntryInFolder(const filesystem::path& folder, string_view fileName) {
        Option<ResourceHandle> handle;

        for (auto& folderItem : filesystem::directory_iterator(folder)) {
            auto name = string(fileName);
            auto& itemPath = folderItem.path();

            if (String::InvariantEquals(itemPath.extension().string(), ".dxa")) {
                if (auto zip = zip_open(itemPath.string().c_str(), 0, 'r')) {
                    if (zip_entry_open(zip, name.c_str()) == 0) {
                        handle = ResourceHandle(itemPath, string(fileName));
                        zip_entry_close(zip);
                    }

                    zip_close(zip);
                }
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
                return ResourceHandle::FromHog(unpacked, fileName);
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

    //bool FileExists(string_view fileName, LoadFlag flags) {
    //    auto name = string(fileName);

    //    // current HOG file
    //    if (Game::Mission && HasFlag(flags, LoadFlag::Mission)) {
    //        // Check unpacked data folder for mission
    //        auto path = Game::Mission->Path.parent_path();
    //        auto unpacked = path / Game::Mission->Path.stem() / name;
    //        if (filesystem::exists(unpacked))
    //            return true;

    //        if (Game::Mission->Exists(name))
    //            return true;
    //    }

    //    if (HasFlag(flags, LoadFlag::Dxa)) {
    //        // Check for addon (dxa) data
    //        if (HasFlag(flags, LoadFlag::Descent1) && FindDxaEntryInFolder(D1_FOLDER, fileName))
    //            return true;

    //        if (HasFlag(flags, LoadFlag::Descent2) && FindDxaEntryInFolder(D2_FOLDER, fileName))
    //            return true;

    //        if (HasFlag(flags, LoadFlag::Common) && FindDxaEntryInFolder(COMMON_FOLDER, fileName))
    //            return true;
    //    }

    //    if (HasFlag(flags, LoadFlag::Filesystem)) {
    //        if (HasFlag(flags, LoadFlag::Descent1) && filesystem::exists(D1_FOLDER / name))
    //            return true;

    //        if (HasFlag(flags, LoadFlag::Descent2) && filesystem::exists(D2_FOLDER / name))
    //            return true;

    //        if (HasFlag(flags, LoadFlag::Common) && filesystem::exists(COMMON_FOLDER / name))
    //            return true;
    //    }

    //    // Base HOG file
    //    if (HasFlag(flags, LoadFlag::BaseHog)) {
    //        if (HasFlag(flags, LoadFlag::Descent1) && Descent1.hog.Exists(name))
    //            return true;

    //        if (HasFlag(flags, LoadFlag::Descent2) && Descent2.hog.Exists(name))
    //            return true;
    //    }

    //    return false; // Wasn't found
    //}

    Option<List<byte>> ReadBinaryFileFromZip(const filesystem::path& filePath, string_view name) {
        try {
            List<byte> data;
            if (auto zip = zip_open(filePath.string().c_str(), 0, 'r')) {
                if (zip_entry_open(zip, string(name).c_str()) == 0) {
                    void* buffer;
                    size_t bufferSize;
                    auto readBytes = zip_entry_read(zip, &buffer, &bufferSize);
                    if (readBytes > 0) {
                        SPDLOG_INFO("Read file from {}:{}", filePath.string(), name);
                        data.assign((byte*)buffer, (byte*)buffer + bufferSize);
                    }

                    zip_entry_close(zip);
                }

                zip_close(zip);
            }

            if (!data.empty())
                return data;
            /*auto totalEntries = zip_entries_total(zip);

            for (size_t i = 0; i < totalEntries; i++) {
                zip_entry_openbyindex(zip, i);
                auto entry = zip_entry_name(zip);
                SPDLOG_INFO("{}", entry);
                zip_entry_close(zip);
            }*/
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error reading {}: {}", filePath.string(), e.what());
        }

        return {};
    }


    // GetGameDataFolder(Game::Level)
    Option<List<byte>> ReadFromDxaFolder(const filesystem::path& folder, string_view name) {
        for (auto& file : filesystem::directory_iterator(folder)) {
            if (!file.is_regular_file()) continue;

            auto& filePath = file.path();

            if (String::InvariantEquals(filePath.extension().string(), ".dxa")) {
                if (auto data = ReadBinaryFileFromZip(filePath, name))
                    return data;
            }

            //    List<byte> data;
            //    if (auto zip = zip_open(filePath.string().c_str(), 0, 'r')) {
            //        if (zip_entry_open(zip, string(name).c_str()) == 0) {
            //            void* buffer;
            //            size_t bufferSize;
            //            auto readBytes = zip_entry_read(zip, &buffer, &bufferSize);
            //            if (readBytes > 0) {
            //                SPDLOG_INFO("Read addon data: {}:{}", filePath.string(), name);
            //                data.assign((byte*)buffer, (byte*)buffer + bufferSize);
            //                return data;
            //            }

            //            zip_entry_close(zip);
            //        }

            //        zip_close(zip);
            //    }

            //    /*auto totalEntries = zip_entries_total(zip);

            //    for (size_t i = 0; i < totalEntries; i++) {
            //        zip_entry_openbyindex(zip, i);
            //        auto entry = zip_entry_name(zip);
            //        SPDLOG_INFO("{}", entry);
            //        zip_entry_close(zip);
            //    }*/
            //}
        }

        return {};
    }

    //Option<List<byte>> ReadFileFromFolder(filesystem::path& folder, string_view name) {
    //    for (auto& file : filesystem::directory_iterator(folder)) {}

    //    return {};
    //}

    Option<List<byte>> ReadBinaryFile(string_view fileName, LoadFlag flags) {
        if (fileName.empty()) return {};
        auto file = string(fileName);

        // current HOG file
        if (Game::Mission && HasFlag(flags, LoadFlag::Mission)) {
            // Check unpacked data folder for mission
            //auto path = Game::Mission->Path.parent_path();
            //auto unpacked = path / Game::Mission->Path.stem() / fileName;
            //if (filesystem::exists(unpacked)) {
            //    SPDLOG_INFO("Reading {}", unpacked.string());
            //    return File::ReadAllBytes(unpacked);
            //}

            if (auto data = Game::Mission->TryReadEntry(file)) {
                SPDLOG_INFO("Reading {} from mission", file);
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
                return Descent1.hog.TryReadEntry(file);
            }

            if (HasFlag(flags, LoadFlag::Descent2) && Descent2.hog.Exists(file)) {
                SPDLOG_INFO("Reading {} from descent2.hog", file);
                return Descent1.hog.TryReadEntry(file);
            }
        }

        return {}; // Wasn't found

        //auto resource = Find(name, flags);
        //if (!resource) return {};

        //switch (resource->source) {
        //    case Filesystem:
        //        break;
        //    case Hog:
        //    {
        //        auto hog = HogFile::Read(resource->path);
        //        //hog.ReadEntry(
        //    }

        //        if (HasFlag(flags, LoadFlag::Descent1)) {
        //            if (auto data = Resources::Descent1.hog.TryReadEntry(name)) {
        //                SPDLOG_INFO("Reading {} from descent1.hog", name);
        //                return *data;
        //            }
        //        }
        //        else if (HasFlag(flags, LoadFlag::Descent2)) {
        //            if (auto data = Resources::Descent2.hog.TryReadEntry(name)) {
        //                SPDLOG_INFO("Reading {} from descent2.hog", name);
        //                return *data;
        //            }
        //        }
        //        break;
        //    case Zip:
        //        return ReadBinaryFileFromZip(resource->path, name);
        //}
    }

    string ReadTextFile(string_view name, LoadFlag flags) {
        if (auto bytes = ReadBinaryFile(name, flags)) {
            string str((char*)bytes->data(), bytes->size());
            return str;
        }

        return {};
    }

    bool LoadMaterialTables(LoadFlag flags) {
        // todo: replace with Find()
        //auto commonPath = COMMON_FOLDER / "material.yml";
        //if (FileSystem::TryFindFile(commonPath)) {
        //    auto data = File::ReadAllText(commonPath);
        //    SPDLOG_INFO("Reading material table from `{}`", commonPath.string());
        //    LoadMaterialTable(data, Resources::Materials.GetAllMaterialInfo());
        //}

        auto& gamePath = GetMaterialTablePath(HasFlag(flags, LoadFlag::Descent1));

        if (Game::Mission) {
            auto data = Game::Mission->TryReadEntryAsString("material.yml");
            SPDLOG_INFO("Reading material table from mission");

            if (!data.empty()) {
                LoadMaterialTable(data, Resources::Materials.GetAllMaterialInfo());
                return true;
            }
        }

        if (FileSystem::TryFindFile(gamePath)) {
            auto data = File::ReadAllText(gamePath);
            SPDLOG_INFO("Reading material table from `{}`", gamePath.string());
            LoadMaterialTable(data, Resources::Materials.GetAllMaterialInfo());
            return true;
        }

        return false;
    }

    void LoadDataTables(LoadFlag flags) {
        LoadLightTables(flags);
        LoadMaterialTables(flags);
        LoadGameTables(flags);
    }

    span<JointPos> GetRobotJoints(int robotId, int gun, Animation state) {
        ASSERT((int)state <= 4 && (int)state >= 0);
        auto& robotInfo = GetRobotInfo(robotId);
        ASSERT(gun <= robotInfo.Guns && gun >= 0);
        auto& animStates = robotInfo.Joints[gun][(int)state];
        if (GameData.RobotJoints.empty()) return {};
        auto& joints = GameData.RobotJoints[animStates.Offset];
        return span{ &joints, (uint)animStates.Count };
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

                AvailablePalettes = FindAvailablePalettes();
                GameData = FullGameData(Descent2);
                // todo: switch palette based on level

                // todo: it is not ideal to reload palettes and their textures each time. Cache them.
                // Find the 256 for the palette first. In most cases it is located inside of the d2 hog.
                // But for custom palettes it is on the filesystem
                auto paletteData = Descent2.hog.TryReadEntry(level.Palette);
                auto pigName = ReplaceExtension(level.Palette, ".pig");
                auto pigPath = FileSystem::FindFile(pigName);

                if (!paletteData) {
                    // Wasn't in hog, find on filesystem
                    if (auto path256 = FileSystem::TryFindFile(level.Palette)) {
                        paletteData = File::ReadAllBytes(*path256);
                        pigPath = path256->replace_extension(".pig");
                    }
                    else {
                        // Give up and load groupa
                        paletteData = Descent2.hog.ReadEntry("GROUPA.256");
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

                    if (D1DemoTextureCache.Entries.empty())
                        D1DemoTextureCache = TextureMapCache(D1_DEMO_CACHE, 1800);

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

            Materials = { Render::MATERIAL_COUNT };

            for (auto& obj : level.Objects) {
                if (obj.Type == ObjectType::Hostage)
                    level.TotalHostages++;
            }

            // it should prioritize the current mission then the data folder
            LoadDataTables(LoadFlag::Filesystem | LoadFlag::Mission | GetLevelLoadFlag(level));
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
        return FileSystem::TryFindFile("descent.hog").has_value();
    }

    bool FoundDescent1Demo() {
        return filesystem::exists(D1_DEMO_FOLDER / "descent.hog");
    }

    bool FoundDescent2() {
        return FileSystem::TryFindFile("descent2.hog").has_value();
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
