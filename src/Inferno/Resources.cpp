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

namespace Inferno::Resources {
    SoundFile SoundsD1, SoundsD2;

    namespace {
        List<string> RobotNames;
        List<string> PowerupNames;
        HogFile Hog;
        Palette LevelPalette;
        PigFile Pig;
        Dictionary<TexID, PigBitmap> CustomTextures;
        List<PigBitmap> Textures;
        List<string> StringTable; // Text for the UI

        std::mutex PigMutex;

        constexpr EffectClip DefaultEffectClip{};
        constexpr VClip DefaultVClip{};
        const RobotInfo DefaultRobotInfo{};
        const DoorClip DefaultDoorClip{};
        const Model DefaultModel{};
        const LevelTexture DefaultTexture{};

        struct ModelEntry { string Name; Outrage::Model Model; };
        List<ModelEntry> OutrageModels;
    }

    void LoadRobotNames(filesystem::path path) {
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

    void LoadPowerupNames(filesystem::path path) {
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

    void Init() {
        // Load some default resources.
        LoadPowerupNames("powerups.txt");
        LoadRobotNames("robots.txt");
    }

    const DoorClip& GetDoorClip(DClipID id) {
        if (!Seq::inRange(GameData.DoorClips, (int)id)) return DefaultDoorClip;
        return GameData.DoorClips[(int)id];
    }

    DClipID GetDoorClipID(LevelTexID id) {
        for (int i = 0; i < GameData.DoorClips.size(); i++) {
            if (GameData.DoorClips[i].Frames[0] == id)
                return DClipID(i);
        }

        return DClipID::None;
    }

    const EffectClip& GetEffectClip(EClipID id) {
        if (!Seq::inRange(GameData.Effects, (int)id)) return DefaultEffectClip;
        return GameData.Effects[(int)id];
    }

    const EffectClip& GetEffectClip(LevelTexID id) {
        return GetEffectClip(LookupTexID(id));
    }

    const EffectClip& GetEffectClip(TexID id) {
        for (auto& clip : GameData.Effects) {
            if (clip.VClip.Frames[0] == id)
                return clip;
        }

        return DefaultEffectClip;
    }

    EClipID GetEffectClipID(TexID tid) {
        for (int i = 0; i < GameData.Effects.size(); i++) {
            if (GameData.Effects[i].VClip.Frames[0] == tid)
                return EClipID(i);
        }

        return EClipID::None;
    }

    EClipID GetEffectClipID(LevelTexID id) {
        auto tid = LookupTexID(id);
        if (tid == TexID::None) return EClipID::None;
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
        if (GameData.VClips.size() <= (int)id) return DefaultVClip;
        return GameData.VClips[(int)id];
    }

    const Inferno::Model& GetModel(ModelID id) {
        if ((int)id >= GameData.Models.size()) return DefaultModel;
        return GameData.Models[(int)id];
    }

    const RobotInfo& GetRobotInfo(uint id) {
        if (id >= GameData.Robots.size()) return DefaultRobotInfo;
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

    PigEntry DefaultPigEntry = { .Name = "Unknown", .Width = 64, .Height = 64 };

    const PigEntry& GetTextureInfo(TexID id) {
        if (id <= TexID::Invalid || (int)id >= Pig.Entries.size()) return DefaultPigEntry;
        return Pig.Get(id);
    }

    const PigEntry& GetTextureInfo(LevelTexID id) {
        return Pig.Get(LookupTexID(id));
    }

    int GetSoundCount() { return (int)GameData.Sounds.size(); }

    int GetSoundIndex(SoundID id) {
        return GameData.Sounds[(int)id];
    }

    SoundResource GetSoundResource(SoundID id) {
        if (Game::Level.IsDescent1())
            return { .D1 = GameData.Sounds[(int)id] };
        else
            return { .D2 = GameData.Sounds[(int)id] };
    }

    TexID LookupModelTexID(const Model& m, int16 i) {
        if (i >= m.TextureCount || m.FirstTexture + i >= GameData.ObjectBitmapPointers.size()) return TexID::None;
        auto ptr = GameData.ObjectBitmapPointers[m.FirstTexture + i];
        return GameData.ObjectBitmaps[ptr];
    }

    Weapon DefaultWeapon{};

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
            auto& bmp = ReadBitmap(entry.ID);
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

    void LoadDescent2Resources(Level& level) {
        std::scoped_lock lock(PigMutex);
        SPDLOG_INFO("Loading Descent 2 level: '{}'\r\n Version: {} Segments: {} Vertices: {}", level.Name, level.Version, level.Segments.size(), level.Vertices.size());
        StreamReader reader(FileSystem::FindFile(L"descent2.ham"));
        auto ham = ReadHam(reader);
        auto hog = HogFile::Read(FileSystem::FindFile(L"descent2.hog"));
        auto pigName = ReplaceExtension(level.Palette, ".pig");
        auto pig = ReadPigFile(FileSystem::FindFile(pigName));

        auto paletteData = hog.ReadEntry(level.Palette);
        auto palette = ReadPalette(paletteData);
        auto textures = ReadAllBitmaps(pig, palette);

        if (level.IsVertigo()) {
            auto d2xhog = HogFile::Read(FileSystem::FindFile(L"d2x.hog"));
            auto data = d2xhog.ReadEntry("d2x.ham");
            StreamReader d2xreader(data);
            AppendVHam(d2xreader, ham);
        }

        auto pog = ReplaceExtension(level.FileName, ".pog");
        if (Game::Mission && Game::Mission->Exists(pog)) {
            SPDLOG_INFO("POG data found");
            auto data = Game::Mission->ReadEntry(pog);
            CustomTextures = ReadPoggies(pig.Entries, data, palette);
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

        if (Game::Mission && Game::Mission->Exists(hxm)) {
            SPDLOG_INFO("Loading HXM data...");
            auto hxmData = Game::Mission->ReadEntry(hxm);
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

    const string UnknownString = "???";

    const string_view GetString(GameString i) {
        if (!Seq::inRange(StringTable, (int)i)) return UnknownString;
        return StringTable[(int)i];
    }

    void LoadExtendedWeaponInfo() {
        if (GameData.Weapons.size() < 30) return; // No D1 data
        auto GetWeapon = [](WeaponID id) -> Weapon& { return GameData.Weapons[(int)id]; };
        auto& fusion = GetWeapon(WeaponID::Fusion);
        fusion.Extended.Chargable = true;
        fusion.Extended.ScorchTexture = "scorchC";
        fusion.Extended.ScorchRadius = 3.25f;
        fusion.Extended.Glow = Color{ 0.20f, 0.0f, 0.45f };
        fusion.EnergyUsage = 2.0f; // 2.0 matches original behavior
        fusion.Extended.ModelPath = "FusionBlobNewJ.OOF";
        fusion.Extended.RotationalVelocity = Vector3{ 0, 0, DirectX::XM_PIDIV2 };
        //fusion.Flags |= WeaponFlag::FixedRotationalVelocity
        fusion.ModelSizeRatio = 2.5f;
        fusion.Extended.Size = 2.1f;
        // fusion.HitEffect = "FusionHit1"

        constexpr float LASER_MASS = 0.05f;
        constexpr float LASER_SCALE = 0.7f;

        GetWeapon(WeaponID::Laser1).Extended.Glow = Color(0.85f, 0.0f, 0.0f) * 0.55;
        GetWeapon(WeaponID::Laser2).Extended.Glow = Color(0.7f, 0.25f, 0.25f) * 0.35;
        GetWeapon(WeaponID::Laser3).Extended.Glow = Color(0.55f, 0.55f, 0.75f) * 0.35;
        GetWeapon(WeaponID::Laser4).Extended.Glow = Color(0.1f, 0.7f, 0.1f) * 0.35;

        GetWeapon(WeaponID::Laser1).Extended.ModelPath = "RedLaser.OOF";
        GetWeapon(WeaponID::Laser2).Extended.ModelPath = "bluelaser.OOF";
        GetWeapon(WeaponID::Laser3).Extended.ModelPath = "PurpleLaser.OOF";
        GetWeapon(WeaponID::Laser4).Extended.ModelPath = "GreenLaser.OOF";

        GetWeapon(WeaponID::Laser1).Extended.ModelScale =
            GetWeapon(WeaponID::Laser2).Extended.ModelScale =
            GetWeapon(WeaponID::Laser3).Extended.ModelScale =
            GetWeapon(WeaponID::Laser4).Extended.ModelScale = LASER_SCALE;

        GetWeapon(WeaponID::Laser1).Mass =
            GetWeapon(WeaponID::Laser2).Mass =
            GetWeapon(WeaponID::Laser3).Mass =
            GetWeapon(WeaponID::Laser4).Mass = LASER_MASS;

        GetWeapon(WeaponID::Spreadfire).Extended.Glow = Color{ 0.4f, 0.4f, 0.6f };
        GetWeapon(WeaponID::Spreadfire).Extended.Behavior = "spreadfire";

        GetWeapon(WeaponID::Plasma).Extended.Glow = Color{ 0.4f, 0.5f, 0.4f };

        auto& vulcan = GetWeapon(WeaponID::Vulcan);
        vulcan.Extended.Behavior = "vulcan";
        vulcan.Extended.ScorchTexture = "BulletHole01";
        vulcan.AmmoUsage = 13;

        GetWeapon(WeaponID::ProxMine).Extended.InheritParentVelocity = true;

        GetWeapon(WeaponID::Plasma).Extended.ScorchTexture = "scorchB";
        GetWeapon(WeaponID::Concussion).Extended.ScorchTexture = "scorchC";

        GetWeapon(WeaponID::Flare).Extended.Sticky = true;
        GetWeapon(WeaponID::Flare).FireDelay = 0.5f;
        GetWeapon(WeaponID::Flare).Lifetime = 30.0f;
        GetWeapon(WeaponID::Flare).Extended.ModelPath = "FlareYellowBright.OOF";
        GetWeapon(WeaponID::Flare).Extended.ModelPath = "Buddybot.oof";
        GetWeapon(WeaponID::Flare).Speed.fill(0);
        GetWeapon(WeaponID::Flare).Extended.Glow = Color(0.05f, 0.05f, 0.05f);

        auto& mega = GetWeapon(WeaponID::Mega).Extended;
        mega.ScorchTexture = "scorchC";
        mega.ScorchRadius = 8.0f;

        if (GameData.Weapons.size() < 35) return;
        // D2 WEAPONS BELOW!

        GetWeapon(WeaponID::Laser5).Extended.Glow = Color(0.7f, 0.4f, 0.1f) * 0.35;
        GetWeapon(WeaponID::Laser6).Extended.Glow = Color(0.65f, 0.65f, 0.65f) * 0.35;
        GetWeapon(WeaponID::Laser5).Extended.ModelPath = "YellowLaser.OOF";
        GetWeapon(WeaponID::Laser6).Extended.ModelPath = "WhiteLaser.OOF";
        GetWeapon(WeaponID::Laser5).Extended.ModelScale = LASER_SCALE;
        GetWeapon(WeaponID::Laser6).Extended.ModelScale = LASER_SCALE;
        GetWeapon(WeaponID::Laser5).Mass = LASER_MASS;
        GetWeapon(WeaponID::Laser6).Mass = LASER_MASS;

        GetWeapon(WeaponID::Phoenix).Extended.Glow = Color{ 0.7f, 0.3f, 0.1f };
        GetWeapon(WeaponID::Phoenix).Extended.Bounces = 2;
        GetWeapon(WeaponID::Phoenix).Extended.ScorchTexture = "scorchB";
        GetWeapon(WeaponID::Phoenix).Bounce = 0; // Don't use the old bounce flag

        GetWeapon(WeaponID::Helix).Extended.Glow = Color{ 0.4f, 0.5f, 0.4f };
        GetWeapon(WeaponID::Helix).Extended.Behavior = "helix";

        GetWeapon(WeaponID::Omega).Extended.Behavior = "omega";
        GetWeapon(WeaponID::SmartMine).Extended.InheritParentVelocity = true;

        auto& gauss = GetWeapon(WeaponID::Gauss);
        gauss.AmmoUsage = 26;
        gauss.Model = ModelID::None;
        gauss.RenderType = WeaponRenderType::None;
        gauss.Extended.ScorchTexture = "BulletHole02";
        gauss.Extended.ScorchRadius = 1.75f;

        auto& shaker = GetWeapon(WeaponID::Shaker).Extended;
        shaker.ScorchTexture = "scorchC";
        shaker.ScorchRadius = 8.0f;

        GameData.Robots[37].Mass = 2; // IT droid
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

        auto dtx = ReplaceExtension(level.FileName, ".dtx");
        if (Game::Mission && Game::Mission->Exists(dtx)) {
            SPDLOG_INFO("DTX data found");
            auto data = Game::Mission->ReadEntry(dtx);
            CustomTextures = ReadDTX(pig.Entries, data, palette);
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
        LevelPalette = {};
        Pig = {};
        Hog = {};
        GameData = {};
        CustomTextures.clear();
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
                    obj.Render.Model.ID = ModelID::Mine;
                    break;

                case ObjectType::Player:
                    obj.Render.Model.ID = level.IsDescent1() ? ModelID::D1Player : ModelID::D2Player;
                    break;

                case ObjectType::Coop:
                    obj.Render.Model.ID = level.IsDescent1() ? ModelID::D1Coop : ModelID::D2Player;
                    break;

            }
        }
    }

    void LoadLevel(Level& level) {
        try {
            ResetResources();

            if (level.IsDescent2()) {
                LoadDescent2Resources(level);
            }
            else if (level.IsDescent1()) {
                LoadDescent1Resources(level);
            }
            else {
                throw Exception("Unsupported level version");
            }

            LoadStringTable();
            UpdateAverageTextureColor();

            FixObjectModelIds(level);
            LoadExtendedWeaponInfo();
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
        }
    }

    const PigBitmap& ReadBitmap(TexID id) {
        //std::scoped_lock lock(PigMutex);
        if (Textures.empty()) {
            static const PigBitmap empty(64, 64, "default");
            return empty;
        }

        if (CustomTextures.contains(id)) return CustomTextures[id];
        if (!Seq::inRange(Textures, (int)id)) id = (TexID)0;
        return Textures[(int)id];
    }

    int GetTextureCount() { return (int)Textures.size(); }

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
        auto data = ReadFile(name);
        auto level = Level::Deserialize(data);
        level.FileName = name;
        return level;
    }

    bool FoundDescent1() { return FileSystem::TryFindFile("descent.hog").has_value(); }
    bool FoundDescent2() { return FileSystem::TryFindFile("descent2.hog").has_value(); }
    bool FoundDescent3() { return FileSystem::TryFindFile("d3.hog").has_value(); }
    bool FoundVertigo() { return FileSystem::TryFindFile("d2x.hog").has_value(); }
    bool FoundMercenary() { return FileSystem::TryFindFile("merc.hog").has_value(); }

    bool HasCustomTextures() {
        return !CustomTextures.empty();
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

    Option<Outrage::Bitmap> ReadOutrageBitmap(const string& name) {
        if (auto r = OpenFile(name))
            return Outrage::Bitmap::Read(*r);

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
