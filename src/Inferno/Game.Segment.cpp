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


    Array<float, 6> GetSideDistances(const Level& level, const Segment& seg, const Vector3& point) {
        Array<float, 6> distances{};

        for (auto& sideId : SIDE_IDS) {
            auto& dist = distances[(int)sideId];
            auto face = ConstFace::FromSide(level, seg, sideId);
            if (face.Area() < 0.01f)
                continue;

            if (face.Side.Type == SideSplitType::Tri02) {
                Plane p0(face[1], face.Side.Normals[0]);
                Plane p1(face[3], face.Side.Normals[1]);
                bool concave = p0.DotCoordinate(face[3]) > 0; // other triangle point is in front of plane
                auto d0 = p0.DotCoordinate(point);
                auto d1 = p1.DotCoordinate(point);

                // when concave the point is outside if point is behind either plane
                // when convex point must be behind both planes
                if (concave) {
                    dist = std::min({ dist, d0, d1 });
                }
                else if (d0 < 0 && d1 < 0) {
                    dist = std::min({ dist, d0, d1 });
                }
            }
            else if (face.Side.Type == SideSplitType::Tri13) {
                Plane p0(face[0], face.Side.Normals[0]);
                Plane p1(face[2], face.Side.Normals[1]);
                bool concave = p0.DotCoordinate(face[2]) > 0; // other triangle point (2) is in front of plane
                auto d0 = p0.DotCoordinate(point);
                auto d1 = p1.DotCoordinate(point);

                if (concave) {
                    dist = std::min({ dist, d0, d1 });
                }
                else if (d0 < 0 && d1 < 0) {
                    dist = std::min({ dist, d0, d1 });
                }
            }
            else {
                Plane p(face.Side.Center, face.Side.AverageNormal);
                dist = std::min(dist, p.DotCoordinate(point));
            }
        }

        return distances;
    }

    // Returns true if a point is inside of a segment
    bool SegmentContainsPoint(const Level& level, SegID id, const Vector3& point) {
        auto seg = level.TryGetSegment(id);
        if (!seg || level.Vertices.empty()) return false;

        auto distances = GetSideDistances(level, *seg, point);
        return ranges::all_of(distances, [](float d) { return d >= 0; });

        //for (auto& d : distances) {
        //    if (d < 0) return false;
        //}

        /*if (!ranges::all_of(distances, [](float d) { return d >= 0; }))
            return false;

        return true;*/

        //// Check if the point is in front of all triangles of the segment
        //return ranges::all_of(SideIDs, [&](SideID sideId) {
        //    auto& side = level.GetSide(Tag{ id, sideId });
        //    auto face = Face2::FromSide(level, Tag{ id, sideId });
        //    if (side.Type == SideSplitType::Tri02) {
        //        Plane p0(face[1], face.Side->Normals[0]);
        //        if (p0.DotCoordinate(point) < 0) return false;

        //        Plane p1(face[3], face.Side->Normals[1]);
        //        if (p1.DotCoordinate(point) < 0) return false;
        //        return true;
        //    }
        //    else if (side.Type == SideSplitType::Tri13) {
        //        Plane p0(face[0], face.Side->Normals[0]);
        //        if (p0.DotCoordinate(point) < 0) return false;

        //        Plane p1(face[2], face.Side->Normals[1]);
        //        if (p1.DotCoordinate(point) < 0) return false;
        //        return true;
        //    }
        //    else {
        //        Plane p(side.Center, side.AverageNormal);
        //        return p.DotCoordinate(point) > 0;
        //    }
        //});

        //for (auto& sideId : SideIDs) {
        //    auto& side = level.GetSide(Tag{ id, sideId });
        //    Plane p(side.Center, side.AverageNormal);
        //    if (p.DotCoordinate(point) < 0)
        //        return false;
        //}

        //return true;
    }

    SegID TraceSegmentInternal(const Level& level, SegID start, const Vector3& point, int iterations) {
        if (start == SegID::None || start == SegID::Terrain || start == SegID::Exit)
            return start; // passthrough special segments

        //ASSERT(iterations <= 50);
        if (iterations > 50) {
            SPDLOG_ERROR("Trace depth limit reached, something is wrong");
            return start;
        }

        auto startSeg = level.TryGetSegment(start);
        if (!startSeg) {
            SPDLOG_ERROR("Trace start seg {} does not exist", start);
            return start;
        }

        auto distances = GetSideDistances(level, *startSeg, point);
        if (ranges::all_of(distances, [](float d) { return d >= -0.001f; }))
            return start;

        auto biggestSide = SideID::None;

        uint biggestSideIterations = 0;

        do {
            biggestSide = SideID::None;
            if (biggestSideIterations++ > 50) {
                // Rarely the trace function can get completely stuck finding the biggest side
                SPDLOG_ERROR("Trace depth biggest side iteration limit reached, something is wrong");
                return SegID::None;
            }

            auto seg = level.TryGetSegment(start);
            float biggestVal = 0;

            if (!seg) {
                SPDLOG_WARN("Invalid trace segment {}", start);
                return start;
            }

            for (auto& sid : SIDE_IDS) {
                if (distances[(int)sid] < biggestVal) {
                    biggestVal = distances[(int)sid];
                    biggestSide = sid;
                }
            }

            if (biggestSide != SideID::None) {
                distances[(int)biggestSide] = 0;
                auto check = TraceSegmentInternal(level, seg->GetConnection(biggestSide), point, iterations + 1);
                if (check != SegID::None)
                    return check;
            }
        }
        while (biggestSide != SideID::None);

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
        auto& ti = Resources::GetTextureInfo(side.TMap);

        auto lightInfo1 = Resources::GetLightInfo(ti.Name);
        if (lightInfo1 && lightInfo1->Color != LIGHT_UNSET) {
            baseColor += lightInfo1->Color;
        }
        else if (tmap1.Lighting > 0) {
            baseColor += Resources::GetTextureInfo(side.TMap).AverageColor;
        }

        if (side.HasOverlay()) {
            auto& ti2 = Resources::GetTextureInfo(side.TMap2);
            auto lightInfo2 = Resources::GetLightInfo(ti2.Name);

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

    void RelinkEnvironments(Level& level) {
        for (auto& seg : level.Segments) {
            seg.Environment = EnvironmentID::None;
        }

        for (uint16 id = 0; id < level.Environments.size(); id++) {
            for (auto& segid : level.Environments[id].segments) {
                if (auto seg = level.TryGetSegment(segid)) {
                    seg->Environment = (EnvironmentID)id;
                }
            }
        }
    }
}
