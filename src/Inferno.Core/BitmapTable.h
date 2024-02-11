#pragma once
#include "HamFile.h"
#include "Types.h"

namespace Inferno {
    // Pig file must be loaded
    void ReadBitmapTable(span<byte> data, const PigFile& pig, HamFile& ham, SoundFile& sounds);
}