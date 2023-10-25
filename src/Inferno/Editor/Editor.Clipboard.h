#pragma once

#include "Command.h"

namespace Inferno::Editor {
    struct SegmentClipboardData {
        List<Vector3> Vertices;
        List<Segment> Segments;
        List<Object> Objects;
        List<Wall> Walls;
        List<Trigger> Triggers;
        List<Matcen> Matcens;
        Matrix Reference;
    };

    SegmentClipboardData CopySegments(Level& level, span<SegID> segments, bool segmentsOnly = false);
    void PasteSegmentsInPlace(Level&, const SegmentClipboardData&, bool markSegs = false);

    namespace Commands {
        extern Command Cut, Copy, Paste;
        extern Command MirrorSegments, PasteMirrored;
    }
}