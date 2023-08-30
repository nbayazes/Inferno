#include "pch.h"
#include "Application.h"
#include "SoundSystem.h"
#include "Input.h"
#include "Editor/Bindings.h"
#include "Game.h"
#include "imgui_local.h"
#include "Resources.h"
#include "Editor/Editor.h"
#include "SystemClock.h"
#include "Graphics/Render.h"

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

    if (Settings::Inferno.Descent3Enhanced)
        Resources::MountDescent3();

    Editor::Initialize();

    Sound::Init(Shell::Hwnd);

    OnActivated();

    Events::SettingsChanged += [this] {
        UpdateFpsLimit();
    };
}

using Keys = Keyboard::Keys;

void Application::Update() {}

void Application::UpdateFpsLimit() {
    auto limit = _isForeground ? Settings::Graphics.ForegroundFpsLimit : Settings::Graphics.BackgroundFpsLimit;
    _fpsLimit = limit > 0 ? int(1000.0f / (float)limit) : 0;
}

void Application::Tick() {
    auto milliseconds = Inferno::Clock.GetTotalMilliseconds();
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

    Inferno::Clock.Update(false);

    auto dt = (float)Inferno::Clock.GetFrameTimeSeconds();
    if (dt > 2) {
        SPDLOG_WARN("Long delta time of {}, clamping to 2s", dt);
        dt = 2;
    }

    Render::FrameTime = dt;

    if (Game::GetState() == GameState::Game)
        Game::Time += dt;

    if (Settings::Editor.ShowAnimation)
        Render::ElapsedTime += dt;

    Game::Update(dt);
}

bool Inferno::Application::OnClose() {
    return Editor::CanCloseCurrentFile();
}

// Message handlers
void Application::OnActivated() {
    if (Game::GetState() == GameState::Game)
        Input::SetMouseMode(Input::MouseMode::Mouselook);

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
    Inferno::Clock.ResetFrameTime();

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
