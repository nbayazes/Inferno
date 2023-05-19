#pragma once

#include "Command.h"
#include "Settings.h"

namespace Inferno::Editor {
    // Adds the default size segment at the world origin
    SegID AddDefaultSegment(Level&, const Matrix& transform = Matrix::Identity);
    Tag TryDeleteSegment(Level& level, SegID id);
    void DeleteSegments(Level&, span<SegID> ids);

    SegID InsertSegment(Level&, Tag, int alignedToVert, InsertMode mode, const Vector3* offset = nullptr);

    bool CanAddFlickeringLight(Level&, Tag);

    void SetTextureFromDoorClip(Level&, Tag, DClipID);
    bool AddFlickeringLight(Level& level, Tag tag, FlickeringLight light);

    void BreakConnection(Level& level, Tag tag);
    void DetachSide(Level& level, Tag tag);

    bool SetSegmentType(Level& level, Tag tag, SegmentType type);

    namespace Commands {
        void AddEnergyCenter();
        void AddMatcen();
        void AddReactor();
        void AddSecretExit();
        void AddFlickeringLight();
        void RemoveFlickeringLight();

        extern Command InsertSegmentAtOrigin, InsertAlignedSegment;
        extern Command InsertMirrored;
        extern Command ExtrudeFaces, ExtrudeSegment, InsertSegment;
        extern Command JoinPoints, ConnectSides;
        extern Command DetachSegments, DetachSides;
        extern Command JoinSides, MergeSegment;
        extern Command SplitSegment2, SplitSegment3, SplitSegment5, SplitSegment7, SplitSegment8;
    }
}