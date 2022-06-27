#pragma once

#include "Level.h"
#include "Types.h"
#include "Gizmo.h"
#include "Editor.Undo.h"
#include "Game.h"
#include "Command.h"

namespace Inferno::Editor {
    // Returns the matching edge of the connected seg and side of the provided tag. Also returns 0 if not found.
    short GetPairedEdge(Level&, Tag, short point);

    void DeleteSegment(Level&, SegID);
    void DeleteVertex(Level&, PointID);

    struct VertexReplacement { PointID Old, New; };
    void ReplaceVertices(Level&, span<VertexReplacement>);

    void JoinTouchingSegments(Level&, SegID, span<SegID>, float tolerance, bool skipValidation = false);
    void JoinTouchingSegmentsExclusive(Level&, span<Tag>, float tolerance);

    bool PruneVertices(Level&);

    // Merges overlapping verts for segments
    void WeldVertices(Level&, span<SegID>, float tolerance);
    // Welds only the vertices between connected faces
    void WeldConnection(Level& level, Tag srcid, float tolerance);
    // Merges overlapping verts of open sides
    void WeldVerticesOfOpenSides(Level&, span<SegID>, float tolerance);

    List<SegID> GetNearbySegments(Level&, SegID, float distance = 150);
    List<SegID> GetNearbySegmentsExclusive(Level&, span<SegID>, float distance = 150);

    void ApplyNoise(Level& level, List<PointID> points, float scale, const Vector3& strength, int64 seed);

    List<SegID> ExtrudeFaces(Level& level, span<Tag> faces, const Vector3& offset);
    bool BeginExtrude(Level& level);
    void UpdateExtrudes(Level&, const TransformGizmo&);
    bool FinishExtrude(Level& level, const TransformGizmo&);
    void TransformSelection(Level&, const TransformGizmo&);

    namespace Commands {
        void ApplyNoise(float scale, const Vector3& strength, int64 seed);

        void WeldVertices();
        void SnapToGrid();

        // Joins nearby segment faces that overlap with the selected segment
        extern Command MakeCoplanar;
        extern Command JoinTouchingSegments;
        extern Command DetachPoints;
    }
}
