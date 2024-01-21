#include "pch.h"
#define NOMINMAX
#include <numeric>

#include "Game.h"
#include "DebugOverlay.h"
#include "Editor/Editor.h"
#include "Editor/Editor.Object.h"
#include "Editor/UI/EditorUI.h"
#include "FileSystem.h"
#include "Game.AI.h"
#include "Game.Bindings.h"
#include "Game.Input.h"
#include "Game.Object.h"
#include "Game.Reactor.h"
#include "Game.Room.h"
#include "Game.Segment.h"
#include "Game.Wall.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"
#include "HUD.h"
#include "imgui_local.h"
#include "Input.h"
#include "LegitProfiler.h"
#include "Resources.h"
#include "SoundSystem.h"

using namespace DirectX;

namespace Inferno::Game {
    namespace {
        GameState State = GameState::Editor;
        GameState RequestedState = GameState::Editor;
        Camera EditorCameraSnapshot;
        constexpr size_t OBJECT_BUFFER_SIZE = 100; // How many new objects to keep in reserve
        int MenuIndex = 0;
    }

    bool StartLevel();

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
            bool wasSecret = LevelNumber < 0;
            FixMatcenLinks(level);

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

            if (auto path = FileSystem::TryFindFile("env.dds")) {
                ResourceUploadBatch batch(Render::Device);
                batch.Begin();
                Render::Materials->EnvironmentCube.LoadDDS(batch, *path);
                Render::Materials->EnvironmentCube.CreateCubeSRV();
                batch.End(Render::Adapter->BatchUploadQueue->Get());
            }

            for (auto& seg : Level.Segments) {
                // Clamp volume light if overly bright segments are saved
                if (seg.VolumeLight.x == seg.VolumeLight.y && seg.VolumeLight.x == seg.VolumeLight.z && seg.VolumeLight.x > 2.0f)
                    seg.VolumeLight = Color(1, 1, 1);
            }

            Render::LoadLevel(Level);
            Render::ResetEffects();
            InitObjects(Game::Level);

            Editor::OnLevelLoad(reload);
            Render::Materials->Prune();
            Render::Adapter->PrintMemoryUsage();

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
    }

    void LoadMission(const filesystem::path& file) {
        Mission = HogFile::Read(FileSystem::FindFile(file));
    }

    void UpdateAmbientSounds() {
        auto& player = Level.Objects[0];
        if (player.Segment == SegID::None) return;

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
            Sound3D s(sound);
            s.Volume = Random() * 0.1f + 0.05f;
            Sound::AtPlayer(s);
        }
    }

    void AddPointsToScore(int points) {
        auto score = Player.Score;

        Player.Score += points;
        AddPointsToHUD(points);

        // This doesn't account for negative scoring (which never happens in D2)
        auto lives = Player.Score / EXTRA_LIFE_POINTS - score / EXTRA_LIFE_POINTS;
        if (lives > 0) {
            Sound::Play2D({ SoundID::ExtraLife });
            Player.GiveExtraLife((uint8)lives);
        }
    }

    Object* GetObject(ObjRef ref) {
        if (!Seq::inRange(Level.Objects, (int)ref.Id)) return nullptr;
        auto& obj = Level.Objects[(int)ref.Id];
        if (obj.Signature != ref.Signature) return nullptr;
        return &obj;
    }

    void UpdateEffects(Object& obj, float dt) {
        auto& e = obj.Effects;

        if (HasFlag(e.Flags, EffectFlags::Cloaked)) {
            e.CloakTimer += dt;
            e.CloakFlickerTimer -= dt;

            if (e.CloakDuration > 0 && e.CloakTimer >= e.CloakDuration)
                UncloakObject(obj);
        }

        if (HasFlag(e.Flags, EffectFlags::Invulnerable)) {
            e.InvulnerableTimer += dt;

            if (e.InvulnerableDuration > 0 && e.InvulnerableTimer >= e.InvulnerableDuration)
                MakeVulnerable(obj);
        }

        if (HasFlag(e.Flags, EffectFlags::PhaseIn)) {
            e.PhaseTimer += dt;
            if (e.PhaseTimer >= e.PhaseDuration)
                ClearFlag(e.Flags, EffectFlags::PhaseIn);
        }

        if (HasFlag(e.Flags, EffectFlags::PhaseOut)) {
            e.PhaseTimer += dt;
            if (e.PhaseTimer >= e.PhaseDuration)
                e.PhaseTimer = e.PhaseDuration;
        }

        if (HasFlag(e.Flags, EffectFlags::Ignited)) {
            e.IgniteDuration -= dt;
            if (e.IgniteDuration <= 0)
                ClearFlag(e.Flags, EffectFlags::Ignited);
        }
    }

    constexpr bool ShouldAlwaysUpdate(const Object& obj) {
        return obj.Type == ObjectType::Weapon || HasFlag(obj.Flags, ObjectFlag::AlwaysUpdate);
    }

    // Updates on each game tick
    void FixedUpdate(float dt) {
        Debug::ActiveRobots = 0;
        Debug::LiveObjects = 0;
        Player.Update(dt);
        BeginAIFrame();

        UpdateAmbientSounds();
        UpdateDoors(Level, dt);
        UpdateMatcens(Game::Level, dt);

        Sound::UpdateSoundEmitters(dt);
        UpdateExplodingWalls(Game::Level, dt);

        if (ControlCenterDestroyed)
            UpdateReactorCountdown(dt);

        Render::FixedUpdateEffects(dt);

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            ClearFlag(obj.Flags, ObjectFlag::Updated);

            // Always update weapons and live robots in case they go out of sight
            if (ShouldAlwaysUpdate(obj))
                FixedUpdateObject(dt, ObjID(i), obj);

            if (obj.Type == ObjectType::Reactor)
                UpdateReactor(obj);

            UpdateEffects(obj, dt);

            if (obj.Lifespan > 0)
                obj.Lifespan -= TICK_RATE;
        }

        auto playerRoom = Level.GetRoomID(GetPlayerObject());
        auto flags = TraversalFlag::StopSecretDoor | TraversalFlag::PassTransparent;
        // Stop AI at doors on hotshot and below
        if (Difficulty < 3) flags |= TraversalFlag::StopDoor;

        ActiveRooms = GetRoomsByDepth(Level.Rooms, playerRoom, NEARBY_PORTAL_DEPTH, flags);

        // Merge the nearby rooms with the visible rooms
        for (auto& id : Render::GetVisibleRooms()) {
            if (!Seq::contains(ActiveRooms, id)) ActiveRooms.push_back(id);
        }

        for (auto& roomId : ActiveRooms) {
            if (auto room = Level.GetRoom(roomId)) {
                for (auto& segId : room->Segments) {
                    auto seg = Level.TryGetSegment(segId);
                    if (!seg) continue;

                    for (auto& objId : seg->Objects) {
                        auto obj = Level.TryGetObject(objId);
                        if (obj && !ShouldAlwaysUpdate(*obj)) {
                            FixedUpdateObject(dt, objId, *obj);
                        }
                    }
                }
            }
        }
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
        if (Game::State == GameState::Paused)
            return LerpAmount; // Don't update anything except camera while paused

        // Grow the object buffer ahead of time in case new objects are created
        if (Level.Objects.size() + OBJECT_BUFFER_SIZE > Level.Objects.capacity()) {
            Level.Objects.reserve(Level.Objects.size() + OBJECT_BUFFER_SIZE * 2);
            SPDLOG_INFO("Growing object buffer to {}", Level.Objects.capacity());
        }

        Game::Player.HomingObjectDist = -1; // Clear each frame. Updating objects sets this.
        Game::Player.DirectLight = Color();
        DecayScreenFlash(dt);

        // Update global dimming
        GlobalDimming = ControlCenterDestroyed ? float(sin(CountdownTimer * 4) * 0.5 + 0.5) : 1;

        DestroyedClips.Update(Level, dt);
        for (auto& clip : Resources::GameData.Effects) {
            if (clip.TimeLeft > 0) {
                clip.TimeLeft -= dt;
                if (clip.TimeLeft <= 0) {
                    if (auto side = Level.TryGetSide(clip.OneShotTag))
                        side->TMap2 = clip.DestroyedTexture;

                    clip.OneShotTag = {};
                    Render::LevelChanged = true; // Need to update textures on mesh. This is not ideal.
                }
            }
        }

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            if (obj.Render.Type == RenderType::Hostage || obj.Render.Type == RenderType::Powerup) {
                obj.Render.VClip.DirectLight = Color();
            }
        }

        static double accumulator = 0;
        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        //LegitProfiler::ProfilerTask task("Fixed update");
        while (accumulator >= TICK_RATE) {
            FixedUpdate(TICK_RATE);
            accumulator -= TICK_RATE;
            Game::DeltaTime += TICK_RATE;
        }
        //LegitProfiler::AddCpuTask(std::move(task));

        if (Game::ShowDebugOverlay && Game::GetState() != GameState::Editor) {
            auto vp = ImGui::GetMainViewport();
            constexpr float topOffset = 50;
            //UpdateLiveObjectCount();
            DrawDebugOverlay({ vp->Size.x, topOffset }, { 1, 0 });
            DrawGameDebugOverlay({ 10, topOffset }, { 0, 0 });
        }

        return float(accumulator / TICK_RATE);
    }

    Inferno::Editor::EditorUI EditorUI;

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp) {
        Matrix transform = obj.GetTransform(lerp);
        camera.Position = transform.Translation();
        camera.Target = camera.Position + transform.Forward();
        camera.Up = transform.Up();
    }

    void ResetGlobalLighting() {
        // todo: this should blend back to normal instead of being instant
        Render::ToneMapping->ToneMap.BloomStrength = .35f;
        Render::ToneMapping->ToneMap.Exposure = 1;
        Game::GlobalDimming = 1; // Clear dimming
    }

    void UpdateExitSequence() {
        // todo: escape sequence
        // for first 5? seconds move camera to player
        MoveCameraToObject(Render::Camera, Level.Objects[0], LerpAmount);
        // otherwise shift camera in front of player by 20? units

        // use a smoothed path between segment centers

        // escape cancels sequence?

        // restore default exposure in case the reactor started going critical.
        ResetGlobalLighting();
        SetState(GameState::Editor); // just exit for now
    }

    void UpdateGameState() {
        if (State == RequestedState) return;
        Input::ResetState(); // Clear input when switching game states

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
                ResetGlobalLighting();
                break;

            case GameState::Game:
                if (State == GameState::GameMenu) {
                    Input::SetMouseMode(Input::MouseMode::Mouselook);
                }
                else if (State == GameState::Paused) {
                    GetPlayerObject().Render.Type = RenderType::None; // Make player invisible
                    Input::SetMouseMode(Input::MouseMode::Mouselook);
                }
                else {
                    if (!StartLevel()) {
                        RequestedState = State;
                        return;
                    }
                }

                break;

            case GameState::ExitSequence:
                break;

            case GameState::Paused:
                if (State != GameState::Game && State != GameState::ExitSequence) return;
                State = GameState::Paused;
                MoveCameraToObject(Render::Camera, GetPlayerObject(), LerpAmount);
                GetPlayerObject().Render.Type = RenderType::Model; // Make player visible
                Input::SetMouseMode(Input::MouseMode::Mouselook);
                break;

            case GameState::GameMenu:
                if (State == GameState::GameMenu) return;
                MenuIndex = 0; // select the top
                Input::SetMouseMode(Input::MouseMode::Normal);
                State = GameState::GameMenu;
                break;
        }

        State = RequestedState;
    }

    void UpdateDeathSequence(float dt) {
        Player.DoDeathSequence(dt);

        if (Player.TimeDead > 2 && Player.Lives == 0) {
            Render::Canvas->DrawGameText("game over", 0, 0, FontSize::Big, Color(1, 1, 1), 1, AlignH::Center, AlignV::Center);
        }

        if (Game::Player.TimeDead > 2 && ConfirmedInput()) {
            if (Game::Player.Lives == 0)
                Game::SetState(GameState::Editor); // todo: score screen
            else
                Game::Player.Respawn(true); // todo: EndCutscene() with fades
        }
    }


    void UpdateMenu(float /*dt*/) {

        if (Input::IsKeyPressed(Input::Keys::Down))
            MenuIndex++;

        if (Input::IsKeyPressed(Input::Keys::Up))
            MenuIndex--;

        MenuIndex = MenuIndex % 2;

        if (Input::IsKeyPressed(Input::Keys::Escape))
            Game::SetState(GameState::Game);

        if (Input::IsKeyPressed(Input::Keys::Enter)) {
            switch (MenuIndex) {
                default:
                case 0:
                    Game::SetState(GameState::Game);
                    break;

                case 1:
                    Game::SetState(GameState::Editor);
                    break;
            }
        }

        auto fontHeight = Inferno::Atlas.GetFont(FontSize::MediumBlue)->Height;
        auto lineHeight = fontHeight * FONT_LINE_SPACING;

        auto scale = Render::Canvas->GetScale();

        //Render::Canvas->DrawRectangle({ 0, 0 }, Render::Canvas->GetSize(), Color(0, 0, 0, 0.25f));

        Vector2 bgSize = Vector2(200, fontHeight * 3.5f) * scale;
        Vector2 alignment = Render::GetAlignment(bgSize, AlignH::Center, AlignV::Center, Render::Canvas->GetSize());
        Render::Canvas->DrawRectangle(alignment, bgSize, Color(0, 0, 0, 0.65f));

        float y = -lineHeight * 0.85f;

        {
            auto font = MenuIndex == 0 ? FontSize::MediumGold : FontSize::Medium;
            Render::Canvas->DrawGameText("continue", 0, y * scale, font, Color(1, 1, 1), 1, AlignH::Center, AlignV::CenterTop);
        }

        y += lineHeight;

        {
            auto font = MenuIndex == 1 ? FontSize::MediumGold : FontSize::Medium;
            Render::Canvas->DrawGameText("quit", 0, y * scale, font, Color(1, 1, 1), 1, AlignH::Center, AlignV::CenterTop);
        }
    }

    void Update(float dt) {
        LegitProfiler::ProfilerTask update("Update game", LegitProfiler::Colors::CARROT);

        Inferno::Input::Update();
        Bindings.Update();
        CheckGlobalHotkeys();

        if (Game::State == GameState::Editor) {
            if (Settings::Editor.EnablePhysics)
                HandleEditorDebugInput(dt);
        }
        else {
            HandleInput();
            HandleShipInput(dt);
        }

        Render::Debug::BeginFrame(); // enable debug calls during updates
        Game::DeltaTime = 0;
        UpdateGameState();

        g_ImGuiBatch->BeginFrame();
        switch (State) {
            case GameState::Game:
                LerpAmount = GameUpdate(dt);

                if (!Level.Objects.empty()) {
                    if (Player.IsDead)
                        UpdateDeathSequence(dt);
                    else
                        MoveCameraToObject(Render::Camera, Level.Objects[0], LerpAmount);
                }

                if (Input::IsKeyPressed(Input::Keys::Escape))
                    Game::SetState(GameState::GameMenu);

                break;

            case GameState::Cutscene:
                LerpAmount = GameUpdate(dt);
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

            case GameState::GameMenu:
                UpdateMenu(dt);
                break;

            case GameState::Paused:
                if (Input::IsKeyPressed(Input::Keys::OemTilde) && Input::IsKeyDown(Input::Keys::LeftAlt))
                    Game::SetState(Game::GetState() == GameState::Paused ? GameState::Game : GameState::Paused);

                Editor::Bindings::Update(); // Using editor camera bindings
                Editor::UpdateCamera(Render::Camera);
                break;
        }

        LegitProfiler::AddCpuTask(std::move(update));
        //LegitProfiler::Profiler.Render();

        // Reset direct light as rendering effects sets it
        Game::Player.DirectLight = Color{};

        g_ImGuiBatch->EndFrame();
        Render::Present();

        LegitProfiler::Profiler.cpuGraph.LoadFrameData(LegitProfiler::CpuTasks);
        LegitProfiler::Profiler.gpuGraph.LoadFrameData(LegitProfiler::GpuTasks);
        LegitProfiler::CpuTasks.clear();
        LegitProfiler::GpuTasks.clear();

        Input::NextFrame();
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
            for (auto& sid : SIDE_IDS) {
                if (!seg.SideIsSolid(sid, Level)) continue;

                auto& side = seg.GetSide(sid);
                auto sound = GetSoundForSide(side);
                if (sound == SoundID::None) continue;

                if (auto cside = Level.TryGetConnectedSide({ segid, sid })) {
                    auto csound = GetSoundForSide(*cside);
                    if (csound == sound && seg.GetConnection(sid) < segid)
                        continue; // skip sound on lower numbered segment
                }

                Sound3D s(sound);
                s.Looped = true;
                s.Radius = 80;
                s.Volume = 0.50f;
                //s.Occlusion = false;
                Sound::Play(s, side.Center, segid, sid);
            }
        }
    }

    void MarkNearby(SegID id, span<int8> marked, int depth) {
        if (depth < 0) return;
        marked[(int)id] = true;

        auto& seg = Level.GetSegment(id);
        for (auto& sid : SIDE_IDS) {
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
            for (auto& sid : SIDE_IDS) {
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

    bool StartLevel() {
        auto player = Level.TryGetObject(ObjID(0));

        if (!player || !player->IsPlayer()) {
            ShowErrorMessage(L"No player start at object 0!", L"Unable to playtest");
            return false;
        }

        if (Input::ControlDown && State == GameState::Editor) {
            // Move player to selected segment if control is held down
            Editor::Selection.Object = ObjID(0);
            Editor::Commands::MoveObjectToSegment();
        }
        else {
            Editor::History.SnapshotLevel("Playtest");
        }

        // Reset level timing
        Game::Time = Game::FrameTime = Game::DeltaTime = 0;
        ResetDeltaTime = true;

        // Activate game mode
        InitObject(Level, *player, ObjectType::Player);
        Player.Reference = { ObjID(0), player->Signature };
        Player.SpawnPosition = player->Position;
        Player.SpawnRotation = player->Rotation;
        Player.SpawnSegment = player->Segment;
        Player.Lives = PlayerData::INITIAL_LIVES;
        player->Faction = Faction::Player;

        State = GameState::Game;

        ResetCountdown();
        StuckObjects = {};
        Sound::WaitInitialized();
        Sound::Reset();
        Resources::LoadGameTables(Level);
        Render::ResetEffects();
        Render::Materials->UnloadNamedTextures();
        Render::Materials->LoadGameTextures();
        InitObjects(Level);
        InitializeMatcens(Level);
        Render::LoadHUDTextures();
        PreloadTextures();

        Editor::SetPlayerStartIDs(Level);
        // Default the gravity direction to the player start
        Gravity = player->Rotation.Up() * -DEFAULT_GRAVITY;

        Navigation = NavigationNetwork(Level);
        Level.Rooms = CreateRooms(Level);
        Render::LevelChanged = true; // regenerate level meshes

        // init objects
        for (int id = 0; id < Level.Objects.size(); id++) {
            auto& obj = Level.Objects[id];

            if ((obj.IsPlayer() && obj.ID != 0) || obj.IsCoop())
                obj.Lifespan = -1; // Remove non-player 0 starts (no multiplayer)

            if (obj.Type == ObjectType::Robot) {
                auto& ri = Resources::GetRobotInfo(obj.ID);
                obj.MaxHitPoints = obj.HitPoints = ri.HitPoints;
                obj.Faction = Faction::Robot;
                SetFlag(obj.Physics.Flags, PhysicsFlag::SphereCollidePlayer);
                obj.NextThinkTime = 0.5f; // Help against hot-starts by sleeping robots on level load
                PlayRobotAnimation(obj, AnimState::Rest);
                //obj.Physics.Wiggle = obj.Radius * 0.01f;
                //obj.Physics.WiggleRate = 0.33f;
            }

            if (obj.IsPowerup(PowerupID::Gauss) || obj.IsPowerup(PowerupID::Vulcan))
                obj.Control.Powerup.Count = VULCAN_AMMO_PICKUP;

            if (obj.IsPowerup(PowerupID::FlagBlue) || obj.IsPowerup(PowerupID::FlagRed))
                obj.Lifespan = -1; // Remove CTF flags (no multiplayer)

            if (obj.IsAlive())
                UpdateObjectSegment(Level, obj);

            //if (auto seg = Level.TryGetSegment(obj.Segment)) {
            //    seg->AddObject((ObjID)id);
            //    obj.Ambient.SetTarget(seg->VolumeLight, 0);
            //}

            if (obj.Type == ObjectType::Reactor) {
                obj.Faction = Faction::Robot;
                InitReactor(Level, obj);
            }

            if (obj.IsWeapon() && obj.ID == (int)WeaponID::LevelMine)
                obj.Faction = Faction::Robot;

            FixObjectPosition(obj);
        }

        MarkAmbientSegments(SoundFlag::AmbientLava, TextureFlag::Volatile);
        MarkAmbientSegments(SoundFlag::AmbientWater, TextureFlag::Water);
        AddSoundSources();

        EditorCameraSnapshot = Render::Camera;
        Settings::Editor.RenderMode = RenderMode::Shaded;
        Input::SetMouseMode(Input::MouseMode::Mouselook);
        Player.Respawn(false);
        return true;
    }

    void SetState(GameState state) {
        RequestedState = state;
    }

    GameState GetState() { return State; }
}
