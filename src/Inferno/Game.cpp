#include "pch.h"
#include "Game.h"

#include <numeric>

#include "FileSystem.h"
#include "Graphics/Render.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Input.h"
#include "Graphics/Render.Particles.h"
#include "Physics.h"
#include "Graphics/Render.Debug.h"
#include "imgui_local.h"
#include "Editor/Editor.h"
#include "Editor/UI/EditorUI.h"
#include "Editor/Editor.Object.h"
#include "Game.Input.h"
#include "DebugOverlay.h"
#include "HUD.h"
#include "Game.Wall.h"

using namespace DirectX;

namespace Inferno::Game {
    namespace {
        uint16 ObjSigIndex = 1;
        GameState State = GameState::Editor;
        GameState RequestedState = GameState::Editor;
        Camera EditorCameraSnapshot;
    }

    void StartLevel();

    ObjSig GetObjectSig() {
        return ObjSig(ObjSigIndex++);
    }

    void LoadLevel(Inferno::Level&& level) {
        Inferno::Level backup = Level;

        try {
            assert(level.FileName != "");
            bool reload = level.FileName == Level.FileName;

            Editor::LoadTextureFilter(level);
            bool forceReload =
                level.IsDescent2() != Level.IsDescent2() ||
                Resources::HasCustomTextures() ||
                !String::InvariantEquals(level.Palette, Level.Palette);

            IsLoading = true;

            Level = std::move(level); // Move to global so resource loading works properly
            Resources::LoadLevel(Level);

            ObjSigIndex = 1;
            for (auto& obj : Level.Objects) {
                obj.LastPosition = obj.Position;
                obj.LastRotation = obj.Rotation;
                obj.Signature = GetObjectSig();
            }

            if (forceReload || Resources::HasCustomTextures()) // Check for custom textures before or after load
                Render::Materials->Unload();

            Render::Materials->LoadLevelTextures(Level, forceReload);
            Render::LoadLevel(Level);
            IsLoading = false;

            //Sound::Reset();
            Editor::OnLevelLoad(reload);
            Render::Materials->Prune();
            Render::Adapter->PrintMemoryUsage();

            //List<TexID> ids(Resources::GetTextureCount());
            //for (size_t i = 0; i < ids.size(); i++)
            //    ids[i] = TexID(i);
            //Render::Materials2->LoadMaterials(ids, false);
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

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo() {
        try {
            if (!Mission) return {};
            auto path = Mission->GetMissionPath();
            MissionInfo mission{};
            if (!mission.Read(path)) return {};
            return mission;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
            return {};
        }
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
            Sound3D s(ObjID(0));
            s.Volume = Random() + 0.5f;
            s.Resource = Resources::GetSoundResource(sound);
            s.AttachToSource = true;
            s.FromPlayer = true;
            Sound::Play(s);
        }
    }

    using Keys = Keyboard::Keys;

    void HandleGlobalInput() {
        if (Input::IsKeyPressed(Keys::F1))
            Game::ShowDebugOverlay = !Game::ShowDebugOverlay;

        if (Input::IsKeyPressed(Keys::F2))
            SetState(State == GameState::Game ? GameState::Editor : GameState::Game);

        if (Input::IsKeyPressed(Keys::F3))
            Settings::Inferno.ScreenshotMode = !Settings::Inferno.ScreenshotMode;

        if (Input::IsKeyPressed(Keys::F5))
            Render::Adapter->ReloadResources();

        if (Input::IsKeyPressed(Keys::F6))
            Render::ReloadTextures();

        if (Input::IsKeyPressed(Keys::F7)) {
            Settings::Graphics.HighRes = !Settings::Graphics.HighRes;
            Render::ReloadTextures();
        }
    }

    Object& AllocObject() {
        for (auto& obj : Level.Objects) {
            if (!obj.IsAlive()) {
                obj = {};
                return obj;
            }
        }

        return Level.Objects.emplace_back(Object{});
    }

    // Objects to be added at the end of this tick.
    // Exists to prevent modifying object list size mid-update.
    List<Object> PendingNewObjects;

    void SpawnContained(const ContainsData& contains, const Vector3& position, SegID segment) {
        switch (contains.Type) {
            case ObjectType::Powerup:
            {
                for (int i = 0; i < contains.Count; i++) {
                    Object powerup{};
                    powerup.Position = position;
                    powerup.Type = ObjectType::Powerup;
                    powerup.ID = contains.ID;
                    powerup.Segment = segment;

                    auto vclip = Resources::GameData.Powerups[powerup.ID].VClip;
                    powerup.Render.Type = RenderType::Powerup;
                    powerup.Render.VClip.ID = vclip;
                    powerup.Render.VClip.Frame = 0;
                    powerup.Render.VClip.FrameTime = Resources::GetVideoClip(vclip).FrameTime;
                    powerup.Radius = Resources::GameData.Powerups[powerup.ID].Size;

                    powerup.Movement = MovementType::Physics;
                    powerup.Physics.Velocity = RandomVector(32);
                    powerup.Physics.Mass = 1;
                    powerup.Physics.Drag = 0.01f;
                    powerup.Physics.Flags = PhysicsFlag::Bounce;

                    Render::LoadTextureDynamic(vclip);
                    AddObject(powerup);
                }
                break;
            }
            case ObjectType::Robot:
            {
                // todo: spawn robots
                break;
            }
        }
    }

    void DropContainedItems(const Object& obj) {
        assert(obj.Type == ObjectType::Robot);

        if (obj.Contains.Type != ObjectType::None) {
            SpawnContained(obj.Contains, obj.Position, obj.Segment);
        }
        else {
            auto& ri = Resources::GetRobotInfo(obj.ID);
            if (ri.Contains.Count > 0) {
                if (Random() < (float)ri.ContainsChance / 16.0f) {
                    auto div = (float)ri.Contains.Count / 1.001f; // 1.001f so never exactly equals count
                    auto contains = ri.Contains;
                    contains.Count = (int8)std::floor(Random() * div) + 1;
                    SpawnContained(contains, obj.Position, obj.Segment);
                }
            }
        }
    }

    void AddObject(const Object& obj) {
        PendingNewObjects.push_back(obj);
    }

    void AddPointsToScore(int points) {
        auto score = Player.Score;

        Player.Score += points;
        AddPointsToHUD(points);

        // This doesn't account for negative scoring (which never happens in D2)
        auto lives = Player.Score / EXTRA_LIFE_POINTS - score / EXTRA_LIFE_POINTS;
        if (lives > 0) {
            Player.GiveExtraLife((uint8)lives);
        }
    }

    void UpdateReactorCountdown(float dt) {
        auto fc = std::min(CountdownSeconds, 16);
        auto scale = Difficulty == 0 ? 0.25f : 1; // reduce shaking on trainee

        // Shake the player ship
        Level.Objects[0].Physics.AngularVelocity.x += RandomN11() * (3.0f / 16 + (16 - fc) / 32.0f) * scale;
        Level.Objects[0].Physics.AngularVelocity.z += RandomN11() * (3.0f / 16 + (16 - fc) / 32.0f) * scale;

        auto time = CountdownTimer;
        CountdownTimer -= dt;
        CountdownSeconds = int(CountdownTimer + 7.0f / 8);

        constexpr float COUNTDOWN_VOICE_TIME = 12.75f;
        if (time > COUNTDOWN_VOICE_TIME && CountdownTimer <= COUNTDOWN_VOICE_TIME) {
            Sound::Play(Resources::GetSoundResource(SoundID::Countdown13));
        }

        if (int(time + 7.0f / 8) != CountdownSeconds) {
            if (CountdownSeconds >= 0 && CountdownSeconds < 10)
                Sound::Play(Resources::GetSoundResource(SoundID((int)SoundID::Countdown0 + CountdownSeconds)));
            if (CountdownSeconds == TotalCountdown - 1)
                Sound::Play(Resources::GetSoundResource(SoundID::SelfDestructActivated));
        }

        if (CountdownTimer > 0) {
            auto size = (float)TotalCountdown - CountdownTimer / 0.65f;
            auto oldSize = (float)TotalCountdown - time / 0.65f;
            if (std::floor(size) != std::floor(oldSize) && CountdownSeconds < TotalCountdown - 5)
                Sound::Play(Resources::GetSoundResource(SoundID::Siren)); // play siren every 2 seconds
        }
        else {
            if (time > 0)
                Sound::Play(Resources::GetSoundResource(SoundID::MineBlewUp));

            auto flash = -CountdownTimer / 4.0f; // 4 seconds to fade out
            ScreenFlash = Color{ flash, flash, flash };

            if (CountdownTimer < -4) {
                // todo: kill player, show "you have died in the mine" message
                SetState(GameState::Editor);
            }
        }
    }

    void DestroyReactor(Object& obj) {
        assert(obj.Type == ObjectType::Reactor);

        obj.Render.Model.ID = Resources::GameData.DeadModels[(int)obj.Render.Model.ID];
        Render::LoadModelDynamic(obj.Render.Model.ID);

        AddPointsToScore(REACTOR_SCORE);

        for (auto& tag : Level.ReactorTriggers) {
            if (auto wall = Level.TryGetWall(tag)) {
                if (wall->Type == WallType::Door && wall->State == WallState::Closed)
                    OpenDoor(Level, tag);

                if (wall->Type == WallType::Destroyable)
                    DestroyWall(Level, tag);
            }
        }

        if (Level.BaseReactorCountdown != DEFAULT_REACTOR_COUNTDOWN) {
            TotalCountdown = Level.BaseReactorCountdown + Level.BaseReactorCountdown * (5 - Difficulty - 1) / 2;
        }
        else {
            std::array DefaultCountdownTimes = { 90, 60, 45, 35, 30 };
            TotalCountdown = DefaultCountdownTimes[Difficulty];
        }

        TotalCountdown = 30; // debug
        CountdownTimer = (float)TotalCountdown;
        ControlCenterDestroyed = true;

        {
            Render::SparkEmitter e;
            e.Position = obj.Position;
            e.Segment = obj.Segment;
            e.Duration = { 0.75f, 2.4f };
            e.Restitution = 0.6f;
            e.Velocity = { 65, 85 };
            e.Count = { 120, 120 };
            e.Color = Color{ 4, 3, 3 };
            e.Texture = "Hotspark";
            e.Width = 0.65f;
            Render::AddSparkEmitter(e);
        }

        {
            // Initial explosion
            Render::ExplosionInfo e;
            e.Radius = { obj.Radius * 0.5f, obj.Radius * 0.7f };
            e.Clip = VClipID::SmallExplosion;
            e.Sound = SoundID::Explosion;
            e.Segment = obj.Segment;
            e.Position = obj.Position;
            e.FadeTime = 0.25f;
            e.Instances = 5;
            e.Variance = obj.Radius * 0.9f;
            e.Delay = { 0, 0 };
            e.Color = Color{ 1.3f, 1.3f, 1.3f };

            Render::CreateExplosion(e);
        }

        {
            // Larger periodic explosions with sound
            Render::ExplosionInfo e;
            e.Radius = { 2, 3 };
            e.Clip = VClipID::SmallExplosion;
            e.Sound = SoundID::ExplodingWall;
            e.Volume = 0.5f;
            e.Segment = obj.Segment;
            e.Position = obj.Position;
            e.FadeTime = 0.25f;
            e.Variance = obj.Radius * 0.45f;
            e.Instances = TotalCountdown;
            e.Delay = { 1.25f, 2.00f };
            Render::CreateExplosion(e);
        }

        {
            // Small periodic explosions
            Render::ExplosionInfo e;
            e.Radius = { 0.75f, 1.5f };
            e.Clip = VClipID::SmallExplosion;
            e.Segment = obj.Segment;
            e.Position = obj.Position;
            e.FadeTime = 0.25f;
            e.Variance = obj.Radius * 0.45f;
            e.Instances = TotalCountdown * 4;
            e.Delay = { 0.25f, 0.35f };
            Render::CreateExplosion(e);
        }

        // todo: disable secret portals
        // todo: start countdown
    }

    void DestroyObject(Object& obj) {
        obj.Flags |= ObjectFlag::Destroyed;

        switch (obj.Type) {
            case ObjectType::Reactor:
            {
                DestroyReactor(obj);
                break;
            }

            case ObjectType::Robot:
            {
                constexpr float EXPLOSION_DELAY = 0.2f;

                auto& robot = Resources::GetRobotInfo(obj.ID);

                Render::ExplosionInfo expl;
                expl.Sound = robot.ExplosionSound2;
                expl.Clip = robot.ExplosionClip2;
                expl.Radius = { obj.Radius * 1.75f, obj.Radius * 1.9f };
                expl.Segment = obj.Segment;
                expl.Position = obj.GetPosition(LerpAmount);
                Render::CreateExplosion(expl);

                expl.Sound = SoundID::None;
                expl.InitialDelay = EXPLOSION_DELAY;
                expl.Radius = { obj.Radius * 1.15f, obj.Radius * 1.55f };
                expl.Variance = obj.Radius * 0.5f;
                expl.Instances = 1;
                Render::CreateExplosion(expl);

                AddPointsToScore(robot.Score);

                auto& model = Resources::GetModel(robot.Model);
                for (int i = 0; i < model.Submodels.size(); i++) {
                    // accumulate the offsets for each submodel
                    auto submodelOffset = model.GetSubmodelOffset(i);
                    Matrix transform = Matrix::Lerp(obj.GetLastTransform(), obj.GetTransform(), Game::LerpAmount);
                    //auto transform = obj.GetLastTransform();
                    transform.Forward(-transform.Forward()); // flip z axis to correct for LH models
                    auto world = Matrix::CreateTranslation(submodelOffset) * transform;
                    //world = obj.GetLastTransform();

                    auto explosionVec = world.Translation() - obj.Position;
                    explosionVec.Normalize();

                    auto hitForce = obj.LastHitForce * (1.0f + Random() * 0.5f);

                    Render::Debris debris;
                    //Vector3 vec(Random() + 0.5, Random() + 0.5, Random() + 0.5);
                    //auto vec = RandomVector(obj.Radius * 5);
                    //debris.Velocity = vec + obj.LastHitVelocity / (4 + obj.Movement.Physics.Mass);
                    //debris.Velocity =  RandomVector(obj.Radius * 5);
                    debris.Velocity = i == 0 ? hitForce : explosionVec * 25 + RandomVector(10) + hitForce;
                    debris.Velocity += obj.Physics.Velocity;
                    debris.AngularVelocity = RandomVector(std::min(obj.LastHitForce.Length(), 3.14f));
                    debris.Transform = world;
                    //debris.Transform.Translation(debris.Transform.Translation() + RandomVector(obj.Radius / 2));
                    debris.PrevTransform = world;
                    debris.Mass = 1; // obj.Movement.Physics.Mass;
                    debris.Drag = 0.0075f; // obj.Movement.Physics.Drag;
                    // It looks weird if the main body (sm 0) sticks around too long, so destroy it quicker
                    debris.Life = 0.15f + Random() * (i == 0 ? 0.0f : 1.75f);
                    debris.Radius = model.Submodels[i].Radius;
                    //debris.Model = (ModelID)Resources::GameData.DeadModels[(int)robot.Model];
                    debris.Model = robot.Model;
                    debris.Submodel = i;
                    debris.TexOverride = Resources::LookupTexID(obj.Render.Model.TextureOverride);
                    AddDebris(debris, obj.Segment);
                }

                DropContainedItems(obj);
                obj.Flags |= ObjectFlag::Dead;
                break;
            }

            case ObjectType::Player:
            {
                obj.Flags |= ObjectFlag::Destroyed;
                // Player_ship->expl_vclip_num
                break;
            }

            case ObjectType::Weapon:
            {
                // weapons are destroyed in physics
                break;
            }
        }
    }

    Tuple<ObjID, float> FindNearestObject(const Vector3& position, float maxDist, ObjectMask mask) {
        auto id = ObjID::None;
        //auto& srcObj = Level.GetObject(src);
        float dist = FLT_MAX;

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            if (!obj.PassesMask(mask)) continue;
            if (!obj.IsAlive() /*|| obj.Signature == src.Signature*/) continue;
            auto d = Vector3::Distance(obj.Position, position);
            if (d <= maxDist && d < dist) {
                id = (ObjID)i;
                dist = d;
            }
        }

        return { id, dist };
    }

    void UpdatePlayerFireState(Inferno::Player& player) {
        // must check held keys inside of fixed updates so events aren't missed due to the state changing
        // on a frame that doesn't have a game tick
        if ((Game::State == GameState::Editor && Input::IsKeyDown(Keys::Enter)) ||
            (Game::State != GameState::Editor && Input::Mouse.leftButton == Input::MouseState::HELD)) {
            if (player.PrimaryState == FireState::None)
                player.PrimaryState = FireState::Press;
            else if (player.PrimaryState == FireState::Press)
                player.PrimaryState = FireState::Hold;
        }
        else {
            if (player.PrimaryState == FireState::Release)
                player.PrimaryState = FireState::None;
            else if (player.PrimaryState != FireState::None)
                player.PrimaryState = FireState::Release;
        }

        if ((Game::State != GameState::Editor && Input::Mouse.rightButton == Input::MouseState::HELD)) {
            if (player.SecondaryState == FireState::None)
                player.SecondaryState = FireState::Press;
            else if (player.SecondaryState == FireState::Press)
                player.SecondaryState = FireState::Hold;
        }
        else {
            if (player.SecondaryState == FireState::Release)
                player.SecondaryState = FireState::None;
            else if (player.SecondaryState != FireState::None)
                player.SecondaryState = FireState::Release;
        }
    }

    void AddPendingObjects() {
        for (auto& obj : PendingNewObjects) {
            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;
            obj.Signature = GetObjectSig();

            bool foundExisting = false;
            auto id = ObjID::None;

            for (int i = 0; i < Level.Objects.size(); i++) {
                auto& o = Level.Objects[i];
                if (!o.IsAlive()) {
                    o = obj;
                    foundExisting = true;
                    id = ObjID(i);
                    break;
                }
            }

            if (!foundExisting) {
                id = ObjID(Level.Objects.size());
                Level.Objects.push_back(obj);
            }

            Level.GetSegment(obj.Segment).Objects.push_back(id);

            // Hack to attach tracers due to not having the object ID in firing code
            if (obj.Type == ObjectType::Weapon) {
                //auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);

                constexpr auto TRACER_FADE_SPEED = 0.2f;
                constexpr auto TRACER_LENGTH = 30.0f;

                if ((WeaponID)obj.ID == WeaponID::Vulcan) {
                    Render::TracerInfo tracer;
                    tracer.Parent = id;
                    tracer.Length = TRACER_LENGTH;
                    tracer.Width = 0.30f;
                    tracer.Texture = "vausstracer";
                    tracer.BlobTexture = "Tracerblob";
                    tracer.Color = Color{ 2, 2, 2 };
                    tracer.FadeSpeed = TRACER_FADE_SPEED;
                    Render::AddTracer(tracer, obj.Segment);
                }

                if ((WeaponID)obj.ID == WeaponID::Gauss) {
                    Render::TracerInfo tracer;
                    tracer.Parent = id;
                    tracer.Length = 25.0f;
                    tracer.Width = 0.90f;
                    tracer.Texture = "MassDriverTracer";
                    tracer.BlobTexture = "MassTracerblob";
                    tracer.Color = Color{ 2, 2, 2 };
                    tracer.FadeSpeed = TRACER_FADE_SPEED;
                    Render::AddTracer(tracer, obj.Segment);
                }
            }
        }

        PendingNewObjects.clear();
    }

    // Updates on each game tick
    void FixedUpdate(float dt) {
        UpdatePlayerFireState(Player);
        Player.Update(dt);

        //Render::UpdateExplosions(dt);
        UpdateAmbientSounds();
        UpdateExplodingWalls(Game::Level, dt);
        if (ControlCenterDestroyed)
            UpdateReactorCountdown(dt);
        Render::FixedUpdateEffects(dt);

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];

            if (obj.HitPoints < 0 && obj.Lifespan > 0 && !HasFlag(obj.Flags, ObjectFlag::Destroyed)) {
                DestroyObject(obj);
            }
            else if (obj.Lifespan < 0 && !HasFlag(obj.Flags, ObjectFlag::Dead)) {
                ExplodeWeapon(obj); // explode expired weapons
                obj.Flags |= ObjectFlag::Dead;
            }

            if (HasFlag(obj.Flags, ObjectFlag::Dead)) {
                if (auto seg = Level.TryGetSegment(obj.Segment))
                    Seq::remove(seg->Objects, (ObjID)i);
            }

            if (obj.Type == ObjectType::Weapon)
                UpdateWeapon(obj, dt);
        }

        AddPendingObjects();
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
            auto& physics = Level.Objects[0].Physics; // player
            physics.Thrust = Vector3::Zero;
            physics.AngularThrust = Vector3::Zero;

            if (Game::State == GameState::Editor) {
                if (Settings::Editor.EnablePhysics)
                    HandleEditorDebugInput(dt);
            }
            else if (Game::State == GameState::Game) {
                HandleInput(dt);
            }
        }

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

        while (accumulator >= TICK_RATE) {
            UpdatePhysics(Game::Level, t, TICK_RATE); // catch up if physics falls behind
            FixedUpdate(TICK_RATE);
            accumulator -= TICK_RATE;
            t += TICK_RATE;
            Game::DeltaTime += TICK_RATE;
        }

        if (Game::ShowDebugOverlay) {
            auto vp = ImGui::GetMainViewport();
            constexpr float topOffset = 50;
            DrawDebugOverlay({ vp->Size.x, topOffset }, { 1, 0 });
            DrawGameDebugOverlay({ 10, topOffset }, { 0, 0 });
        }

        return float(accumulator / TICK_RATE);
    }

    Inferno::Editor::EditorUI EditorUI;

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp) {
        Matrix transform = Matrix::Lerp(obj.GetLastTransform(), obj.GetTransform(), lerp);
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
                Render::Camera = EditorCameraSnapshot;
                Input::SetMouselook(false);
                Sound::Reset();
                Render::ResetParticles();
                LerpAmount = 1;
                break;

            case GameState::Game:
                // Activate game mode
                if (!Level.Objects.empty() && Level.Objects[0].Type == ObjectType::Player) {
                    Editor::InitObject(Level, Level.Objects[0], ObjectType::Player);
                }
                else {
                    SPDLOG_ERROR("No player start at object 0!");
                    return; // no player start!
                }

                Editor::History.SnapshotLevel("Playtest");
                State = GameState::Game;

                StartLevel();
                break;

            case GameState::ExitSequence:
                //RequestedState = GameState::Editor;
                break;

            case GameState::Paused:
                break;
        }

        State = RequestedState;
    }

    void Update(float dt) {
        Inferno::Input::Update();
        HandleGlobalInput();
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

        g_ImGuiBatch->EndFrame();
        Render::Present();
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

                Sound3D s(side.Center - side.AverageNormal * 10, segid);
                s.Looped = true;
                s.Radius = 150;
                s.Resource = Resources::GetSoundResource(sound);
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
        //Set<TexID> ids;

        //for (auto& vclip : Resources::GameData.VClips) {
        //    Seq::insert(ids, vclip.GetFrames());
        //}

        //for (auto& eclip : Resources::GameData.Effects) {
        //    Seq::insert(ids, eclip.VClip.GetFrames());
        //    ids.insert(Resources::LookupTexID(eclip.DestroyedTexture));
        //}

        //Seq::insert(ids, Resources::GameData.HiResGauges);
        //Render::Materials->LoadMaterials(Seq::ofSet(ids), false);

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
            "SmHilite"
        };

        Render::Materials->LoadTextures(customHudTextures);
    }

    void StartLevel() {
        Editor::SetPlayerStartIDs(Level);
        Gravity = Level.Objects[0].Rotation.Up() * -200;

        Render::InitEffects(Level);

        for (auto& seg : Level.Segments) {
            seg.Objects.clear();
        }

        // init objects
        for (int id = 0; id < Level.Objects.size(); id++) {
            auto& obj = Level.Objects[id];
            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;
            obj.Signature = GetObjectSig();

            if ((obj.Type == ObjectType::Player && obj.ID != 0) || obj.Type == ObjectType::Coop)
                obj.Lifespan = -1; // Remove non-player 0 starts (no multiplayer)

            if (obj.Type == ObjectType::Robot) {
                auto& ri = Resources::GetRobotInfo(obj.ID);
                obj.HitPoints = ri.HitPoints;
                obj.Physics.Flags |= PhysicsFlag::Bounce;
            }

            if (obj.Type == ObjectType::Powerup &&
                (obj.ID == (int)PowerupID::Gauss || obj.ID == (int)PowerupID::Vulcan)) {
                obj.Control.Powerup.Count = 2500;
            }

            if (obj.Type == ObjectType::Powerup &&
                (obj.ID == (int)PowerupID::FlagBlue || obj.ID == (int)PowerupID::FlagRed)) {
                obj.Lifespan = -1; // Remove CTF flags (no multiplayer)
            }

            Editor::UpdateObjectSegment(Level, obj);
            if (auto seg = Level.TryGetSegment(obj.Segment)) {
                seg->Objects.push_back((ObjID)id);
            }
        }

        ControlCenterDestroyed = false;
        TotalCountdown = CountdownSeconds = -1;
        CountdownTimer = -1.0f;

        StuckObjects = {};
        Render::ResetParticles();
        Sound::Reset();
        MarkAmbientSegments(SoundFlag::AmbientLava, TextureFlag::Volatile);
        MarkAmbientSegments(SoundFlag::AmbientWater, TextureFlag::Water);
        AddSoundSources();

        EditorCameraSnapshot = Render::Camera;
        Settings::Editor.RenderMode = RenderMode::Shaded;
        Input::SetMouselook(true);
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
        Player.Energy = 10;

        // Max vulcan ammo changes between D1 and D2
        PyroGX.Weapons[(int)PrimaryWeaponIndex::Vulcan].MaxAmmo = Level.IsDescent1() ? 10000 : 20000;

        Player.PrimaryWeapons = 0xffff;
        Player.SecondaryWeapons = 0xffff;
        std::ranges::generate(Player.SecondaryAmmo, [] { return 5; });
        std::ranges::generate(Player.PrimaryAmmo, [] { return 5000; });
    }

    void SetState(GameState state) {
        RequestedState = state;

    }

    GameState GetState() { return State; }

}
