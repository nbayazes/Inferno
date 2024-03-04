#include "pch.h"
#include "Editor.IO.h"
#include "Editor.Diagnostics.h"
#include "Editor.h"
#include "Editor.Object.h"
#include "Editor.Segment.h"
#include "Events.h"
#include "FileSystem.h"
#include "Game.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"
#include "LevelMetadata.h"
#include "logging.h"
#include "Resources.h"
#include "Settings.h"

namespace Inferno::Editor {
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
        if (Resources::CustomTextures.Any()) {
            auto ext = level.IsDescent1() ? ".dtx" : ".pog";
            filesystem::path texPath = path;
            texPath.replace_extension(ext);
            std::ofstream file(texPath, std::ios::binary);
            StreamWriter writer(file, false);

            if (level.IsDescent1()) {
                Resources::CustomTextures.WriteDtx(writer, Resources::GetPalette());
            }
            else {
                Resources::CustomTextures.WritePog(writer, Resources::GetPalette());
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

    void AppendVertigoData(HogWriter& writer, string_view hamName) {
        try {
            //if (mission.ContainsFileType(".ham")) return; // Already has ham data
            if (!Resources::FoundVertigo()) {
                SPDLOG_WARN("Level is marked as Vertigo but has no .ham and d2x.hog was not found");
                return;
            }

            // Insert vertigo data
            auto xhog = HogFile::Read(FileSystem::FindFile(L"d2x.hog"));
            auto vertigoData = xhog.ReadEntry("d2x.ham");
            writer.WriteEntry(hamName, vertigoData);
            SPDLOG_INFO("Copied Vertigo d2x.ham into HOG");
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Unable to add vertigo data: {}", e.what());
        }
    }

    // Serializes data to a vector using the provided function
    std::vector<ubyte> SerializeToMemory(const std::function<size_t(StreamWriter&)>& fn) {
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
            auto metadataName = baseName + METADATA_EXTENSION;
            HogWriter writer(tempPath); // write to temp
            fmt::print("Copying existing HOG files:\n");

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

            if (Resources::CustomTextures.Any()) {
                if (mission.IsDescent1()) {
                    auto dtx = SerializeToMemory([](StreamWriter& w) {
                        return Resources::CustomTextures.WriteDtx(w, Resources::GetPalette());
                    });
                    writer.WriteEntry(baseName + ".dtx", dtx);
                    fmt::print("{}:{} ", baseName + ".dtx", dtx.size());
                }
                else {
                    auto pog = SerializeToMemory([](StreamWriter& w) {
                        return Resources::CustomTextures.WritePog(w, Resources::GetPalette());
                    });
                    writer.WriteEntry(baseName + ".pog", pog);
                    fmt::print("{}:{} ", baseName + ".pog", pog.size());
                }
            }
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            SPDLOG_ERROR(path.string() + ": " + e.what());
            return;
        }

        BackupFile(path);
        filesystem::remove(path); // Remove existing
        filesystem::rename(tempPath, path); // Rename temp to destination
        fmt::print("\n");
    }

    Level NewLevel(const NewLevelInfo& info) {
        if (!info.AddToHog)
            Game::UnloadMission();

        Level level;
        level.Name = info.Title;
        level.Version = info.Version;
        level.GameVersion = info.Version == 1 ? 25 : 32;
        auto ext = level.IsDescent1() ? ".rdl" : ".rl2";
        level.FileName = info.FileName.substr(0, 8) + ext;

        if (Game::Mission) {
            // Find a unique file name in the hog
            int i = 1;
            while (Game::Mission->Exists(level.FileName))
                level.FileName = fmt::format("{}{}{}", info.FileName.substr(0, 7), i++, ext);
        }

        auto tag = Editor::AddDefaultSegment(level);
        Editor::AddObject(level, { tag, SideID::Bottom }, ObjectType::Player);

        // Add the new level to the mission and reload it
        if (Game::Mission) {
            WriteHog(level, *Game::Mission, Game::Mission->Path);
            Game::LoadMission(Game::Mission->Path); // reload
        }

        return level;
    }

    void BackupFile(const filesystem::path& path, string_view ext) {
        if (!filesystem::exists(path)) return;
        filesystem::path backupPath = path;
        backupPath.replace_extension(ext);
        filesystem::copy(path, backupPath, filesystem::copy_options::overwrite_existing);
    }

    void SaveUnpackagedLevel(Level& level, const filesystem::path& path) {
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

    constexpr auto SHAREWARE_SAVE_ERROR = L"Shareware levels cannot be saved.";

    void OnSaveAs() {
        if (Game::Level.IsShareware) {
            ShowErrorMessage(SHAREWARE_SAVE_ERROR);
            return;
        }

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
        if (Game::Level.IsShareware) {
            ShowErrorMessage(SHAREWARE_SAVE_ERROR);
            return;
        }

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

    double _nextAutosave = DBL_MAX;

    void ResetAutosaveTimer() {
        if (Settings::Editor.AutosaveMinutes == 0) _nextAutosave = DBL_MAX;
        _nextAutosave = Clock.GetTotalTimeSeconds() + Settings::Editor.AutosaveMinutes * 60;
    }

    void WritePlaytestLevel(const filesystem::path& missionFolder, Level& level, HogFile* mission = nullptr) {
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

        if (Resources::CustomTextures.Any()) {
            if (level.IsDescent1()) {
                auto dtx = SerializeToMemory([](StreamWriter& w) {
                    return Resources::CustomTextures.WriteDtx(w, Resources::GetPalette());
                });
                writer.WriteEntry("_test.dtx", dtx);
            }
            else {
                auto pog = SerializeToMemory([](StreamWriter& w) {
                    return Resources::CustomTextures.WritePog(w, Resources::GetPalette());
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
        if (Game::Level.IsShareware) return; // Don't autosave shareware levels

        if (Clock.GetTotalTimeSeconds() > _nextAutosave && Game::GetState() == GameState::Editor) {
            try {
                auto& path = Game::Mission ? Game::Mission->Path : Game::Level.Path;
                if (path.empty()) path = Game::Level.FileName;
                wstring backupPath = path.wstring() + L".sav";
                SPDLOG_INFO(L"Autosaving backup to {}", backupPath);

                auto permissions = std::filesystem::status(backupPath).permissions();
                if ((permissions & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
                    SPDLOG_INFO("Temp file {} is read only", path.string());
                }
                else {
                    if (Game::Mission) {
                        WriteHog(Game::Level, *Game::Mission, backupPath);
                    }
                    else {
                        SaveLevelToPath(Game::Level, backupPath, true);
                    }
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
                    Game::LoadLevel(*file, "", true);
            },
            .Name = "Open..."
        };

        Command Save{ .Action = OnSave, .CanExecute = Resources::HasGameData, .Name = "Save" };
        Command SaveAs{ .Action = OnSaveAs, .CanExecute = Resources::HasGameData, .Name = "Save As..." };
    }
}
