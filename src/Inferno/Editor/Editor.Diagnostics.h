#pragma once

#include "Types.h"
#include "Level.h"

namespace Inferno::Editor {
    // Fixes common errors in a level
    void FixLevel(Level&);

    void CheckLevelForErrors(const Level&);
}