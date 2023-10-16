#include "pch.h"
#define NOMINMAX
#include <numeric>

#include "Game.h"
#include "FileSystem.h"
#include "Graphics/Render.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Input.h"
#include "Graphics/Render.Particles.h"
#include "Graphics/Render.Debug.h"
#include "imgui_local.h"
#include "Editor/Editor.h"
#include "Editor/UI/EditorUI.h"
#include "Editor/Editor.Object.h"
#include "Game.Input.h"
#include "DebugOverlay.h"
#include "Game.Object.h"
#include "HUD.h"
#include "Game.Wall.h"
#include "Game.AI.h"
#include "Game.Reactor.h"
#include "Game.Room.h"
#include "Game.Segment.h"
#include "LegitProfiler.h"

using namespace DirectX;

namespace Inferno::Game {
    namespace {
        GameState State = GameState::Editor;
        GameState RequestedState = GameState::Editor;
        Camera EditorCameraSnapshot;
    }

    void StartLevel();

    void ResetCountdown() {
        ControlCenterDestroyed = false;
        TotalCountdown = CountdownSeconds = -1;
        CountdownTimer = -1.0f;
        ScreenFlash = Color();
    }


    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo() {
        try {
            if (!Mission) return {};
            MissionInfo mission{};

            // Read mission from filesystem
            std::ifstream file(Mission->GetMissionPath());
            if (mission.Read(file))
                return mission;

            // Descent2 stores its mn2 in the hog file
            auto ext = Level.IsDescent1() ? "msn" : "mn2";

            if (auto entry = Mission->FindEntryOfType(ext)) {
                auto bytes = Mission->ReadEntry(*entry);
                string str((char*)bytes.data(), bytes.size());
                std::stringstream stream(str);
                mission.Read(stream);
                return mission;
            }

            return {};
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
            return {};
        }
    }

    int GetLevelNumber(Inferno::Level& level) {
        if (Game::Mission) {
            auto filename = Game::Mission->Path.filename().string();
            //auto levels = Game::Mission->GetLevels();
            if (auto info = Game::TryReadMissionInfo()) {
                if (auto index = Seq::indexOf(info->Levels, level.FileName))
                    return 1 + (int)index.value();

                if (auto index = Seq::indexOf(info->SecretLevels, level.FileName))
                    return -1 - (int)index.value(); // Secret levels have a negative index
            }
            else if (String::ToUpper(filename) == "DESCENT.HOG") {
                // Descent 1 doesn't have a msn file and relies on hard coded values
                if (level.FileName.starts_with("levelS")) {
                    auto index = level.FileName.substr(6, 1);
                    return -std::stoi(index);
                }
                else if (level.FileName.starts_with("level")) {
                    auto index = level.FileName.substr(5, 2);
                    return std::stoi(index);
                }
            }
        }

        return 1;
    }

    void LoadLevel(Inferno::Level&& level) {
        Inferno::Level backup = Level;

        try {
            assert(level.FileName != "");
            bool reload = level.FileName == Level.FileName;

            Editor::LoadTextureFilter(level);
            bool forceReload =
                level.IsDescent2() != Level.IsDescent2() ||
                NeedsResourceReload ||
                Resources::CustomTextures.Any() ||
                !String::InvariantEquals(level.Palette, Level.Palette);

            NeedsResourceReload = false;
            //Rooms.clear();
            IsLoading = true;

            Level = std::move(level); // Move to global so resource loading works properly
            FreeProceduralTextures();
            Resources::LoadLevel(Level);
            Level.Rooms = CreateRooms(Level);
            Navigation = NavigationNetwork(Level);
            LevelNumber = GetLevelNumber(Level);

            if (forceReload || Resources::CustomTextures.Any()) // Check for custom textures before or after load
                Render::Materials->Unload();

            Render::Materials->LoadLevelTextures(Level, forceReload);
            string extraTextures[] = { "noise" };
            Render::Materials->LoadTextures(extraTextures);

            Render::LoadLevel(Level);
            Render::ResetEffects();
            InitObjects(Game::Level);

            Editor::OnLevelLoad(reload);
            Render::Materials->Prune();
            Render::Adapter->PrintMemoryUsage();
            IsLoading = false;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
            Level = backup; // restore the old level if something went wrong
            throw;
        }
    }

    void LoadMission(const filesystem::path& file) {
        Mission = HogFile::Read(FileSystem::FindFile(file));
    }

    void UpdateAmbientSounds() {
        auto& player = Level.Objects[0];
        bool hasLava = bool(Level.GetSegment(player.Segment).AmbientSound & SoundFlag::AmbientLava);
        bool hasWater = bool(Level.GetSegment(player.Segment).AmbientSound & SoundFlag::AmbientWater);

        SoundID sound{};

        if (hasLava) {
            sound = SoundID::AmbientLava;
            if (hasWater && Random() > 0.5f) // if both water and lava pick one at random
                sound = SoundID::AmbientWater;
        }
        else if (hasWater) {
            sound = SoundID::AmbientWater;
        }
        else {
            return;
        }

        if (Random() < 0.003f) {
            // Playing the sound at player is what the original game does,
            // but it would be nicer to come from the environment instead...
            Sound3D s({ sound }, Player.Reference);
            s.Volume = Random() * 0.1f + 0.05f;
            s.AttachToSource = true;
            s.FromPlayer = true;
            Sound::Play(s);
        }
    }

    using Keys = Keyboard::Keys;

    void CheckGlobalHotkeys() {
        if (Input::IsKeyPressed(Keys::F1))
            Game::ShowDebugOverlay = !Game::ShowDebugOverlay;

        if (Input::IsKeyPressed(Keys::F2))
            SetState(State == GameState::Game ? GameState::Editor : GameState::Game);

        if (Input::IsKeyPressed(Keys::F3))
            Settings::Inferno.ScreenshotMode = !Settings::Inferno.ScreenshotMode;

        if (Input::IsKeyPressed(Keys::F5)) {
            Resources::LoadDataTables(Game::Level);
            Render::Adapter->ReloadResources();
            Editor::Events::LevelChanged();
        }

        if (Input::IsKeyPressed(Keys::F6))
            Render::ReloadTextures();

        if (Input::IsKeyPressed(Keys::F7)) {
            Settings::Graphics.HighRes = !Settings::Graphics.HighRes;
            Render::ReloadTextures();
        }

        if (Input::IsKeyPressed(Keys::F9)) {
            Settings::Graphics.NewLightMode = !Settings::Graphics.NewLightMode;
        }

        if (Input::IsKeyPressed(Keys::F10)) {
            Settings::Graphics.ToneMapper++;
            if (Settings::Graphics.ToneMapper > 2) Settings::Graphics.ToneMapper = 0;
        }
    }


    void AddPointsToScore(int points) {
        auto score = Player.Score;

        Player.Score += points;
        AddPointsToHUD(points);

        // This doesn't account for negative scoring (which never happens in D2)
        auto lives = Player.Score / EXTRA_LIFE_POINTS - score / EXTRA_LIFE_POINTS;
        if (lives > 0) {
            Sound::Play({ SoundID::ExtraLife });
            Player.GiveExtraLife((uint8)lives);
        }
    }

    void UpdateLiveObjectCount() {
        Stats::LiveObjects = 0;

        if (auto currentRoom = GetCurrentRoom()) {
            for (auto& roomId : currentRoom->VisibleRooms) {
                auto room = Level.GetRoom(roomId);
                if (!room) continue;

                for (auto& segId : room->Segments) {
                    auto& seg = Level.GetSegment(segId);
                    Stats::LiveObjects += (int)seg.Objects.size();
                }
            }
        }
    }

    // Updates on each game tick
    void FixedUpdate(float dt) {
        Player.Update(dt);

        UpdateAmbientSounds();
        UpdateMatcens(Game::Level, dt);

        Sound::UpdateSoundEmitters(dt);
        UpdateExplodingWalls(Game::Level, dt);
        if (ControlCenterDestroyed)
            UpdateReactorCountdown(dt);
        Render::FixedUpdateEffects(dt);

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            ClearFlag(obj.Flags, ObjectFlag::Updated);

            // Always update weapons in case they go out of sight
            if (obj.Type == ObjectType::Weapon || HasFlag(obj.Flags, ObjectFlag::AlwaysUpdate))
                FixedUpdateObject(dt, ObjID(i), obj);

            if (obj.Type == ObjectType::Reactor)
                UpdateReactor(obj);

            obj.Effects.Update(dt);
        }

        if (auto currentRoom = GetCurrentRoom()) {
            for (auto& segId : currentRoom->VisibleSegments) {
                if (auto seg = Level.TryGetSegment(segId)) {
                    for (auto& objId : seg->Objects) {
                        auto obj = Level.TryGetObject(objId);
                        if (obj && obj->Type != ObjectType::Weapon) {
                            // When physics moves an object to another seg it gets updated twice
                            FixedUpdateObject(dt, objId, *obj);
                        }
                    }
                }
            }
        }

        AddPendingObjects(Game::Level);
    }

    void DecayScreenFlash(float dt) {
        if (ScreenFlash.x > 0) ScreenFlash.x -= FLASH_DECAY_RATE * dt;
        if (ScreenFlash.y > 0) ScreenFlash.y -= FLASH_DECAY_RATE * dt;
        if (ScreenFlash.z > 0) ScreenFlash.z -= FLASH_DECAY_RATE * dt;
        ClampColor(ScreenFlash);
    }

    void AddScreenFlash(const Color& color) {
        ScreenFlash += color;
        ClampColor(ScreenFlash, { 0, 0, 0 }, { MAX_FLASH, MAX_FLASH, MAX_FLASH });
    }

    // Returns the lerp amount for the current tick. Executes every frame.
    float GameUpdate(float dt) {
        if (!Level.Objects.empty()) {
            if (Game::State == GameState::Editor) {
                if (Settings::Editor.EnablePhysics)
                    HandleEditorDebugInput(dt);
            }
            else if (Game::State == GameState::Game) {
                HandleInput(dt);
            }
        }

        Game::Player.HomingObjectDist = -1; // Clear each frame. Updating objects sets this.

        DecayScreenFlash(dt);

        DestroyedClips.Update(Level, dt);
        for (auto& clip : Resources::GameData.Effects) {
            if (clip.TimeLeft > 0) {
                clip.TimeLeft -= dt;
                if (clip.TimeLeft <= 0) {
                    if (auto side = Level.TryGetSide(clip.OneShotTag))
                        side->TMap2 = clip.DestroyedTexture;

                    clip.OneShotTag = {};
                    Editor::Events::LevelChanged();
                }
            }
        }

        static double accumulator = 0;
        static double t = 0;

        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        //LegitProfiler::ProfilerTask task("Fixed update");
        while (accumulator >= TICK_RATE) {
            for (auto& obj : Level.Objects)
                obj.Lifespan -= TICK_RATE;

            UpdateDoors(Level, TICK_RATE);
            FixedUpdate(TICK_RATE);
            accumulator -= TICK_RATE;
            t += TICK_RATE;
            Game::DeltaTime += TICK_RATE;
        }
        //LegitProfiler::AddCpuTask(std::move(task));

        if (Game::ShowDebugOverlay) {
            auto vp = ImGui::GetMainViewport();
            constexpr float topOffset = 50;
            UpdateLiveObjectCount();
            DrawDebugOverlay({ vp->Size.x, topOffset }, { 1, 0 });
            DrawGameDebugOverlay({ 10, topOffset }, { 0, 0 });
        }

        return float(accumulator / TICK_RATE);
    }

    Inferno::Editor::EditorUI EditorUI;

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp) {
        Matrix transform = Matrix::Lerp(obj.GetPrevTransform(), obj.GetTransform(), lerp);
        camera.Position = transform.Translation();
        camera.Target = camera.Position + transform.Forward();
        camera.Up = transform.Up();
    }

    void UpdateExitSequence() {
        // todo: escape sequence
        // for first 5? seconds move camera to player
        MoveCameraToObject(Render::Camera, Level.Objects[0], LerpAmount);
        // otherwise shift camera in front of player by 20? units

        // use a smoothed path between segment centers

        // escape cancels sequence?

        SetState(GameState::Editor); // just exit for now
    }

    void UpdateState() {
        if (State == RequestedState) return;

        switch (RequestedState) {
            case GameState::Editor:
                // Activate editor mode
                Editor::History.Undo();
                State = GameState::Editor;
                ResetCountdown();
                Render::Camera = EditorCameraSnapshot;
                Input::SetMouseMode(Input::MouseMode::Normal);
                Sound::Reset();
                Render::ResetEffects();
                LerpAmount = 1;
                break;

            case GameState::Game:
                StartLevel();
                break;

            case GameState::ExitSequence:
                break;

            case GameState::Paused:
                break;
        }

        State = RequestedState;
    }

    void Update(float dt) {
        LegitProfiler::ProfilerTask update("Update game", LegitProfiler::Colors::CARROT);

        Inferno::Input::Update();
        CheckGlobalHotkeys();
        Render::Debug::BeginFrame(); // enable debug calls during updates
        Game::DeltaTime = 0;
        UpdateState();

        g_ImGuiBatch->BeginFrame();
        switch (State) {
            case GameState::Game:
                LerpAmount = GameUpdate(dt);
                if (!Level.Objects.empty())
                    MoveCameraToObject(Render::Camera, Level.Objects[0], LerpAmount);

                break;

            case GameState::ExitSequence:
                LerpAmount = GameUpdate(dt);
                UpdateExitSequence();
                break;

            case GameState::Editor:
                if (Settings::Editor.EnablePhysics) {
                    LerpAmount = Settings::Editor.EnablePhysics ? GameUpdate(dt) : 1;
                }
                else {
                    LerpAmount = 1;
                }

                Editor::Update();
                if (!Settings::Inferno.ScreenshotMode) EditorUI.OnRender();
                break;
            case GameState::Paused:
                break;
        }

        LegitProfiler::AddCpuTask(std::move(update));
        //LegitProfiler::Profiler.Render();

        g_ImGuiBatch->EndFrame();
        Render::Present();

        LegitProfiler::Profiler.cpuGraph.LoadFrameData(LegitProfiler::CpuTasks);
        LegitProfiler::Profiler.gpuGraph.LoadFrameData(LegitProfiler::GpuTasks);
        LegitProfiler::CpuTasks.clear();
        LegitProfiler::GpuTasks.clear();
    }

    SoundID GetSoundForSide(const SegmentSide& side) {
        auto& ti1 = Resources::GetEffectClip(side.TMap);
        auto& ti2 = Resources::GetEffectClip(side.TMap2);

        if (ti1.Sound != SoundID::None)
            return ti1.Sound;
        if (ti2.Sound != SoundID::None)
            return ti2.Sound;

        return SoundID::None;
    }

    // Adds sound sources from eclips such as lava and forcefields
    void AddSoundSources() {
        for (int i = 0; i < Level.Segments.size(); i++) {
            auto segid = SegID(i);
            auto& seg = Level.GetSegment(segid);
            for (auto& sid : SideIDs) {
                if (!seg.SideIsSolid(sid, Level)) continue;

                auto& side = seg.GetSide(sid);
                auto sound = GetSoundForSide(side);
                if (sound == SoundID::None) continue;

                if (auto cside = Level.TryGetConnectedSide({ segid, sid })) {
                    auto csound = GetSoundForSide(*cside);
                    if (csound == sound && seg.GetConnection(sid) < segid)
                        continue; // skip sound on lower numbered segment
                }

                Sound3D s({ sound }, side.Center, segid);
                s.Looped = true;
                s.Radius = 80;
                s.Volume = 0.50f;
                s.Occlusion = false;
                s.Side = sid;
                Sound::Play(s);
            }
        }
    }

    void MarkNearby(SegID id, span<int8> marked, int depth) {
        if (depth < 0) return;
        marked[(int)id] = true;

        auto& seg = Level.GetSegment(id);
        for (auto& sid : SideIDs) {
            auto conn = seg.GetConnection(sid);
            if (conn > SegID::None && !seg.SideIsWall(sid) && !marked[(int)conn])
                MarkNearby(conn, marked, depth - 1);
        }
    }

    void MarkAmbientSegments(SoundFlag sflag, TextureFlag tflag) {
        List<int8> marked(Level.Segments.size());

        for (auto& seg : Level.Segments) {
            seg.AmbientSound &= ~sflag;
        }

        for (int i = 0; i < Level.Segments.size(); i++) {
            auto& seg = Level.Segments[i];
            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);
                auto& tmi1 = Resources::GetLevelTextureInfo(side.TMap);
                auto& tmi2 = Resources::GetLevelTextureInfo(side.TMap2);
                if (tmi1.HasFlag(tflag) || tmi2.HasFlag(tflag)) {
                    seg.AmbientSound |= sflag;
                }
            }
        }

        constexpr auto MAX_DEPTH = 5;

        for (int i = 0; i < Level.Segments.size(); i++) {
            auto& seg = Level.Segments[i];
            if (bool(seg.AmbientSound & sflag))
                MarkNearby(SegID(i), marked, MAX_DEPTH);
        }

        for (int i = 0; i < Level.Segments.size(); i++) {
            if (marked[i])
                Level.Segments[i].AmbientSound |= sflag;
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
            "noise"
        };

        Render::Materials->LoadTextures(customHudTextures);
    }

    void StartLevel() {
        auto player = Level.TryGetObject(ObjID(0));

        if (!player || !player->IsPlayer()) {
            SPDLOG_ERROR("No player start at object 0!");
            return; // no player start!
        }

        // Activate game mode
        Editor::InitObject(Level, *player, ObjectType::Player);
        Player.Reference = { ObjID(0), player->Signature };

        Editor::History.SnapshotLevel("Playtest");
        State = GameState::Game;

        ResetCountdown();
        StuckObjects = {};
        Sound::WaitInitialized();
        Sound::Reset();
        Resources::LoadGameTable();
        Render::ResetEffects();
        Render::Materials->UnloadNamedTextures();
        Render::Materials->LoadGameTextures();
        InitObjects(Level);
        InitializeMatcens(Level);

        Editor::SetPlayerStartIDs(Level);
        // Default the gravity direction to the player start
        Gravity = player->Rotation.Up() * -DEFAULT_GRAVITY;

        Navigation = NavigationNetwork(Level);
        Level.Rooms = CreateRooms(Level);

        // init objects
        for (int id = 0; id < Level.Objects.size(); id++) {
            auto& obj = Level.Objects[id];

            if (obj.IsPlayer()) {
                obj.Physics.Wiggle = Resources::GameData.PlayerShip.Wiggle;
                if (Player.HasPowerup(PowerupFlag::Invulnerable))
                    obj.MakeInvulnerable(3600.0f);

                if (Player.HasPowerup(PowerupFlag::Cloak))
                    obj.Cloak(3600.0f);
            }

            if ((obj.IsPlayer() && obj.ID != 0) || obj.IsCoop())
                obj.Lifespan = -1; // Remove non-player 0 starts (no multiplayer)

            if (obj.Type == ObjectType::Robot) {
                auto& ri = Resources::GetRobotInfo(obj.ID);
                obj.MaxHitPoints = obj.HitPoints = ri.HitPoints;
                PlayRobotAnimation(obj, AnimState::Rest);
                //obj.Physics.Wiggle = obj.Radius * 0.01f;
                //obj.Physics.WiggleRate = 0.33f;
            }

            if (obj.IsPowerup(PowerupID::Gauss) || obj.IsPowerup(PowerupID::Vulcan))
                obj.Control.Powerup.Count = 2500;

            if (obj.IsPowerup(PowerupID::FlagBlue) || obj.IsPowerup(PowerupID::FlagRed))
                obj.Lifespan = -1; // Remove CTF flags (no multiplayer)

            UpdateObjectSegment(Level, obj);

            //if (auto seg = Level.TryGetSegment(obj.Segment)) {
            //    seg->AddObject((ObjID)id);
            //    obj.Ambient.SetTarget(seg->VolumeLight, 0);
            //}

            if (obj.Type == ObjectType::Reactor)
                InitReactor(Level, obj);

            if (obj.Type == ObjectType::Robot) {
                obj.NextThinkTime = Time + 0.5f;
                SetFlag(obj.Physics.Flags, PhysicsFlag::SphereCollidePlayer);
            }
        }

        MarkAmbientSegments(SoundFlag::AmbientLava, TextureFlag::Volatile);
        MarkAmbientSegments(SoundFlag::AmbientWater, TextureFlag::Water);
        AddSoundSources();

        EditorCameraSnapshot = Render::Camera;
        Settings::Editor.RenderMode = RenderMode::Shaded;
        Input::SetMouseMode(Input::MouseMode::Mouselook);
        Render::LoadHUDTextures();

        PreloadTextures();

        Player.GiveWeapon(PrimaryWeaponIndex::Laser);
        Player.GiveWeapon(PrimaryWeaponIndex::Vulcan);
        Player.GiveWeapon(PrimaryWeaponIndex::Spreadfire);
        Player.GiveWeapon(PrimaryWeaponIndex::Helix);
        Player.GiveWeapon(PrimaryWeaponIndex::Fusion);
        Player.GiveWeapon(SecondaryWeaponIndex::Concussion);
        Player.GivePowerup(PowerupFlag::Afterburner);

        // Reset shields and energy to at least 100 on level start
        Player.Shields = std::max(Player.Shields, 100.0f);
        Player.Energy = std::max(Player.Energy, 100.0f);

        // Max vulcan ammo changes between D1 and D2
        PyroGX.Weapons[(int)PrimaryWeaponIndex::Vulcan].MaxAmmo = Level.IsDescent1() ? 10000 : 20000;

        Player.PrimaryWeapons = 0xffff;
        Player.SecondaryWeapons = 0xffff;
        int weaponCount = Level.IsDescent2() ? 10 : 5;
        for (int i = 0; i < weaponCount; ++i) {
            Player.SecondaryAmmo[i] = 99;
            Player.PrimaryAmmo[i] = 5000;
        }
    }

    void SetState(GameState state) {
        RequestedState = state;
    }

    GameState GetState() { return State; }
}
