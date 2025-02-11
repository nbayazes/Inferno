#include "pch.h"
#define NOMINMAX
#include <numeric>
#include <gsl/pointers.h>
#include "Game.h"
#include "DebugOverlay.h"
#include "Editor/Editor.h"
#include "Editor/Editor.Object.h"
#include "Editor/UI/EditorUI.h"
#include "VisualEffects.h"
#include "Game.AI.h"
#include "Game.Automap.h"
#include "Game.Cinematics.h"
#include "Game.Input.h"
#include "Game.Object.h"
#include "Game.Reactor.h"
#include "Game.Room.h"
#include "Game.Segment.h"
#include "Game.UI.h"
#include "Game.UI.ScoreScreen.h"
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
        std::atomic State = GameState::Startup;
        std::atomic RequestedState = GameState::Startup;
        constexpr size_t OBJECT_BUFFER_SIZE = 100; // How many new objects to keep in reserve
        Ptr<Editor::EditorUI> EditorUI;
        auto ActiveCamera = gsl::strict_not_null(&MainCamera);
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

    uint8 AddPointsToScore(int points) {
        auto score = Player.Score;

        Player.Score += points;
        AddPointsToHUD(points);

        // This doesn't account for negative scoring (which never happens in D2)
        auto lives = Player.Score / EXTRA_LIFE_POINTS - score / EXTRA_LIFE_POINTS;
        if (lives < 0) lives = 0;

        if (lives > 0) {
            Sound::Play2D({ SoundID::ExtraLife });
            Player.GiveExtraLife((uint8)lives);
        }

        return (uint8)lives;
    }

    Object* GetObject(ObjRef ref) {
        if (!Seq::inRange(Level.Objects, (int)ref.Id)) return nullptr;
        auto& obj = Level.Objects[(int)ref.Id];
        if (obj.Signature != ref.Signature) return nullptr;
        return &obj;
    }

    void PlayMainMenuMusic() {
        Game::PlayMusic("d1/descent");
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
        return obj.Type == ObjectType::Weapon || HasFlag(obj.Flags, ObjectFlag::AlwaysUpdate) || IsAnimating(obj);
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

        HandleFixedUpdateInput(dt);
        Player.Update(dt);
        BeginAIFrame();

        UpdateAmbientSounds();
        UpdateDoors(Level, dt);
        UpdateMatcens(Game::Level, dt);

        Sound::UpdateSoundEmitters(dt);
        UpdateExplodingWalls(Game::Level, dt);

        if (ControlCenterDestroyed && State == GameState::Game)
            UpdateReactorCountdown(dt);

        FixedUpdateEffects(dt);

        if (State == GameState::ExitSequence)
            UpdateEscapeSequence(dt);

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
        if (Difficulty < DifficultyLevel::Hotshot) flags |= TraversalFlag::StopDoor;

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
        if (Game::State == GameState::PhotoMode)
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
        Game::ScreenGlow.Update(Game::Time);
        Game::FusionTint.Update(Game::Time);
        Game::BloomStrength.Update(dt);
        Game::Exposure.Update(dt);
        DecayScreenFlash(dt);
        Graphics::UpdateTimers();

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
                    Graphics::NotifyLevelChanged(); // Need to update textures on mesh. This is not ideal.
                }
            }
        }

        if (State == GameState::Game || State == GameState::ExitSequence)
            TraverseSegments(GetActiveCamera(), GetPlayerObject().Segment, TraversalFlag::None);

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

    bool IsFinalLevel() {
        auto info = GetMissionInfo();
        if (info.Levels.empty()) return true; // standalone level;
        return info.Levels.size() == LevelNumber;
    }

    UI::ScoreInfo CalculateEndLevelScore(int /*levelNumber*/, uint totalHostages) {
        //if (level.Version != 0) {
        //    return {
        //        .LevelName = "PLACEHOLDER LEVEL",
        //        .LevelNumber = 1,
        //        .Difficulty = Game::Difficulty,
        //        .Time = "0:00",
        //        .Secrets = 3,
        //        .SecretsFound = 1,
        //        .RobotsDestroyed = 10,
        //        .ShieldBonus = 1000,
        //        .EnergyBonus = 1000,
        //        .HostageBonus = 1000,
        //        .FullRescue = true,
        //        .SkillBonus = 0,
        //        .TotalBonus = 3000,
        //        .TotalScore = 53000,
        //        .ExtraLives = 1
        //    };
        //}

        // D2 fix for secret levels having negative numbers
        //if (levelNumber < 0)
        //    levelNumber *= -(levelCount / secretLevelCount);

        auto finalLevel = IsFinalLevel();
        auto& player = Game::Player;
        auto difficulty = (int)Game::Difficulty;
        auto levelPoints = player.Score - player.LevelStartScore;

        UI::ScoreInfo score;
        score.FinalLevel = finalLevel;

        if (!Game::Cheater) {
            if (difficulty > 1) {
                if (Game::Level.IsDescent1())
                    score.SkillBonus = levelPoints * (difficulty - 1) / 2; // D1 (0.5 to 1.5x)
                else
                    score.SkillBonus = levelPoints * difficulty / 4; // D2 (0.5 to 1x)

                score.SkillBonus -= score.SkillBonus % 100; // round
            }

            // D2 uses level number for shield and energy bonus, D1 uses difficulty level
            //score.ShieldBonus = (int)player.Shields * 5 * levelNumber;
            //score.EnergyBonus = (int)player.Energy * 2 * levelNumber;
            score.ShieldBonus = (int)player.Shields * 10 * (difficulty + 1);

            // Remove energy bonus, it's kind of lame and rewards guass / backtracking to an energy center
            //score.EnergyBonus = (int)player.Energy * 5 * (difficulty + 1);
            //score.EnergyBonus = std::max((int)player.Energy - 100, 0) * 10 * (difficulty + 1);
            score.HostageBonus = player.HostagesOnboard * 500 * (difficulty + 1);

            score.ShieldBonus -= score.ShieldBonus % 50;
            score.EnergyBonus -= score.EnergyBonus % 50;

            if (player.HostagesOnboard == totalHostages) {
                score.HostageBonus += player.HostagesOnboard * 1000 * (difficulty + 1);

                score.FullRescue = true;
            }

            // Convert extra lives to points on the final level
            if (finalLevel) {
                score.ShipBonus = player.Lives * 10000;
            }
        }

        score.Difficulty = Game::Difficulty;
        score.TotalBonus = score.SkillBonus + score.EnergyBonus + score.ShieldBonus + score.HostageBonus + score.ShipBonus;
        score.Deaths = Game::LevelDeaths;
        score.RobotsDestroyed = Game::Player.Stats.Kills;

        string time = "0:00";
        int minutes = (int)Time / 60;
        int seconds = (int)Time % 60;
        score.Time = fmt::format("{}:{:02}", minutes, seconds);

        score.ExtraLives = Game::AddPointsToScore(score.TotalBonus);
        // don't show extra lives on the final level (they were just removed for bonus points)
        if (finalLevel) score.ExtraLives = 0;
        score.TotalScore = Game::Player.Score;
        score.LevelNumber = Game::LevelNumber;
        score.LevelName = Game::Level.Name;
        return score;
    }

    // Changes the game state if a new one is requested
    void CheckGameStateChange() {
        if (State == RequestedState) return;
        Input::ResetState(); // Clear input when switching game states

        switch (RequestedState) {
            case GameState::MainMenu:
            {
                Sound::StopAllSounds();
                Game::Level = {};
                Game::MainCamera.Up = Vector3::UnitY;
                Game::MainCamera.Position = MenuCameraPosition;
                Game::MainCamera.Target = MenuCameraTarget;
                Sound::SetMusicVolume(Settings::Inferno.MusicVolume);
                Input::SetMouseMode(Input::MouseMode::Normal);
                Game::ScreenGlow.SetTarget(Color(0, 0, 0, 0), Game::Time, 0);
                PlayMainMenuMusic();
                UI::ShowMainMenu();

                break;
            }

            case GameState::Briefing:
            {
                Input::SetMouseMode(Input::MouseMode::Normal);
                if (!Game::Briefing.IsValid()) return;
                break;
            }

            case GameState::LoadLevel:
                Input::SetMouseMode(Input::MouseMode::Mouselook);
                break;

            case GameState::ScoreScreen:
            {
                auto score = CalculateEndLevelScore(LevelNumber, Level.TotalHostages);
                UI::ShowScoreScreen(score, LoadSecretLevel);
                Input::SetMouseMode(Input::MouseMode::Normal);
                break;
            }

            case GameState::FailedEscape:
                Game::FailedEscape = true;
                if (!Game::Player.IsDead)
                    Game::Player.LoseLife(); // Lose a life if timed out (instead of dying)

                UI::ShowFailedEscapeDialog(Game::Player.Lives == 0);
                Input::SetMouseMode(Input::MouseMode::Normal);
                break;

            case GameState::Editor:
                if (Level.Version == 0) {
                    // Null file
                    //Shell::UpdateWindowTitle("Loading editor");
                    Editor::OpenRecentOrEmpty();
                }

                if (State == GameState::ExitSequence) {
                    Settings::Editor.ShowTerrain = false;
                }

                Editor::History.Undo();
                ResetCountdown();
                Input::SetMouseMode(Input::MouseMode::Normal);
                Sound::StopAllSounds();
                Sound::StopMusic();
                ResetEffects();
                LerpAmount = 1;
                ResetGlobalLighting();
                Game::ScreenGlow.SetTarget(Color(0, 0, 0, 0), Game::Time, 0);
                break;

            case GameState::Automap:
                Sound::SetMusicVolume(Settings::Inferno.MusicVolume * 0.33f);
                Sound::PauseSounds();
                OpenAutomap();
                break;

            case GameState::Game:
                Input::ResetState(); // Reset so clicking a menu doesn't fire
                Sound::ResumeSounds();
                Settings::Editor.ShowTerrain = false;

                if (State == GameState::Briefing) {
                    //Game::CheckLoadLevel();
                }
                if (State == GameState::PauseMenu) {
                    Input::SetMouseMode(Input::MouseMode::Mouselook);
                }
                else if (State == GameState::PhotoMode) {
                    GetPlayerObject().Render.Type = RenderType::None; // Make player invisible
                    Input::SetMouseMode(Input::MouseMode::Mouselook);
                }
                else if (State == GameState::Automap) {
                    CloseAutomap();
                }

                Sound::SetMusicVolume(Settings::Inferno.MusicVolume);
                break;

            case GameState::ExitSequence:
                // Turn off the player headlight during exit sequences because 3D art is missing
                Player.TurnOffHeadlight();
                break;

            case GameState::PhotoMode:
                if (State != GameState::Game && State != GameState::ExitSequence) return;
                MoveCameraToObject(Game::MainCamera, GetPlayerObject(), LerpAmount);
                GetPlayerObject().Render.Type = RenderType::Model; // Make player visible
                Input::SetMouseMode(Input::MouseMode::Mouselook);
                break;

            case GameState::PauseMenu:
                if (State == GameState::PauseMenu) return;
                Sound::PauseSounds();
                Sound::SetMusicVolume(Settings::Inferno.MusicVolume * 0.33f);
                Input::SetMouseMode(Input::MouseMode::Normal);
                Input::ResetState();
                UI::ShowPauseDialog();
                break;
        }

        State.store(RequestedState);
        Shell::UpdateWindowTitle();
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
        float scale = Render::Canvas->GetScale();
        Inferno::Render::CanvasBitmapInfo info;
        info.Size = Vector2{ 640, 480 } * scale;
        info.Texture = Render::Adapter->BriefingColorBuffer.GetSRV();
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Center;
        info.Color = Color(1, 1, 1);
        Render::Canvas->DrawBitmap(info);
    }

    void Update(float dt) {
        Game::FrameTime = 0;

        // Stop time when not in game or in editor. Editor uses gametime to animate vclips.
        if (Game::State == GameState::Game || Game::State == GameState::Editor || Game::State == GameState::ExitSequence) {
            Game::Time += dt * Game::TimeScale;
            Game::FrameTime = dt * Game::TimeScale;
        }

        LegitProfiler::ProfilerTask update("Update game", LegitProfiler::Colors::CARROT);
        LegitProfiler::AddCpuTask(std::move(update));

        if (State == GameState::Game || State == GameState::Editor)
            CheckLoadLevel();

        Game::BriefingVisible = false;
        Input::Update(dt);
        CheckGlobalHotkeys();

        if (Game::State == GameState::Editor) {
            if (Settings::Editor.EnablePhysics)
                HandleEditorDebugInput(dt);
        }
        else {
            HandleInput(dt);
        }

        Graphics::BeginFrame(); // enable debug calls during updates

        CheckGameStateChange();
        g_ImGuiBatch->BeginFrame();

        // Reset direct lighting on sprites. Light effects accumulate on nearby objects each frame.
        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            if (obj.Render.Type == RenderType::Hostage || obj.Render.Type == RenderType::Powerup) {
                obj.Render.VClip.DirectLight = Color();
            }
        }

        // Reset direct light on player. Light effects set it.
        Game::Player.DirectLight = Color{};

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

            case GameState::FailedEscape:
                Inferno::UI::Update();
                break;

            case GameState::ScoreScreen:
                Inferno::UI::Update();
                break;

            case GameState::Briefing:
                Game::BriefingVisible = true;
                Game::Briefing.Update(dt);
                DrawBriefing();
                break;

            case GameState::LoadLevel:
            {
                auto scale = Render::Canvas->GetScale();

                //Vector2 bgSize = Vector2(200, lineHeight * 3.5f) * scale;
                Render::Canvas->DrawRectangle({ 0, 0 }, Render::Canvas->GetSize(), Color(0, 0, 0, 1));

                constexpr auto text = "prepare for descent";
                const auto size = MeasureString(text, FontSize::Medium);

                {
                    auto borderSize = (size + Vector2(41, 41)) * scale;
                    Vector2 alignment = Render::GetAlignment(borderSize, AlignH::Center, AlignV::Center, Render::Canvas->GetSize());
                    Render::Canvas->DrawRectangle(alignment, borderSize, Color(0.25f, 0.25f, 0.25f, 1));
                }

                {
                    auto fillSize = (size + Vector2(40, 40)) * scale;
                    Vector2 alignment = Render::GetAlignment(fillSize, AlignH::Center, AlignV::Center, Render::Canvas->GetSize());
                    Render::Canvas->DrawRectangle(alignment, fillSize, Color(0.1f, 0.1f, 0.1f, 1));
                }
                Render::DrawTextInfo info;
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::Center;
                info.Font = FontSize::Medium;
                info.Position.y = 3;
                Render::Canvas->DrawGameText(text, info);
                break;
            }

            case GameState::Automap:
                SetActiveCamera(Game::AutomapCamera);
                Game::AutomapCamera.SetFov(Settings::Graphics.FieldOfView);

                if (Input::IsKeyPressed(Input::Keys::Tab) || Input::IsKeyPressed(Input::Keys::Escape))
                    Game::SetState(GameState::Game);

                break;

            case GameState::Game:
                LerpAmount = GameUpdate(dt);
            //UpdateCommsMessage();
                SetActiveCamera(Game::MainCamera);
                Game::MainCamera.SetFov(Settings::Graphics.FieldOfView);

                if (!Level.Objects.empty()) {
                    if (Player.IsDead)
                        UpdateDeathSequence(dt);
                    else if (!Level.Objects.empty())
                        MoveCameraToObject(Game::MainCamera, Level.Objects[0], LerpAmount);
                }

                break;

            case GameState::Cutscene:
                LerpAmount = GameUpdate(dt);
                break;

            case GameState::ExitSequence:
                LerpAmount = GameUpdate(dt);
                UpdateEscapeCamera(dt);
                MoveCameraToObject(Game::MainCamera, Level.Objects[0], LerpAmount);
                Game::ScreenGlow.SetTarget(Color(0, 0, 0, 0), Game::Time, 0.5f);
                break;

            case GameState::Editor:
                if (Settings::Editor.EnablePhysics) {
                    LerpAmount = Settings::Editor.EnablePhysics ? GameUpdate(dt) : 1;
                }
                else {
                    LerpAmount = 1;
                }

                Editor::Update();
                SetActiveCamera(Editor::EditorCamera);
                Editor::EditorCamera.SetFov(Settings::Editor.FieldOfView);

                if (!Settings::Inferno.ScreenshotMode) {
                    if (!EditorUI) EditorUI = make_unique<Inferno::Editor::EditorUI>();
                    EditorUI->OnRender();
                }
                break;

            case GameState::PauseMenu:
                UI::Update();
                break;

            case GameState::PhotoMode:
                break;
        }

        //LegitProfiler::Profiler.Render();

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

        //Input::NextFrame(dt);

        if (State == GameState::LoadLevel) {
            Game::CheckLoadLevel(); // block until done loading

            if (!StartLevel()) {
                // something went wrong loading level
                // todo: go back to editor if started from there
                SetState(GameState::MainMenu);
            }
            else {
                SetState(GameState::Game);
            }
        }
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

    // Adds sound sources from eclips such as fans, lava and forcefields
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
                s.Radius = 160;
                s.Volume = 0.60f;
                //s.Occlusion = false;
                // Offset so raycasts don't get stuck inside wall
                Sound::Play(s, side.Center + side.AverageNormal, segid, sid);
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

    bool CheckForPlayerStart(Inferno::Level& level) {
        auto player = level.TryGetObject(ObjID(0));

        if (!player || !player->IsPlayer()) {
            ShowErrorMessage("No player start at object 0!", "Unable to load level");
            return false;
        }

        return true;
    }

    void ShowBriefing(const MissionInfo& mission, int levelNumber, const Inferno::Level& level, string briefingName, bool endgame) {
        if (!Game::Mission)
            return;

        if (String::Extension(briefingName).empty())
            briefingName += ".txb";

        auto entry = Game::Mission->TryReadEntry(briefingName);
        auto briefing = Briefing::Read(entry, level.IsDescent1());

        auto isShareware = Game::Mission->ContainsFileType(".sdl");

        if (endgame) {
            SetD1EndBriefingBackground(briefing, isShareware);
        }
        else {
            SetD1BriefingBackgrounds(briefing, isShareware);
        }

        if (mission.Name == FIRST_STRIKE_NAME && levelNumber == 1) {
            AddPyroAndReactorPages(briefing);
        }

        Game::Briefing = BriefingState(briefing, levelNumber, level.IsDescent1(), endgame);
        Game::Level.Version = level.Version; // hack: due to LoadResources
        Game::Briefing.LoadResources(); // TODO: Load resources depends on the level being fully loaded to pick the right assets!

        auto music = IsLastLevel() ? "d1/endgame" : "d1/briefing";
        Game::PlayMusic(music);
        Game::SetState(GameState::Briefing);
    }

    void LoadLevelFromMission(const MissionInfo& mission, int levelNumber) {
        try {
            if (levelNumber == 0)
                levelNumber = 1;

            if (levelNumber < 0) {
                if (abs(levelNumber) > mission.SecretLevels.size()) {
                    ShowErrorMessage(std::format("Secret level number {} not found", levelNumber));
                    return;
                }
            }
            else if (levelNumber > mission.Levels.size()) {
                ShowErrorMessage(std::format("Level number {} not found", levelNumber));
                return;
            }

            // load next level or exit to main menu
            filesystem::path hogPath = mission.Path;
            hogPath.replace_extension(".hog");

            if (!Game::LoadMission(hogPath)) {
                ShowErrorMessage(std::format("Unable to load mission {}", hogPath.string()));
                return;
            }

            auto isShareware = Game::Mission->ContainsFileType(".sdl");

            string levelEntry;

            if (levelNumber < 0) {
                levelEntry = mission.SecretLevels.at(abs(levelNumber) - 1);
                auto index = levelEntry.find_first_of(',');
                if (index > 0)
                    levelEntry = levelEntry.substr(0, index);
            }
            else {
                levelEntry = mission.Levels.at(levelNumber - 1);
            }

            if (!levelEntry.empty()) {
                auto data = Game::Mission->ReadEntry(levelEntry);
                auto level = isShareware ? Level::DeserializeD1Demo(data) : Level::Deserialize(data);
                //Game::LoadLevelFromMission(_mission->Levels[_level]);
                Resources::LoadLevel(level);
                Graphics::LoadLevel(level);
                Game::LoadLevel(hogPath, levelEntry);
                Render::MaterialsChanged = true;

                auto briefingName = mission.GetValue("briefing");

                if (!briefingName.empty()) {
                    ShowBriefing(mission, levelNumber, level, briefingName, false);
                }
                else {
                    Game::SetState(GameState::LoadLevel);
                }
            }
        }
        catch (const std::exception& e) {
            ShowErrorMessage(std::format("Unable to load mission {}\n{}", mission.Path.string(), e.what()));

            Game::SetState(GameState::MainMenu);
        }
    }

    int GetNextLevel(MissionInfo& mission, int levelNumber) {
        if (LoadSecretLevel) {
            LoadSecretLevel = false;

            if (auto secretLevel = mission.FindSecretLevel(levelNumber)) {
                return *secretLevel;
            }
            // Fallback to regular loading if the secret level isn't found
        }

        // check if current level is a secret level, and if it is, load the level after it
        if (levelNumber < 0) {
            if (auto level = Seq::tryItem(mission.SecretLevels, abs(levelNumber) - 1)) {
                auto tokens = String::Split(*level, ',');
                if (tokens.size() == 2) {
                    levelNumber = std::stoi(tokens.at(1));
                }
            }
        }

        return levelNumber + 1;
    }

    void LoadNextLevel() {
        auto mission = GetMissionInfo();

        if (mission.Levels.empty()) {
            Game::SetState(GameState::MainMenu);
            return;
        }

        auto levelNumber = Game::GetNextLevel(mission, Game::LevelNumber);

        if (IsFinalLevel()) {
            SPDLOG_WARN("Called LoadNextLevel() on final level");
            SetState(GameState::MainMenu);
        }
        else {
            LoadLevelFromMission(mission, levelNumber);
        }
    }

    void StartMission() {
        Player = {};
    }

    bool StartLevel() {
        SPDLOG_INFO("Starting level");
        Editor::SetPlayerStartIDs(Level);

        if (!CheckForPlayerStart(Level))
            return false;

        auto& player = GetPlayerObject();

        // todo: disable this when not launched from the editor
        if (Input::ControlDown && Level.SegmentExists(Editor::Selection.Segment)) {
            // Move player to selected segment if control is held down
            Editor::Selection.Object = ObjID(0);
            Editor::Commands::MoveObjectToSegment();
        }
        else {
            Editor::History.SnapshotLevel("Playtest");
        }

        // Reset level timing
        Game::Time = Game::FrameTime = 0;
        Settings::Editor.ShowTerrain = false;
        Game::ScreenGlow.SetTarget(Color(0, 0, 0, 0), Game::Time, 0);
        Game::FailedEscape = false;
        Game::LevelDeaths = 0;

        // Activate game mode
        State = GameState::Game;

        // Reset resources
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

        Automap = AutomapInfo(Level);

        // Reset player state
        InitObject(player, ObjectType::Player);

        Player.Reference = { ObjID(0), player.Signature };
        Player.SpawnPosition = player.Position;
        Player.SpawnRotation = player.Rotation;
        Player.SpawnSegment = player.Segment;
        Player.Stats.HostagesOnLevel = 0;
        Player.Stats.TotalKills += Player.Stats.Kills;
        Player.Stats.Kills = 0;
        Player.Stats.Robots = 0;
        Player.HostagesOnboard = 0;
        Player.HostagesRescued = 0;

        Player.LevelStartScore = Player.Score;
        Player.TurnOffHeadlight(false);

        // Default the gravity direction to the player start
        Gravity = player.Rotation.Up() * -DEFAULT_GRAVITY;
        Game::Player.Ship = Resources::GameData.PlayerShip;

        // Copy settings into player object
        player.Faction = Faction::Player;
        player.Physics.TurnRollRate = Game::Player.Ship.TurnRollRate;
        player.Physics.TurnRollScale = Game::Player.Ship.TurnRollScale;

        Navigation = NavigationNetwork(Level);
        Level.Rooms = CreateRooms(Level);
        Level.HasBoss = false;
        Graphics::NotifyLevelChanged(); // regenerate level meshes

        // init objects
        for (int id = 0; id < Level.Objects.size(); id++) {
            auto& obj = Level.Objects[id];

            if ((obj.IsPlayer() && id != 0) || obj.IsCoop()) {
                obj.Lifespan = -1; // Remove non-player 0 starts (no multiplayer)
                obj.Render.Type = RenderType::None; // Make invisible
            }

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
        ResetGameTime = true; // Reset game time so objects don't move before fully loaded
        return true;
    }

    void SetState(GameState state) {
        RequestedState = state;
    }

    GameState GetState() { return State; }
}
