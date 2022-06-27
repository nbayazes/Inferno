#pragma once
#include "Command.h"

namespace Inferno::Editor {
    // Resets UVs of a face, aligning it to the specified edge. Angle applies an additional rotation.
    void ResetUVs(Level&, Tag, int edge = 0, float angle = 0);

    void ResetSegmentUVs(Level& level, IEnumerable<SegID> auto segs, int edge = 0, float angle = 0) {
        for (auto& seg : segs)
            for (auto& side : SideIDs)
                ResetUVs(level, { seg, side }, edge, angle);
    }

    void OnTransformTextures(Level&, const TransformGizmo&);

    namespace Commands {
        void FlipTextureU();
        void FlipTextureV();
        void RotateOverlay();
        extern Command ResetUVs, AlignMarked;
        extern Command CopyUVsToFaces, PlanarMapping, CubeMapping;
    }
}