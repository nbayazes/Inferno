#include "pch.h"
#include "Game.Segment.h"
#include "Graphics/Render.h"
#include "Settings.h"

namespace Inferno {
    void ChangeLight(Level& level, const LightDeltaIndex& index, float multiplier = 1.0f) {
        for (int j = 0; j < index.Count; j++) {
            auto& dlp = level.LightDeltas[index.Index + j];
            assert(level.SegmentExists(dlp.Tag));
            auto& side = level.GetSide(dlp.Tag);

            for (int k = 0; k < 4; k++) {
                side.Light[k] += dlp.Color[k] * multiplier;
                ClampColor(side.Light[k], 0.0f, Settings::Lighting.MaxValue);
            }
        }

        Render::LevelChanged = true;
    }

    void SubtractLight(Level& level, Tag light, Segment& seg) {
        auto index = level.GetLightDeltaIndex(light);
        if (!index) return;

        if (seg.LightIsSubtracted(light.Side))
            return;

        seg.LightSubtracted |= (1 << (int)light.Side);
        ChangeLight(level, *index, -1);
    }

    void AddLight(Level& level, Tag light, Segment& seg) {
        auto index = level.GetLightDeltaIndex(light);
        if (!index) return;

        if (!seg.LightIsSubtracted(light.Side))
            return;

        seg.LightSubtracted &= ~(1 << (int)light.Side);
        ChangeLight(level, *index, 1);
    }

    void ToggleLight(Level& level, Tag light) {
        auto index = level.GetLightDeltaIndex(light);
        if (!index) return;

        auto& seg = level.GetSegment(light);
        if (seg.LightSubtracted & (1 << (int)light.Side)) {
            AddLight(level, light, seg);
        }
        else {
            SubtractLight(level, light, seg);
        }
    }

    void UpdateFlickeringLights(Level& level, float t, float dt) {
        for (auto& light : level.FlickeringLights) {
            auto& seg = level.GetSegment(light.Tag);

            if (seg.SideHasConnection(light.Tag.Side) && !seg.SideIsWall(light.Tag.Side))
                continue;

            if (light.Timer == FLT_MAX || light.Delay <= 0.001f)
                continue; // disabled

            light.Timer -= dt;

            if (light.Timer < 0) {
                while (light.Timer < 0) light.Timer += light.Delay;

                auto bit = 32 - (int)std::floor(t / light.Delay) % 32;

                if ((light.Mask >> bit) & 0x1) // shift to the bit and test it
                    AddLight(level, light.Tag, seg);
                else
                    SubtractLight(level, light.Tag, seg);
            }
        }
    }
}