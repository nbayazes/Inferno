#pragma once

#include <wtypes.h>
#include "Editor/Editor.h"
#include "Graphics/ShaderLibrary.h"
#include "Graphics/Render.h"
#include "Level.h"
#include "SystemClock.h"
#include "Editor/UI/EditorUI.h"

namespace Inferno {

    class Application final : public IDeviceNotify {
        Inferno::Editor::EditorUI _editorUI;
        SystemClock _clock;
        double _fpsLimit = 0;
        double _nextUpdate = 0;
        bool _isForeground = false;
    public:
        Application() = default;
        void OnShutdown();
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
