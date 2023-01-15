#pragma once

#include "Types.h"
#include "Level.h"

namespace Inferno::Editor {
    struct DiagnosticInfo {
        string Message;
        Tag Tag;
        ObjID Object;
    };

    struct SegmentDiagnostic {
        int ErrorLevel;
        Tag Tag;
        string Message;
    };

    // Fixes common errors in a level
    void FixLevel(Level&);

    // Lowered from 90 degrees to 80 degrees due to false negatives
    constexpr float MAX_DEGENERACY = 80 * DegToRad;

    float CheckDegeneracy(const Level& level, const Segment& seg);

    List<SegmentDiagnostic> CheckObjects(Level& level);
    List<SegmentDiagnostic> CheckSegments(Level& level, bool fixErrors);
}