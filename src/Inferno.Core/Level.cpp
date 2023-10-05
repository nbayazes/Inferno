#include "pch.h"
#include "Level.h"

namespace Inferno {
    List<int8> Matcen::GetEnabledRobots() const {
        List<int8> enabled;
        enabled.reserve(8);

        for (int8 i = 0; i < 2; i++) {
            int8 robotIndex = i * 32;
            auto flags = i == 0 ? Robots : Robots2;
            while (flags) {
                if (flags & 1)
                    enabled.push_back(robotIndex);
                flags >>= 1;
                robotIndex++;
            }
        }

        return enabled;
    }

    bool Level::HasSecretExit() const {
        for (auto& trigger : Triggers) {
            if (IsDescent1() && trigger.HasFlag(TriggerFlagD1::SecretExit))
                return true;
            else if (trigger.Type == TriggerType::SecretExit)
                return true;
        }

        return false;
    }


    List<SegID> Level::SegmentsByVertex(uint i) const {
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
