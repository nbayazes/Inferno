#pragma once

#include "Command.h"
#include "Settings.h"

namespace Inferno::Editor {
    // Adds the default size segment at the world origin
    SegID AddDefaultSegment(Level&);
    Tag TryDeleteSegment(Level& level, SegID id);
    void DeleteSegments(Level&, span<SegID> ids);

    bool PointInSegment(Level& level, SegID id, const Vector3& point);
    SegID InsertSegment(Level&, Tag, int alignedToVert, InsertMode mode, const Vector3* offset = nullptr);

    SegID FindContainingSegment(Level& level, const Vector3& point);
    bool CanAddFlickeringLight(Level&, Tag);

    bool IsSecretExit(const Trigger& trigger);
    bool IsExit(const Trigger& trigger);

    void SetTextureFromDoorClip(Level&, Tag, DClipID);
    bool AddFlickeringLight(Level& level, Tag tag, FlickeringLight light);

    void BreakConnection(Level& level, Tag tag);
    void DetachSide(Level& level, Tag tag);

    // Tries to return a segment connected to this one
    SegID GetConnectedSegment(Level&, SegID);
    bool SetSegmentType(Level& level, Tag tag, SegmentType type);

    List<SegID> GetConnectedSegments(Level& level, SegID start, int maxDepth = 2);

    namespace Commands {
        void AddEnergyCenter();
        void AddMatcen();
        void AddReactor();
        void AddSecretExit();
        void AddFlickeringLight();
        void RemoveFlickeringLight();
        void AddDefaultSegment();
        
        extern Command InsertMirrored;
        extern Command ExtrudeFaces, ExtrudeSegment, InsertSegment;
        extern Command JoinPoints, ConnectSides;
        extern Command DetachSegments, DetachSides;
        extern Command JoinSides, MergeSegment;
        extern Command SplitSegment2, SplitSegment5, SplitSegment7, SplitSegment8;
    }
}