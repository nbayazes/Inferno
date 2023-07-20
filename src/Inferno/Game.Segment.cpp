#include "pch.h"
#include "Game.Segment.h"

#include "Resources.h"
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
                ClampColor(side.Light[k], 0.0f, Settings::Editor.Lighting.MaxValue);
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

    // Returns true if a point is inside of a segment
    bool PointInSegment(Level& level, SegID id, const Vector3& point) {
        if (!level.SegmentExists(id)) return false;

        // Use estimation that treats the sides as planes instead of triangles
        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, id, side);
            if (face.Distance(point) < 0)
                return false;
        }

        return true;
    }

    bool IsSecretExit(const Level& level, const Trigger& trigger) {
        if (level.IsDescent1())
            return trigger.HasFlag(TriggerFlagD1::SecretExit);
        else
            return trigger.Type == TriggerType::SecretExit;
    }

    bool IsExit(const Level& level, const Trigger& trigger) {
        if (level.IsDescent1())
            return trigger.HasFlag(TriggerFlagD1::Exit);
        else
            return trigger.Type == TriggerType::Exit;
    }

    List<SegID> GetConnectedSegments(Level& level, SegID start, int maxDepth) {
        Set<SegID> nearby;
        struct SearchTag { SegID Seg; int Depth; };
        Stack<SearchTag> search;
        search.push({ start, 0 });

        while (!search.empty()) {
            SearchTag tag = search.top();
            search.pop();
            if (tag.Depth > maxDepth) continue;

            auto seg = level.TryGetSegment(tag.Seg);
            if (!seg) continue;

            nearby.insert(tag.Seg);

            for (auto& side : SideIDs) {
                if (seg->SideIsWall(side) && Settings::Editor.Selection.StopAtWalls) continue;
                auto conn = seg->GetConnection(side);
                if (conn > SegID::None && !nearby.contains(conn)) {
                    search.push({ conn, tag.Depth + 1 });
                }
            }
        }

        return Seq::ofSet(nearby);
    }

    SegID FindContainingSegment(Level& level, const Vector3& point) {
        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.GetSegment((SegID)id);
            if (Vector3::Distance(seg.Center, point) > 200) continue;

            if (PointInSegment(level, (SegID)id, point))
                return (SegID)id;
        }

        return SegID::None;
    }

    short GetPairedEdge(Level& level, Tag tag, short point) {
        auto other = level.GetConnectedSide(tag);
        if (!level.SegmentExists(tag) || !other) return 0;

        auto [seg, side] = level.GetSegmentAndSide(tag);
        //auto face = Face::FromSide(level, tag);
        auto srcIndices = seg.GetVertexIndices(tag.Side);
        auto i0 = srcIndices[point % 4];
        auto i1 = srcIndices[(point + 1) % 4];

        auto& otherSeg = level.GetSegment(other);
        auto otherIndices = otherSeg.GetVertexIndices(other.Side);

        for (short i = 0; i < 4; i++) {
            if ((i0 == otherIndices[i] && i1 == otherIndices[(i + 1) % 4]) ||
                (i1 == otherIndices[i] && i0 == otherIndices[(i + 1) % 4]))
                return i;
        }

        return 0;
    }

    // Returns the light contribution from both textures on this side
    Color GetLightColor(const SegmentSide& side, bool enableColor) {
        if (side.LightOverride) return *side.LightOverride;

        auto& tmap1 = Resources::GetLevelTextureInfo(side.TMap);
        auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
        auto light = tmap1.Lighting + tmap2.Lighting;

        if (!enableColor)
            return { light, light, light };

        Color color;

        auto lightInfo1 = Seq::findKey(Resources::LightInfoTable, side.TMap);
        if (lightInfo1 && lightInfo1->Color != Color(0,0,0)) {
            color += lightInfo1->Color;
        }
        else if (tmap1.Lighting > 0) {
            color += Resources::GetTextureInfo(side.TMap).AverageColor;
        }

        if (side.HasOverlay()) {
            auto lightInfo2 = Seq::findKey(Resources::LightInfoTable, side.TMap2);
            if (lightInfo2 && lightInfo2->Color != Color(0, 0, 0)) {
                color += lightInfo2->Color;
            }
            else if (tmap2.Lighting > 0) {
                color += Resources::GetTextureInfo(side.TMap2).AverageColor;
            }
        }

        color.w = 0;
        return color /** light*/;
    }
}
