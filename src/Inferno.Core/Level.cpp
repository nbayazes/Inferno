#include "pch.h"
#include "Level.h"
#include "Streams.h"

namespace Inferno {
    bool Level::HasSecretExit() const {
        for (auto& trigger : Triggers) {
            if (IsDescent1() && trigger.HasFlag(TriggerFlagD1::SecretExit))
                return true;
            else if (trigger.Type == TriggerType::SecretExit)
                return true;
        }

        return false;
    }


    List<SegID> Level::SegmentsByVertex(uint i) {
        List<SegID> segments;
        auto id = SegID(0);

        for (auto& seg : Segments) {
            for (auto& v : seg.Indices)
                if (v == i) segments.push_back(id);

            id = SegID((int)id + 1);
        }

        return segments;
    }
}
