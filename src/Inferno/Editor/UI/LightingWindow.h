#pragma once

#include "WindowBase.h"

namespace Inferno::Editor {
    class LightingWindow final : public WindowBase {
    public:
        LightingWindow();

    protected:
        void OnUpdate() override;
    };
}
