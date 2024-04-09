#include "pch.h"
#include "logging.h"
#include "Editor.IO.h"
#include "Editor.Segment.h"
#include "LevelSettings.h"
#include "Events.h"
#include "Game.h"
#include "Settings.h"
#include "Editor.Object.h"
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
        level.CameraPosition = Render::Camera.Position;
        level.CameraTarget = Render::Camera.Target;
        level.CameraUp = Render::Camera.Up;
        SaveLevelMetadata(level, metadata, EditorLightSettings);
        SetStatusMessage(L"Saved level to {}", path.wstring());

        // Save custom textures
        if (Resources::CustomResources.Any()) {
            auto ext = level.IsDescent1() ? ".dtx" : ".pog";
            filesystem::path texPath = path;
            texPath.replace_extension(ext);
            std::ofstream file(texPath, std::ios::binary);
            StreamWriter writer(file, false);

            if (level.IsDescent1()) {
                Resources::CustomResources.WriteDtx(writer, Resources::GetPalette());
            }
            else {
                Resources::CustomResources.WritePog(writer, Resources::GetPalette());
            }
        }

        if (!autosave) {
            Editor::History.UpdateCleanSnapshot();
            Game::Level.Path = path;
            Game::Level.FileName = path.filename().string();
            UpdateWindowTitle();
            History.UpdateCleanSnapshot();
        }
    }

    // Loads a D1 to D2 Vertigo level (no XL)
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
            LoadLevelMetadata(level, metadata.str(), EditorLightSettings);
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

    // Serializes data to a vector using the provided function
    std::vector<ubyte> SerializeToMemory(std::function<size_t(StreamWriter&)> fn) {
        std::stringstream stream;
        stream.unsetf(std::ios::skipws);
        StreamWriter writer(stream);
        auto len = fn(writer);
        std::vector<ubyte> data(len);
        stream.read((char*)data.data(), data.size());
        return data;
    }

    // Serializes level settings to bytes
    std::vector<ubyte> SerializeLevelMetadata(const Level& level) {
        std::stringstream stream;
        stream.unsetf(std::ios::skipws);
        SaveLevelMetadata(level, stream, EditorLightSettings);
        std::vector<ubyte> data(stream.tellp());
        stream.read((char*)data.data(), data.size());
        return data;
    }

    // Writes a HOG file and updates the level
    void WriteHog(Level& level, HogFile& mission, filesystem::path path) {
        filesystem::path tempPath = path;
        tempPath.replace_extension(".tmp");

        try {
            if (level.FileName.empty())
                throw Exception("Level filename is empty!");

            auto baseName = String::NameWithoutExtension(level.FileName);
            auto metadataName = baseName + "." + METADATA_EXTENSION;
            HogWriter writer(tempPath); // write to temp
            fmt::println("Copying existing HOG files:");

            for (auto& entry : mission.Entries) {
                // Does the file match the level name?
                if (entry.NameWithoutExtension() == baseName) {
                    // Skip files serialized later
                    constexpr std::array skippedExtensions = { ".dtx", ".pog", ".rl2", ".rdl", ".ied" };
                    auto ext = String::ToLower(entry.Extension());
                    if (Seq::contains(skippedExtensions, ext))
                        continue;
                }

                auto data = mission.ReadEntry(entry);
                writer.WriteEntry(entry.Name, data);
                fmt::print("{}:{}\n", entry.Name, data.size());
            }


            fmt::print("\nWriting new files: ");

            // Write level and metadata
            auto levelData = SerializeToMemory([&level](StreamWriter& w) { return SaveLevel(level, w); });
            writer.WriteEntry(level.FileName, levelData);
            fmt::print("{}:{} ", level.FileName, levelData.size());

            auto levelMetadata = SerializeLevelMetadata(level);
            writer.WriteEntry(metadataName, levelMetadata); // IED file
            fmt::print("{}:{} ", metadataName, levelMetadata.size());

            if (level.IsVertigo() && !mission.ContainsFileType(".ham"))
                AppendVertigoData(writer, path.stem().string() + ".ham");

            if (Resources::CustomResources.Any()) {
                if (mission.IsDescent1()) {
                    auto dtx = SerializeToMemory([](StreamWriter& w) {
                        return Resources::CustomResources.WriteDtx(w, Resources::GetPalette());
                    });
                    writer.WriteEntry(baseName + ".dtx", dtx);
                    fmt::print("{}:{} ", baseName + ".dtx", dtx.size());
                }
                else {
                    auto pog = SerializeToMemory([](StreamWriter& w) {
                        return Resources::CustomResources.WritePog(w, Resources::GetPalette());
                    });
                    writer.WriteEntry(baseName + ".pog", pog);
                    fmt::print("{}:{} ", baseName + ".pog", pog.size());
                }
            }

            fmt::println("");
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            SPDLOG_ERROR(e.what());
            return;
        }

        BackupFile(path);
        filesystem::remove(path); // Remove existing
        filesystem::rename(tempPath, path); // Rename temp to destination
        fmt::print("\n");
    }

    void LoadFile(filesystem::path path) {
        try {
            auto version = FileVersionFromHeader(path);
            if (version > 0 && version <= 8) {
                LoadLevel(path);
            }
            else if (version == 0) {
                // Hog file
                Game::LoadMission(path);

                // Load first sorted level in the mission
                auto levelEntries = Game::Mission->GetLevels();
                Seq::sortBy(levelEntries, [](HogEntry& a, HogEntry& b) { return a.Name < b.Name; });
                if (!levelEntries.empty()) {
                    LoadLevelFromHOG(levelEntries[0].Name);

                    // Show hog editor if there's more than one level and game data is present.
                    // If there's no game data, the config dialog will conflict causing the UI to get stuck.
                    if (levelEntries.size() > 1 && Resources::HasGameData()) 
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
                LoadLevelMetadata(level, buffer, EditorLightSettings);
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

    void BackupFile(filesystem::path path, string_view ext) {
        if (!filesystem::exists(path)) return;
        filesystem::path backupPath = path;
        backupPath.replace_extension(ext);
        filesystem::copy(path, backupPath, filesystem::copy_options::overwrite_existing);
    }

    void SaveUnpackagedLevel(Level& level, filesystem::path path) {
        filesystem::path folder = path;
        folder.remove_filename();
        string newFileName = String::NameWithoutExtension(path.filename().string());
        string nfn = path.stem().replace_extension().string();
        auto originalName = String::NameWithoutExtension(level.FileName);

        if (Game::Mission) {
            for (auto& entry : Game::Mission->Entries) {
                // Copy any matching files from the HOG as loose files
                if (String::InvariantEquals(entry.NameWithoutExtension(), originalName)) {
                    auto data = Game::Mission->ReadEntry(entry);
                    auto fpath = folder / (newFileName + entry.Extension());
                    try {
                        File::WriteAllBytes(fpath, data);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_ERROR("Error saving file {}:\n{}", fpath.string(), e.what());
                    }
                }
            }
        }

        // Save level after copying files in case any have changed since the last save
        SaveLevelToPath(level, path);
    }

    void OnSaveAs() {
        if (!Resources::HasGameData()) return;

        auto& level = Game::Level;
        List<COMDLG_FILTERSPEC> filter = { { L"Mission", L"*.hog" } };

        if (level.IsDescent1())
            filter.push_back({ L"Descent 1 Level", L"*.rdl" });
        else
            filter.push_back({ L"Descent 2 Level", L"*.rl2" });

        wstring defaultName;
        uint filterIndex = 0;

        if (Game::Mission) {
            defaultName = Game::Mission->Path.filename();
            filterIndex = 1;
        }
        else {
            auto name = level.FileName == "" ? "level" : level.FileName;
            defaultName = Convert::ToWideString(name);
            filterIndex = 2;
        }

        auto ext = level.IsDescent1() ? "rdl" : "rl2";
        auto path = SaveFileDialog(filter, filterIndex, defaultName);
        if (!path) return;

        if (ExtensionEquals(*path, L"hog")) {
            if (Game::Mission) {
                // Update level in existing hog
                WriteHog(level, *Game::Mission, *path);
                auto srcMsn = Game::Mission->GetMissionPath(); // get the msn path before reloading
                Game::LoadMission(*path);

                // copy the MSN if it existed
                if (filesystem::exists(srcMsn)) {
                    auto destMsn = Game::Mission->GetMissionPath();
                    filesystem::copy(srcMsn, destMsn);
                    SPDLOG_INFO(L"Copied mission to {}", destMsn.wstring());
                }
            }
            else {
                // Create a new hog
                HogFile hog{}; // empty
                WriteHog(level, hog, *path);
                Game::LoadMission(*path);
                Events::ShowDialog(DialogType::HogEditor);
            }
            SetStatusMessage("Mission saved to {}", path->string());
        }
        else {
            path->replace_extension(ext);
            SaveUnpackagedLevel(level, *path);
            Game::UnloadMission();
        }

        Settings::Editor.AddRecentFile(*path);
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

    void WritePlaytestLevel(filesystem::path missionFolder, Level& level, HogFile* mission = nullptr) {
        auto hogPath = missionFolder / "_test.hog";

        HogWriter writer(hogPath);
        bool wroteHam = false;

        if (mission) {
            auto missionFileName = mission->GetMissionPath().stem().string();
            auto baseName = String::NameWithoutExtension(level.FileName);

            // Copy aux entries for level if any exist such as hxms
            for (auto& entry : mission->Entries) {
                constexpr std::array skippedExtensions = { ".dtx", ".pog", ".rl2", ".rdl", ".ied" };
                auto ext = String::ToLower(entry.Extension());
                if (Seq::contains(skippedExtensions, ext))
                    continue; // skip custom textures and the level as they are written after

                if (String::InvariantEquals(entry.NameWithoutExtension(), baseName)) {
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

        if (Resources::CustomResources.Any()) {
            if (level.IsDescent1()) {
                auto dtx = SerializeToMemory([](StreamWriter& w) {
                    return Resources::CustomResources.WriteDtx(w, Resources::GetPalette());
                });
                writer.WriteEntry("_test.dtx", dtx);
            }
            else {
                auto pog = SerializeToMemory([](StreamWriter& w) {
                    return Resources::CustomResources.WritePog(w, Resources::GetPalette());
                });
                writer.WriteEntry("_test.pog", pog);
            }
        }

        auto levelFileName = level.IsDescent1() ? "_test.rdl" : "_test.rl2";
        auto levelData = SerializeToMemory([&level](StreamWriter& w) { return SaveLevel(level, w); });
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
                if (path.empty()) path = Game::Level.FileName;
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

                static constexpr COMDLG_FILTERSPEC filter[] = {
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
