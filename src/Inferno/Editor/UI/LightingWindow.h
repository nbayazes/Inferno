#pragma once

#include "WindowBase.h"

namespace Inferno::Editor {
    class LightingWindow : public WindowBase {
    public:
        LightingWindow();

    protected:
        void OnUpdate() override;
    };
}
