#pragma once
#include "Graphics/IDeviceNotify.h"
#include "Types.h"

namespace Inferno {
    class Application final : public IDeviceNotify {
        uint64 _nextUpdate = 0;
        bool _isForeground = false;

    public:
        Application() = default;
        Application(const Application& other) = delete;
        Application(Application&& other) noexcept = delete;
        Application& operator=(const Application& other) = delete;
        Application& operator=(Application&& other) noexcept = delete;
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
        float GetFpsLimit() const;
    };
}
