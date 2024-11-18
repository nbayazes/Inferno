#include "pch.h"
#include "Application.h"
#include "Shell.h"
#include <Windows.h>
#include <dwmapi.h>
#include <WinUser.h>
#include "imgui_local.h"
#include "Input.h"
#include "Version.h"
#include "logging.h"
#include "Convert.h"
#include "Editor/Editor.Undo.h"
#include "Game.h"

//#include <versionhelpers.h>

#pragma warning(disable: 26477)
using namespace Inferno;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    int AppWidth = 1024, AppHeight = 768;
    bool AppSuspended = false;
    bool AppMinimized = false;
    bool AppFullscreen = false;
    bool AppMaximized = false;
    HBRUSH BackgroundBrush{};
    RECT AppWindowRect{};
}

void EnableDarkMode(HWND hwnd) {
    // We don't care if this fails, but hopefully it doesn't crash
    BOOL useDarkMode = true;
    //IsWindowsVersionOrGreater(); 
    // support for this attribute was added in Windows 10 20H1, whatever version that was
    DwmSetWindowAttribute(hwnd, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &useDarkMode, sizeof(useDarkMode));
}

void GetWindowPlacement() {
    auto hWnd = Inferno::Shell::Hwnd;
    if (!hWnd) return;

    WINDOWPLACEMENT placement{};
    GetWindowPlacement(hWnd, &placement);
    AppMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
    AppWindowRect = placement.rcNormalPosition;
}

// Updates the window fullscreen state based on the app setting
void UpdateFullscreen() {
    auto hWnd = Inferno::Shell::Hwnd;
    if (!hWnd) return;
    if (AppFullscreen == Settings::Inferno.Fullscreen) return;

    AppFullscreen = Settings::Inferno.Fullscreen;

    if (!Inferno::Settings::Inferno.Fullscreen) {
        // Restore window position and size
        SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, 0);

        auto width = AppWindowRect.right - AppWindowRect.left;
        auto height = AppWindowRect.bottom - AppWindowRect.top;
        auto flags = SWP_NOZORDER | SWP_FRAMECHANGED;

        // Don't override the window size if the app was maximized, so that restoring keeps the original size.
        //if (AppMaximized) flags |= SWP_NOSIZE | SWP_NOMOVE;

        SetWindowPos(hWnd, HWND_TOP, AppWindowRect.left, AppWindowRect.top, width, height, flags);
        ShowWindow(hWnd, AppMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    }
    else {
        GetWindowPlacement();

        SetWindowLongPtr(hWnd, GWL_STYLE, 0);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);

        SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<Application*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    UpdateFullscreen();
    Input::ProcessMessage(message, wParam, lParam);

    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message) {
        case WM_CLOSE:
            if (!app->OnClose())
                return 0;

            break;

        case WM_SYSKEYDOWN:
            // Implements the classic ALT+ENTER fullscreen toggle
            if (app && wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000) {
                Inferno::Settings::Inferno.Fullscreen = !Inferno::Settings::Inferno.Fullscreen;
            }

            break;

        //case WM_PAINT:
        //    if (/*AppMoving &&*/ app) {
        //        app->Tick();
        //    }
        //    else {
        //        PAINTSTRUCT ps;
        //        (void)BeginPaint(hWnd, &ps);
        //        EndPaint(hWnd, &ps);
        //    }
        //    break;

        case WM_MOVE:
            if (app) {
                app->OnWindowMoved();
                // Redrawing while moving works, but is laggy. Need to limit framerate.
                //app->Tick(); 
            }
            break;

        case WM_DISPLAYCHANGE:
            if (app) {
                SPDLOG_INFO("Display Resolution Changed {} {}", LOWORD(lParam), HIWORD(lParam));

                WINDOWPLACEMENT wp = {};
                wp.length = sizeof(wp);
                GetWindowPlacement(hWnd, &wp);

                if (wp.showCmd == SW_MAXIMIZE) {
                    // Workaround for client area not updating correctly after screen resolution changes.
                    // This is not ideal, but I am unable to locate the exact cause of the problem.
                    ShowWindow(hWnd, SW_RESTORE);
                    ShowWindow(hWnd, SW_MAXIMIZE);
                }

                Inferno::Shell::DpiScale = (float)GetDpiForWindow(hWnd) / 96.0f;
                app->Tick();
            }
            break;

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                if (!AppMinimized) {
                    AppMinimized = true;
                    if (!AppSuspended && app)
                        app->OnSuspending();
                    AppSuspended = true;
                }
            }
            else if (AppMinimized) {
                AppMinimized = false;
                if (AppSuspended && app)
                    app->OnResuming();
                AppSuspended = false;
            }
            else if (/*!AppMoving && */app) {
                app->OnWindowSizeChanged(LOWORD(lParam), HIWORD(lParam));
                //app->Tick(); // this crashes when toggling fullscreen
                return 0;
            }
            break;

        case WM_ENTERSIZEMOVE:
            //AppMoving = true;
            break;

        case WM_EXITSIZEMOVE:
            //AppMoving = false;
            if (app) {
                RECT rc;
                GetClientRect(hWnd, &rc);
                app->OnWindowSizeChanged(rc.right - rc.left, rc.bottom - rc.top);
                return 0;
            }
            break;

        case WM_GETMINMAXINFO:
            if (lParam) {
                auto info = reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize.x = 320;
                info->ptMinTrackSize.y = 200;
            }
            break;

        case WM_ACTIVATEAPP:
            if (app) {
                Inferno::Shell::HasFocus = (bool)wParam;

                if (wParam)
                    app->OnActivated();
                else
                    app->OnDeactivated();
            }
            break;

        case WM_POWERBROADCAST:
            switch (wParam) {
                case PBT_APMQUERYSUSPEND:
                    if (!AppSuspended && app)
                        app->OnSuspending();
                    AppSuspended = true;
                    return TRUE;

                case PBT_APMRESUMESUSPEND:
                    if (!AppMinimized) {
                        if (AppSuspended && app)
                            app->OnResuming();
                        AppSuspended = false;
                    }
                    return TRUE;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_MENUCHAR:
            // A menu is active and the user presses a key that does not correspond
            // to any mnemonic or accelerator key. Ignore so we don't produce an error beep.
            return MAKELRESULT(0, MNC_CLOSE);

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;

        case WM_CHAR:
            if (wParam > 0 && wParam < 0x10000)
                if (app) {}
            break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

bool RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEX wc{};
    if (GetClassInfoEx(hInstance, WindowClass, &wc))
        return true;

    BackgroundBrush = CreateSolidBrush(RGB(25, 25, 25));

    // Register class
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, L"IDI_ICON");
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    //wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hbrBackground = BackgroundBrush;
    wc.lpszClassName = WindowClass;
    wc.hIconSm = LoadIcon(wc.hInstance, L"IDI_ICON");

    return RegisterClassEx(&wc) != 0;
}

Inferno::Shell::~Shell() {
    UnregisterClass(WindowClass, _hInstance);
}

int Inferno::Shell::Show(int width, int height, int nCmdShow) const {
    if (!RegisterWindowClass(_hInstance))
        throw std::exception("Failed to register window class");

    // Create window
    AppWidth = width;
    AppHeight = height;
    RECT rc = { 0, 0, width, height };

    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    // Change to CreateWindowExW(WS_EX_TOPMOST, , , WS_POPUP, to default to fullscreen.
    HWND hwnd = CreateWindowEx(0, WindowClass, Convert::ToWideString(APP_TITLE).c_str(),
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top,
                               nullptr, nullptr, _hInstance, nullptr);

    if (!hwnd)
        throw std::exception("Failed to create window");

    Shell::DpiScale = (float)GetDpiForWindow(hwnd) / 96.0f;

    EnableDarkMode(hwnd);
    Shell::Hwnd = hwnd;
    ShowWindow(hwnd, nCmdShow);
    GetWindowPlacement();

    Application app;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    GetClientRect(hwnd, &rc);
    app.Initialize(rc.right - rc.left, rc.bottom - rc.top);

    // Main message loop
    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            app.Tick();
        }
    }

    DestroyWindow(hwnd);
    DeleteObject(BackgroundBrush);
    return (int)msg.wParam;
}

void Inferno::Shell::UpdateWindowTitle(string_view message) {
    if (!message.empty()) {
        string title = fmt::format("{} - {}", message, APP_TITLE);
        SetWindowTextW(Hwnd, Convert::ToWideString(title).c_str());
        return;
    }

    auto state = Game::GetState();

    if (state == GameState::Editor) {
        auto dirtyFlag = Editor::History.Dirty() ? "*" : "";
        auto levelName = Game::Level.FileName == "" ? "untitled" : Game::Level.FileName + dirtyFlag;

        string title =
            Game::Mission
            ? fmt::format("{} [{}] - {}", levelName, Game::Mission->Path.filename().string(), APP_TITLE)
            : fmt::format("{} - {}", levelName, APP_TITLE);

        SetWindowTextW(Hwnd, Convert::ToWideString(title).c_str());
    }
    else if (state == GameState::MainMenu) {
        SetWindowTextW(Hwnd, Convert::ToWideString(APP_TITLE).c_str());
    }
    else {
        auto info = Game::TryReadMissionInfo();

        string title =
            info
            ? fmt::format("{} [{}] - {}", String::ToUpper(Game::Level.Name), info->Name, APP_TITLE)
            : fmt::format("{} - {}", String::ToUpper(Game::Level.Name), APP_TITLE);

        SetWindowTextW(Hwnd, Convert::ToWideString(title).c_str());
    }
}
