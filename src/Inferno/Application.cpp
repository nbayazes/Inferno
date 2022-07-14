#include "pch.h"
#include "Application.h"
#include "SoundSystem.h"
#include "FileSystem.h"
#include "Input.h"
#include "Editor/Bindings.h"
#include "Game.h"
#include "imgui_local.h"
#include "BitmapCache.h"

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

    Resources::MountDescent3();

    //Sound::Init(hwnd);
    //Sound::Play(SoundID(72), 0.1f, -0.5f, 1.0f);
    //Sound::Play(SoundID(72), 0.1f, -0.3f, -1.0f);

    //if (auto gyro = Resources::Descent3Hog->ReadEntry("gyro.oof")) {
    //    StreamReader reader(*gyro);
    //    auto model = OutrageModel::Read(reader);
    //}

    auto& vclips = Resources::VClips;
    auto& textures = Resources::GameTable.Textures;

    //for (auto& vclip : Resources::VClips) {
    //    fmt::print("v: {} FrameTime: {}s Pingpong: {}\n", vclip.Version, vclip.FrameTime, vclip.PingPong);
    //    for (auto& frame : vclip.Frames) {
    //        fmt::print("    {} : {} x {}\n", frame.Name, frame.Width, frame.Height);
    //    }
    //}



    //List<int> handles;
    //if (auto r = Resources::OpenFile("drsweitzer.oof")) {
    //    auto model = Outrage::Model::Read(*r);
    //    /*for (auto& name : model.Textures) {
    //        handles.push_back(texCache->Resolve(name));
    //    }*/
    //}

    //for (auto& texture : Resources::GameTable.Textures) {
    //    //if (texture.Name == "pillar.ifl1")
    //        texCache->Resolve(texture.Name);
    //}

    //for (auto& entry : Resources::Descent3Hog.Entries) {
    //    if (String::ToLower(entry.name).ends_with("oof")) {
    //        try {
    //            auto r = Resources::OpenFile(entry.name);
    //            auto model = Outrage::Model::Read(*r);

    //        }
    //        catch (const std::exception& e) {
    //            SPDLOG_ERROR("{}: {}", entry.name, e.what());
    //        }

    //        //for (auto& sm : model.Submodels) {
    //        //    if (sm.Props.empty()) continue;
    //        //    fmt::print("{}\n", sm.Props);
    //        //}

    //        //for (auto& name : model.Textures) {
    //        //    texCache->Resolve(name);
    //        //}
    //    }
    //}

    Render::Adapter->PrintMemoryUsage();
    Render::Heaps->Shader.GetFreeDescriptors();

    Editor::Initialize();

    OnActivated();

    if (Settings::Descent1Path.empty() && Settings::Descent2Path.empty()) {
        Events::ShowDialog(DialogType::Settings);
    }

    Events::SettingsChanged += [this] {
        UpdateFpsLimit();
    };
}

void Application::Update() {
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

    Inferno::Input::Update();

    using Keys = Keyboard::Keys;

    if (Input::Keyboard.IsKeyPressed(Keys::F1))
        Editor::ShowDebugOverlay = !Editor::ShowDebugOverlay;

    if (Input::Keyboard.IsKeyPressed(Keys::F5))
        Render::Adapter->ReloadResources();

    if (Input::Keyboard.IsKeyPressed(Keys::F6))
        Render::ReloadTextures();

    if (Input::Keyboard.IsKeyPressed(Keys::F7)) {
        Settings::HighRes = !Settings::HighRes;
        Render::ReloadTextures();
    }

    Editor::Update();

    g_ImGuiBatch->BeginFrame();
    _editorUI.OnRender();
    g_ImGuiBatch->EndFrame();

    PIXEndEvent();

    Render::Present();
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
