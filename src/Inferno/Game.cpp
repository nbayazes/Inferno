#include "pch.h"
#include "Game.h"
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
#include "Game.Input.h"

using namespace DirectX;

namespace Inferno::Game {
    void LoadLevel(Inferno::Level&& level) {
        Inferno::Level backup = Level;

        try {
            assert(level.FileName != "");
            bool reload = level.FileName == Level.FileName;

            bool forceReload =
                level.IsDescent2() != Level.IsDescent2() ||
                Resources::HasCustomTextures() ||
                !String::InvariantEquals(level.Palette, Level.Palette);

            IsLoading = true;

            Level = std::move(level); // Move to global so resource loading works properly
            Resources::LoadLevel(Level);

            if (forceReload || Resources::HasCustomTextures()) // Check for custom textures before or after load
                Render::Materials->Unload();

            Render::Materials->LoadLevelTextures(Level, forceReload);
            Render::LoadLevel(Level);
            IsLoading = false;

            //Sound::Reset();
            Editor::OnLevelLoad(reload);
            Render::Materials->Prune();
            Render::Adapter->PrintMemoryUsage();
        }
        catch (const std::exception&) {
            Level = backup; // restore the old level if something went wrong
            throw;
        }
    }

    void LoadMission(filesystem::path file) {
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


    using Keys = Keyboard::Keys;

    void HandleGlobalInput() {
        if (Input::IsKeyPressed(Keys::F1))
            Editor::ShowDebugOverlay = !Editor::ShowDebugOverlay;

        if (Input::IsKeyPressed(Keys::F2))
            Game::ToggleEditorMode();

        if (Input::IsKeyPressed(Keys::F3))
            Settings::ScreenshotMode = !Settings::ScreenshotMode;

        if (Input::IsKeyPressed(Keys::F5))
            Render::Adapter->ReloadResources();

        if (Input::IsKeyPressed(Keys::F6))
            Render::ReloadTextures();

        if (Input::IsKeyPressed(Keys::F7)) {
            Settings::HighRes = !Settings::HighRes;
            Render::ReloadTextures();
        }
    }

    void FireTestWeapon(Inferno::Level& level, ObjID objId, int gun, int id) {
        auto& obj = level.Objects[(int)objId];
        //auto& guns = Resources::GameData.PlayerShip.GunPoints;
        auto point = Vector3::Transform(Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1), obj.GetTransform());
        auto& weapon = Resources::GameData.Weapons[id];

        Object bullet{};
        bullet.Movement.Type = MovementType::Physics;
        bullet.Movement.Physics.Velocity = obj.Rotation.Forward() * weapon.Speed[0] * 1;
        bullet.Movement.Physics.Flags = weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
        bullet.Movement.Physics.Drag = weapon.Drag;
        bullet.Movement.Physics.Mass = weapon.Mass;
        bullet.Position = bullet.LastPosition = point;
        bullet.Rotation = bullet.LastRotation = obj.Rotation;

        if (weapon.RenderType == WeaponRenderType::Blob || weapon.RenderType == WeaponRenderType::VClip) {
            bullet.Render.Type = RenderType::WeaponVClip;
            bullet.Render.VClip.ID = weapon.WeaponVClip;
            bullet.Render.VClip.Rotation = Random() * DirectX::XM_2PI;
            Render::LoadTextureDynamic(weapon.WeaponVClip);
        }
        else if (weapon.RenderType == WeaponRenderType::Model) {
            bullet.Render.Type = RenderType::Model;
            bullet.Render.Model.ID = weapon.Model;
            auto& model = Resources::GetModel(weapon.Model);
            bullet.Radius = model.Radius / weapon.ModelSizeRatio;
            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
        }

        bullet.Lifespan = weapon.Lifetime;

        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = ObjID(0);
        bullet.Render.Emissive = { 0.8f, 0.4f, 0.1f }; // laser level 5

        //auto pitch = -Random() * 0.2f;
        Sound::Sound3D sound(point, obj.Segment);
        sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
        sound.Source = ObjID(0);
        sound.Volume = 0.35f;
        Sound::Play(sound);


        Render::Particle p{};
        p.Clip = weapon.FlashVClip;
        p.Position = point;
        p.Radius = weapon.FlashSize;
        Render::AddParticle(p);

        for (auto& o : level.Objects) {
            if (o.Lifespan <= 0) {
                o = bullet;
                return; // found a dead object to reuse!
            }
        }

        level.Objects.push_back(bullet); // insert a new object
    }

    // Returns the lerp amount for the current tick
    float GameTick(float dt) {
        constexpr double tickRate = 1.0f / 64; // 64 ticks per second (homing missiles use 32 ticks per second)
        static double accumulator = 0;
        static double t = 0;

        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        //float lerp = 1; // blending between previous and current position

        while (accumulator >= tickRate) {
            if(!Level.Objects.empty())
                HandleInput(Level.Objects[0], tickRate);
            UpdatePhysics(Game::Level, t, tickRate); // catch up if physics falls behind
            accumulator -= tickRate;
            t += tickRate;
        }

        //lerp = float(accumulator / tickRate);
        //Render::Present(lerp);
        return float(accumulator / tickRate);
    }

    Inferno::Editor::EditorUI EditorUI;

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp) {
        Matrix transform = Matrix::Lerp(obj.GetLastTransform(), obj.GetTransform(), lerp);
        camera.Position = transform.Translation();
        camera.Target = camera.Position + transform.Forward();
        camera.Up = transform.Up();
    }

    void Update(float dt) {
        Inferno::Input::Update();
        HandleGlobalInput();
        Render::Debug::BeginFrame(); // enable Debug calls during physics

        float lerp = 1; // blending between previous and current position

        g_ImGuiBatch->BeginFrame();
        switch (State) {
            case GameState::Game:
                lerp = GameTick(dt);
                if (!Level.Objects.empty())
                    MoveCameraToObject(Render::Camera, Level.Objects[0], lerp);

                Render::UpdateParticles(dt);
                break;
            case GameState::Editor:
                Editor::Update();
                if (!Settings::ScreenshotMode) EditorUI.OnRender();
                break;
            case GameState::Paused:
                break;
        }

        g_ImGuiBatch->EndFrame();
        Render::Present(lerp);
    }

    Camera EditorCameraSnapshot;

    void ToggleEditorMode() {
        if (State == GameState::Game) {
            // Activate editor mode
            Editor::History.Undo();
            State = GameState::Editor;
            Render::Camera = EditorCameraSnapshot;
            Input::SetMouselook(false);
            Sound::Stop3DSounds();
        }
        else if (State == GameState::Editor) {
            // Activate game mode
            Editor::History.SnapshotLevel("Playtest");
            State = GameState::Game;
            for (auto& obj : Level.Objects) {
                obj.LastPosition = obj.Position;
                obj.LastRotation = obj.Rotation;
            }

            EditorCameraSnapshot = Render::Camera;
            Settings::RenderMode = RenderMode::Shaded;
            Input::SetMouselook(true);
            Render::LoadHUDTextures();
        }
    }
}
