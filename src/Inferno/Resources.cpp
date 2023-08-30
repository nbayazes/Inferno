#include "pch.h"
#include "Resources.h"
#include "FileSystem.h"
#include "Sound.h"
#include "Pig.h"
#include <fstream>
#include <mutex>
#include "Game.h"
#include "logging.h"
#include "Graphics/Render.h"
#include <Briefing.h>
#include "GameTable.h"
#include "Graphics/MaterialLibrary.h"

namespace Inferno::Resources {
    SoundFile SoundsD1, SoundsD2;

    namespace {
        List<string> RobotNames;
        List<string> PowerupNames;
        HogFile Hog; // Main hog file (descent.hog, descent2.hog)
        Palette LevelPalette;
        PigFile Pig;
        //Dictionary<TexID, PigBitmap> CustomTextures;
        List<PigBitmap> Textures;
        List<string> StringTable; // Text for the UI

        std::mutex PigMutex;
        List<PaletteInfo> AvailablePalettes;

        constexpr VClip DEFAULT_VCLIP{};
        const Model DEFAULT_MODEL{};
        const LevelTexture DEFAULT_TEXTURE{};
        Powerup DEFAULT_POWERUP{};
        DoorClip DEFAULT_DOOR_CLIP{};
        RobotInfo DEFAULT_ROBOT{};

        struct ModelEntry {
            string Name;
            Outrage::Model Model;
        };

        List<ModelEntry> OutrageModels;
    }

    int GetTextureCount() { return (int)Textures.size(); }
    const Palette& GetPalette() { return LevelPalette; }

    void LoadRobotNames(const filesystem::path& path) {
        try {
            std::ifstream file(path);
            string line;
            while (std::getline(file, line))
                RobotNames.push_back(line);
        }
        catch (...) {
            SPDLOG_ERROR(L"Error reading robot names from `{}`", path.wstring());
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
            SPDLOG_ERROR(L"Error reading powerup names from `{}`", path.wstring());
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

    void Init() {
        // Load some default resources.
        LoadPowerupNames("powerups.txt");
        LoadRobotNames("robots.txt");
    }

    const DoorClip& GetDoorClip(DClipID id) {
        if (!Seq::inRange(GameData.DoorClips, (int)id)) return DEFAULT_DOOR_CLIP;
        return GameData.DoorClips[(int)id];
    }

    DClipID GetDoorClipID(LevelTexID id) {
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
        if (!Seq::inRange(GameData.TexInfo, (int)id)) return DEFAULT_TEXTURE; // fix for invalid ids in some levels
        return GameData.TexInfo[(int)id];
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

    const PigEntry& GetTextureInfo(TexID id) {
        if (auto bmp = CustomTextures.Get(id)) return bmp->Info;
        return Pig.Get(id);
    }

    const PigEntry* TryGetTextureInfo(TexID id) {
        if (id <= TexID::Invalid || (int)id >= Pig.Entries.size()) return nullptr;
        if (auto bmp = CustomTextures.Get(id)) return &bmp->Info;
        return &Pig.Get(id);
    }

    const PigEntry& GetTextureInfo(LevelTexID id) {
        return Pig.Get(LookupTexID(id));
    }

    SoundResource GetSoundResource(SoundID id) {
        if (!Seq::inRange(GameData.Sounds, (int)id)) return {};

        if (Game::Level.IsDescent1())
            return { .D1 = GameData.Sounds[(int)id] };
        else
            return { .D2 = GameData.Sounds[(int)id] };
    }

    string_view GetSoundName(SoundID id) {
        if (!Seq::inRange(GameData.Sounds, (int)id)) return "None";
        auto index = GameData.Sounds[(int)id];
        if (Game::Level.IsDescent1())
            return SoundsD1.Sounds[index].Name;
        else
            return SoundsD2.Sounds[index].Name;
    }

    TexID LookupModelTexID(const Model& m, int16 i) {
        if (i >= m.TextureCount || m.FirstTexture + i >= (int16)GameData.ObjectBitmapPointers.size()) return TexID::None;
        //if (i < 0 || i >= m.TextureCount || m.FirstTexture + i >= GameData.ObjectBitmapPointers.size()) return TexID::None;
        auto ptr = GameData.ObjectBitmapPointers[m.FirstTexture + i];
        return GameData.ObjectBitmaps[ptr];
    }

    bool IsLevelTexture(TexID id) {
        auto tex255 = Game::Level.IsDescent1() ? TexID(971) : TexID(1485);
        auto tid = Resources::LookupLevelTexID(id);
        // Default tid is 255, so check if the real 255 texid is passed in
        return tid != LevelTexID(255) || id == tex255;
    }

    Weapon DefaultWeapon{ .AmmoUsage = 1 };

    Weapon& GetWeapon(WeaponID id) {
        if (!Seq::inRange(GameData.Weapons, (int)id)) return DefaultWeapon;
        return GameData.Weapons[(int)id];
    }

    string ReplaceExtension(string src, string ext) {
        auto offset = src.find('.');
        if (!ext.starts_with('.')) ext = "." + ext;
        if (offset == string::npos) return src + ext;
        return src.substr(0, offset) + ext;
    }

    void UpdateAverageTextureColor() {
        SPDLOG_INFO("Update average texture color");

        for (auto& entry : Pig.Entries) {
            auto& bmp = GetBitmap(entry.ID);
            entry.AverageColor = GetAverageColor(bmp.Data);
        }
        //for (auto& tid : GameData.LevelTexIdx) {
        //    auto id = LookupLevelTexID(tid);
        //    if (!Seq::inRange(Pig.Entries, (int)id)) continue;

        //    auto& entry = Pig.Entries[(int)id];
        //    auto& bmp = ReadBitmap(id);
        //    entry.AverageColor = GetAverageColor(bmp.Data);
        //}
    }

    // Reads a file from the current mission or the file system
    // Returns empty list if not found
    List<ubyte> TryReadFile(const filesystem::path& path) {
        auto fileName = path.filename().string();
        if (Game::Mission && Game::Mission->Exists(fileName)) {
            return Game::Mission->ReadEntry(fileName);
        }

        if (filesystem::exists(path)) {
            return File::ReadAllBytes(path);
        }

        return {};
    }

    // Reads a file from the current mission or the file system
    // Returns empty if not found
    List<ubyte> TryReadMissionFile(const filesystem::path& path) {
        auto fileName = path.filename().string();
        if (Game::Mission && Game::Mission->Exists(fileName))
            return Game::Mission->ReadEntry(fileName);

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
        if (Hog.Exists(file))
            return Hog.ReadEntry(file);

        // Then the filesystem
        if (auto path = FileSystem::TryFindFile(file))
            return File::ReadAllBytes(*path);

        auto msg = fmt::format("Required game resource file not found: {}", file);
        SPDLOG_ERROR(msg);
        throw Exception(msg);
    }

    void LoadDescent2Resources(Level& level) {
        std::scoped_lock lock(PigMutex);
        SPDLOG_INFO("Loading Descent 2 level: '{}'\r\n Version: {} Segments: {} Vertices: {}", level.Name, level.Version, level.Segments.size(), level.Vertices.size());
        auto hamData = ReadGameResource("descent2.ham");
        StreamReader reader(hamData);
        auto ham = ReadHam(reader);
        auto hog = HogFile::Read(FileSystem::FindFile(L"descent2.hog"));

        // Find the 256 for the palette first. In most cases it is located inside of the hog.
        // But for custom palettes it is on the filesystem
        auto paletteData = hog.TryReadEntry(level.Palette);
        auto pigName = ReplaceExtension(level.Palette, ".pig");
        auto pigPath = FileSystem::FindFile(pigName);

        if (paletteData.empty()) {
            // Wasn't in hog, find on filesystem
            if (auto path256 = FileSystem::TryFindFile(level.Palette)) {
                paletteData = File::ReadAllBytes(*path256);
                pigPath = path256->replace_extension(".pig");
            }
            else {
                // Give up and load groupa
                paletteData = hog.ReadEntry("GROUPA.256");
            }
        }

        auto pig = ReadPigFile(pigPath);
        auto palette = ReadPalette(paletteData);
        auto textures = ReadAllBitmaps(pig, palette);

        if (level.IsVertigo()) {
            auto vHog = HogFile::Read(FileSystem::FindFile(L"d2x.hog"));
            auto data = vHog.ReadEntry("d2x.ham");
            StreamReader vReader(data);
            AppendVHam(vReader, ham);
        }

        filesystem::path folder = level.Path;
        folder.remove_filename();

        auto pog = ReplaceExtension(level.FileName, ".pog");
        auto pogData = TryReadMissionFile(folder / pog);
        if (!pogData.empty()) {
            SPDLOG_INFO("Loading POG data");
            CustomTextures.LoadPog(pig.Entries, pogData, palette);
        }


        // Everything loaded okay, set the internal data
        LevelPalette = std::move(palette);
        Pig = std::move(pig);
        Hog = std::move(hog);
        GameData = std::move(ham);
        Textures = std::move(textures);

        //FixVClipTimes(GameData.Effects);

        // Read hxm
        auto hxm = ReplaceExtension(level.FileName, ".hxm");
        auto hxmData = TryReadMissionFile(folder / hxm);
        if (!hxmData.empty()) {
            SPDLOG_INFO("Loading HXM data");
            StreamReader hxmReader(hxmData);
            ReadHXM(hxmReader, GameData);
        }
    }

    void LoadSounds() {
        if (FoundDescent1()) {
            try {
                // Unfortunately have to parse the whole pig file because there's no specialized method
                // for just reading sounds
                auto hog = HogFile::Read(FileSystem::FindFile(L"descent.hog"));
                auto paletteData = hog.ReadEntry("palette.256");
                auto palette = ReadPalette(paletteData);

                auto path = FileSystem::FindFile(L"descent.pig");
                StreamReader reader(path);
                auto [ham, pig, sounds] = ReadDescent1GameData(reader, palette);
                sounds.Path = path;
                SoundsD1 = std::move(sounds);
            }
            catch (const std::exception&) {
                SPDLOG_ERROR("Unable to read D1 sound data");
            }
        }

        if (auto s22 = FileSystem::TryFindFile(L"descent2.s22")) {
            SoundsD2 = ReadSoundFile(*s22);
        }
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

    void LoadDescent1Resources(Level& level) {
        std::scoped_lock lock(PigMutex);
        SPDLOG_INFO("Loading Descent 1 level: '{}'\r\n Version: {} Segments: {} Vertices: {}", level.Name, level.Version, level.Segments.size(), level.Vertices.size());
        auto hog = HogFile::Read(FileSystem::FindFile(L"descent.hog"));
        auto paletteData = hog.ReadEntry("palette.256");
        auto palette = ReadPalette(paletteData);

        auto path = FileSystem::FindFile(L"descent.pig");
        StreamReader reader(path);
        auto [ham, pig, sounds] = ReadDescent1GameData(reader, palette);
        pig.Path = path;
        sounds.Path = path;
        //ReadBitmap(pig, palette, TexID(61)); // cockpit
        auto textures = ReadAllBitmaps(pig, palette);

        filesystem::path folder = level.Path;
        folder.remove_filename();
        auto dtx = ReplaceExtension(level.FileName, ".dtx");
        auto dtxData = TryReadMissionFile(folder / dtx);
        if (!dtxData.empty()) {
            SPDLOG_INFO("DTX data found");
            CustomTextures.LoadDtx(pig.Entries, dtxData, palette);
        }

        FixD1ReactorModel(level);

        // Everything loaded okay, set the internal data
        Textures = std::move(textures);
        LevelPalette = std::move(palette);
        Pig = std::move(pig);
        Hog = std::move(hog);
        GameData = std::move(ham);
    }

    void LoadStringTable() {
        StringTable.clear();
        StringTable.reserve(700);
        auto data = Hog.ReadEntry("descent.txb");
        auto briefing = Briefing::Read(data);

        std::stringstream ss(briefing.Raw);
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

    void UpdateObjectRadii(Level& level) {
        for (auto& obj : level.Objects) {
            switch (obj.Type) {
                case ObjectType::Robot:
                {
                    auto& info = Resources::GetRobotInfo(obj.ID);
                    auto& model = Resources::GetModel(info.Model);
                    obj.Radius = model.Radius;
                    break;
                }
                case ObjectType::Coop:
                case ObjectType::Player:
                case ObjectType::Reactor:
                {
                    auto& model = Resources::GetModel(obj.Render.Model.ID);
                    obj.Radius = model.Radius;
                    break;
                }
            }
        }
    }

    void ResetResources() {
        AvailablePalettes = {};
        LevelPalette = {};
        Pig = {};
        Hog = {};
        GameData = {};
        LightInfoTable = {};
        CustomTextures.Clear();
        Textures.clear();
    }

    void LoadLightInfo(const filesystem::path& path) {
        try {
            auto file = FileSystem::ReadFileText(path);
            LightInfoTable = LoadLightTable(file);
        }
        catch (...) {
            SPDLOG_ERROR("Unable to read light table from {}", path.string());
        }
    }

    void LoadMaterialInfo(const filesystem::path& path) {
        try {
            auto file = FileSystem::ReadFileText(path);
            LoadMaterialTable(file, Resources::Materials.GetAllMaterialInfo());
        }
        catch (...) {
            SPDLOG_ERROR("Unable to read material table from {}", path.string());
        }
    }

    void LoadDataTables(const Level& level) {
        // todo: support loading from hog file. note that file names need to be shortened to 8.3
        LoadLightInfo(GetLightFileName(level));
        LoadMaterialInfo(GetMaterialFileName(level));
    }

    void LoadGameTable() {
        // todo: support handle loading game.yml from hog file
        if (auto file = FileSystem::TryFindFile("game.yml"))
            LoadGameTable(*file, GameData);
    }

    span<JointPos> GetRobotJoints(int robotId, int gun, AnimState state) {
        assert((int)state <= 4 && (int)state >= 0);
        auto& robotInfo = GetRobotInfo(robotId);
        assert(gun <= robotInfo.Guns && gun >= 0);
        auto& animStates = robotInfo.Joints[gun][(int)state];
        auto& joints = GameData.RobotJoints[animStates.Offset];
        return span{ &joints, (uint)animStates.Count };
    }

    // Resets all object sizes to their resource defined values
    //void ResetObjectSizes(Level& level) {
    //    for (auto& obj : level.Objects) {
    //        obj.Radius = Editor::GetObjectRadius(obj);
    //    }
    //}

    void LoadLevel(Level& level) {
        try {
            ResetResources();

            if (level.IsDescent2()) {
                LoadDescent2Resources(level);
                AvailablePalettes = FindAvailablePalettes();
            }
            else if (level.IsDescent1()) {
                LoadDescent1Resources(level);
            }
            else {
                throw Exception("Unsupported level version");
            }

            Materials = { Render::MATERIAL_COUNT };

            LoadGameTable();
            LoadDataTables(level);
            LoadStringTable();
            UpdateAverageTextureColor();

            //FixObjectModelIds(level);
            //ResetObjectSizes(level);

            for (auto& seg : level.Segments) {
                // Clamp volume light because some D1 levels use unscaled values
                auto volumeLight = seg.VolumeLight.ToVector4();
                volumeLight.Clamp({ 0, 0, 0, 1 }, { 1, 1, 1, 1 });
                seg.VolumeLight = volumeLight;
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
        }
    }

    const PigBitmap DEFAULT_BITMAP = { PigEntry{ "default", 64, 64 } };

    const PigBitmap& GetBitmap(TexID id) {
        if (Textures.empty())
            return DEFAULT_BITMAP;

        if (auto bmp = CustomTextures.Get(id)) return *bmp;
        if (!Seq::inRange(Textures, (int)id)) id = (TexID)0;
        return Textures[(int)id];
    }

    const PigBitmap& GetBitmap(LevelTexID tid) {
        return GetBitmap(LookupTexID(tid));
    }

    Level ReadLevel(string name) {
        SPDLOG_INFO("Reading level {}", name);
        List<ubyte> data;

        // Search mounted mission first
        if (Game::Mission && Game::Mission->Exists(name))
            data = Game::Mission->ReadEntry(name);

        // Then main hog file
        if (Hog.Exists(name))
            data = Hog.ReadEntry(name);

        if (data.empty()) {
            SPDLOG_ERROR("File not found: {}", name);
            throw Exception("File not found");
        }

        auto level = Level::Deserialize(data);
        level.FileName = name;
        return level;
    }

    bool FoundDescent1() { return FileSystem::TryFindFile("descent.hog").has_value(); }
    bool FoundDescent2() { return FileSystem::TryFindFile("descent2.hog").has_value(); }
    bool FoundDescent3() { return FileSystem::TryFindFile("d3.hog").has_value(); }
    bool FoundVertigo() { return FileSystem::TryFindFile("d2x.hog").has_value(); }
    bool FoundMercenary() { return FileSystem::TryFindFile("merc.hog").has_value(); }

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
                SPDLOG_INFO(L"Loading {} and Table.gam", path->wstring());
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

        if (auto model = TryReadOutrageModel(name)) {
            for (auto& texture : model->Textures) {
                model->TextureHandles.push_back(Render::NewTextureCache->ResolveFileName(texture));
            }
            Render::Materials->LoadTextures(model->Textures);
            OutrageModels.push_back({ name, std::move(*model) });
            return ModelID(OutrageModels.size() - 1);
        }

        return ModelID::None;
    }

    Outrage::Model const* GetOutrageModel(ModelID id) {
        auto i = (int)id;
        if (Seq::inRange(OutrageModels, i))
            return &OutrageModels[i].Model;

        return nullptr;
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
