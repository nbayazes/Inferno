#pragma once

#include "WindowBase.h"

namespace Inferno::Editor {
    class DebugWindow final : public WindowBase {
        float _frameTime = 0, _timeCounter = 1;

    public:
        DebugWindow();

    protected:
        void OnUpdate() override;
    };
}
