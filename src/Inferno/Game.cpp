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
        auto gunOffset = Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);
        auto point = Vector3::Transform(gunOffset, obj.GetTransform());
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

        //bullet.Lifespan = weapon.Lifetime;
        bullet.Lifespan = 3;
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = ObjID(0);
        bullet.Render.Emissive = { 0.8f, 0.4f, 0.1f }; // laser level 5

        //auto pitch = -Random() * 0.2f;
        //Sound::Sound3D sound(point, obj.Segment);
        Sound::Sound3D sound(ObjID(0));
        sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
        sound.Volume = 0.55f;
        sound.AttachToSource = true;
        sound.AttachOffset = gunOffset;
        sound.FromPlayer = true;
        Sound::Play(sound);

        Render::Particle p{};
        p.Clip = weapon.FlashVClip;
        p.Position = point;
        p.Radius = weapon.FlashSize;
        p.Parent = ObjID(0);
        p.ParentOffset = gunOffset;
        p.FadeTime = 0.175f;
        Render::AddParticle(p);

        for (auto& o : level.Objects) {
            if (o.Lifespan <= 0) {
                o = bullet;
                return; // found a dead object to reuse!
            }
        }

        level.Objects.push_back(bullet); // insert a new object
    }

    float g_FireDelay = 0;

    // Updates on each game tick
    void FixedUpdate(float dt) {
        g_FireDelay -= dt;

        // must check held keys inside of fixed updates so events aren't missed
        if ((Game::State == GameState::Editor && Input::IsKeyDown(Keys::Enter)) ||
           (Game::State != GameState::Editor && Input::Mouse.leftButton == Input::MouseState::HELD)) {
            if (g_FireDelay <= 0) {
                auto id = Game::Level.IsDescent2() ? 13 : 13; // plasma: 13, super laser: 30
                auto& weapon = Resources::GameData.Weapons[id];
                g_FireDelay = weapon.FireDelay;
                FireTestWeapon(Game::Level, ObjID(0), 0, id);
                FireTestWeapon(Game::Level, ObjID(0), 1, id);
                //FireTestWeapon(Game::Level, ObjID(0), 2, id);
                //FireTestWeapon(Game::Level, ObjID(0), 3, id);
            }
        }
    }

    // Returns the lerp amount for the current tick
    float GameTick(float dt) {
        static double accumulator = 0;
        static double t = 0;

        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        //float lerp = 1; // blending between previous and current position
        if (!Level.Objects.empty()) {
            auto& physics = Level.Objects[0].Movement.Physics;
            physics.Thrust = Vector3::Zero;
            physics.AngularThrust = Vector3::Zero;

            if (Game::State == GameState::Editor) {
                if (Settings::EnablePhysics)
                    HandleEditorDebugInput(dt);
            }
            else {
                HandleInput(dt);
            }

            // Clamp max input speeds
            auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
            auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;
            Vector3 maxAngVec(Settings::LimitPitchSpeed ? maxAngularThrust / 2 : maxAngularThrust, maxAngularThrust, maxAngularThrust);
            physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
            Vector3 maxThrustVec(maxThrust, maxThrust, maxThrust);
            physics.Thrust.Clamp(-maxThrustVec, maxThrustVec);
        }

        while (accumulator >= TICK_RATE) {
            UpdatePhysics(Game::Level, t, TICK_RATE); // catch up if physics falls behind
            FixedUpdate(TICK_RATE);
            accumulator -= TICK_RATE;
            t += TICK_RATE;
        }


        //lerp = float(accumulator / tickRate);
        //Render::Present(lerp);
        return float(accumulator / TICK_RATE);
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

        g_ImGuiBatch->BeginFrame();
        switch (State) {
            case GameState::Game:
                LerpAmount = GameTick(dt);
                if (!Level.Objects.empty())
                    MoveCameraToObject(Render::Camera, Level.Objects[0], LerpAmount);

                //Sound::UpdateEmitterPositions(dt);
                Render::UpdateParticles(Level, dt);
                break;
            case GameState::Editor:
                if (Settings::EnablePhysics) {
                    LerpAmount = Settings::EnablePhysics ? GameTick(dt) : 1;
                    Render::UpdateParticles(Level, dt);
                    //Sound::UpdateEmitterPositions(dt);
                }
                else {
                    LerpAmount = 0;
                }

                Editor::Update();
                if (!Settings::ScreenshotMode) EditorUI.OnRender();
                break;
            case GameState::Paused:
                break;
        }

        g_ImGuiBatch->EndFrame();
        Render::Present(LerpAmount);
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
            LerpAmount = 1;
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

            string customHudTextures[] = {
                "cockpit-ctr",
                "cockpit-left",
                "cockpit-right",
                "gauge01b#0",
                "gauge02b",
                "gauge03b",
                "gauge16b", // lock
                "Hilite",
                "SmHilite"
            };

            Render::Materials->LoadTextures(customHudTextures);

            //TexID weaponTextures[] = {
            //    TexID(30), TexID(11), TexID(
            //};
        }
    }
}
