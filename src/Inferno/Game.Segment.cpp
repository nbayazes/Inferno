#include "pch.h"
#include "Game.Segment.h"
#include "Editor/Editor.h"
#include "VisualEffects.h"
#include "Game.AI.Pathing.h"
#include "Game.h"
#include "Graphics.h"
#include "Resources.h"
#include "Settings.h"
#include "SoundSystem.h"
#include "logging.h"

namespace Inferno {
    void ChangeLight(Level& level, const LightDeltaIndex& index, float multiplier = 1.0f) {
        for (int j = 0; j < index.Count; j++) {
            auto& dlp = level.LightDeltas[index.Index + j];
            assert(level.SegmentExists(dlp.Tag));
            auto& side = level.GetSide(dlp.Tag);

            for (int k = 0; k < 4; k++) {
                side.Light[k] += dlp.Color[k] * multiplier;
                ClampColor(side.Light[k], 0.0f, Editor::EditorLightSettings.MaxValue);
            }
        }

        Graphics::NotifyLevelChanged();
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

    constexpr auto PLANE_DIST_TOLERANCE = FixToFloat(250);

    Array<PointID, 6> create_abs_vertex_lists(const Segment& seg, SideID side) {
        auto& indices = seg.Indices;
        auto& sidep = seg.GetSide(side);
        auto& sv = SIDE_INDICES[(int)side];
        Array<PointID, 6> vertices{};

        //Assert((segnum <= Highest_segment_index) && (segnum >= 0));

        switch (sidep.Type) {
            case SideSplitType::Quad:

                vertices[0] = indices[sv[0]];
                vertices[1] = indices[sv[1]];
                vertices[2] = indices[sv[2]];
                vertices[3] = indices[sv[3]];
                break;
            case SideSplitType::Tri02:
                vertices[0] = indices[sv[0]];
                vertices[1] = indices[sv[1]];
                vertices[2] = indices[sv[2]];

                vertices[3] = indices[sv[2]];
                vertices[4] = indices[sv[3]];
                vertices[5] = indices[sv[0]];
                break;
            case SideSplitType::Tri13:
                vertices[0] = indices[sv[3]];
                vertices[1] = indices[sv[0]];
                vertices[2] = indices[sv[1]];

                vertices[3] = indices[sv[1]];
                vertices[4] = indices[sv[2]];
                vertices[5] = indices[sv[3]];
                break;
        }

        return vertices;
    }

    // Returns a 6 bit mask indicating if the point is behind that side
    uint8 GetSideDistances(const Level& level, const Segment& seg, const Vector3& point, Array<float, 6>& distances) {
        uint8 mask = 0;

        for (int sn = 0, facebit = 1, sidebit = 1; sn < 6; sn++, sidebit <<= 1) {
            auto face = ConstFace::FromSide(level, seg, SideID(sn));
            auto& sideVerts = SIDE_INDICES[sn];

            auto vertex_list = create_abs_vertex_lists(seg, SideID(sn)); 

            if (face.Side.Type == SideSplitType::Tri02 || face.Side.Type == SideSplitType::Tri13) {
                int vertnum = std::min(vertex_list[0], vertex_list[2]);
                float dist = 0;

                if (vertex_list[4] < vertex_list[1]) {
                    Plane plane(level.Vertices[vertnum], face.Side.Normals[0]);
                    dist = plane.DotCoordinate(level.Vertices[vertex_list[4]]);
                }
                else {
                    Plane plane(level.Vertices[vertnum], face.Side.Normals[1]);
                    dist = plane.DotCoordinate(level.Vertices[vertex_list[1]]);
                }

                bool side_pokes_out = dist > PLANE_DIST_TOLERANCE;
                int center_count = 0;

                for (int fn = 0; fn < 2; fn++, facebit <<= 1) {
                    Plane plane(level.Vertices[vertnum], face.Side.Normals[fn]);
                    dist = plane.DotCoordinate(point);

                    if (dist < -PLANE_DIST_TOLERANCE) //in front of face
                    {
                        center_count++;
                        distances[sn] += dist;
                    }
                }

                if (!side_pokes_out) //must be behind both faces
                {
                    if (center_count == 2) {
                        mask |= sidebit;
                        distances[sn] /= 2; //get average
                    }
                }
                else //must be behind at least one face
                {
                    if (center_count) {
                        mask |= sidebit;
                        if (center_count == 2)
                            distances[sn] /= 2; //get average
                    }
                }
            }
            else {
                //only one face on this side
                //use lowest point number
                int vertnum = seg.Indices[sideVerts[0]];
                for (int i = 1; i < 4; i++)
                    if (seg.Indices[sideVerts[0]] < vertnum)
                        vertnum = seg.Indices[sideVerts[0]];

                Plane plane(level.Vertices[vertnum], face.Side.Normals[0]);
                float dist = plane.DotCoordinate(point);

                if (dist < -PLANE_DIST_TOLERANCE) {
                    mask |= sidebit;
                    distances[sn] = dist;
                }

                facebit <<= 2;
            }
        }

        return mask;
    }

    // Returns true if a point is inside of a segment
    bool SegmentContainsPoint(const Level& level, SegID id, const Vector3& point) {
        auto seg = level.TryGetSegment(id);
        if (!seg || level.Vertices.empty()) return false;

        Array<float, 6> distances{};
        auto mask = GetSideDistances(level, *seg, point, distances);
        return mask == 0;
    }

    SegID TraceSegmentInternal(const Level& level, SegID start, const Vector3& point, int iterations) {
        if (iterations > 512) {
            SPDLOG_ERROR("Trace depth limit reached, something is wrong");
            return SegID::None;
        }

        auto startSeg = level.TryGetSegment(start);
        if (!startSeg) {
            SPDLOG_ERROR("Trace start seg does not exist");
            return SegID::None;
        }

        Array<float, 6> distances{};
        auto mask = GetSideDistances(level, *startSeg, point, distances);
        if (mask == 0)
            return start; // in current seg

        //not in old seg.  trace through to find seg
        auto biggestSide = SideID::None;

        do {
            int bit = 1;
            auto& seg = level.GetSegment(start);

            biggestSide = SideID::None;
            fix biggest_val = 0;

            for (int sidenum = 0; sidenum < 6; sidenum++, bit <<= 1) {
                if ((mask & bit) && seg.SideHasConnection((SideID)sidenum)) {
                    if (distances[sidenum] < biggest_val) {
                        biggest_val = distances[sidenum];
                        biggestSide = (SideID)sidenum;
                    }
                }
            }

            if (biggestSide != SideID::None) {
                distances[(int)biggestSide] = 0;
                auto check = TraceSegmentInternal(level, seg.GetConnection(biggestSide), point, iterations + 1); //trace into adjacent segment

                if (check != SegID::None) //we've found a segment
                    return check;
            }
        } while (biggestSide != SideID::None);

        return SegID::None;
    }

    SegID TraceSegment(const Level& level, SegID start, const Vector3& point) {
        ASSERT(start != SegID::None);
        if (start == SegID::None) return SegID::None;
        int iterations = 0;
        return TraceSegmentInternal(level, start, point, iterations);
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
        struct SearchTag {
            SegID Seg;
            int Depth;
        };
        Stack<SearchTag> search;
        search.push({ start, 0 });

        while (!search.empty()) {
            SearchTag tag = search.top();
            search.pop();
            if (tag.Depth > maxDepth) continue;

            auto seg = level.TryGetSegment(tag.Seg);
            if (!seg) continue;

            nearby.insert(tag.Seg);

            for (auto& side : SIDE_IDS) {
                if (seg->SideIsWall(side) && Settings::Editor.Selection.StopAtWalls) continue;
                auto conn = seg->GetConnection(side);
                if (conn > SegID::None && !nearby.contains(conn)) {
                    search.push({ conn, tag.Depth + 1 });
                }
            }
        }

        return Seq::ofSet(nearby);
    }

    SegID FindContainingSegment(const Level& level, const Vector3& point) {
        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.GetSegment((SegID)id);
            if (Vector3::Distance(seg.Center, point) > 200) continue;

            if (SegmentContainsPoint(level, (SegID)id, point))
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
        if (side.LightOverride)
            return *side.LightOverride;

        auto& tmap1 = Resources::GetLevelTextureInfo(side.TMap);
        auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
        auto light = tmap1.Lighting + tmap2.Lighting;

        if (!enableColor)
            return { 1, 1, 1, light };

        Color baseColor(0, 0, 0, 0), overlayColor(0, 0, 0, 0);

        auto lightInfo1 = Resources::GetLightInfo(side.TMap);
        if (lightInfo1 && lightInfo1->Color != LIGHT_UNSET) {
            baseColor += lightInfo1->Color;
        }
        else if (tmap1.Lighting > 0) {
            baseColor += Resources::GetTextureInfo(side.TMap).AverageColor;
        }

        if (side.HasOverlay()) {
            auto lightInfo2 = Resources::GetLightInfo(side.TMap2);

            if (lightInfo2 && lightInfo2->Color != LIGHT_UNSET) {
                overlayColor = lightInfo2->Color;
            }
            else if (tmap2.Lighting > 0) {
                overlayColor = Resources::GetTextureInfo(side.TMap2).AverageColor;
            }
        }

        // add the colors after premultiplying but maintain the intensity separately
        /*float intensity = baseColor.w + overlayColor.w;
        baseColor.Premultiply();
        overlayColor.Premultiply();
        auto finalColor = baseColor + overlayColor;
        finalColor.w = intensity;*/
        return baseColor + overlayColor;
    }


    Vector3 RandomPointInSegment(const Level& level, const Segment& seg) {
        auto verts = seg.GetVertices(level);
        auto vert = verts[RandomInt((int)verts.size() - 1)];
        auto offset = *vert - seg.Center;
        return seg.Center + offset * Random() * 0.5f;
    }

    bool NewObjectIntersects(const Level& level, const Segment& seg, const Vector3& position, float radius, ObjectMask mask) {
        for (auto& objid : seg.Objects) {
            if (auto obj = level.TryGetObject(objid)) {
                if (!obj->PassesMask(mask))
                    continue;

                if (Vector3::Distance(obj->Position, position) < obj->Radius + radius)
                    return true;
            }
        }

        return false;
    }

    Tag FindExit(Level& level) {
        for (auto& wall : level.Walls) {
            if (auto trigger = level.TryGetTrigger(wall.Trigger)) {
                if (IsExit(level, *trigger))
                    return wall.Tag;
            }
        }

        return {};
    }
}
