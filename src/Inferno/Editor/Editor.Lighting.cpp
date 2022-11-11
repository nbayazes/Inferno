#include "pch.h"
#include "Types.h"
#include "Level.h"
#include "Utility.h"
#include "Resources.h"
#include "Game.h"
#include "Editor.h"
#include "ScopedTimer.h"
#include "WindowsDialogs.h"
#include "Editor.Segment.h"

namespace Inferno::Editor {
    constexpr float PlaneTolerance = -0.01f;

    // Scales a color down to a max brightness while retaining color
    constexpr void ScaleColor(Color& color, float maxValue) {
        auto max = std::max({ color.x, color.y, color.z });
        if (max > 1)
            color *= maxValue / max;
    }

    // Scales a color up or down to target brightness
    constexpr void ScaleColor2(Color& color, float target) {
        auto max = std::max({ color.x, color.y, color.z });
        if (max < 0.1f) color = { target, target, target };
        else color *= target / max;
    }

    void ClampColor(Color& src, const Color& min = Color(0, 0, 0, 0), const Color& max = Color(1, 1, 1)) {
        src.x = std::clamp(src.x, min.x, max.x);
        src.y = std::clamp(src.y, min.y, max.y);
        src.z = std::clamp(src.z, min.z, max.z);
        src.w = std::clamp(src.w, min.w, max.w);
    }

    float GetBrightness(const Color& color) {
        return (color.x + color.y + color.z) / 3;
    }

    float AverageBrightness(SideLighting side) {
        auto avg = AverageColors(side);
        return GetBrightness(avg);
    }

    Array<Vector3, 4> InsetTowardsPoint(const Vector3& center, const Face& face, float distance = 1) {
        Array<Vector3, 4> result;
        for (int i = 0; i < 4; i++) {
            auto vec = center - face[i];
            vec.Normalize();
            result[i] = face[i] + vec * distance;
        }
        return result;
    }

    Array<Vector3, 4> InsetTowardsPointPercentage(const Vector3& center, const Face& face, float percent) {
        Array<Vector3, 4> result;
        for (int i = 0; i < 4; i++) {
            auto vec = center - face[i];
            result[i] = face[i] + vec * percent;
        }
        return result;
    }

    struct LightSource {
        Tag Tag;
        Array<uint16, 4> Indices{}; // Which vertices emit light?
        Array<Color, 4> Colors{}; // Need per-vertex colors because intensity can vary due to ReduceCoplanarBrightness()
        bool IsDynamic = false; // Is this source destroyable?
        float Radius = 20;
        float LightPlaneTolerance = -0.45f;
        bool EnableOcclusion = true;
        float DynamicMultiplier = 1; // To reduce the intensity of flickering lights

        Color MaxBrightness() const {
            Color max;
            for (auto& c : Colors)
                if (max.ToVector3().Length() < c.ToVector3().Length()) max = c;
            return max;
        }
    };

    // light info during ray casting
    struct LightRayCast {
        Dictionary<Tag, SideLighting> Accumulated; // Accumulated light for all passes
        Dictionary<Tag, SideLighting> Pass; // Light for this pass, cleared after each iteration
        // Maximum value of light in the pass.
        // This prevents faces adjacent to a light source exceeding the source brightness.
        Color PassMaxValue;
        const LightSource* Source = nullptr;

        void AddLight(Tag tag, const Color& light, int16 point) {
            Pass[tag][point] += light;
        }

        void UpdateMaxValueFromPass(float reflectance) {
            Color max;
            for (auto& colors : Pass | views::values)
                for (auto& color : colors)
                    if (max.ToVector3().Length() < color.ToVector3().Length())
                        max = color;

            PassMaxValue = max * reflectance;
        }

        // Accumulates lighting from the pass
        void AccumulatePass(bool keep = true) {
            for (auto& [dest, target] : Pass) {
                for (auto& light : target)
                    ClampColor(light, { 0, 0, 0, 0 }, PassMaxValue);

                auto& light = Accumulated[dest]; // will create in place if missing

                if (keep) {
                    for (int i = 0; i < 4; i++)
                        light[i] += target[i]; // change to assignment instead of sum to view the final pass contribution
                }
            }
        }
    };

    // checks that there's enough light to bother saving. Prevents wasteful raycasts.
    constexpr bool CheckMinLight(const Color& color) {
        return color.x + color.y + color.z >= 0.001f;
    }

    Dictionary<Tag, LightRayCast> RayCasts;

    // Key is a combination of src seg, src vertex and dest vertex. Value indicates if dest is visible.
    Dictionary<int64, bool> HitTests;

    // Returns sides that are coplanar to the source within an angle
    List<Tag> FindCoplanarSides(const Level& level, Tag src, float thresholdAngle = 10.0f, bool sameTexture = false) {
        Set<Tag> coplanar;
        Set<Tag> scanned;
        Stack<Tag> toScan;
        toScan.push(src);

        while (!toScan.empty()) {
            auto& tag = toScan.top();
            toScan.pop();
            coplanar.insert(tag); // if we're scanning it, it must be planar
            scanned.insert(tag);
            auto& seg = level.GetSegment(tag.Segment);
            auto& side = seg.GetSide(tag.Side);

            for (auto& cid : seg.Connections) {
                if (cid == SegID::None || cid == SegID::Exit) continue;
                auto& conn = level.GetSegment(cid);

                for (auto& csid : SideIDs) {
                    Tag target{ cid, csid };
                    if (scanned.contains(target)) continue; // skip already scanned sides

                    auto& cside = conn.GetSide(csid);
                    float angle = acos(side.AverageNormal.Dot(cside.AverageNormal)) * RadToDeg;
                    if (angle < thresholdAngle) {
                        if (sameTexture && !(side.TMap == cside.TMap && side.TMap2 == cside.TMap2))
                            continue;

                        toScan.push(target);
                    }
                }
            }
        }

        return Seq::ofSet(coplanar);
    }

    constexpr float Attenuate1(float dist, float a = 0, float b = 1) {
        return 1.0f / (1.0f + a * dist + b * dist * dist);
    }

    // Returns the falloff using a cutoff 
    constexpr float Attenuate2(float dist, float radius, float cutoff) {
        // https://imdoingitwrong.wordpress.com/2011/01/31/light-attenuation/
        float denom = dist / radius + 1;
        float atten = 1 / (denom * denom);
        // scale and bias attenuation such that:
        //   attenuation == 0 at extent of max influence
        //   attenuation == 1 when d == 0
        atten = (atten - cutoff) / (1 - cutoff);
        return std::max(atten, 0.0f);
    }

    // Original light eq
    float Attenuate0(float dist, float lightDot, float constant) {
        return constant * std::powf(lightDot, 2) / dist;
    }

    // Returns true if light can pass through this side. Depends on the connections, texture and wall type if present.
    bool LightPassesThroughSide(const Level& level, const Segment& seg, SideID sideId) {
        auto& side = seg.GetSide(sideId);
        auto connection = seg.GetConnection(sideId);
        if (connection == SegID::None || connection == SegID::Exit) return false; // solid wall

        if (side.Wall == WallID::None) return true; // not a wall and this side is open

        auto& wall = level.GetWall(side.Wall);
        if (wall.BlocksLight) return !(*wall.BlocksLight); // User defined

        switch (wall.Type) {
            case WallType::Cloaked:
            case WallType::Open:
                return true;

            case WallType::Door:
                if (side.HasOverlay()) {
                    auto& tmap2 = Resources::GetTextureInfo(side.TMap2);
                    return tmap2.SuperTransparent;
                }
                return false;

            case WallType::WallTrigger: // triggers are always on a solid wall
                return false;

            default:
            {
                // Check if the textures are transparent
                auto& tmap1 = Resources::GetTextureInfo(side.TMap);
                bool transparent = tmap1.Transparent;

                if (side.HasOverlay()) {
                    auto& tmap2 = Resources::GetTextureInfo(side.TMap2);
                    transparent |= tmap2.SuperTransparent;
                }

                return transparent;
            }
        }
    }

    bool SideIsVisible(const Level& level, const Segment& seg, SideID sideId) {
        auto connection = seg.GetConnection(sideId);
        if (connection == SegID::None || connection == SegID::Exit) return true; // solid wall

        auto& side = seg.GetSide(sideId);
        if (side.Wall == WallID::None) return false; // no wall

        auto& wall = level.GetWall(side.Wall);
        switch (wall.Type) {
            case WallType::Open:
            case WallType::None:
                return false;
            default:
                return true;
        }
    }

    // Returns the light contribution from both textures on this side
    Color GetLightColor(const SegmentSide& side) {
        if (side.LightOverride) return *side.LightOverride;

        auto& tmap1 = Resources::GetLevelTextureInfo(side.TMap);
        auto light = tmap1.Lighting;

        Color color;
        if (tmap1.Lighting > 0) {
            auto& tmap1i = Resources::GetTextureInfo(side.TMap);
            color += tmap1i.AverageColor;
            return Color(tmap1.Lighting, tmap1.Lighting, tmap1.Lighting);
        }

        if (side.HasOverlay()) {
            auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
            light += tmap2.Lighting;
            if (tmap2.Lighting > 0) {
                auto& tmap2i = Resources::GetTextureInfo(side.TMap2);
                color += tmap2i.AverageColor;
                return Color(tmap2.Lighting, tmap2.Lighting, tmap2.Lighting);
            }
        }

        color.w = 0;
        return color * light;
    }

    // Returns segments that are within range and visible from the source surface.
    // Culls segments that are behind the plane of src.
    Set<SegID> GetSegmentsInRange(Level& level, Tag src, float distanceThreshold) {
        auto srcFace = Face::FromSide(level, src);

        Set<SegID> segmentsToLight;
        segmentsToLight.insert(src.Segment);

        Stack<SegID> segmentsToSearch;
        segmentsToSearch.push(src.Segment);

        while (!segmentsToSearch.empty()) {
            auto segId = segmentsToSearch.top();
            segmentsToSearch.pop();
            auto& seg = level.GetSegment(segId);
            segmentsToLight.insert(segId);

            for (auto& sideId : SideIDs) {
                if (!LightPassesThroughSide(level, seg, sideId)) continue;
                auto connection = seg.GetConnection(sideId);
                if (segmentsToLight.contains(connection)) continue; // Don't add visited connections

                if (src.Segment == segId) {
                    // always search valid connections from source (fix for zero volume segments)
                    segmentsToSearch.push(connection);
                    continue;
                }

                auto portal = Face::FromSide(level, segId, sideId);
                auto inset = portal.Inset(1, 1); // inset the portal verts so light doesn't wrap around corners

                bool found = false;

                for (int i = 0; i < 4; i++) {
                    // is the portal vert behind the light source?
                    auto planeDist = DistanceFromPlane(inset[i], srcFace.Center(), srcFace.AverageNormal());
                    if (planeDist < PlaneTolerance) continue;

                    // Don't travel through sides that are too far
                    for (int j = 0; j < 4; j++) {
                        if (Vector3::Distance(srcFace[j], portal[i]) <= distanceThreshold) {
                            segmentsToSearch.push(connection);
                            found = true;
                            break;
                        }
                    }

                    if (found) break;
                }
            }
        }

        return segmentsToLight;
    }

    // Returns true if the ray intersects any faces of the segment
    bool HitTestRay(Level& level, const Set<SegID>& segments, const Ray& ray, float minDist) {
        for (auto& segId : segments) {
            const auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                if (LightPassesThroughSide(level, seg, sideId)) continue; // ignore sides that light passes through
                const auto& side = seg.GetSide(sideId);
                bool sideIsWall = side.Wall != WallID::None;
                if (sideIsWall && side.Normals[0].Dot(ray.direction) > 0) continue; // skip walls pointing the same direction (allows passing through one-way walls)

                auto ri = side.GetRenderIndices();
                auto indices = seg.GetVertexIndices(sideId);
                float dist{};

                Metrics::RaysCast++;
                if (ray.Intersects(level.Vertices[indices[ri[0]]],
                                   level.Vertices[indices[ri[1]]],
                                   level.Vertices[indices[ri[2]]],
                                   dist)
                    && dist < minDist) {
                    Metrics::RayHits++;
                    return true;
                }

                Metrics::RaysCast++;
                if (ray.Intersects(level.Vertices[indices[ri[3]]],
                                   level.Vertices[indices[ri[4]]],
                                   level.Vertices[indices[ri[5]]],
                                   dist)
                    && dist < minDist) {
                    Metrics::RayHits++;
                    return true;
                }
            }
        }

        return false;
    }

    // Returns true if geometry blocks the path between src point and light. Caches results.
    bool HitTest(Level& level,
                 const Set<SegID>& segments,
                 PointID destPoint,
                 PointID lightPoint,
                 const Vector3& lightPos,
                 const Vector3& samplePos,
                 Tag src,
                 Tag dest) {
        if (src.Segment == dest.Segment) return false;

        if ((int)src.Segment > 32767 || (int)dest.Segment > 32767 || (int)destPoint > 46339 || (int)lightPoint > 46339)
            throw Exception("Lighting only supports up to 32767 segments and 46339 verts");

        auto packedSegId = SzudzikPairing((uint16)src.Segment, (uint16)dest.Segment); // limited to 32767 (28 bit result)
        auto packedPointId = SzudzikPairing((uint16)destPoint, lightPoint); // limited to 46339 (30 bit result)
        uint64 id = (uint64)dest.Side << (28 + 30 + 3) | (uint64)src.Side << (28 + 30) | (uint64)packedPointId << 28 | (uint64)packedSegId;

        //// Note that this packing breaks if there are more than 8191 segments in a level
        //uint16 packedSrc = (uint16)src.Segment | ((uint16)src.Side << (16 - 3)); // pack side into the 3 high bits
        //uint16 packedDest = (uint16)dest.Segment | ((uint16)dest.Side << (16 - 3)); // pack side into the 3 high bits
        //uint64 id = (uint64)packedDest << 48 | (uint64)packedSrc << 32 | (uint64)destPoint << 16 | lightPoint;

        if (!HitTests.contains(id)) {
            auto dir = samplePos - lightPos;
            float minDist = dir.Length() - 0.01f; // minimum distance the light must travel. hitting something before this means a wall was in the way.
            dir.Normalize();

            bool result = false;
            // Direction length can be zero if segment has zero volume, assume it misses
            Ray ray(lightPos, dir);
            result = dir.Length() != 0 ? HitTestRay(level, segments, ray, minDist) : false;

            HitTests[id] = result;
            return result;
        }
        else {
            Metrics::CacheHits++;
            return HitTests[id];
        }
    }

    void LightSegments(Level& level,
                       const SideLighting& lightColors,
                       const LightSettings& settings,
                       Set<SegID> segmentsToLight,
                       Tag src,
                       bool bouncePass, // is this a bounce light pass?
                       LightRayCast& cast) {
        auto [srcSeg, srcSide] = level.GetSegmentAndSide(src);
        auto center = srcSeg.Center;
        const auto srcFace = Face::FromSide(level, srcSeg, src.Side);

        // Move occlusion sample points off of faces to improve light wrapping around corners
        auto lightSamples = InsetTowardsPointPercentage(srcFace.Center() + srcFace.AverageNormal() * 5, srcFace, 0.25f);

        // Tangent offset lights so they are always 0.5f from edges. This makes plane offset of < 0.5f reliable to prevent bleed.
        Array<Vector3, 4> lightPositions = srcFace.InsetTangent(0.5f, 1.01f);
        auto lightVertIds = srcSeg.GetVertexIndices(src.Side);

        for (int lightIndex = 0; lightIndex < 4; lightIndex++) { // for each light source
            const auto& lightPos = lightPositions[lightIndex];
            const auto& lightColor = lightColors[lightIndex];
            if (!CheckMinLight(lightColor)) continue; // skip vert with no light

            for (auto& destId : segmentsToLight) {
                auto& destSeg = level.GetSegment(destId);

                for (auto& destSideId : SideIDs) { // for each side in dest
                    if (!settings.AccurateVolumes && !SideIsVisible(level, destSeg, destSideId))
                        continue; // skip invisible sides when accurate volumes is off

                    const auto destVertIds = destSeg.GetVertexIndices(destSideId);
                    const auto destFace = Face::FromSide(level, destId, destSideId);
                    Tag dest = { destId, destSideId };

                    // Move occlusion sample points off of faces to improve light wrapping around corners
                    auto destSamples =
                        destSeg.IsZeroVolume(level) ?
                        InsetTowardsPointPercentage(destFace.Center() + destFace.AverageNormal() * 5, destFace, 0.25f) :
                        InsetTowardsPointPercentage(destSeg.Center, destFace, 0.1f);

                    auto calcIntensity = [&](int vertIndex) {
                        bool fullBright = !bouncePass && (src == dest || Seq::contains(lightVertIds, destVertIds[vertIndex]));
                        auto dist = Vector3::Distance(destFace[vertIndex], lightPos); // use the real vertex position and not the sample for attenuation
                        auto attenuation = fullBright ? 1 : Attenuate2(dist, cast.Source->Radius, settings.Falloff);
                        if (attenuation <= 0) return Color();

                        if (cast.Source->EnableOcclusion &&
                            HitTest(level, segmentsToLight, destVertIds[vertIndex], lightVertIds[lightIndex], lightSamples[lightIndex], destSamples[vertIndex], src, dest))
                            return Color();

                        auto multiplier = bouncePass ? settings.Reflectance : settings.Multiplier;
                        return lightColor * attenuation * multiplier;
                    };

                    //auto planeSamples = destFace.Inset(1, 1.01f);
                    auto checkPlanes = [&](int srcVertIndex, int destEdge) {
                        if (src.Segment != dest.Segment) {
                            // is the light behind the dest face?
                            if (destFace.Distance(lightPos, destEdge) < cast.Source->LightPlaneTolerance) return false;
                            // Is the vert behind the light?
                            if (srcFace.Distance(destFace[srcVertIndex], lightIndex) < PlaneTolerance) return false;
                        }
                        return true;
                    };

                    if (destFace.Side.Type == SideSplitType::Quad) {
                        // Quads are flat and can be treated as a single polygon
                        for (int vertIndex = 0; vertIndex < 4; vertIndex++) { // for each vert on side
                            if (!checkPlanes(vertIndex, vertIndex)) continue;
                            auto intensity = calcIntensity(vertIndex);
                            if (CheckMinLight(intensity))
                                cast.Pass[dest][vertIndex] += intensity;
                        }
                    }
                    else {
                        // Light triangulated faces twice using the clip plane for each normal. Then average along seam.
                        Color face0Color[4]{}, face1Color[4]{};
                        auto ri = destFace.Side.GetRenderIndices();

                        for (int i = 0; i < 3; i++) { // for each vert of triangle 1
                            auto vertIndex = ri[i];
                            if (!checkPlanes(vertIndex, 0)) continue;
                            face0Color[vertIndex] += calcIntensity(vertIndex);
                        }

                        for (int i = 3; i < 6; i++) { // for each vert of triangle 2
                            auto vertIndex = ri[i];
                            if (!checkPlanes(vertIndex, 2)) continue;
                            face1Color[vertIndex] += calcIntensity(vertIndex);
                        }

                        for (int i = 0; i < 4; i++) {
                            auto intensity = face0Color[i] + face1Color[i];

                            // Average the shared edges
                            if (destFace.Side.Type == SideSplitType::Tri02) {
                                if (i == 0 || i == 2) intensity *= 0.5f;
                            }
                            else {
                                if (i == 1 || i == 3) intensity *= 0.5f;
                            }

                            if (CheckMinLight(intensity))
                                cast.Pass[dest][i] += intensity;
                        }
                    }
                }
            }
        }
    }

    LightRayCast& CastBounces(Level& level, const LightSettings& settings, LightRayCast& cast) {
        cast.UpdateMaxValueFromPass(settings.Reflectance);

        // Use the previous pass targets as the light sources
        Dictionary<Tag, SideLighting> prevPass = std::move(cast.Pass);
        cast.Pass = {};

        for (const auto& [src, lightColors] : prevPass) {
            auto [srcSeg, srcSide] = level.GetSegmentAndSide(src);

            // don't emit from open connections (from accurate volumes setting)
            if (srcSeg.SideHasConnection(src.Side) && !srcSeg.SideIsWall(src.Side)) continue;

            Set<SegID> segmentsToLight = GetSegmentsInRange(level, src, settings.DistanceThreshold);
            Color tmapColor = Resources::GetTextureInfo(srcSide.TMap).AverageColor;
            tmapColor.AdjustSaturation(2); // boost saturation to look nicer
            ScaleColor2(tmapColor, 1); // 100% brightness
            SideLighting adjColors = lightColors;
            for (auto& c : adjColors)
                c *= tmapColor; // premultiply the texture color into the light color

            LightSegments(level, adjColors, settings, segmentsToLight, src, true, cast);
        }

        return cast;
    }

    LightRayCast& CastDirectLight(Level& level, const LightSource& light, const LightSettings& settings) {
        Set<SegID> segmentsToLight = GetSegmentsInRange(level, light.Tag, settings.DistanceThreshold);

        auto& cast = RayCasts[light.Tag];
        cast.Source = &light;
        cast.PassMaxValue = light.MaxBrightness() * settings.Multiplier;
        // Clamp to the max light value setting
        ClampColor(cast.PassMaxValue, Color(0, 0, 0), Color(settings.MaxValue, settings.MaxValue, settings.MaxValue));

        LightSegments(level, light.Colors, settings, segmentsToLight, light.Tag, false, cast);
        return cast;
    }

    // Reduces the intensity of touching co-planar light sources to make the
    // brightness consistent across the entire surface
    void ReduceCoplanarBrightness(const Level& level, span<LightSource> lights) {
        Set<Tag> scanned;

        for (auto& light : lights) {
            if (scanned.contains(light.Tag)) continue; // skip already scanned lights

            // scan each source to see if it is co-planar and connected
            List<Tag> coplanars = FindCoplanarSides(level, light.Tag, 10.0f, true);
            List<LightSource*> coplanarLights;

            for (auto& other : coplanars) {
                for (auto& otherlt : lights) {
                    if (otherlt.Tag == other) {
                        coplanarLights.push_back(&otherlt); // the light was coplanar to this light
                        scanned.insert(otherlt.Tag); // don't scan this source again
                    }
                }
            }

            // reduce the brightness of the coplanar lights
            for (int v = 0; v < level.Vertices.size(); v++) {
                int count = 0;

                // Check the number of times this vertex is used to emit light
                for (auto& source : coplanarLights) {
                    for (int j = 0; j < 4; j++)
                        if (source->Indices[j] == v) count++;
                }

                if (count <= 1) continue;

                // if multiple sources have the same index, reduce the brightness
                for (auto& source : coplanarLights) {
                    for (int j = 0; j < 4; j++) {
                        if (source->Indices[j] == v)
                            source->Colors[j] *= (1.0f / (float)count);
                    }
                }
            }
        }
    }

    // Gathers all light sources in the level
    List<LightSource> GatherLightSources(Level& level, const LightSettings& settings) {
        List<LightSource> sources;

        for (int i = 0; i < level.Segments.size(); i++) {
            auto segId = SegID(i);
            auto& seg = level.Segments[i];

            for (auto& sideId : SideIDs) {
                if (seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // open sides can't have lights

                auto& side = seg.GetSide(sideId);
                auto color = GetLightColor(side);
                if (!CheckMinLight(color)) continue;

                Tag tag = { segId, sideId };

                LightSource light = {
                    .Tag = tag,
                    .Indices = seg.GetVertexIndices(sideId),
                    .Colors = { color, color, color, color },
                    .IsDynamic = Resources::GetDestroyedTexture(side.TMap2) > LevelTexID::Unset || level.GetFlickeringLight(tag),
                    .Radius = side.LightRadiusOverride.value_or(settings.Radius),
                    .LightPlaneTolerance = side.LightPlaneOverride.value_or(settings.LightPlaneTolerance),
                    .EnableOcclusion = side.EnableOcclusion,
                    .DynamicMultiplier = side.DynamicMultiplierOverride.value_or(1)
                };
                sources.push_back(light);
            }
        }

        return sources;
    }

    // The initial lighting pass directly from light sources
    void EmitDirectLight(Level& level, const LightSettings& settings, span<LightSource> lights) {
        if (settings.CheckCoplanar)
            ReduceCoplanarBrightness(level, lights);

        for (auto& source : lights) {
            auto& cast = CastDirectLight(level, source, settings);
            cast.AccumulatePass();
        }
    }

    // Calculates the volume light for all segments in the level based on surface lighting
    void SetVolumeLight(Level& level, bool accurateVolumes) {
        for (auto& seg : level.Segments) {
            if (seg.LockVolumeLight) continue;
            Color volume;

            int contributingSides = 0;
            // 6 sides with four color values
            for (auto& sideId : SideIDs) {
                if (!accurateVolumes && seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // skip open sides unless accurate volumes enabled
                auto& side = seg.GetSide(sideId);
                for (auto& v : side.Light)
                    volume += v;
                contributingSides++;
            }

            if (contributingSides == 0) continue;
            seg.VolumeLight += volume * (1.0f / (contributingSides * 4));
            seg.VolumeLight.A(1);
        }
    }

    // Scales the brightness of values over 1 while retaining color
    constexpr void ClampColorBrightness(Level& level, float maxValue) {
        for (auto& seg : level.Segments) {
            for (auto& side : seg.Sides) {
                for (auto& color : side.Light) {
                    ScaleColor(color, maxValue);
                    //color.A(1);

                    //constexpr auto whiteMagnitude = 1.73205078f; // (1,1,1).Length()
                    //auto overbright = color.ToVector3().Length() - whiteMagnitude;
                    //if (overbright <= 0) continue;
                    //// if overbright = 2
                    //color *= 1 + overbright / (settings.MaxValue);
                    //    
                    //auto mult = pow(overbright, 3); // smooth scaling from 1 to 2
                    //color *= 1 / (mult + 1);
                    // auto contrast = 1.0f / (overbright * settings.ClampStrength + 1);
                    //color.AdjustContrast(contrast);
                }
            }
        }
    };

    // Approximate area of side based on UVs.
    float AreaOfSide(const SegmentSide& side) {
        auto width = (side.UVs[1] - side.UVs[0]).Length();
        auto height = (side.UVs[3] - side.UVs[0]).Length();
        return width * height;
    }

    // Sets the initial brightness for all geometry in the level
    void SetAmbientLight(Color ambient) {
        for (auto& seg : Game::Level.Segments) {
            for (auto& side : seg.Sides) {
                for (int i = 0; i < 4; i++) {
                    if (side.LockLight[i]) continue;
                    side.Light[i] = ambient;
                }
            }

            if (!seg.LockVolumeLight)
                seg.VolumeLight = ambient;

            seg.LightSubtracted = 0;
            seg.VolumeLight.A(1);
        }
    }

    // Generates the dynamic light table for destroyable and flickering lights
    void SetDynamicLights(Level& level, const Dictionary<Tag, LightRayCast>& rayCasts) {
        for (auto& [src, light] : rayCasts) {
            if (!light.Source->IsDynamic) continue;

            if (level.LightDeltaIndices.size() >= MaxDynamicLights) {
                ShowWarningMessage(L"Maximum dynamic lights reached. Some lights will not work as expected.");
                return;
            }

            if (level.LightDeltas.size() + MaxDeltasPerLight > MaxLightDeltas) {
                ShowWarningMessage(L"Maximum light deltas reached. Some lights will not work as expected.");
                return;
            }

            auto startIndex = (int16)level.LightDeltas.size();

            // Sort light by brightness
            struct Accumulated { Tag Tag; SideLighting Lighting; };
            auto accumulated = Seq::map(light.Accumulated, [](auto x) { return Accumulated{ x.first, x.second }; });
            Seq::sortBy(accumulated, [](auto& a, auto& b) { return AverageBrightness(a.Lighting) > AverageBrightness(b.Lighting); });

            uint8 deltaCount = 0;
            for (auto& [dest, color] : accumulated) {
                if (AverageBrightness(color) < 0.005f) continue; // discard low brightness faces

                if (light.Source->IsDynamic && deltaCount >= MaxDeltasPerLight) {
                    SPDLOG_WARN("Reached delta limit for light {}-{}", light.Source->Tag.Segment, light.Source->Tag.Side);
                    break;
                }

                auto& seg = level.GetSegment(dest);
                if (seg.SideHasConnection(dest.Side) && !seg.SideIsWall(dest.Side)) continue;

                for (auto& c : color) c *= light.Source->DynamicMultiplier;
                LightDelta ld = { .Tag = dest, .Color = color };
                for (short i = 0; i < 4; i++) ld.Color[i].A(0); // Don't affect alphas
                level.LightDeltas.push_back(ld);
                deltaCount++;
            }

            level.LightDeltaIndices.push_back(LightDeltaIndex{
                .Tag = src,
                .Count = deltaCount,
                .Index = startIndex });
        }

        SPDLOG_INFO("Delta lights: {} of {}\nIndices: {} of {}", level.LightDeltaIndices.size(), MaxDynamicLights, level.LightDeltas.size(), MaxLightDeltas);
    }

    // Copies accumulated light to the level faces
    void SetSideLighting(Level& level, const Dictionary<Tag, LightRayCast>& rayCasts, Color max, bool color) {
        for (auto& [src, light] : rayCasts) {
            for (auto& [dest, l] : light.Accumulated) {
                auto& side = level.GetSide(dest);
                for (int vert = 0; vert < 4; vert++) {
                    if (side.LockLight[vert]) continue;
                    side.Light[vert] += l[vert];
                    if (!color)
                        ClampColor(side.Light[vert], { 0, 0, 0, 1 }, max); // clamp accumulated values to max
                }
            }
        }
    }

    // Removes all color from results
    void DesaturateAccumulated(Dictionary<Tag, LightRayCast>& rayCasts) {
        for (auto& [_, cast] : rayCasts)
            for (auto& [__, side] : cast.Accumulated)
                for (auto& l : side)
                    l.AdjustSaturation(0);
    }

    // Lights the level geometry and volumes. Not thread safe (needs refactoring to not use globals).
    void Commands::LightLevel(Level& level, const LightSettings& settings) {
        try {
            ScopedCursor cursor(IDC_WAIT);
            Metrics::Reset();
            HitTests.clear();
            RayCasts.clear();
            level.LightDeltaIndices.clear();
            level.LightDeltas.clear();

            ScopedTimer timer(&Metrics::LightCalculationTime);
            SetAmbientLight(settings.Ambient);

            auto sources = GatherLightSources(level, settings);
            EmitDirectLight(level, settings, sources);

            auto bounces = std::clamp(settings.Bounces, 0, 10);

            // Accumulate radiosity bounces
            for (int i = 0; i < bounces; i++) {
                for (auto& [_, light] : RayCasts) {
                    auto& info = CastBounces(level, settings, light);
                    info.AccumulatePass(!(settings.SkipFirstPass && i == 0));
                }
            }

            if (!settings.EnableColor)
                DesaturateAccumulated(RayCasts);

            auto maxValue = std::clamp(settings.MaxValue, 0.0f, 10.0f);
            const Color max = { maxValue, maxValue, maxValue, 1 };
            SetSideLighting(level, RayCasts, max, settings.EnableColor);
            if (settings.EnableColor)
                ClampColorBrightness(level, settings.MaxValue);

            SetVolumeLight(level, settings.AccurateVolumes);
            SetDynamicLights(level, RayCasts);
            Editor::History.SnapshotLevel("Light Level");
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }
}