#include "pch.h"
#include "Application.h"
#include <SDL3/SDL_init.h>
#include "SoundSystem.h"
#include "Input.h"
#include "Game.h"
#include "imgui_local.h"
#include "Resources.h"
#include "Editor/Editor.h"
#include "Game.Bindings.h"
#include "Graphics.h"
#include "SystemClock.h"
#include "Graphics/Render.h"
#include "Graphics/Render.MainMenu.h"
#include "Version.h"

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
    Input::Shutdown();
    SDL_Quit();
}

void Application::Initialize(int width, int height) {
    // SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS
    // SDL_INIT_VIDEO is necessary to get keyboard and mouse devices
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR)) {
        SPDLOG_ERROR("Error initializing SDL: {}", SDL_GetError());
    }

    SDL_SetAppMetadata(Inferno::APP_TITLE, Inferno::VERSION_STRING, nullptr);

    Input::AddJoystickCallback = [](const Input::InputDevice& device) {
        /**
         * Resolving bindings is a bit complicated due to certain joysticks and controllers having
         * duplicate guids (such as the Thrustmaster or PS controllers).
         * Rely on hardware path to determine bindings.
         */

        // Check for exact bindings first (GUID and path)
        if (Game::Bindings.GetExact(device)) {
            SPDLOG_INFO("Exact bindings found for device: {}", device.name);
            return;
        }

        // Count connected devices with this GUID
        uint8 duplicates = 0;
        for (auto& d : Input::GetDevices()) {
            if (d.guid == device.guid) duplicates++;
        }

        if (duplicates >= 2) {
            SPDLOG_INFO("Detected {} input devices with the same GUID", duplicates);

            auto devices = Input::GetDevices();

            // try to find bindings not used by existing devices
            for (auto& bindings : Game::Bindings.GetDevices()) {
                bool inUse = false;
                for (auto& existing : devices) {
                    if (bindings.path == existing.path) {
                        inUse = true;
                        break;
                    }
                }

                if (inUse) continue; // bindings already in use by a device sharing the guid

                if (bindings.guid == device.guid) {
                    // update bindings with this guid to use the path
                    bindings.path = device.path;
                    SPDLOG_INFO("Updating bindings to use device path {}", device.path);
                    return;
                }
            }
        }
        else {
            if (auto bindings = Game::Bindings.GetForDevice(device)) {
                bindings->path = device.path;
                SPDLOG_INFO("Bindings found for device using GUID {}", device.guid);
                return; // Already has bindings
            }
        }

        SPDLOG_INFO("Adding bindings for new device {}", device.name);

        // No existing bindings with guid or path, add new ones
        if (device.IsGamepad()) {
            auto& bindings = Game::Bindings.AddForDevice(device, Input::InputType::Gamepad);

            // xbox controllers tend to have terrible stick drift and need a higher deadzone
            float innerDeadzone = device.IsXBoxController() ? 0.12f : 0.08f;
            ResetGamepadBindings(bindings, innerDeadzone);
        }
        else {
            Game::Bindings.AddForDevice(device, Input::InputType::Joystick);
        }
    };

    if (Settings::Inferno.Descent3Enhanced)
        Resources::MountDescent3();

    OnActivated();

    Game::MainCamera.Up = Vector3::UnitY;
    Game::MainCamera.Position = MenuCameraPosition;
    Game::MainCamera.Target = MenuCameraTarget;

    Inferno::Input::Initialize(Shell::Hwnd);
    Render::Initialize(Shell::Hwnd, width, height);

    Sound::Init(Shell::Hwnd);
    //Resources::LoadSounds();
    Sound::CopySoundIds();

    if (Settings::Inferno.UseTextureCaching)
        LoadTextureCaches();

    LoadFonts();

    // Load global textures
    Graphics::LoadEnvironmentMap("assets/textures/env.dds");
    Graphics::LoadMatcap("assets/textures/matcap.dds");

    // Set color picker to use wheel and HDR by default
    ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_PickerHueWheel);

    // todo: check for editor command line option
    Game::SetState(GameState::MainMenu);
}

// Gets the FPS limit in milliseconds
int Application::GetFpsLimit() const {
    if (_isForeground && Settings::Graphics.EnableForegroundFpsLimit) {
        if (Settings::Graphics.ForegroundFpsLimit > 0) {
            return int(1000.0f / (float)Settings::Graphics.ForegroundFpsLimit);
        }
    }
    else if (!_isForeground && Settings::Graphics.BackgroundFpsLimit) {
        if (Settings::Graphics.BackgroundFpsLimit > 0) {
            return int(1000.0f / (float)Settings::Graphics.BackgroundFpsLimit);
        }
    }

    return 0;
}

void Application::Tick() {
    if (auto fpsLimit = GetFpsLimit(); fpsLimit > 0) {
        if (Inferno::Clock.MaybeSleep(fpsLimit))
            return; // spinwait
    }

    UpdateFullscreen();

    Inferno::Clock.Update();

    auto dt = Inferno::Clock.GetFrameTimeSeconds();
    if (dt == 0)
        return; // Skip first tick


    if (Game::ResetGameTime) {
        SPDLOG_INFO("Reset game time");
        dt = 0;
        Game::Time = 0;
        Game::ResetGameTime = false;
    }

    if (dt > 2) {
        SPDLOG_WARN("Long delta time of {}, clamping to 2s", dt);
        dt = 2;
    }

    Render::FrameTime = dt;
    Game::Update(dt);
}

bool Application::OnClose() {
    return Editor::CanCloseCurrentFile();
}

// Message handlers
void Application::OnActivated() {
    Input::HasFocus = true;
    if (Game::GetState() == GameState::Game || Game::GetState() == GameState::Automap || Game::GetState() == GameState::PhotoMode)
        Input::SetMouseMode(Input::MouseMode::Mouselook);

    Input::ResetState();
    _isForeground = true;
    GetFpsLimit();
}

void Application::OnDeactivated() {
    Input::HasFocus = false;
    Input::SetMouseMode(Input::MouseMode::Normal);
    Input::ResetState();
    _isForeground = false;
    GetFpsLimit();
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
