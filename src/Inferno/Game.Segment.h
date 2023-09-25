#pragma once
#include "Level.h"

namespace Inferno {
    void SubtractLight(Level& level, Tag light, Segment& seg);
    void AddLight(Level& level, Tag light, Segment& seg);
    void ToggleLight(Level& level, Tag light);
    void UpdateFlickeringLights(Level& level, float t, float dt);
    bool PointInSegment(const Level& level, SegID id, const Vector3& point);
    bool IsSecretExit(const Level& level, const Trigger& trigger);
    bool IsExit(const Level& level, const Trigger& trigger);

    // Returns connected segments up to a depth
    List<SegID> GetConnectedSegments(Level& level, SegID start, int maxDepth = 2);

    SegID FindContainingSegment(const Level& level, const Vector3& point);

    // Returns the matching edge of the connected seg and side of the provided tag.
    // Returns 0 if not found.
    short GetPairedEdge(Level&, Tag, short point);

    Color GetLightColor(const SegmentSide& side, bool enableColor);
    void CreateMatcenEffect(const Level& level, SegID segId);
    void UpdateMatcens(Level& level, float dt);
    void TriggerMatcen(Level& level, SegID segId, SegID triggerSeg);
    void InitializeMatcens(Level& level);
}
