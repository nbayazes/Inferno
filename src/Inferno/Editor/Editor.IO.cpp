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

    void AppendVertigoData(HogWriter& writer, string hamName) {
        try {
            //if (mission.ContainsFileType(".ham")) return; // Already has ham data
            if (!Resources::FoundVertigo()) {
                SPDLOG_WARN("Level is marked as Vertigo but has no .ham and d2x.hog was not found");
                return;
            }

            // Insert vertigo data
            auto d2xhog = HogFile::Read(FileSystem::FindFile(L"d2x.hog"));
            auto vertigoData = d2xhog.ReadEntry("d2x.ham");
            writer.WriteEntry(hamName, vertigoData);
            SPDLOG_INFO("Copied Vertigo d2x.ham into HOG");
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Unable to add vertigo data: {}", e.what());
        }
    }

    // Serializes a level to bytes
    std::vector<ubyte> SerializeLevel(Level& level) {
        std::stringstream stream;
        stream.unsetf(std::ios::skipws);
        StreamWriter writer(stream);
        auto len = SaveLevel(level, writer);
        std::vector<ubyte> data(len);
        stream.read((char*)data.data(), data.size());
        return data;
    }

    // Serializes level settings to bytes
    std::vector<ubyte> SerializeLevelMetadata(const Level& level) {
        std::stringstream stream;
        stream.unsetf(std::ios::skipws);
        SaveLevelMetadata(level, stream);
        std::vector<ubyte> data(stream.tellp());
        stream.read((char*)data.data(), data.size());
        return data;
    }

    // Writes a HOG file and updates the level
    void WriteHog(Level& level, HogFile& mission, filesystem::path path) {
        filesystem::path tempPath = path;
        tempPath.replace_extension(".tmp");

        try {
            auto metadataName = String::NameWithoutExtension(level.FileName) + "." + METADATA_EXTENSION;
            HogWriter writer(tempPath); // write to temp
            fmt::print("Writing HOG files: ");

            for (auto& entry : mission.Entries) {
                auto data = mission.ReadEntry(entry);
                if (entry.Name == level.FileName || entry.Name == metadataName) {
                    continue; // skip level and metadata
                }
                else {
                    writer.WriteEntry(entry.Name, data);
                    fmt::print("{}:{} ", entry.Name, data.size());
                }
            }

            if (level.FileName.empty())
                throw Exception("Level filename is empty!");

            // Write level and metadata
            auto levelData = SerializeLevel(level);
            writer.WriteEntry(level.FileName, levelData);
            fmt::print("{}:{} ", level.FileName, levelData.size());

            auto levelMetadata = SerializeLevelMetadata(level);
            writer.WriteEntry(metadataName, levelMetadata); // IED file
            fmt::print("{}:{}\n", metadataName, levelMetadata.size());

            if (level.IsVertigo() && !mission.ContainsFileType(".ham"))
                AppendVertigoData(writer, path.stem().string() + ".ham");
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            SPDLOG_ERROR(e.what());
            return;
        }

        BackupFile(path);
        filesystem::remove(path); // Remove existing
        filesystem::rename(tempPath, path); // Rename temp to destination
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

            Settings::Editor.AddRecentFile(path);
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
        level.Path = level.FileName;

        if (Game::Mission) {
            // Find a unique file name in the hog
            int i = 1;
            while (Game::Mission->Exists(level.FileName))
                level.FileName = fmt::format("{}{}{}", fileName.substr(0, 7), i++, ext);
        }

        auto tag = AddDefaultSegment(level);
        AddObject(level, { tag, SideID::Bottom }, ObjectType::Player);

        if (Game::Mission) {
            WriteHog(level, *Game::Mission, Game::Mission->Path);
            Game::LoadMission(Game::Mission->Path); // reload
        }

        Game::LoadLevel(std::move(level));
    }

    void BackupFile(const filesystem::path& path, string_view ext) {
        if (!filesystem::exists(path)) return;
        filesystem::path backupPath = path;
        backupPath.replace_extension(ext);
        filesystem::copy(path, backupPath, filesystem::copy_options::overwrite_existing);
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
                if (Game::Mission) {
                    // Update level in existing hog
                    WriteHog(Game::Level, *Game::Mission, *path);
                    Game::LoadMission(*path);
                    auto msnPath = Game::Mission->GetMissionPath(); // get the msn path before reloading

                    if (filesystem::exists(msnPath)) {
                        filesystem::path destMsnPath = Game::Mission->GetMissionPath();
                        filesystem::copy(msnPath, destMsnPath);
                    }
                }
                else {
                    // Create a new hog
                    HogFile hog{}; // empty
                    WriteHog(Game::Level, hog, *path);
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

            Settings::Editor.AddRecentFile(*path);
        }

        History.UpdateCleanSnapshot();
    }

    void OnSave() {
        if (!Resources::HasGameData()) return;
        auto& level = Game::Level;

        if (Game::Mission) {
            assert(level.FileName != "");
            WriteHog(level, *Game::Mission, Game::Mission->Path);
            Game::LoadMission(Game::Mission->Path);
            SetStatusMessage(L"Mission saved to {}", Game::Mission->Path.filename().wstring());
            Settings::Editor.AddRecentFile(Game::Mission->Path);
        }
        else {
            // standalone level
            if (level.Path.empty()) {
                OnSaveAs();
            }
            else {
                SaveLevelToPath(level, level.Path);
                Settings::Editor.AddRecentFile(level.Path);
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
        if (Settings::Editor.AutosaveMinutes == 0) _nextAutosave = FLT_MAX;
        _nextAutosave = Game::ElapsedTime + Settings::Editor.AutosaveMinutes * 60;
    }

    void WritePlaytestLevel(const filesystem::path& missionFolder, Level& level, HogFile* mission = nullptr) {
        auto hogPath = missionFolder / "_test.hog";

        HogWriter writer(hogPath);
        bool wroteHam = false;

        if (mission) {
            auto missionFileName = mission->GetMissionPath().stem().string();

            // Copy aux entries for level if any exist such as pogs / hxms
            for (auto& entry : mission->Entries) {
                if (String::InvariantEquals(entry.NameWithoutExtension(), String::NameWithoutExtension(level.FileName)) &&
                    !entry.IsLevel()) { // level is written after using latest data
                    auto data = mission->ReadEntry(entry);
                    writer.WriteEntry("_test" + entry.Extension(), data);
                }

                // Copy HAM if present
                if (entry.IsHam() && String::InvariantEquals(entry.NameWithoutExtension(), missionFileName)) {
                    auto data = mission->ReadEntry(entry);
                    writer.WriteEntry("_test.ham", data);
                    wroteHam = true;
                }
            }
        }

        auto levelFileName = level.IsDescent1() ? "_test.rdl" : "_test.rl2";
        auto levelData = SerializeLevel(level);
        writer.WriteEntry(levelFileName, levelData);

        if (level.IsVertigo() && !wroteHam)
            AppendVertigoData(writer, "_test.ham");

        // Write the mission info file
        auto infoFile = level.IsDescent1() ? "_test.msn" : "_test.mn2";
        MissionInfo info;
        info.Name = "_test";
        info.Levels.push_back(levelFileName);
        info.Enhancement = level.IsVertigo() ? MissionEnhancement::VertigoHam : MissionEnhancement::Standard;
        info.Write(missionFolder / infoFile);

        SetStatusMessage("Test mission saved to {}", hogPath.string());
    }

    void CheckForAutosave() {
        if (Game::ElapsedTime > _nextAutosave) {
            try {
                auto& path = Game::Mission ? Game::Mission->Path : Game::Level.Path;
                wstring backupPath = path.wstring() + L".sav";
                SPDLOG_INFO(L"Autosaving backup to {}", backupPath);

                if (Game::Mission) {
                    WriteHog(Game::Level, *Game::Mission, backupPath);
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