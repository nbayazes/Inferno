#include "pch.h"
#include "logging.h"
#include "Editor.IO.h"
#include "Editor.Segment.h"
#include "LevelSettings.h"
#include "Events.h"
#include "Game.h"
#include "Settings.h"
#include "Editor.Object.h"
#include "Editor.Wall.h"
#include "Editor.h"
#include "Graphics/Render.h"
#include "Editor.Diagnostics.h"

namespace Inferno::Editor {
    constexpr auto METADATA_EXTENSION = "ied"; // inferno engine data

    size_t SaveLevel(Level& level, StreamWriter& writer) {
        if (level.Walls.size() >= (int)WallID::Max)
            throw Exception("Cannot save a level with more than 255 walls");

        DisableFlickeringLights(level);
        ResetFlickeringLightTimers(level);
        FixLevel(level);

        for (auto& obj : level.Objects) {
            if (obj.Type == ObjectType::SecretExitReturn) {
                level.SecretExitReturn = obj.Segment;
                level.SecretReturnOrientation = obj.Rotation;
            }
        }
        return level.Serialize(writer);
    }

    // Saves a level to the file system
    void SaveLevelToPath(Level& level, std::filesystem::path path, bool autosave = false) {
        CleanLevel(level);

        filesystem::path temp = path;
        temp.replace_extension("tmp");

        {
            // Write to temp file
            std::ofstream file(temp, std::ios::binary);
            StreamWriter writer(file, false);
            SaveLevel(Game::Level, writer);
        }

        if (filesystem::exists(path)) {
            // Backup the current file
            filesystem::path backup = path;
            backup.replace_extension("bak");
            filesystem::copy_file(path, backup, filesystem::copy_options::overwrite_existing);
        }

        // Replace the current from temp
        filesystem::copy_file(temp, path, filesystem::copy_options::overwrite_existing);
        filesystem::remove(temp);

        filesystem::path metadataPath = path;
        metadataPath.replace_extension(METADATA_EXTENSION);
        std::ofstream metadata(metadataPath);
        SaveLevelMetadata(level, metadata);
        SetStatusMessage(L"Saved level to {}", path.wstring());

        if (!autosave) {
            Editor::History.UpdateCleanSnapshot();
            Game::Level.Path = path;
            Game::Level.FileName = path.filename().string();
            History.UpdateCleanSnapshot();
        }
    }

    void LoadLevel(std::filesystem::path path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw Exception("File does not exist");

        auto size = filesystem::file_size(path);
        List<ubyte> buffer(size);
        if (!file.read((char*)buffer.data(), size))
            throw Exception("Error reading file");

        auto level = Level::Deserialize(buffer);
        level.FileName = path.filename().string();
        level.Path = path;

        filesystem::path metadataPath = path;
        metadataPath.replace_extension(METADATA_EXTENSION);

        // Load metadata
        std::ifstream metadataStream(metadataPath);
        if (metadataStream) {
            std::stringstream metadata;
            metadata << metadataStream.rdbuf();
            LoadLevelMetadata(level, metadata.str());
        }

        Game::UnloadMission();
        Game::LoadLevel(std::move(level));
        SetStatusMessage("Loaded level {}", path.filename().string());
    }

    // Returns a level version, 0 for a mission, or -1 for unknown
    int32 FileVersionFromHeader(filesystem::path path) {
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

    void LoadFile(filesystem::path path) {
        try {
            auto version = FileVersionFromHeader(path);
            if (version > 0 && version <= 8) { // D1 to Vertigo level (no XL)
                LoadLevel(path);
            }
            else if (version == 0) { // Hog file
                Game::LoadMission(path);

                // Load first sorted level in the mission
                auto levelEntries = Game::Mission->GetLevels();
                Seq::sortBy(levelEntries, [](HogEntry& a, HogEntry& b) { return a.Name < b.Name; });
                if (!levelEntries.empty()) {
                    LoadLevelFromHOG(levelEntries[0].Name);

                    if (levelEntries.size() > 1) // show hog editor if there's more than one level
                        Events::ShowDialog(DialogType::HogEditor);
                }
            }
            else {
                throw Exception("Unknown file type");
            }

            Settings::AddRecentFile(path);
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }

    void LoadLevelFromHOG(string name) {
        try {
            auto level = Resources::ReadLevel(name);
            level.FileName = name;
            // Load metadata
            auto metadataPath = String::NameWithoutExtension(level.FileName) + "." + METADATA_EXTENSION;
            auto metadata = Game::Mission->TryReadEntry(metadataPath);
            if (!metadata.empty()) {
                string buffer((char*)metadata.data(), metadata.size());
                LoadLevelMetadata(level, buffer);
            }

            Game::LoadLevel(std::move(level));
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }

    void OnSave();

    void NewLevel(string name, string fileName, int16 version, bool addToHog) {
        if (!addToHog)
            Game::UnloadMission();

        Level level;
        level.Name = name;
        level.Version = version;
        level.GameVersion = version == 1 ? 25 : 32;
        auto ext = level.IsDescent1() ? ".rdl" : ".rl2";
        level.FileName = fileName.substr(0, 8) + ext;

        if (Game::Mission) {
            // Find a unique file name
            int i = 1;
            while (Game::Mission->Exists(level.FileName))
                level.FileName = fmt::format("{}{}{}", fileName.substr(0, 7), i++, ext);
        }

        auto tag = AddDefaultSegment(level);
        AddObject(level, { tag, SideID::Bottom }, ObjectType::Player);
        Game::LoadLevel(std::move(level));

        if (Game::Mission)
            OnSave(); // Trigger save to add the level to the HOG
    }


    // Serializes a level to memory
    std::vector<ubyte> WriteLevelToMemory(Level& level) {
        std::stringstream stream;
        stream.unsetf(std::ios::skipws);
        StreamWriter writer(stream);
        auto len = SaveLevel(level, writer);
        std::vector<ubyte> data(len);
        stream.read((char*)data.data(), data.size());
        return data;
    }

    std::vector<ubyte> WriteLevelMetadataToMemory(const Level& level) {
        std::stringstream stream;
        stream.unsetf(std::ios::skipws);
        SaveLevelMetadata(level, stream);
        std::vector<ubyte> data(stream.tellp());
        stream.read((char*)data.data(), data.size());
        return data;
    }

    void EnsureVertigoData(filesystem::path missionPath) {
        try {
            if (!Game::Level.IsVertigo()) return;
            auto mission = HogFile::Read(missionPath);

            if (mission.ContainsFileType(".ham")) return; // Already has ham data
            if (!Resources::FoundVertigo()) {
                SPDLOG_WARN("Level is marked as Vertigo but has no .ham and d2x.hog was not found");
                return;
            }

            // Insert vertigo data
            auto d2xhog = HogFile::Read(FileSystem::FindFile(L"d2x.hog"));
            auto vertigoData = d2xhog.ReadEntry("d2x.ham");
            auto hamName = missionPath.stem().string() + ".ham";
            mission.AddOrUpdateEntry(hamName, vertigoData);
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Unable to add vertigo data: {}", e.what());
        }
    }

    void OnSaveAs() {
        if (!Resources::HasGameData()) return;

        auto& level = Game::Level;
        List<COMDLG_FILTERSPEC> filter = { { L"Mission", L"*.hog" } };

        if (level.IsDescent1())
            filter.push_back({ L"Descent 1 Level", L"*.rdl" });
        else
            filter.push_back({ L"Descent 2 Level", L"*.rl2" });

        auto name = level.FileName == "" ? "level" : level.FileName;

        wstring defaultName;
        uint filterIndex = 0;

        if (Game::Mission) {
            defaultName = Game::Mission->Path.filename();
            filterIndex = 1;
        }
        else {
            defaultName = Convert::ToWideString(name);
            filterIndex = 2;
        }

        auto ext = level.IsDescent1() ? "rdl" : "rl2";

        if (auto path = SaveFileDialog(filter, filterIndex, defaultName)) {
            if (ExtensionEquals(*path, L"hog")) {
                auto levelData = WriteLevelToMemory(level);
                auto levelMetadata = WriteLevelMetadataToMemory(level);

                if (Game::Mission) {
                    // Save to new location, then update the current level in it
                    Game::Mission = Game::Mission->SaveCopy(*path);
                    Game::Mission->AddOrUpdateEntry(level.FileName, levelData);
                    auto metadataName = String::NameWithoutExtension(level.FileName) + "." + METADATA_EXTENSION;
                    Game::Mission->AddOrUpdateEntry(metadataName, levelMetadata);
                    EnsureVertigoData(*path);
                }
                else {
                    // Create a new hog
                    filesystem::path fileName = name; // set proper extension
                    fileName.replace_extension(ext);
                    level.FileName = fileName.string();
                    HogFile::CreateFromEntry(*path, level.FileName, levelData);
                    EnsureVertigoData(*path);
                    Game::LoadMission(*path);
                    Events::ShowDialog(DialogType::HogEditor);
                }

                SetStatusMessage("Mission saved to {}", path->string());
            }
            else {
                path->replace_extension(ext);
                SaveLevelToPath(level, *path);
                Game::UnloadMission();
            }

            Settings::AddRecentFile(*path);
        }

        History.UpdateCleanSnapshot();
    }

    void OnSave() {
        if (!Resources::HasGameData()) return;
        auto& level = Game::Level;

        if (Game::Mission) {
            assert(level.FileName != "");
            auto data = WriteLevelToMemory(level);
            Game::Mission->AddOrUpdateEntry(level.FileName, data);
            auto levelMetadata = WriteLevelMetadataToMemory(level);
            auto metadataName = String::NameWithoutExtension(level.FileName) + "." + METADATA_EXTENSION;
            Game::Mission->AddOrUpdateEntry(metadataName, levelMetadata);
            EnsureVertigoData(Game::Mission->Path);
            Game::LoadMission(Game::Mission->Path);
            SetStatusMessage(L"Mission saved to {}", Game::Mission->Path.filename().wstring());
            Settings::AddRecentFile(Game::Mission->Path);
        }
        else {
            // standalone level
            if (level.Path.empty()) {
                OnSaveAs();
            }
            else {
                SaveLevelToPath(level, level.Path);
                Settings::AddRecentFile(level.Path);
            }
        }

        History.UpdateCleanSnapshot();
    }

    bool CanConvertToD2() { return !Game::Level.IsDescent2NoVertigo(); }

    void ConvertToD2() {
        if (!CanConvertToD2()) return;

        // change version and reload resources
        Game::Level.Version = 7;
        Resources::LoadLevel(Game::Level);

        // replace vertigo robots with hulk
        auto maxRobotIndex = Resources::GameData.Robots.size();
        for (auto& obj : Game::Level.Objects) {
            if (obj.Type == ObjectType::Robot && obj.ID >= maxRobotIndex)
                obj.ID = 0;
        }

        Render::Materials->LoadLevelTextures(Game::Level, false);
        Render::LoadLevel(Game::Level);
        Editor::History.Reset(); // Undo / redo could cause models to get loaded without the proper data
    }

    bool CanConvertToVertigo() { return !Game::Level.IsVertigo(); }

    void ConvertToVertigo() {
        if (!CanConvertToVertigo()) return;

        if (!Resources::FoundVertigo()) {
            ShowErrorMessage(L"No Vertigo data found!", L"Unable to Convert");
            return; // Can't do it!
        }

        // change version and reload resources
        Game::Level.Version = 8;
        Resources::LoadLevel(Game::Level);
        Render::LoadLevel(Game::Level);
        Editor::History.Reset(); // Undo / redo could cause models to get loaded without the proper data
    }

    double _nextAutosave = FLT_MAX;

    void ResetAutosaveTimer() {
        if (Settings::AutosaveMinutes == 0) _nextAutosave = FLT_MAX;
        _nextAutosave = Game::ElapsedTime + Settings::AutosaveMinutes * 60;
    }

    void CheckForAutosave() {
        if (Game::ElapsedTime > _nextAutosave) {
            try {
                const auto& path = Game::Mission ? Game::Mission->Path : Game::Level.Path;
                wstring backupPath = path.wstring() + L".sav";
                SPDLOG_INFO(L"Autosaving backup to {}", backupPath);

                if (Game::Mission) {
                    Game::Mission->SaveCopy(backupPath);
                }
                else {
                    SaveLevelToPath(Game::Level, backupPath, true);
                }

                ResetAutosaveTimer();
            }
            catch (const std::exception& e) {
                SPDLOG_WARN(e.what());
            }
        }
    }

    namespace Commands {
        Command ConvertToD2{ .Action = Editor::ConvertToD2, .CanExecute = CanConvertToD2, .Name = "Convert to D2" };
        Command ConvertToVertigo{ .Action = Editor::ConvertToVertigo, .CanExecute = CanConvertToVertigo, .Name = "Convert to Vertigo" };

        Command NewLevel{
            .Action = [] { Events::ShowDialog(DialogType::NewLevel); },
            .Name = "New Level..."
        };

        Command Open{
            .Action = [] {
                if (!CanCloseCurrentFile()) return;

                static const COMDLG_FILTERSPEC filter[] = {
                    { L"Descent Levels", L"*.hog;*.rl2;*.rdl" },
                    { L"Missions", L"*.hog" },
                    { L"Levels", L"*.rl2;*.rdl" },
                    { L"All Files", L"*.*" }
                };

                if (auto file = OpenFileDialog(filter, L"Open Mission"))
                    LoadFile(*file);
            },
            .Name = "Open..."
        };

        Command Save{ .Action = OnSave, .CanExecute = Resources::HasGameData, .Name = "Save" };
        Command SaveAs{ .Action = OnSaveAs, .CanExecute = Resources::HasGameData, .Name = "Save As..." };
    }
}