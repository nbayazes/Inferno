#include "pch.h"
#define NOMINMAX
#include <numeric>
#include <gsl/pointers>
#include "Game.h"
#include "DebugOverlay.h"
#include "Editor/Editor.h"
#include "Editor/Editor.Object.h"
#include "Editor/UI/EditorUI.h"
#include "VisualEffects.h"
#include "Game.AI.h"
#include "Game.Bindings.h"
#include "Game.Cinematics.h"
#include "Game.Input.h"
#include "Game.Object.h"
#include "Game.Reactor.h"
#include "Game.Room.h"
#include "Game.Segment.h"
#include "Game.UI.h"
#include "Game.Visibility.h"
#include "Game.Wall.h"
#include "Graphics.Debug.h"
#include "Graphics.h"
#include "Graphics/Render.MainMenu.h"
#include "HUD.h"
#include "imgui_local.h"
#include "Input.h"
#include "LegitProfiler.h"
#include "Resources.h"
#include "SoundSystem.h"

using namespace DirectX;

namespace Inferno::Game {
    namespace {
        auto State = GameState::Startup;
        auto RequestedState = GameState::Startup;
        constexpr size_t OBJECT_BUFFER_SIZE = 100; // How many new objects to keep in reserve
        int MenuIndex = 0;
        Ptr<Editor::EditorUI> EditorUI;
        gsl::not_null ActiveCamera = &MainCamera;
        LerpedValue LerpedTimeScale(1);
        Object NULL_PLAYER{ .Type = ObjectType::Player };
    }

    void SetTimeScale(float scale, float transitionSpeed) {
        LerpedTimeScale.SetTarget(scale, transitionSpeed);
    }

    Camera& GetActiveCamera() { return *ActiveCamera.get(); }

    void SetActiveCamera(Camera& camera) { ActiveCamera = &camera; }

    bool EnableAi() {
        return !Settings::Cheats.DisableAI && Game::GetState() == GameState::Game;
    }

    bool StartLevel();


    void ResetCountdown() {
        ControlCenterDestroyed = false;
        TotalCountdown = CountdownSeconds = -1;
        CountdownTimer = -1.0f;
        ScreenFlash = Color();
    }

    void UpdateAmbientSounds() {
        if (Level.Objects.empty()) return;
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
            Sound::PlayFrom(s, GetPlayerObject());
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

    void AutomapInfo::Update(const Inferno::Level& level) {
        if (Game::LevelNumber < 0)
            LevelNumber = fmt::format("Secret Level {}", -Game::LevelNumber);
        else
            LevelNumber = fmt::format("Level {}", Game::LevelNumber);

        if (Game::Player.Stats.HostagesOnLevel > 0) {
            auto hostagesLeft = Game::Player.Stats.HostagesOnLevel - Game::Player.HostagesRescued;
            HostageText = hostagesLeft <= 0 ? "all hostages rescued" : hostagesLeft == 1 ? "1 hostage left" : fmt::format("{} hostages left", hostagesLeft);
        }
        else {
            HostageText = {};
        }

        RobotScore = 0;
        for (auto& obj : level.Objects) {
            if (!obj.IsRobot()) continue;

            auto& info = Resources::GetRobotInfo(obj);
            RobotScore += info.Score;
        }

        for (auto& matcen : level.Matcens) {
            auto matcenSum = 0;

            auto robots = matcen.GetEnabledRobots();
            for (auto& id : robots) {
                auto& info = Resources::GetRobotInfo(id);
                matcenSum += info.Score;
            }

            // Multiply matcen score by max spawns
            int8 activations = 3;
            if (Game::Difficulty == 3) activations = 4; // Ace
            if (Game::Difficulty >= 4) activations = 5; // Insane or above
            auto spawnCount = (int8)Game::Difficulty + 3;
            matcenSum *= activations * spawnCount;

            // Average the matcenValue
            if (robots.size() > 0) {
                matcenSum = int((float)matcenSum / robots.size());
            }

            RobotScore += matcenSum;
        }

        if (RobotScore > 80'000) {
            Threat = "threat: extreme";
        }
        if (RobotScore > 60'000) {
            Threat = "threat: high";
        }
        if (RobotScore > 40'000) {
            Threat = "threat: moderate";
        }
        else if (RobotScore > 20'000) {
            Threat = "threat: light";
        }
        else if (RobotScore > 0) {
            Threat = "threat: minimal";
        }
        else {
            Threat = "threat: none";
        }

#ifdef _DEBUG
        Threat += " " + std::to_string(RobotScore);
#endif
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

    Object& GetPlayerObject() {
        if (!Seq::inRange(Level.Objects, (int)Player.Reference.Id))
            return NULL_PLAYER;

        return Level.Objects[(int)Player.Reference.Id];
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

        FixedUpdateEffects(dt);

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
                obj.Lifespan -= dt;
        }

        auto playerRoom = Level.GetRoomID(GetPlayerObject());
        auto flags = TraversalFlag::StopSecretDoor | TraversalFlag::PassTransparent;
        // Stop AI at doors on hotshot and below
        if (Difficulty < 3) flags |= TraversalFlag::StopDoor;

        ActiveRooms = GetRoomsByDepth(Level.Rooms, playerRoom, NEARBY_PORTAL_DEPTH, flags);

        // Merge the nearby rooms with the visible rooms
        for (auto& id : Graphics::GetVisibleRooms()) {
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

        for (auto& objId : Level.Terrain.Objects) {
            if (auto obj = Level.TryGetObject(objId))
                FixedUpdateObject(dt, objId, *obj);
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

        LerpedTimeScale.Update(dt);
        TimeScale = LerpedTimeScale.GetValue();

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

        if (Game::ActiveCamera)
            TraverseSegments(*Game::ActiveCamera, GetPlayerObject().Segment, TraversalFlag::None);

        static double accumulator = 0;
        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        //const float tickRate = TICK_RATE * Game::TimeScale;
        //SPDLOG_INFO("Tick rate: {}", tickRate);

        //LegitProfiler::ProfilerTask task("Fixed update");
        while (accumulator >= TICK_RATE) {
            FixedUpdate(TICK_RATE * Game::TimeScale);
            accumulator -= TICK_RATE;
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

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp) {
        Matrix transform = obj.GetTransform(lerp);
        auto target = transform.Translation() + transform.Forward();
        camera.MoveTo(transform.Translation(), target, transform.Up());
    }

    void ResetGlobalLighting() {
        // todo: this should lerp back to normal instead of being instant
        Render::ToneMapping->ToneMap.BloomStrength = .35f;
        Render::ToneMapping->ToneMap.Exposure = 1;
        Game::GlobalDimming = 1; // Clear dimming
    }

    void UpdateExitSequence() {
        return;

        // todo: escape sequence
        // escape cancels sequence?

        // restore default exposure in case the reactor started going critical.
        //ResetGlobalLighting();

        //auto nextLevel = LevelNameByIndex(LevelNumber + 1);
        //if (nextLevel.empty()) {
        //    SetState(GameState::Editor);
        //}
        //else {
        //    LoadLevel(Level.Path, nextLevel);
        //    SetState(GameState::Game);
        //}
        // reset level game state. countdown etc
        // respawn
    }

    constexpr float NAVIGATE_SPEED = 800.0f;

    void PanAutomapTo(const Vector3& target) {
        auto distance = Vector3::Distance(target, AutomapCamera.Target);
        if (distance > 1.0f) {
            auto duration = distance / NAVIGATE_SPEED;
            AutomapCamera.LerpTo(target, duration);
        }
    }

    void ResetAutomapCamera(bool instant) {
        auto& player = Game::GetPlayerObject();

        // overload style camera positioned directly behind the player
        //auto vOffset = player.Rotation.Up() * 5.0f;
        //auto position = player.Position + player.Rotation.Backward() * 15.0f + vOffset;
        //auto target = player.Position + vOffset + player.Rotation.Forward() * 25.0f;
        //auto dir = target - position;

        constexpr float hDistance = 120;
        constexpr float vDistance = 100;
        auto vOffset = player.Rotation.Up() * vDistance;
        auto position = player.Position + player.Rotation.Backward() * hDistance + vOffset;
        auto target = player.Position;
        auto dir = target - position;

        dir.Normalize();
        auto right = dir.Cross(player.Rotation.Up());
        auto up = right.Cross(dir);

        if (instant) {
            AutomapCamera.MoveTo(position, target, up);
        }
        else {
            PanAutomapTo(target);
        }
    }

    void NavigateToEnergy() {
        static int index = -1;
        List<Room*> energyRooms;

        for (auto& room : Level.Rooms) {
            if (room.Type != SegmentType::Energy) continue;
            energyRooms.push_back(&room);
        }

        if (energyRooms.empty())
            return;

        index++;
        if (index >= energyRooms.size())
            index = 0;

        PanAutomapTo(energyRooms[index]->Center);
    }

    void NavigateToReactor() {
        Object* reactor = nullptr;

        for (auto& obj : Level.Objects) {
            if (obj.IsReactor()) {
                reactor = &obj;
                break;
            }
        }

        if (!reactor) return;
        PanAutomapTo(reactor->Position);
    }

    void NavigateToExit() {
        auto exit = FindExit(Level);

        if (auto side = Level.TryGetSide(exit))
            PanAutomapTo(side->Center);
    }

    void OpenAutomap() {
        Game::Automap.Update(Game::Level);
        Graphics::UpdateAutomap();

        Sound::PauseSounds();
        Sound::GetVolume();
        Sound::SetMusicVolume(Settings::Inferno.MusicVolume * 0.15f);
        Input::SetMouseMode(Input::MouseMode::Mouselook);
        ResetAutomapCamera(true);
    }

    void CloseAutomap() {
        Sound::ResumeSounds();
        Sound::SetMusicVolume(Settings::Inferno.MusicVolume);
        Input::SetMouseMode(Input::MouseMode::Mouselook);
    }

    // Changes the game state if a new one is requested
    void UpdateGameState() {
        if (State == RequestedState) return;
        Input::ResetState(); // Clear input when switching game states

        switch (RequestedState) {
            case GameState::MainMenu:
            {
                State = GameState::MainMenu;
                UpdateWindowTitle();
                Game::Level = {};
                Editor::History.Reset();
                Game::MainCamera.Up = Vector3::UnitY;
                Game::MainCamera.Position = MenuCameraPosition;
                Game::MainCamera.Target = MenuCameraTarget;
                break;
            }

            case GameState::Editor:
                if (Level.Version == 0) {
                    // Null file
                    UpdateWindowTitle("Loading editor");
                    Editor::OpenRecentOrEmpty();
                } else {
                    UpdateWindowTitle();
                }

                Editor::History.Undo();
                State = GameState::Editor;
                ResetCountdown();
                Input::SetMouseMode(Input::MouseMode::Normal);
                Sound::StopAllSounds();
                Sound::StopMusic();
                ResetEffects();
                LerpAmount = 1;
                ResetGlobalLighting();
                break;

            case GameState::Automap:
            {
                OpenAutomap();
                break;
            }

            case GameState::Game:
                if (State == GameState::GameMenu) {
                    Input::SetMouseMode(Input::MouseMode::Mouselook);
                }
                else if (State == GameState::Paused) {
                    GetPlayerObject().Render.Type = RenderType::None; // Make player invisible
                    Input::SetMouseMode(Input::MouseMode::Mouselook);
                }
                else if (State == GameState::Automap) {
                    CloseAutomap();
                }
                else {
                    if (!StartLevel()) {
                        RequestedState = State;
                        return;
                    }
                }

                State = RequestedState;
                UpdateWindowTitle();
                break;

            case GameState::ExitSequence:
                UpdateWindowTitle("Escaping the mine!");
                break;

            case GameState::Paused:
                if (State != GameState::Game && State != GameState::ExitSequence) return;
                State = GameState::Paused;
                MoveCameraToObject(Game::MainCamera, GetPlayerObject(), LerpAmount);
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

    void UpdateMenu(float /*dt*/) {
        if (Input::IsKeyPressed(Input::Keys::Down))
            MenuIndex++;

        if (Input::IsKeyPressed(Input::Keys::Up))
            MenuIndex--;

        MenuIndex = Mod(MenuIndex, 2);

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

        auto font = Inferno::Atlas.GetFont(FontSize::MediumBlue);
        if (!font) return;
        auto lineHeight = font->Height * FONT_LINE_SPACING * font->Scale;

        auto scale = Render::Canvas->GetScale();

        //Render::Canvas->DrawRectangle({ 0, 0 }, Render::Canvas->GetSize(), Color(0, 0, 0, 0.25f));

        Vector2 bgSize = Vector2(200, lineHeight * 3.5f) * scale;
        Vector2 alignment = Render::GetAlignment(bgSize, AlignH::Center, AlignV::Center, Render::Canvas->GetSize());
        Render::Canvas->DrawRectangle(alignment, bgSize, Color(0, 0, 0, 0.65f));

        float y = -lineHeight * 0.85f;

        Render::DrawTextInfo info;
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;

        {
            info.Font = MenuIndex == 0 ? FontSize::MediumGold : FontSize::Medium;
            info.Position = Vector2(0, y * scale);
            Render::Canvas->DrawGameText("continue", info);
        }

        y += lineHeight;

        {
            info.Font = MenuIndex == 1 ? FontSize::MediumGold : FontSize::Medium;
            info.Position = Vector2(0, y * scale);
            Render::Canvas->DrawGameText("quit", info);
        }
    }

    // Test code for showing a message from an NPC
    void UpdateCommsMessage() {
        auto scale = Render::Canvas->GetScale();
        constexpr float PORTRAIT_SIZE = 48;
        constexpr float PADDING = 8;

        constexpr float width = 192.0f; // todo: measure longest string
        constexpr float height = 48.0f; // todo: measure string
        constexpr float xOffset = 8.0f;

        {
            Vector2 bgSize = Vector2(width - PADDING, height + PADDING * 2) * scale;
            Vector2 alignment = Render::GetAlignment(bgSize, AlignH::Left, AlignV::CenterTop, Render::Canvas->GetSize());
            alignment.y -= PORTRAIT_SIZE / 2 * scale;
            alignment.x += xOffset * scale;
            Render::Canvas->DrawRectangle(alignment, bgSize, Color(0, 0, 0, 0.65f));

            //auto scale = Render::HudCanvas->GetScale();
            auto& material = Render::Materials->Get("endguy.bbm");
            Inferno::Render::CanvasBitmapInfo info;
            info.Size = Vector2{ PORTRAIT_SIZE, PORTRAIT_SIZE } * scale;
            info.Position = Vector2{ xOffset + PADDING, -PORTRAIT_SIZE / 2 } * scale;
            info.Texture = material.Handle();
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::Center;
            info.Color = Color(1, 1, 1);
            Render::Canvas->DrawBitmap(info);
        }

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::MediumBlue;
            //info.Color = Color(0, 0.7f, 0);
            //info.Color = GREEN_TEXT;
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::Center;
            info.Scanline = 0.5f;
            info.Scale = 0.5f;
            info.Position = Vector2{ xOffset + PORTRAIT_SIZE + PADDING * 2, -PORTRAIT_SIZE / 4 + 3 } * scale;
            Render::Canvas->DrawGameText("dravis", info);
        }

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = Color(0, 0.7f, 0);
            //info.Color = GREEN_TEXT;
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::CenterTop;
            info.Scanline = 0.5f;
            info.Scale = 0.5f;
            info.Position = Vector2{ xOffset + PADDING, PADDING } * scale;
            constexpr float SPACING = 10;
            Render::Canvas->DrawGameText("material defender, the primary reactor is", info);
            info.Position.y += SPACING * scale;
            Render::Canvas->DrawGameText("behind the door to your left.", info);
            info.Position.y += SPACING * scale;
            Render::Canvas->DrawGameText("locate the red key to gain access.", info);
        }
    }

    void DrawBriefing() {
        float scale = 1;
        Inferno::Render::CanvasBitmapInfo info;
        info.Size = Vector2{ 640, 480 } * scale;
        info.Texture = Render::Adapter->BriefingColorBuffer.GetSRV();
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Center;
        info.Color = Color(1, 1, 1);
        Render::Canvas->DrawBitmap(info);
    }

    void Update(float dt) {
        LegitProfiler::ProfilerTask update("Update game", LegitProfiler::Colors::CARROT);
        LegitProfiler::AddCpuTask(std::move(update));

        CheckLoadLevel();

        Game::BriefingVisible = false;
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

        Graphics::BeginFrame(); // enable debug calls during updates

        UpdateGameState();
        g_ImGuiBatch->BeginFrame();

        switch (State) {
            case GameState::MainMenu:
                SetActiveCamera(Game::MainCamera);
                Game::MainCamera.SetFov(50);
                Inferno::UI::Update();

            //Game::MainCamera.Up = Vector3::UnitY;
            //Game::MainCamera.Position = MenuCameraPosition;
            //Game::MainCamera.Target = MenuCameraTarget;
            //Game::MainCamera.UpdatePerspectiveMatrices();

            //Input::SetMouseMode(Input::MouseMode::Mouselook);
            //GenericCameraController(MainCamera, 300);
                break;

            case GameState::Automap:
                SetActiveCamera(Game::AutomapCamera);
                Game::AutomapCamera.SetFov(Settings::Graphics.FieldOfView);

                if (Input::IsKeyPressed(Input::Keys::Tab) || Input::IsKeyPressed(Input::Keys::Escape))
                    Game::SetState(GameState::Game);

                break;

            case GameState::Game:
                LerpAmount = GameUpdate(dt);
            //UpdateCommsMessage();
            //DrawBriefing();
                if (!UpdateEscapeSequence(dt)) {
                    SetActiveCamera(Game::MainCamera);
                    Game::MainCamera.SetFov(Settings::Graphics.FieldOfView);
                }

                if (!Level.Objects.empty()) {
                    if (Player.IsDead)
                        UpdateDeathSequence(dt);
                    else if (!Level.Objects.empty())
                        MoveCameraToObject(Game::MainCamera, Level.Objects[0], LerpAmount);
                }

                if (Input::IsKeyPressed(Input::Keys::Escape)) {
                    Game::SetState(GameState::GameMenu);
                }
                else if (Input::IsKeyPressed(Input::Keys::Tab)) {
                    Game::SetState(GameState::Automap);
                }

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
                if (!UpdateEscapeSequence(dt)) {
                    SetActiveCamera(Editor::EditorCamera);
                    Editor::EditorCamera.SetFov(Settings::Editor.FieldOfView);
                }
                else {
                    UpdateEscapeCamera(dt);
                }

                if (!Settings::Inferno.ScreenshotMode) {
                    if (!EditorUI) EditorUI = make_unique<Inferno::Editor::EditorUI>();
                    EditorUI->OnRender();
                }
                break;

            case GameState::GameMenu:
                UpdateMenu(dt);
                break;

            case GameState::Paused:
                // Special detached camera mode
                if (Input::IsKeyPressed(Input::Keys::OemTilde) && Input::IsKeyDown(Input::Keys::LeftAlt))
                    Game::SetState(Game::GetState() == GameState::Paused ? GameState::Game : GameState::Paused);

                Editor::Bindings::Update(); // Using editor camera bindings
            //Editor::UpdateCamera(Editor::EditorCamera);
                break;
        }

        //LegitProfiler::Profiler.Render();

        // Reset direct light as rendering effects sets it
        Game::Player.DirectLight = Color{};

        g_ImGuiBatch->EndFrame();
        auto& camera = Game::GetActiveCamera();
        camera.UpdatePerspectiveMatrices();
        camera.Update(dt);
        Graphics::SetDebugCamera(camera); // this will lag behind by a frame
        Render::Present(camera);

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

    void LoadHUDTextures() {
        Render::Materials->LoadMaterials(Resources::GameData.HiResGauges, false, true);
        Render::Materials->LoadMaterials(Resources::GameData.Gauges, false, true);
    }

    void PreloadTextures();

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
        Game::Time = Game::FrameTime = 0;

        // Activate game mode
        InitObject(*player, ObjectType::Player);
        Player.Reference = { ObjID(0), player->Signature };
        Player.SpawnPosition = player->Position;
        Player.SpawnRotation = player->Rotation;
        Player.SpawnSegment = player->Segment;
        Player.Stats.HostagesOnLevel = 0;
        Player.Stats.Kills = 0;
        Player.Stats.Robots = 0;
        Player.HostagesOnShip = 0;
        Player.HostagesRescued = 0;
        Player.Stats.TotalHostages = 0; // todo: move this to start mission function
        Player.Lives = PlayerData::INITIAL_LIVES; // todo: move this to start mission function
        player->Faction = Faction::Player;

        State = GameState::Game;

        ResetCountdown();
        StuckObjects = {};
        Sound::WaitInitialized();
        Sound::StopAllSounds();
        Resources::LoadGameTables(Level);
        ResetEffects();
        Render::Materials->UnloadNamedTextures();
        Render::Materials->LoadGameTextures();
        InitObjects(Level);
        InitializeMatcens(Level);
        LoadHUDTextures();
        PreloadTextures();
        PlayLevelMusic();

        Automap.Initialize(Level);
        Editor::SetPlayerStartIDs(Level);
        // Default the gravity direction to the player start
        Gravity = player->Rotation.Up() * -DEFAULT_GRAVITY;

        Navigation = NavigationNetwork(Level);
        Level.Rooms = CreateRooms(Level);
        Level.HasBoss = false;
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
                PlayRobotAnimation(obj, Animation::Rest);
                Player.Stats.Robots++;
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

            if (IsBossRobot(obj))
                Level.HasBoss = true;

            if (obj.Type == ObjectType::Hostage) {
                Player.Stats.HostagesOnLevel++;
                Player.Stats.TotalHostages++;
            }

            FixObjectPosition(obj);
        }

        MarkAmbientSegments(SoundFlag::AmbientLava, TextureFlag::Volatile);
        MarkAmbientSegments(SoundFlag::AmbientWater, TextureFlag::Water);
        AddSoundSources();

        //EditorCameraSnapshot = Render::Camera;
        Settings::Editor.RenderMode = RenderMode::Shaded;
        Input::SetMouseMode(Input::MouseMode::Mouselook);
        Player.Respawn(false);
        ResetDeltaTime = true;
        return true;
    }

    void SetState(GameState state) {
        RequestedState = state;
    }

    GameState GetState() { return State; }
}
