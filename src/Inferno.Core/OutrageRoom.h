#pragma once
#include "Level.h"
#include "Types.h"

namespace Inferno {
    void WriteSegmentsToOrf(Level& level, span<SegID> segs, std::filesystem::path& path);
}
