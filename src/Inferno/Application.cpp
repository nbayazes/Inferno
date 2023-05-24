#include "pch.h"
#include "Application.h"
#include "SoundSystem.h"
#include "FileSystem.h"
#include "Input.h"
#include "Editor/Bindings.h"
#include "Game.h"
#include "imgui_local.h"
#include "BitmapCache.h"
#include "Physics.h"
#include "Graphics/Render.Particles.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace Inferno;
using namespace Inferno::Editor;

void DumpD3VClips() {
    for (auto& vclip : Resources::VClips) {
        fmt::print("v: {} FrameTime: {}s Pingpong: {}\n", vclip.Version, vclip.FrameTime, vclip.PingPong);
        for (auto& frame : vclip.Frames) {
            fmt::print("    {} : {} x {}\n", frame.Name, frame.Width, frame.Height);
        }
    }
}

void LoadAllD3Models() {
    //if (auto gyro = Resources::Descent3Hog->ReadEntry("gyro.oof")) {
    //    StreamReader reader(*gyro);
    //    auto model = OutrageModel::Read(reader);
    //}

    for (auto& entry : Resources::Descent3Hog.Entries) {
        if (String::ToLower(entry.name).ends_with("oof")) {
            try {
                auto r = Resources::OpenFile(entry.name);
                auto model = Outrage::Model::Read(*r);

            }
            catch (const std::exception& e) {
                SPDLOG_ERROR("{}: {}", entry.name, e.what());
            }

            //for (auto& sm : model.Submodels) {
            //    if (sm.Props.empty()) continue;
            //    fmt::print("{}\n", sm.Props);
            //}

            //for (auto& name : model.Textures) {
            //    texCache->Resolve(name);
            //}
        }
    }

}

void Application::OnShutdown() {
    Render::Shutdown();
    Sound::Shutdown();
}

void Application::Initialize(int width, int height) {
    Inferno::Input::Initialize(Shell::Hwnd);
    Render::Initialize(Shell::Hwnd, width, height);

    Resources::LoadSounds();
    //Resources::MountDescent3();

    Editor::Initialize();

    Sound::Init(Shell::Hwnd, 0.01f);

    OnActivated();

    Events::SettingsChanged += [this] {
        UpdateFpsLimit();
    };
}

using Keys = Keyboard::Keys;

float g_FireDelay = 0;

void FireTestWeapon(Level& level, const Object& obj, int gun, int id) {
    auto point = Vector3::Transform(Resources::GameData.PlayerShip.GunPoints[gun], obj.GetTransform());
    auto& weapon = Resources::GameData.Weapons[id];

    Object bullet{};
    bullet.Movement.Type = MovementType::Physics;
    bullet.Movement.Physics.Velocity = obj.Rotation.Forward() * weapon.Speed[0] * 1;
    bullet.Movement.Physics.Flags = weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
    bullet.Movement.Physics.Drag = weapon.Drag;
    bullet.Movement.Physics.Mass = weapon.Mass;
    bullet.Position = bullet.LastPosition = point;
    bullet.Rotation = bullet.LastRotation = obj.Rotation;

    bullet.Render.Type = RenderType::WeaponVClip;
    bullet.Render.VClip.ID = weapon.WeaponVClip;
    bullet.Render.VClip.Rotation = Random() * DirectX::XM_2PI;
    bullet.Lifespan = weapon.Lifetime;

    bullet.Type = ObjectType::Weapon;
    bullet.ID = (int8)id;
    bullet.Parent = ObjID(0);

    //auto pitch = -Random() * 0.2f;
    Sound::Sound3D sound(point, obj.Segment);
    sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
    sound.Source = ObjID(0);
    sound.Volume = 0.35f;
    Sound::Play(sound);

    Render::LoadTextureDynamic(weapon.WeaponVClip);

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


void Application::Update() {
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

    Inferno::Input::Update();

    if (Settings::Editor.EnablePhysics) {
        g_FireDelay -= Render::FrameTime;

        if (Input::IsKeyDown(Keys::Enter)) {
            if (g_FireDelay <= 0) {
                g_FireDelay = 0;
                auto id = Game::Level.IsDescent2() ? 13 : 13;
                auto& weapon = Resources::GameData.Weapons[id];
                g_FireDelay += weapon.FireDelay;
                FireTestWeapon(Game::Level, Game::Level.Objects[0], 0, id);
                FireTestWeapon(Game::Level, Game::Level.Objects[0], 1, id);
            }
        }
    }

    if (Input::IsKeyPressed(Keys::F1))
        Editor::ShowDebugOverlay = !Editor::ShowDebugOverlay;

    if (Input::IsKeyPressed(Keys::F12))
        Settings::Inferno.ScreenshotMode = !Settings::Inferno.ScreenshotMode;

    if (Input::IsKeyPressed(Keys::F5))
        Render::Adapter->ReloadResources();

    if (Input::IsKeyPressed(Keys::F6))
        Render::ReloadTextures();

    if (Input::IsKeyPressed(Keys::F7)) {
        Settings::Graphics.HighRes = !Settings::Graphics.HighRes;
        Render::ReloadTextures();
    }

    constexpr double dt = 1.0f / 64;
    static double accumulator = 0;
    static double t = 0;

    accumulator += Render::FrameTime;

    Render::Debug::BeginFrame(); // enable Debug calls during physics

    float alpha = 1; // blending between previous and current position

    if (Settings::Editor.EnablePhysics) {
        while (accumulator >= dt) {
            UpdatePhysics(Game::Level, t, dt); // catch up if physics falls behind
            accumulator -= dt;
            t += dt;
        }

        alpha = float(accumulator / dt);
    }

    // todo: only update particles if game is not paused
    Render::UpdateParticles(Render::FrameTime);
    Editor::Update();

    g_ImGuiBatch->BeginFrame();
    if (!Settings::Inferno.ScreenshotMode) _editorUI.OnRender();
    g_ImGuiBatch->EndFrame();

    PIXEndEvent();

    Render::Present(alpha);
}

void Application::UpdateFpsLimit() {
    auto limit = _isForeground ? Settings::Graphics.ForegroundFpsLimit : Settings::Graphics.BackgroundFpsLimit;
    _fpsLimit = limit > 0 ? 1000.0f / limit : 0;
}

void Application::Tick() {
    auto milliseconds = _clock.GetTotalMilliseconds();
    if (_fpsLimit > 0) {
        if (milliseconds < _nextUpdate) {
            auto sleepTime = _nextUpdate - milliseconds;
            if (sleepTime > 1) // sleep thread to prevent high CPU usage
                std::this_thread::sleep_for(std::chrono::milliseconds((int)sleepTime - 1));

            return;
        }
        else {
            _nextUpdate = milliseconds + _fpsLimit;
        }
    }

    _clock.Update(false);

    Render::FrameTime = (float)_clock.GetElapsedSeconds();
    Game::ElapsedTime = _clock.GetTotalMilliseconds() / 1000.;

    if (Render::FrameTime > 2) Render::FrameTime = 2;

    if (Settings::Editor.ShowAnimation)
        Render::ElapsedTime = milliseconds / 1000.;
    Update();
}

bool Inferno::Application::OnClose() {
    return Editor::CanCloseCurrentFile();
}

// Message handlers
void Application::OnActivated() {
    Input::ResetState();
    _isForeground = true;
    UpdateFpsLimit();
}

void Application::OnDeactivated() {
    Input::SetMouseMode(Input::MouseMode::Normal);

    Input::ResetState();
    _isForeground = false;
    UpdateFpsLimit();
}

void Application::OnSuspending() {
    //_audioEngine->Suspend();
    // TODO: Game is being power-suspended (or minimized).
}

void Application::OnResuming() {
    //Render::Timer.ResetElapsedTime();
    _clock.ResetFrameTime();

    //_audioEngine->Resume();
    // TODO: Game is being power-resumed (or returning from minimize).
}

void Application::OnWindowMoved() {
    //_adapter->OnWindowMoved();
    //auto r = _adapter->GetOutputSize();
    //_adapter->WindowSizeChanged(r.right, r.bottom);
    //Render::Resize()
}

void Application::OnWindowSizeChanged(int width, int height) {
    Render::Resize(width, height);
}


void Application::OnDeviceLost() {
    // TODO: Add Direct3D resource cleanup here.
    //_graphicsMemory.reset();
    //_batch.reset();

    /*m_texture.Reset();
    m_resourceDescriptors.reset();*/
}

void Application::OnDeviceRestored() {
    //CreateDeviceDependentResources();

    //CreateWindowSizeDependentResources();
}
