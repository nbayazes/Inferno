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
        if (!seg) return false;

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

    SegID TraceSegmentInternal(const Level& level, SegID start, const Vector3& point, int& iterations) {
        iterations++;

        //ASSERT(iterations <= 50);
        if (iterations > 50) {
            SPDLOG_ERROR("Trace depth limit reached, something is wrong");
            return start;
        }

        auto startSeg = level.TryGetSegment(start);
        if (!startSeg) {
            SPDLOG_ERROR("Trace start seg does not exist");
            return start;
        }

        auto distances = GetSideDistances(level, *startSeg, point);
        if (ranges::all_of(distances, [](float d) { return d >= -0.001f; }))
            return start;

        auto biggestSide = SideID::None;

        do {
            biggestSide = SideID::None;

            auto seg = level.TryGetSegment(start);
            float biggestVal = 0;

            if (!seg) {
                SPDLOG_WARN("Invalid trace segment {}", start);
                return start;
            }

            for (auto& sid : SIDE_IDS) {
                if (auto conn = level.TryGetSegment(seg->GetConnection(sid))) {
                    if (conn->IsZeroVolume())
                        continue; // Zero volume segs can't contain points

                    if (distances[(int)sid] < biggestVal) {
                        biggestVal = distances[(int)sid];
                        biggestSide = sid;
                    }
                }
            }

            if (biggestSide != SideID::None) {
                distances[(int)biggestSide] = 0;
                auto check = TraceSegmentInternal(level, seg->GetConnection(biggestSide), point, iterations);
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

        auto lightInfo1 = TryGetValue(Resources::LightInfoTable, side.TMap);
        if (lightInfo1 && lightInfo1->Color != LIGHT_UNSET) {
            baseColor += lightInfo1->Color;
        }
        else if (tmap1.Lighting > 0) {
            baseColor += Resources::GetTextureInfo(side.TMap).AverageColor;
        }

        if (side.HasOverlay()) {
            auto lightInfo2 = TryGetValue(Resources::LightInfoTable, side.TMap2);

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

    // Returns a vector that exits the segment
    Vector3 GetExitVector(Level& level, Segment& seg, const Matcen& matcen) {
        // Use active path side
        if (matcen.TriggerPath.size() >= 2) {
            auto exit = matcen.TriggerPath[1].Position - matcen.TriggerPath[0].Position;
            exit.Normalize();

            if (exit == Vector3::Zero) {
                SPDLOG_WARN("Zero vector in GetExitVector()");
                exit = Vector3::Forward;
            }

            return exit;
        }

        // Fallback to open side
        Vector3 exit;

        for (auto& sid : SIDE_IDS) {
            if (!seg.SideHasConnection(sid)) continue;
            auto& side = seg.GetSide(sid);

            if (auto wall = level.TryGetWall(side.Wall)) {
                if (wall->IsSolid()) continue;
            }

            exit = side.Center - seg.Center;
            exit.Normalize();
            break;
        }

        if (exit == Vector3::Zero)
            exit = Vector3::Forward;

        return exit;
    }

    // Visual effects when creating a new robot
    void CreateMatcenEffect(const Level& level, SegID segId) {
        if (auto seg = level.TryGetSegment(segId)) {
            auto& vclip = Resources::GetVideoClip(VClipID::Matcen);
            const auto& top = seg->GetSide(SideID::Top).Center;
            const auto& bottom = seg->GetSide(SideID::Bottom).Center;

            if (vclip.PlayTime == 0) return; // Data not found

            ParticleInfo p{};
            auto up = top - bottom;
            p.Clip = VClipID::Matcen;
            p.Radius = up.Length() / 2;
            up.Normalize(p.Up);
            p.RandomRotation = false;
            p.Color = Color(.2f, 1, .2f, 5);
            AddParticle(p, segId, seg->Center);

            LightEffectInfo light;
            light.Radius = p.Radius * 2.0f;
            light.LightColor = Color(1, 0, 0.8f, 5.0f);
            light.FadeTime = vclip.PlayTime;
            AddLight(light, seg->Center, vclip.PlayTime * 2, segId);

            if (auto beam = EffectLibrary.GetBeamInfo("matcen")) {
                for (int i = 0; i < 4; i++) {
                    //beam->StartDelay = i * 0.4f + Random() * 0.125f;
                    AddBeam(*beam, segId, vclip.PlayTime, top, bottom);
                }
            }

            if (auto beam = EffectLibrary.GetBeamInfo("matcen arcs")) {
                for (int i = 0; i < 8; i++) {
                    //beam->StartDelay = i * 0.4f + Random() * 0.125f;
                    AddBeam(*beam, segId, vclip.PlayTime, seg->Center, {});
                }
            }
        }
    }

    int GetLiveRobots(const Level& level, MatcenID matcen) {
        int liveRobots = 0;
        for (auto& obj : level.Objects) {
            if (obj.IsAlive() && obj.SourceMatcen == matcen)
                liveRobots++;
        }

        return liveRobots;
    }

    void UpdateMatcen(Level& level, Matcen& matcen, float dt) {
        if (!matcen.Active || matcen.Segment == SegID::None)
            return;

        auto matcenId = MatcenID(&matcen - &level.Matcens[0]);

        if (matcen.RobotCount <= 0) {
            // No more robots to spawn
            matcen.Active = false;

            for (auto& obj : level.Objects) {
                if (obj.SourceMatcen == matcenId && obj.Type == ObjectType::Light)
                    obj.Lifespan = 1; // Expire the light object
            }

            if (matcen.Activations <= 0)
                StopEffect(matcen.Light); // Remove the ambient light if out of energy

            return;
        }

        auto seg = level.TryGetSegment(matcen.Segment);
        if (!seg) {
            SPDLOG_WARN("Matcen {} has invalid segment set", (int)matcenId);
            return;
        }

        matcen.Timer += dt;

        // Alternates between playing the spawn effect and actually creating the robot
        if (!matcen.CreateRobotState) {
            if (matcen.Timer < matcen.Delay) return; // Not ready!

            // limit live created robots
            auto robots = GetLiveRobots(level, matcenId);
            if (robots >= (int)Game::Difficulty + 3) {
                SPDLOG_INFO("Matcen {} already has {} active robots", (int)matcenId, robots);
                matcen.Timer /= 2;
            }

            bool wasBlocked = false;

            for (auto& objid : seg->Objects) {
                if (auto obj = level.TryGetObject(objid)) {
                    if (!obj->IsAlive()) continue;

                    if (obj->IsRobot()) {
                        auto dir = GetExitVector(level, *seg, matcen);
                        obj->Physics.Velocity += dir * 50;
                        wasBlocked = true;
                    }
                    else if (obj->IsPlayer()) {
                        Game::Player.ApplyDamage(4, true);
                        auto dir = GetExitVector(level, *seg, matcen);
                        dir += RandomVector(0.25f);
                        dir.Normalize();
                        obj->Physics.Velocity += dir * 50;
                        wasBlocked = true;
                    }
                }
            }

            if (wasBlocked) {
                matcen.Timer = matcen.Delay - 1.5f;
                return; // Don't spawn robot when matcen is blocked by another object
            }

            auto& vclip = Resources::GetVideoClip(VClipID::Matcen);
            auto sound = Sound3D(vclip.Sound);
            sound.Radius = Game::MATCEN_SOUND_RADIUS;
            Sound::Play(sound, seg->Center, matcen.Segment);

            CreateMatcenEffect(level, matcen.Segment);

            matcen.Timer = 0;
            matcen.CreateRobotState = true;
        }
        else {
            auto& vclip = Resources::GetVideoClip(VClipID::Matcen);
            if (matcen.Timer < vclip.PlayTime / 2)
                return; // Wait until half way through animation to create robot

            matcen.Timer = 0;
            matcen.Delay = 1.5f + Random() * 2.0f;

            if (!matcen.Robots && !matcen.Robots2) {
                SPDLOG_WARN("Tried activating matcen {} with no robots set", (int)matcenId);
                matcen.Active = false;
                return;
            }

            auto robots = matcen.GetEnabledRobots();
            auto type = robots[RandomInt((int)robots.size() - 1)];

            // Create a new robot
            Object obj{};
            InitObject(obj, ObjectType::Robot, type);
            obj.Position = seg->Center;
            obj.Segment = matcen.Segment;
            obj.SourceMatcen = matcenId;
            obj.PhaseIn(1.5, Game::MATCEN_PHASING_COLOR);

            auto facing = GetExitVector(Game::Level, *seg, matcen);
            obj.Rotation = VectorToObjectRotation(facing);
            ASSERT(IsNormalized(obj.Rotation.Forward()));
            auto ref = Game::AddObject(obj);

            matcen.RobotCount--;
            matcen.CreateRobotState = false;

            if (auto newObj = Game::GetObject(ref)) {
                // Path newly created robots to their matcen triggers
                AI::SetPath(*newObj, matcen.TriggerPath);
                auto& ai = GetAI(*newObj);
                ai.RemainingSlow = 2;
                ai.State = AIState::MatcenPath;
                ai.LastUpdate = Game::Time;

                // Special case gophers to start in mine laying mode
                if (obj.ID == 10) {
                    newObj->Control.AI.Behavior = AIBehavior::RunFrom;
                    ai.State = AIState::Alert;
                    ai.Awareness = 1;
                    ai.Path = {};
                }
            }
        }
    }

    void UpdateMatcens(Level& level, float dt) {
        for (auto& matcen : level.Matcens) {
            UpdateMatcen(level, matcen, dt);
        }
    }

    void TriggerMatcen(Level& level, SegID segId, SegID triggerSeg) {
        auto seg = level.TryGetSegment(segId);
        if (!seg || seg->Type != SegmentType::Matcen) {
            SPDLOG_WARN("Tried to activate matcen on invalid segment {}", segId);
            return;
        }

        auto matcen = level.TryGetMatcen(seg->Matcen);
        if (!matcen) {
            SPDLOG_WARN("Matcen data is missing for {}", (int)seg->Matcen);
            return;
        }

        if (matcen->Activations <= 0 || matcen->Active)
            return; // Already active or out of activations

        auto matcenId = MatcenID(matcen - &level.Matcens[0]);
        auto robots = GetLiveRobots(level, matcenId);
        if (GetLiveRobots(level, matcenId) >= (int)Game::Difficulty + 3)
            return; // Maximum robots already alive

        SPDLOG_INFO("Triggering matcen {} Live robots {}", (int)matcenId, robots);
        matcen->Active = true;
        matcen->Timer = 0;
        matcen->Delay = 0;
        matcen->RobotCount = (int8)Game::Difficulty + 3; // 3 to 7
        matcen->Activations--;

        if (auto tseg = level.TryGetSegment(triggerSeg)) {
            // Try to generate a path to the trigger, prefering to avoid key doors.
            NavPoint goal = { triggerSeg, tseg->Center };
            matcen->TriggerPath = Game::Navigation.NavigateTo(segId, goal, NavigationFlag::None, level);

            if (matcen->TriggerPath.empty())
                matcen->TriggerPath = Game::Navigation.NavigateTo(segId, goal, NavigationFlag::OpenKeyDoors, level);

            if (matcen->TriggerPath.empty())
                matcen->TriggerPath = GenerateRandomPath(segId, 8); // No path, generate random nearby location
        }

        // Light for when matcen is active
        Object light{};
        light.Type = ObjectType::Light;
        light.Light.Radius = seg->GetLongestEdge() * 2;
        light.Light.Color = Color(1, 0, 0.8f, 0.5f);
        light.Position = seg->Center;
        light.Segment = matcen->Segment;
        light.SourceMatcen = MatcenID(matcen - &level.Matcens[0]);
        Game::AddObject(light);

        seg->Matcen;
    }

    void InitializeMatcens(Level& level) {
        // Increase number of activations on ace and insane.
        // Replaces the infinite spawns on insane that D2 added.
        int8 activations = 3;
        if (Game::Difficulty == DifficultyLevel::Ace) activations = 4; // Ace
        if (Game::Difficulty >= DifficultyLevel::Insane) activations = 5; // Insane or above

        for (auto& matcen : level.Matcens) {
            matcen.Activations = activations;
            matcen.CreateRobotState = false;
            if (matcen.Light == EffectID::None) {
                if (auto seg = level.TryGetSegment(matcen.Segment)) {
                    // Ambient light while matcen has energy remaining
                    LightEffectInfo light;
                    light.Radius = seg->GetLongestEdge() * 1.5f;
                    light.LightColor = Color(1, 0, 0.8f, 0.05f);
                    light.Mode = DynamicLightMode::BigPulse;
                    matcen.Light = AddLight(light, seg->Center, MAX_OBJECT_LIFE, matcen.Segment);
                }
            }
        }
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
        if (auto tid = Seq::findIndex(level.Triggers, [&level](const Trigger& trigger) { return IsExit(level, trigger); })) {
            if (auto wall = level.TryGetWall((TriggerID)*tid)) {
                return wall->Tag;
            }
        }

        return {};
    }
}
