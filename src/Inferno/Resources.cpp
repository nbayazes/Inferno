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
#include "Editor/Editor.Object.h"

namespace Inferno::Resources {
    SoundFile SoundsD1, SoundsD2;

    namespace {
        List<string> RobotNames;
        List<string> PowerupNames;

        HogFile Hog; // Main hog file (descent.hog, descent2.hog)
        Palette LevelPalette;
        PigFile Pig;
        List<PigBitmap> Textures;

        std::mutex PigMutex;
        List<PaletteInfo> AvailablePalettes;
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

    void Init() {
        // Load some default resources.
        LoadPowerupNames("powerups.txt");
        LoadRobotNames("robots.txt");
    }


    WallClip DEFAULT_WALL_CLIP = {};

    const WallClip& GetDoorClip(WClipID id) {
        if (!Seq::inRange(GameData.DoorClips, (int)id)) return DEFAULT_WALL_CLIP;
        return GameData.DoorClips[(int)id];
    }

    const WallClip* TryGetWallClip(WClipID id) {
        if (!Seq::inRange(GameData.DoorClips, (int)id)) return nullptr;
        return &GameData.DoorClips[(int)id];
    }

    WClipID GetWallClipID(LevelTexID id) {
        for (int i = 0; i < GameData.DoorClips.size(); i++) {
            if (GameData.DoorClips[i].Frames[0] == id)
                return WClipID(i);
        }

        return WClipID::None;
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

    Powerup DEFAULT_POWERUP = {
        .VClip = VClipID::None,
        .Size = 5
    };

    const Powerup& GetPowerup(int id) {
        if (!Seq::inRange(GameData.Powerups, id)) return DEFAULT_POWERUP;
        return GameData.Powerups[id];
    }

    VClip DefaultVClip{};

    const VClip& GetVideoClip(VClipID id) {
        if (GameData.VClips.size() <= (int)id) return DefaultVClip;
        return GameData.VClips[(int)id];
    }

    Model DEFAULT_MODEL{};
    RobotInfo DEFAULT_ROBOT{};

    const Inferno::Model& GetModel(ModelID id) {
        if (!Seq::inRange(GameData.Models, (int)id)) return DEFAULT_MODEL;
        return GameData.Models[(int)id];
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

    const LevelTexture* TryGetLevelTextureInfo(LevelTexID id) {
        if (!Seq::inRange(GameData.TexInfo, (int)id)) return nullptr; // fix for invalid ids in some levels
        return &GameData.TexInfo[(int)id];
    }

    const LevelTexture DefaultTexture{};

    const LevelTexture& GetLevelTextureInfo(LevelTexID id) {
        if (!Seq::inRange(GameData.TexInfo, (int)id)) return DefaultTexture; // fix for invalid ids in some levels
        return GameData.TexInfo[(int)id];
    }

    const LevelTexture& GetLevelTextureInfo(TexID id) {
        if (!Seq::inRange(GameData.LevelTexIdx, (int)id)) return DefaultTexture;
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

    const PigEntry& GetTextureInfo(TexID id) {
        if (auto bmp = CustomResources.Get(id)) return bmp->Info;
        return Pig.Get(id);
    }

    const PigEntry* TryGetTextureInfo(TexID id) {
        if (id <= TexID::Invalid || (int)id >= Pig.Entries.size()) return nullptr;
        if (auto bmp = CustomResources.Get(id)) return &bmp->Info;
        return &Pig.Get(id);
    }

    const PigEntry& GetTextureInfo(LevelTexID id) {
        return GetTextureInfo(LookupTexID(id));
    }

    const PigEntry* TryGetTextureInfo(LevelTexID id) {
        return TryGetTextureInfo(LookupTexID(id));
    }

    Sound::SoundResource GetSoundResource(SoundID id) {
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
        auto ptr = GameData.ObjectBitmapPointers[m.FirstTexture + i];
        return GameData.ObjectBitmaps[ptr];
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
            CustomResources.LoadPog(pig.Entries, pogData, palette);
        }

        // Everything loaded okay, set the internal data
        LevelPalette = std::move(palette);
        Pig = std::move(pig);
        Hog = std::move(hog);
        GameData = std::move(ham);
        Textures = std::move(textures);

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

    // Some levels don't have the D1 reactor model set
    void FixD1ReactorModel(Level& level) {
        for (auto& obj : level.Objects) {
            if (obj.Type == ObjectType::Reactor) {
                obj.ID = 0;
                obj.Render.Model.ID = ModelID(39);
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
        auto dtxData = TryReadFile(folder / dtx);
        if (!dtxData.empty()) {
            SPDLOG_INFO("DTX data found");
            CustomResources.LoadDtx(pig.Entries, dtxData, palette);
        }

        FixD1ReactorModel(level);

        // Everything loaded okay, set the internal data
        Textures = std::move(textures);
        LevelPalette = std::move(palette);
        Pig = std::move(pig);
        Hog = std::move(hog);
        GameData = std::move(ham);
    }

    void ResetResources() {
        AvailablePalettes = {};
        LevelPalette = {};
        Pig = {};
        Hog = {};
        GameData = {};
        CustomResources.Clear();
        Textures.clear();
    }

    // Some old levels didn't properly set the render model ids.
    void FixObjectModelIds(Level& level) {
        for (auto& obj : level.Objects) {
            switch (obj.Type) {
                case ObjectType::Robot:
                    obj.Render.Model.ID = Resources::GetRobotInfo(obj.ID).Model;
                    break;

                case ObjectType::Weapon:
                    obj.Render.Model.ID = Models::PlaceableMine;
                    break;

                case ObjectType::Player:
                    obj.Render.Model.ID = level.IsDescent1() ? Models::D1Player : Models::D2Player;
                    break;

                case ObjectType::Coop:
                    obj.Render.Model.ID = level.IsDescent1() ? Models::D1Coop : Models::D2Coop;
                    break;
            }
        }
    }

    // Resets all object sizes to their resource defined values
    void ResetObjectSizes(Level& level) {
        for (auto& obj : level.Objects) {
            obj.Radius = Editor::GetObjectRadius(obj);
        }
    }


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

            UpdateAverageTextureColor();

            FixObjectModelIds(level);
            ResetObjectSizes(level);
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
        }
    }

    const PigBitmap DEFAULT_BITMAP = { PigEntry{ "default", 64, 64 } };

    const PigBitmap& GetBitmap(TexID id) {
        if (Textures.empty())
            return DEFAULT_BITMAP;

        if (auto bmp = CustomResources.Get(id)) return *bmp;
        if (!Seq::inRange(Textures, (int)id)) id = (TexID)0;
        return Textures[(int)id];
    }

    List<ubyte> ReadFile(string file) {
        // Search mounted mission first
        if (Game::Mission && Game::Mission->Exists(file))
            return Game::Mission->ReadEntry(file);

        // Then main hog file
        if (Hog.Exists(file))
            return Hog.ReadEntry(file);

        SPDLOG_ERROR("File not found: {}", file);
        throw Exception("File not found");
    }

    Level ReadLevel(string name) {
        SPDLOG_INFO("Reading level {}", name);
        List<ubyte> data;

        // Search mounted mission first, then the main hog files
        if (Game::Mission && Game::Mission->Exists(name))
            data = Game::Mission->ReadEntry(name);
        else if (Hog.Exists(name))
            data = Hog.ReadEntry(name);

        if (data.empty()) {
            SPDLOG_ERROR("File not found: {}", name);
            throw Exception("File not found");
        }

        auto level = Level::Deserialize(data,
                                        Settings::Editor.UseSharedClosedWalls
                                        ? WallsSerialization::SHARED_SIMPLE_WALLS
                                        : WallsSerialization::STANDARD);
        level.FileName = name;
        return level;
    }

    bool FoundDescent1() { return FileSystem::TryFindFile("descent.hog").has_value(); }
    bool FoundDescent2() { return FileSystem::TryFindFile("descent2.hog").has_value(); }
    bool FoundVertigo() { return FileSystem::TryFindFile("d2x.hog").has_value(); }
    bool FoundDescent3() { return FileSystem::TryFindFile("d3.hog").has_value(); }
    bool FoundMercenary() { return FileSystem::TryFindFile("merc.hog").has_value(); } // todo: steam release of merc uses a different hog name

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

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& name) {
        try {
            if (auto r = OpenFile(name))
                return Outrage::Bitmap::Read(*r);
        }
        catch (const Exception& e) {
            SPDLOG_WARN("Error reading texture {} - {}", name, e.what());
        }

        return {};
    }

    Option<Outrage::Bitmap> ReadOutrageVClip(const string& name) {
        try {
            if (auto r = OpenFile(name)) {
                auto vclip = Outrage::VClip::Read(*r);
                return vclip.Frames[0]; // just return the first frame for now
            }
        }
        catch (const Exception& e) {
            SPDLOG_WARN("Error reading texture {} - {}", name, e.what());
        }

        return {};
    }

    Option<Outrage::Model> ReadOutrageModel(const string& name) {
        if (auto r = OpenFile(name))
            return Outrage::Model::Read(*r);

        return {};
    }

    Dictionary<string, Outrage::Model> OutrageModels;

    Outrage::Model const* GetOutrageModel(const string& name) {
        if (OutrageModels.contains(name))
            return &OutrageModels[name];

        //if (auto model = ReadOutrageModel(name)) {
        //    for (auto& texture : model->Textures) {
        //        model->TextureHandles.push_back(Render::NewTextureCache->ResolveFileName(texture));
        //    }
        //    OutrageModels[name] = std::move(*model);
        //    return &OutrageModels[name];
        //}

        return nullptr;
    }
};
