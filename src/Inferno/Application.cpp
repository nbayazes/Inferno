#include "pch.h"
#include "Application.h"
#include "SoundSystem.h"
#include "Input.h"
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

Application::~Application() {
    Render::Shutdown();
    Sound::Shutdown();
}

void Application::Initialize(int width, int height) {
    Inferno::Input::Initialize(Shell::Hwnd);
    Render::Initialize(Shell::Hwnd, width, height);
    Sound::Init(Shell::Hwnd);

    if (Settings::Inferno.Descent3Enhanced)
        Resources::MountDescent3();

    Editor::Initialize();

    OnActivated();

    // Set color picker to use wheel and HDR by default
    ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_PickerHueWheel);

    Events::SettingsChanged += [this] {
        UpdateFpsLimit();
    };
}

void Application::UpdateFpsLimit() {
    auto limit = _isForeground ? Settings::Graphics.ForegroundFpsLimit : Settings::Graphics.BackgroundFpsLimit;
    _fpsLimitMs = limit > 0 ? int(1000.0f / (float)limit) : 0;
}

void Application::Tick() const {
    if (_fpsLimitMs > 0) {
        if (Inferno::Clock.MaybeSleep(_fpsLimitMs))
            return; // spinwait
    }

    Inferno::Clock.Update();

    auto dt = Inferno::Clock.GetFrameTimeSeconds();
    if (dt == 0)
        return; // Skip first tick

    // Level was just loaded, reset timers
    if (Game::ResetDeltaTime) {
        Game::ResetDeltaTime = false;
        return;
    }

    if (dt > 2) {
        SPDLOG_WARN("Long delta time of {}, clamping to 2s", dt);
        dt = 2;
    }

    Render::FrameTime = dt;
    Game::FrameTime = 0;

    if (Game::GetState() == GameState::Game) {
        Game::Time += dt;
        Game::FrameTime = dt;
    }

    if (Settings::Editor.ShowAnimation)
        Render::ElapsedTime += dt;

    Game::Update(dt);
}

bool Inferno::Application::OnClose() {
    return Editor::CanCloseCurrentFile();
}

// Message handlers
void Application::OnActivated() {
    Input::HasFocus = true;
    if (Game::GetState() == GameState::Game)
        Input::SetMouseMode(Input::MouseMode::Mouselook);

    Input::ResetState();
    _isForeground = true;
    UpdateFpsLimit();
}

void Application::OnDeactivated() {
    Input::HasFocus = false;
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
    //Inferno::Clock.ResetFrameTime();

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
