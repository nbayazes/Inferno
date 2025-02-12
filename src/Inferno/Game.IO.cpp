#include "pch.h"
#include "Editor/Editor.h"
#include "Editor/Editor.IO.h"
#include "Editor/UI/TextureBrowserUI.h"
#include "VisualEffects.h"
#include "FileSystem.h"
#include "Game.EscapeSequence.h"
#include "Game.h"
#include "Game.Room.h"
#include "Game.Text.h"
#include "Graphics.h"
#include "LevelMetadata.h"
#include "Procedural.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Streams.h"
#include "Utility.h"
#include "WindowsDialogs.h"

namespace Inferno::Game {
    namespace {
        struct LoadLevelInfo {
            filesystem::path Path; // hog or level path
            string LevelName; // file name in hog, can be empty
            bool AddToRecent = false;
            Option<Editor::NewLevelInfo> NewLevel;
        };

        Option<LoadLevelInfo> PendingLoad;
    }

    void LoadLevel(const filesystem::path& path, const string& level, bool addToRecent) {
        PendingLoad = LoadLevelInfo{ .Path = path, .LevelName = level, .AddToRecent = addToRecent };
    }

    void NewLevel(Editor::NewLevelInfo& info) {
        PendingLoad = { .NewLevel = info };
    }

    int GetLevelNumber(string_view levelFile) {
        if (Game::Mission) {
            auto filename = Game::Mission->Path.filename().string();

            auto info = Game::GetMissionInfo();

            if (auto index = Seq::indexOf(info.Levels, levelFile))
                return 1 + (int)index.value();

            if (auto index = Seq::indexOf(info.GetSecretLevelsWithoutNumber(), levelFile))
                return -1 - (int)index.value(); // Secret levels have a negative index

            if (String::ToLower(filename) == "descent.hog") {
                // Descent 1 doesn't have a msn file and relies on hard coded values
                if (levelFile.starts_with("levelS")) {
                    if (int index = 0; String::TryParse(levelFile.substr(6, 1), index))
                        return -index;
                }
                else if (levelFile.starts_with("level")) {
                    if (int index = 0; String::TryParse(levelFile.substr(5, 2), index))
                        return index;
                }
            }
        }

        return 1;
    }

    void FixMatcenLinks(Inferno::Level& level) {
        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.Segments[id];

            if (seg.Type == SegmentType::Matcen) {
                if (auto matcen = level.TryGetMatcen(seg.Matcen)) {
                    if (matcen->Segment != SegID(id)) {
                        SPDLOG_WARN("Fixing matcen {} with invalid seg id {}", (int)seg.Matcen, matcen->Segment);
                        matcen->Segment = SegID(id);
                    }
                }
                else {
                    SPDLOG_WARN("Segment {} had invalid matcen ID {}", id, (int)seg.Matcen);
                }
            }
        }
    }


    // Preloads textures for a level
    void PreloadTextures() {
        string customHudTextures[] = {
            "cockpit-ctr",
            "cockpit-left",
            "cockpit-right",
            "gauge01b#0",
            "gauge01b#1",
            "gauge01b#2",
            "gauge01b#3",
            "gauge01b#4",
            "gauge01b#5",
            "gauge01b#6",
            "gauge01b#7",
            "gauge01b#8",
            "gauge01b#10",
            "gauge01b#11",
            "gauge01b#12",
            "gauge01b#13",
            "gauge01b#14",
            "gauge01b#15",
            "gauge01b#16",
            "gauge01b#17",
            "gauge01b#18",
            "gauge01b#19",
            "gauge02b",
            "gauge03b",
            //"gauge16b", // lock
            "Hilite",
            "SmHilite",
            "tracer",
            "Lightning",
            "Lightning3",
            "noise",
            "menu-bg"
        };

        Graphics::LoadTextures(customHudTextures);

        if (Game::Mission) {
            LoadBackgrounds(*Game::Mission);
        }
    }

    void LoadBackgrounds(const HogFile& mission) {
        List<string> bbms;

        for (auto& entry : mission.Entries) {
            if (entry.Extension() == ".bbm") {
                bbms.push_back(entry.Name);
            }
        }

        for (auto& entry : mission.Entries) {
            if (entry.Extension() == ".pcx") {
                bbms.push_back(entry.Name);
            }
        }

        Graphics::LoadTextures(bbms);
    }

    void InitLevel(Inferno::Level&& level) {
        Inferno::Level backup = Level;

        try {
            //LoadMission(PendingLevel);
            //auto level = LoadLevel(PendingLevel);
            ASSERT(level.FileName != "");
            bool reload = level.FileName == Level.FileName;

            // reload game data when switching between shareware and non-shareware
            bool sharewareReload = level.IsShareware != Game::Level.IsShareware;

            Editor::LoadTextureFilter(level);

            bool forceReload =
                level.IsDescent2() != Level.IsDescent2() ||
                NeedsResourceReload ||
                sharewareReload ||
                Resources::CustomTextures.Any() ||
                !String::InvariantEquals(level.Palette, Level.Palette);

            if (sharewareReload)
                Sound::UnloadD1Sounds();

            NeedsResourceReload = false;
            //Rooms.clear();
            IsLoading = true;
            bool wasSecret = LevelNumber < 0;
            FixMatcenLinks(level);

            Level = std::move(level); // Move to global so resource loading works properly
            FreeProceduralTextures();
            Resources::LoadLevel(Level);
            PreloadTextures();
            Level.Rooms = CreateRooms(Level);
            Navigation = NavigationNetwork(Level);
            LevelNumber = GetLevelNumber(Level.FileName);

            if (forceReload || Resources::CustomTextures.Any()) // Check for custom textures before or after load
                Graphics::UnloadTextures();

            Graphics::LoadLevelTextures(Level, forceReload);
            string extraTextures[] = { "noise" };
            Graphics::LoadTextures(extraTextures);
            Graphics::LoadEnvironmentMap("env.dds");

            for (auto& seg : Level.Segments) {
                // Clamp volume light if overly bright segments are saved
                if (seg.VolumeLight.x == seg.VolumeLight.y && seg.VolumeLight.x == seg.VolumeLight.z && seg.VolumeLight.x > 2.0f)
                    seg.VolumeLight = Color(1, 1, 1);
            }

            Graphics::LoadLevel(Level);
            Inferno::ResetEffects();
            InitObjects(Level);

            Editor::OnLevelLoad(reload);
            Graphics::PruneTextures();
            Game::Terrain = {};

            auto exitConfig = String::NameWithoutExtension(Level.FileName) + ".txb";
            if (auto data = Resources::ReadBinaryFile(exitConfig); !data.empty()) {
                DecodeText(data);
                auto lines = String::ToLines(String::OfBytes(data));
                Game::Terrain = ParseEscapeInfo(Level, lines);
                Graphics::LoadTerrain(Game::Terrain);
            }

            //Render::Materials->LoadMaterials(Resources::GameData.HiResGauges, false);
            //Render::Materials->LoadMaterials(Resources::GameData.Gauges, false);

            // Check if we travelled to or from a secret level in D2
            bool secretFlag = false;
            if (Level.IsDescent2()) {
                secretFlag = LevelNumber < 0 || wasSecret;
            }

            Player.StartNewLevel(secretFlag);
            IsLoading = false;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
            Level = backup; // restore the old level if something went wrong
            throw;
        }

        Shell::UpdateWindowTitle();
    }

    struct Level LoadLevel(span<byte> buffer, const filesystem::path& srcPath) {
        auto level = Level::Deserialize(buffer);
        level.FileName = srcPath.filename().string();
        level.Path = srcPath;

        for (auto& seg : level.Segments) {
            // Clamp volume light because some D1 levels use unscaled values
            auto volumeLight = seg.VolumeLight.ToVector4();
            volumeLight.Clamp({ 0, 0, 0, 1 }, { 1, 1, 1, 1 });
            seg.VolumeLight = volumeLight;
        }

        // Load metadata
        filesystem::path metadataPath = srcPath;
        metadataPath.replace_extension(METADATA_EXTENSION);

        if (auto metadata = File::ReadAllText(metadataPath); !metadata.empty())
            LoadLevelMetadata(level, metadata, Editor::EditorLightSettings);

        return level;
    }

    bool LoadMission(const filesystem::path& file) {
        try {
            Mission = HogFile::Read(file);
            return true;
        }
        catch (...) {
            SPDLOG_ERROR("Unable to read HOG {}", file.string());
            return false;
        }
    }

    // Create a mission listing for Descent 1, as it doesn't store one
    MissionInfo CreateDescent1Mission(bool isDemo) {
        if (isDemo) {
            MissionInfo firstStrike{ .Name = FIRST_STRIKE_NAME, .Path = D1_DEMO_PATH / "descent.hog" };
            firstStrike.Levels.resize(7);

            for (int i = 1; i <= firstStrike.Levels.size(); i++)
                firstStrike.Levels[i - 1] = fmt::format("level{:02}.sdl", i);

            firstStrike.Metadata["briefing"] = "briefing";
            firstStrike.Metadata["ending"] = "ending";
            return firstStrike;
        }
        else {
            MissionInfo firstStrike{ .Name = FIRST_STRIKE_NAME, .Path = "d1/descent.hog" };
            firstStrike.Levels.resize(27);

            for (int i = 1; i <= firstStrike.Levels.size(); i++)
                firstStrike.Levels[i - 1] = fmt::format("level{:02}.rdl", i);

            firstStrike.SecretLevels.resize(3);
            firstStrike.SecretLevels[0] = fmt::format("levelS1.rdl,10");
            firstStrike.SecretLevels[1] = fmt::format("levelS2.rdl,21");
            firstStrike.SecretLevels[2] = fmt::format("levelS3.rdl,24");

            firstStrike.Metadata["briefing"] = "briefing";
            firstStrike.Metadata["ending"] = "endreg";
            return firstStrike;
        }
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    MissionInfo GetMissionInfo() {
        try {
            if (!Mission) return {};
            MissionInfo mission{};

            // Read mission from filesystem
            std::ifstream file(Mission->GetMissionPath());
            if (mission.Read(file)) {
                mission.Path = Mission->GetMissionPath();
                return mission;
            }

            // Descent2 stores its mn2 in the hog file
            auto ext = Level.IsDescent1() ? "msn" : "mn2";

            if (auto entry = Mission->FindEntryOfType(ext)) {
                auto bytes = Mission->ReadEntry(*entry);
                string str((char*)bytes.data(), bytes.size());
                std::stringstream stream(str);
                mission.Read(stream);
                return mission;
            }

            // descent.hog does not have an msn, create a replacement
            if (String::ToLower(Mission->Path.string()).ends_with("descent.hog")) {
                auto isDemo = Mission->ContainsFileType(".sdl");
                return CreateDescent1Mission(isDemo);
            }

            return {};
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
            return {};
        }
    }

    // Returns a level version, 0 for a mission, or -1 for unknown
    int32 FileVersionFromHeader(const filesystem::path& path) {
        StreamReader reader(path);
        auto id = reader.ReadString(3);
        if (id == "DHF") return 0; // Return 0 for hog files

        reader.Seek(0);
        auto sig = (uint)reader.ReadInt32();
        if (sig == MakeFourCC("LVLP")) {
            return reader.ReadInt32(); // Level version
        }

        return -1;
    }

    void LoadLevelMetadata(Inferno::Level& level) {
        auto metadataFile = String::NameWithoutExtension(level.FileName) + METADATA_EXTENSION;
        string path = metadataFile;
        auto metadata = Game::Mission->TryReadEntryAsString(metadataFile);

        if (metadata.empty()) {
            auto mission = String::ToLower(Game::Mission->Path.filename().string());

            // todo: remove `data` from paths
            // Read IED from data directories for official missions
            if (mission == "descent.hog")
                path = "data/d1/" + metadataFile;
            else if (mission == "descent2.hog")
                path = "data/d2/" + metadataFile;
            else if (mission == "d2x.hog")
                path = "data/d2/vertigo" + metadataFile; // Vertigo

            if (filesystem::exists(path))
                metadata = File::ReadAllText(path);
        }

        if (!metadata.empty()) {
            SPDLOG_INFO("Reading level metadata from `{}`", path);
            LoadLevelMetadata(level, metadata, Editor::EditorLightSettings);
        }
    }

    Inferno::Level LoadLevelFromMission(const string& name) {
        ASSERT(Game::Mission);

        auto data = Game::Mission->ReadEntry(name);

        auto shareware = String::ToLower(name).ends_with(".sdl");
        if (shareware)
            SPDLOG_INFO("Shareware level loaded! Certain functionality will be unavailable.");

        auto level = shareware ? Level::DeserializeD1Demo(data) : Level::Deserialize(data);
        level.FileName = name;
        level.Path = Mission->Path;

        LoadLevelMetadata(level);
        return level;
    }

    //void LoadLevelMetadata(Inferno::Level& level) {
    //    auto metadataFile = String::NameWithoutExtension(level.FileName) + METADATA_EXTENSION;
    //    auto metadata = Game::Mission->TryReadEntryAsString(metadataFile);

    //    if (metadata.empty()) {
    //        auto mission = String::ToLower(Game::Mission->Path.filename().string());
    //        string path;

    //        // Read IED from the data directories for official missions
    //        if (mission == "descent.hog")
    //            path = "data/d1/" + metadataFile;
    //        else if (mission == "descent2.hog")
    //            path = "data/d2/" + metadataFile;
    //        else if (mission == "d2x.hog")
    //            path = "data/d2/vertigo" + metadataFile; // Vertigo

    //        if (filesystem::exists(path)) {
    //            SPDLOG_INFO("Reading level metadata from `{}`", path);
    //            metadata = File::ReadAllText(path);
    //        }
    //    }

    //    if (!metadata.empty())
    //        LoadLevelMetadata(level, metadata, Editor::EditorLightSettings);
    //}

    // Levels start a 1. Secret levels are negative. 0 is undefined.
    string LevelNameByIndex(int index) {
        if (index == 0) index = 1;

        if (Game::Mission) {
            for (auto& level : Game::Mission->GetLevels()) {
                if (GetLevelNumber(level.Name) == index) {
                    return level.Name;
                }
            }
        }

        return {};
    }

    //bool LoadLevelByIndex(int index) {
    //    if (Game::Mission) {
    //        for (auto& level : Game::Mission->GetLevels()) {
    //            if (GetLevelNumber(level.Name) == index) {
    //                LoadLevelFromHOG(level.Name);
    //                return true;
    //            }
    //        }
    //    }

    //    return false;
    //}


    //bool LoadLevel(const filesystem::path& path) {
    //    try {
    //        auto version = FileVersionFromHeader(path);
    //        if (version > 0 && version <= 8) {
    //            PendingLevel = path;
    //            //LoadLevel(path);
    //        }
    //        else if (version == 0) {
    //            // Hog file
    //            Game::Mission = HogFile::Read(path);
    //            //Game::LoadMission(path);

    //            LoadLevelByIndex(0);
    //        }
    //        else {
    //            throw Exception("Unknown file type");
    //        }

    //        //Settings::Editor.AddRecentFile(path);
    //        return true;
    //    }
    //    catch (const std::exception& e) {
    //        ShowErrorMessage(e);
    //        return false;
    //    }
    //}


    // Loads a D1 or D2 Vertigo level (no XL)
    //struct Level LoadLevel(std::filesystem::path path) {
    //    std::ifstream file(path, std::ios::binary);
    //    if (!file) throw Exception("File does not exist");

    //    auto size = filesystem::file_size(path);
    //    List<ubyte> buffer(size);
    //    if (!file.read((char*)buffer.data(), size))
    //        throw Exception("Error reading file");

    //    LoadLevel(buffer, path);
    //}

    //Level DeserializeLevel(span<byte> data) {
    //    
    //}

    // Top level
    void OnLoadLevel(const LoadLevelInfo& info) {
        Inferno::Level level;

        if (info.NewLevel) {
            level = Editor::CreateNewLevel(*info.NewLevel);
        }
        else {
            if (!filesystem::exists(info.Path)) {
                SPDLOG_ERROR("{} not found, unable to load level", info.Path.string());
                return;
            }

            auto version = FileVersionFromHeader(info.Path);

            if (version > 0 && version <= 8) {
                Game::Mission = {};

                // Load an unpacked level
                auto data = File::ReadAllBytes(info.Path);
                level = Level::Deserialize(data);
                level.FileName = info.Path.filename().string();
                level.Path = info.Path;

                // Load metadata
                filesystem::path metadataFile = info.Path;
                metadataFile.replace_extension(METADATA_EXTENSION);
                if (auto metadata = File::ReadAllText(metadataFile); !metadata.empty()) {
                    SPDLOG_INFO("Loaded level metadata from: {}", metadataFile.string());
                    LoadLevelMetadata(level, metadata, Editor::EditorLightSettings);
                }
            }
            else if (version == 0) {
                // Hog file
                Game::Mission = HogFile::Read(info.Path);
                //Game::LoadMission(path);

                auto entry = Game::Mission->TryReadEntry(info.LevelName);
                if (entry.empty()) {
                    auto name = LevelNameByIndex(1);
                    // no levels in mission, create an empty level
                    if (name.empty())
                        level = Editor::CreateNewLevel(*info.NewLevel);
                    else
                        level = LoadLevelFromMission(name);
                }
                else {
                    level = LoadLevelFromMission(info.LevelName);
                }
            }
            else {
                throw Exception("Unknown file type");
            }
        }

        InitLevel(std::move(level));
    }

    bool HasPendingLoad() {
        return PendingLoad.has_value();
    }

    void CheckLoadLevel() {
        try {
            if (PendingLoad) {
                OnLoadLevel(*PendingLoad);

                // Editor requested the level load
                if (PendingLoad->AddToRecent) {
                    Settings::Editor.AddRecentFile(PendingLoad->Path);

                    Editor::SetStatusMessage("Loaded file {}", PendingLoad->Path.string());

                    if (Game::Mission) {
                        auto levelEntries = Game::Mission->GetLevels();

                        // Show hog editor if there's more than one level and game data is present.
                        // If there's no game data, the config dialog will conflict causing the UI to get stuck.
                        if (levelEntries.size() > 1 && Resources::HasGameData())
                            Editor::Events::ShowDialog(Editor::DialogType::HogEditor);
                    }
                }

                PendingLoad = {};
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Unable to load level:\n{}", e.what());
        }
    }

    List<string> ParseSng(const string& sng) {
        const auto lines = String::Split(sng);
        std::vector<std::string> result;

        // sng files are in two formats, the original tab separated format including drum banks
        // or a simplified format containing only the song name. We only care about the file name.
        for (auto& line : lines) {
            if (line == "\x1a") continue; // weird EOL character at end of d1 sng from CPM filesystem
            auto tokens = String::Split(line, '\t');

            if (!tokens.empty()) {
                tokens[0] = String::TrimEnd(tokens[0], "\r");
                result.push_back(tokens[0]);
            }
        }

        return result;
    }

    void PlayLevelMusic() {
        // Determine the correct song to play based on the level number
        auto sng = Resources::ReadTextFile("descent.sng");
        if (sng.empty()) {
            SPDLOG_WARN("No SNG file found!");
            return;
        }

        auto songs = ParseSng(sng);

        constexpr uint FirstLevelSong = 5;
        if (songs.size() < FirstLevelSong) {
            SPDLOG_WARN("Not enough songs in SNG file. Expected 5, was {}", songs.size());
            return;
        }

        auto availableLevelSongs = songs.size() - FirstLevelSong;
        auto songIndex = FirstLevelSong + std::abs(LevelNumber - 1) % availableLevelSongs;
        string song = songs[songIndex];

        PlayMusic(song);
    }

    void PlayMusic(string_view song, bool loop) {
        //Sound::PlayMusic("Resignation.mp3");
        //Sound::PlayMusic("Title.ogg");
        //Sound::PlayMusic("Hostility.flac");

        //PlayMusic("endlevel.hmp");

        // Try playing the given file name if it exists (ignore hmp / midi for now)
        if (!song.ends_with(".hmp") && Resources::FileExists(song)) {
            Sound::PlayMusic(song);
            return;
        }

        if (Game::Mission) {
            // Check the unpacked mission data folder and mission file for music
            auto base = std::filesystem::path(song).stem();
            std::array extensions = { ".ogg", ".mp3", ".flac" };

            for (auto& ext : extensions) {
                auto unpacked = Game::Mission->Path.parent_path() / Game::Mission->Path.stem() / base;
                unpacked.replace_extension(ext);
                if (filesystem::exists(unpacked)) {
                    Sound::PlayMusic(File::ReadAllBytes(unpacked), loop);
                    return;
                }
            }

            for (auto& ext : extensions) {
                base.replace_extension(ext);
                auto entry = Game::Mission->TryReadEntry(base.string());
                if (!entry.empty()) {
                    Sound::PlayMusic(std::move(entry), loop);
                    return;
                }
            }
        }

        // Check the file system for music. Priority is arbitrary.
        filesystem::path path(song);

        path.replace_extension(".ogg");
        if (Resources::FileExists(path.string()))
            Sound::PlayMusic(path.string(), loop);

        path.replace_extension(".mp3");
        if (Resources::FileExists(path.string()))
            Sound::PlayMusic(path.string(), loop);

        path.replace_extension(".flac");
        if (Resources::FileExists(path.string()))
            Sound::PlayMusic(path.string(), loop);

        // todo: play the original midi if no replacement music
    }
}
