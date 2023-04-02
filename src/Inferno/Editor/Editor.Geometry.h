#pragma once

#include "Level.h"
#include "Types.h"
#include "Gizmo.h"
#include "Editor.Undo.h"
#include "Game.h"
#include "Command.h"

namespace Inferno::Editor {
    // Returns the matching edge of the connected seg and side of the provided tag.
    // Returns 0 if not found.
    short GetPairedEdge(Level&, Tag, uint16 point);

    void DeleteSegment(Level&, SegID);
    void DeleteVertex(Level&, PointID);

    struct VertexReplacement { PointID Old, New; };
    void ReplaceVertices(Level&, span<VertexReplacement>);

    // Tries to join the source segment to all provided segments
    void JoinTouchingSegments(Level&, SegID, span<SegID>, float tolerance, bool skipValidation = false);

    // Joins all segments nearby to each segment excluding segments in the source
    void JoinTouchingSegmentsExclusive(Level&, span<Tag>, float tolerance);

    bool PruneVertices(Level&);

    // Tries to weld vertices in src based on tolerance.
    // Returns the number of vertices welded.
    int WeldVertices(Level& level, span<PointID> src, float tolerance);

    // Merges overlapping verts for segments
    void WeldVertices(Level&, span<SegID>, float tolerance);

    // Welds vertices from src to connected vertices.
    // Returns true if any points were welded
    bool WeldConnection(Level& level, Tag srcid, float tolerance);

    // Merges overlapping verts of open sides
    void WeldVerticesOfOpenSides(Level&, span<SegID>, float tolerance);

    List<SegID> GetNearbySegments(Level&, SegID, float distance = 150);
    List<SegID> GetNearbySegmentsExclusive(Level&, span<SegID>, float distance = 150);

    void ApplyNoise(Level& level, span<PointID> points, float scale, const Vector3& strength, int64 seed);

    List<SegID> ExtrudeFaces(Level& level, span<Tag> faces, const Vector3& offset);
    bool BeginExtrude(Level& level);
    void UpdateExtrudes(Level&, const TransformGizmo&);
    bool FinishExtrude(Level& level, const TransformGizmo&);
    void TransformSelection(Level&, const TransformGizmo&);

    namespace Commands {
        void ApplyNoise(float scale, const Vector3& strength, int64 seed);
        void SnapToGrid();

        extern Command WeldVertices;
        extern Command MakeCoplanar;
        // Joins nearby segment faces that overlap with the selected segment
        extern Command JoinTouchingSegments;
        extern Command DetachPoints;
    }
}
