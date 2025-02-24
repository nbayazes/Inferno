#pragma once
#include "HamFile.h"
#include "Types.h"

namespace Inferno {
    constexpr auto HULK_MODEL_NAME = "robot09.pof";
    constexpr auto RED_HULK_MODEL_NAME = "robot09red.pof";

    // PIG and sound file must be loaded. Populates HAM data.
    // D1 Demo stores its game data in a "bitmap table"
    void ReadBitmapTable(span<byte> data, const PigFile& pig, const SoundFile& sounds, HamFile& ham);
}