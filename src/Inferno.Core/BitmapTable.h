#pragma once
#include "HamFile.h"
#include "Types.h"

namespace Inferno {
    constexpr auto HULK_MODEL_NAME = "robot09.pof";
    constexpr auto RED_HULK_MODEL_NAME = "robot09red.pof";

    // Pig file must be loaded
    void ReadBitmapTable(span<byte> data, const PigFile& pig, HamFile& ham, SoundFile& sounds);
}