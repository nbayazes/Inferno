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

    bool SegmentIsDegenerate(Level& level, Segment& seg);

    List<SegmentDiagnostic> CheckObjects(Level& level);
    List<SegmentDiagnostic> CheckSegments(Level& level, bool fixErrors);
    void SetPlayerStartIDs(Level& level);
}