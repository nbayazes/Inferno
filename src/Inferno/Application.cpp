#include "pch.h"
#include "Application.h"
#include "SoundSystem.h"
#include "FileSystem.h"
#include "Input.h"
#include "Editor/Bindings.h"
#include "Game.h"
#include "imgui_local.h"
#include "Physics.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace Inferno;
using namespace Inferno::Editor;

void Application::OnShutdown() {
    Render::Shutdown();
    Sound::Shutdown();
}

void Application::Initialize(int width, int height) {
    Inferno::Input::Initialize(Shell::Hwnd);
    Render::Initialize(Shell::Hwnd, width, height);

    Editor::Initialize();

    //Sound::Init(hwnd);
    //Sound::Play(SoundID(72), 0.1f, -0.5f, 1.0f);
    //Sound::Play(SoundID(72), 0.1f, -0.3f, -1.0f);

    OnActivated();

    if (Settings::Descent1Path.empty() && Settings::Descent2Path.empty()) {
        Events::ShowDialog(DialogType::Settings);
    }

    Events::SettingsChanged += [this] {
        UpdateFpsLimit();
    };
}

using Keys = Keyboard::Keys;

void FireTestWeapon(Level& level, const Object& obj, int gun) {
    auto point = Vector3::Transform(Resources::GameData.PlayerShip.GunPoints[gun], obj.Transform);

    auto& weapon = Resources::GameData.Weapons[34];

    Object bullet{};
    bullet.Movement.Type = MovementType::Physics;
    bullet.Movement.Physics.Velocity = obj.Transform.Forward() * weapon.Speed[0] * 1;
    bullet.Movement.Physics.Flags = weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
    bullet.Movement.Physics.Drag = weapon.Drag;
    bullet.Transform.Translation(point);
    bullet.PrevTransform.Translation(point);

    bullet.Render.Type = RenderType::WeaponVClip;
    bullet.Render.VClip.ID = weapon.WeaponVClip;
    bullet.Life = weapon.Lifetime;

    bullet.Type = ObjectType::Weapon;

    Render::LoadTextureDynamic(weapon.WeaponVClip);
    level.Objects.push_back(bullet);
}


void Application::Update() {
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

    Inferno::Input::Update();


    if (Input::IsKeyPressed(Keys::Enter)) {
        FireTestWeapon(Game::Level, Game::Level.Objects[0], 0);
        FireTestWeapon(Game::Level, Game::Level.Objects[0], 1);
    }

    if (Input::IsKeyPressed(Keys::F1))
        Editor::ShowDebugOverlay = !Editor::ShowDebugOverlay;

    if (Input::IsKeyPressed(Keys::F5))
        Render::Adapter->ReloadResources();

    if (Input::IsKeyPressed(Keys::F6))
        Render::ReloadTextures();

    if (Input::IsKeyPressed(Keys::F7)) {
        Settings::HighRes = !Settings::HighRes;
        Render::ReloadTextures();
    }

    constexpr double dt = 1.0f / 64;
    static double accumulator = 0;
    static double t = 0;

    accumulator += Render::FrameTime;

    Render::Debug::BeginFrame();

    while (accumulator >= dt) {
        UpdatePhysics(Game::Level, t, dt); // catch up if physics falls behind
        accumulator -= dt;
        t += dt;
    }

    Editor::Update();

    g_ImGuiBatch->BeginFrame();
    _editorUI.OnRender();
    g_ImGuiBatch->EndFrame();

    PIXEndEvent();

    const double alpha = accumulator / dt;
    Render::Present(alpha);
}

void Application::UpdateFpsLimit() {
    auto limit = _isForeground ? Settings::ForegroundFpsLimit : Settings::BackgroundFpsLimit;
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

    if (Settings::ShowAnimation)
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
    if (Input::GetMouselook())
        Input::SetMouselook(false);

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
