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

namespace Inferno::Editor {
    constexpr auto METADATA_EXTENSION = "ied"; // inferno engine data

    void FixObjects(Level& level) {
        bool hasPlayerStart = GetObjectCount(level, ObjectType::Player) > 0;

        if (hasPlayerStart) {
            if (level.Objects[0].Type != ObjectType::Player) {
                SPDLOG_WARN("Level contains a player start but it was not the first object. Swapping objects.");
                auto index = Seq::findIndex(level.Objects, [](Object& obj) { return obj.Type == ObjectType::Player; });
                std::swap(level.Objects[0], level.Objects[*index]);
                Events::SelectObject();
            }
        }

        for (int id = 0; id < level.Objects.size(); id++) {
            auto& obj = level.GetObject((ObjID)id);
            if (obj.Type == ObjectType::Weapon) {
                obj.Control.Weapon.Parent = (ObjID)id;
                obj.Control.Weapon.ParentSig = (ObjSig)id;
                obj.Control.Weapon.ParentType = obj.Type;
            }

            NormalizeObjectVectors(obj);
        }
    }

    void FixWalls(Level& level) {
        for (int id = 0; id < level.Walls.size(); id++) {
            auto& wall = level.GetWall((WallID)id);
            wall.LinkedWall = WallID::None; // Wall links are only valid during runtime
            FixWallClip(level, (WallID)id);
        }
    }

    void FixTriggers(Level& level) {
        for (int id = 0; id < level.Triggers.size(); id++) {
            auto& trigger = level.GetTrigger((TriggerID)id);

            for (int t = (int)trigger.Targets.Count() - 1; t > 0; t--) {
                auto& tag = trigger.Targets[t];
                if (!level.SegmentExists(tag)) {
                    SPDLOG_WARN("Removing invalid trigger target. TID: {} - {}:{}", id, tag.Segment, tag.Side);
                    tag = {};
                    trigger.Targets.Remove(t);
                }
            }
        }

        for (int t = (int)level.ReactorTriggers.Count() - 1; t > 0; t--) {
            auto& tag = level.ReactorTriggers[t];
            if (!level.SegmentExists(tag)) {
                SPDLOG_WARN("Removing invalid reactor trigger target. {}:{}", tag.Segment, tag.Side);
                tag = {};
                level.ReactorTriggers.Remove(t);
            }
        }
    }

    void SetPlayerStartIDs(Level& level) {
        int8 id = 0;
        for (auto& i : level.Objects) {
            if (i.Type == ObjectType::Player)
                i.ID = id++;
        }

        id = 8; // it's unclear if setting co-op IDs is necessary, but do it anyway.
        for (auto& i : level.Objects) {
            if (i.Type == ObjectType::Coop)
                i.ID = id++;
        }
    }

    size_t SaveLevel(Level& level, StreamWriter& writer) {
        if (level.Walls.size() >= (int)WallID::Max)
            throw Exception("Cannot save a level with more than 254 walls");

        DisableFlickeringLights(level);
        ResetFlickeringLightTimers(level);
        FixObjects(level);
        FixWalls(level);
        FixTriggers(level);
        SetPlayerStartIDs(level);
        //WeldVertices(level);

        if (!level.SegmentExists(level.SecretExitReturn))
            level.SecretExitReturn = SegID(0);

        return level.Serialize(writer);
    }

    // Saves a level to the file system
    void SaveLevelToPath(std::filesystem::path path, bool autosave = false) {
        CleanLevel(Game::Level);

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
        SaveLevelMetadata(Game::Level, metadata);
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

    void OnSaveAs() {
        if (!Resources::HasGameData()) return;

        List<COMDLG_FILTERSPEC> filter = { { L"Mission", L"*.hog" } };

        if (Game::Level.IsDescent1())
            filter.push_back({ L"Descent 1 Level", L"*.rdl" });
        else
            filter.push_back({ L"Descent 2 Level", L"*.rl2" });

        auto name = Game::Level.FileName == "" ? "level" : Game::Level.FileName;

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

        auto ext = Game::Level.IsDescent1() ? "rdl" : "rl2";

        if (auto path = SaveFileDialog(filter, filterIndex, defaultName)) {
            if (ExtensionEquals(*path, L"hog")) {
                auto levelData = WriteLevelToMemory(Game::Level);
                auto levelMetadata = WriteLevelMetadataToMemory(Game::Level);

                if (Game::Mission) {
                    // Save to new location, then update the current level in it
                    Game::Mission = Game::Mission->SaveCopy(*path);
                    Game::Mission->AddOrUpdateEntry(Game::Level.FileName, levelData);
                    auto metadataName = String::NameWithoutExtension(Game::Level.FileName) + "." + METADATA_EXTENSION;
                    Game::Mission->AddOrUpdateEntry(metadataName, levelMetadata);
                }
                else {
                    // Create a new hog
                    filesystem::path fileName = name; // set proper extension
                    fileName.replace_extension(ext);
                    Game::Level.FileName = fileName.string();
                    HogFile::CreateFromEntry(*path, Game::Level.FileName, levelData);
                    Game::LoadMission(*path);
                    Events::ShowDialog(DialogType::HogEditor);
                }

                SetStatusMessage("Mission saved to {}", path->string());
            }
            else {
                path->replace_extension(ext);
                SaveLevelToPath(*path);
                Game::UnloadMission();
            }

            Settings::AddRecentFile(*path);

        }

        History.UpdateCleanSnapshot();
    }

    void OnSave() {
        if (!Resources::HasGameData()) return;

        if (Game::Mission) {
            assert(Game::Level.FileName != "");
            auto data = WriteLevelToMemory(Game::Level);
            Game::Mission->AddOrUpdateEntry(Game::Level.FileName, data);
            auto levelMetadata = WriteLevelMetadataToMemory(Game::Level);
            auto metadataName = String::NameWithoutExtension(Game::Level.FileName) + "." + METADATA_EXTENSION;
            Game::Mission->AddOrUpdateEntry(metadataName, levelMetadata);

            Game::LoadMission(Game::Mission->Path);
            SetStatusMessage(L"Mission saved to {}", Game::Mission->Path.filename().wstring());
            Settings::AddRecentFile(Game::Mission->Path);
        }
        else {
            // standalone level
            if (Game::Level.Path.empty()) {
                OnSaveAs();
            }
            else {
                SaveLevelToPath(Game::Level.Path);
                Settings::AddRecentFile(Game::Level.Path);
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
                    SaveLevelToPath(backupPath, true);
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