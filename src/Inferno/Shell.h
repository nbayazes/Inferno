#pragma once
#include <WinUser.h>

namespace Inferno {
    class Application;

    static LPCWSTR WindowClass = L"InfernoWindowClass";

    class Shell {
        HMODULE _hInstance = GetModuleHandle(nullptr);
    public:
        Shell() = default;

        ~Shell() {
            UnregisterClass(WindowClass, _hInstance);
        }
        Shell(const Shell&) = delete;
        Shell(Shell&&) = delete;
        Shell& operator=(const Shell&) = delete;
        Shell& operator=(Shell&&) = delete;

        int Show(Application* app, int width, int height, int nCmdShow = SW_SHOWMAXIMIZED);

        inline static HWND Hwnd = nullptr;
    };
}

