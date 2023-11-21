#pragma once
#include "Graphics/IDeviceNotify.h"

namespace Inferno {
    class Application final : public IDeviceNotify {
        uint64 _fpsLimitMs = 0;
        uint64 _nextUpdate = 0;
        bool _isForeground = false;

    public:
        Application() = default;
        ~Application();
        // Initialization and management
        void Initialize(int width, int height);
        void Tick();
        bool OnClose();
        void OnActivated();
        void OnDeactivated();
        void OnSuspending();
        void OnResuming();
        void OnWindowMoved();
        void OnWindowSizeChanged(int width, int height);
        void OnDeviceLost() override;
        void OnDeviceRestored() override;

        bool EnableImgui = true;

    private:
        void Update();
        void UpdateFpsLimit();
    };
}
