#pragma once

#include "Types.h"

namespace Inferno {
    struct Briefing {
        struct Screen {
            List<string> Pages;
            int Number = -1;
        };

        List<Screen> Screens;
        string Raw;

        static Briefing Read(span<ubyte> data);
    };

}