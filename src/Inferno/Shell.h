#pragma once

namespace Inferno {
    class Application;

    static auto WindowClass = L"InfernoWindowClass";

    class Shell {
        HMODULE _hInstance = GetModuleHandle(nullptr);
    public:
        Shell() = default;
        ~Shell();
        Shell(const Shell&) = delete;
        Shell(Shell&&) = delete;
        Shell& operator=(const Shell&) = delete;
        Shell& operator=(Shell&&) = delete;

        int Show(DirectX::XMUINT2 position, DirectX::XMUINT2 size, int nCmdShow = SW_SHOWMAXIMIZED) const;

        inline static HWND Hwnd = nullptr;
        inline static float DpiScale = 1;
        inline static bool HasFocus = true;
        static void UpdateWindowTitle(string_view message = {});
    };
}

