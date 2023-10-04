#include "pch.h"
#include "Game.Segment.h"

#include "Game.h"
#include "Resources.h"
#include "Graphics/Render.h"
#include "Settings.h"
#include "SoundSystem.h"
#include "Editor/Editor.Object.h"
#include "Graphics/Render.Particles.h"

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
    bool PointInSegment(const Level& level, SegID id, const Vector3& point) {
        if (!level.SegmentExists(id)) return false;

        // Use estimation that treats the sides as planes instead of triangles
        for (auto& sideId : SideIDs) {
            auto& side = level.GetSide(Tag{ id, sideId });
            Plane p(side.Center, side.AverageNormal);
            if (p.DotCoordinate(point) < 0)
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

    SegID FindContainingSegment(const Level& level, const Vector3& point) {
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
        if (side.LightOverride) {
            Color color = *side.LightOverride;
            color.Premultiply();
            color.w = 1;
            return color;
        }

        auto& tmap1 = Resources::GetLevelTextureInfo(side.TMap);
        auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
        auto light = tmap1.Lighting + tmap2.Lighting;

        if (!enableColor)
            return { light, light, light };

        Color color;

        auto lightInfo1 = TryGetValue(Resources::LightInfoTable, side.TMap);
        if (lightInfo1 && lightInfo1->Color != LIGHT_UNSET) {
            color += lightInfo1->Color;
        }
        else if (tmap1.Lighting > 0) {
            color += Resources::GetTextureInfo(side.TMap).AverageColor;
        }

        if (side.HasOverlay()) {
            auto lightInfo2 = TryGetValue(Resources::LightInfoTable, side.TMap2);
            if (lightInfo2 && lightInfo2->Color != LIGHT_UNSET) {
                color += lightInfo2->Color;
            }
            else if (tmap2.Lighting > 0) {
                color += Resources::GetTextureInfo(side.TMap2).AverageColor;
            }
        }

        color.Premultiply();
        color.w = 1;
        return color /** light*/;
    }

    // Returns a vector that exits the segment
    Vector3 GetExitVector(Level& level, Segment& seg, const Matcen& matcen) {
        // Use active path side
        if (matcen.TriggerPath.size() >= 2) {
            auto& p0 = Game::Level.GetSegment(matcen.TriggerPath[0]);
            auto& p1 = Game::Level.GetSegment(matcen.TriggerPath[1]);
            auto exit = p1.Center - p0.Center;
            exit.Normalize();
            return exit;
        }

        // Fallback to open side
        Vector3 exit;

        for (auto& sid : SideIDs) {
            if (!seg.SideHasConnection(sid)) continue;
            auto& side = seg.GetSide(sid);

            if (auto wall = level.TryGetWall(side.Wall)) {
                if (wall->IsSolid()) continue;
            }

            exit = side.Center - seg.Center;
            exit.Normalize();
            break;
        }

        return exit;
    }

    // Visual effects when creating a new robot
    void CreateMatcenEffect(const Level& level, SegID segId) {
        if (auto seg = level.TryGetSegment(segId)) {
            auto& vclip = Resources::GetVideoClip(VClipID::Matcen);
            const auto& top = seg->GetSide(SideID::Top).Center;
            const auto& bottom = seg->GetSide(SideID::Bottom).Center;

            Render::Particle p{};
            auto up = top - bottom;
            p.Clip = VClipID::Matcen;
            p.Radius = up.Length() / 2;
            up.Normalize(p.Up);
            p.Duration = vclip.PlayTime;
            p.RandomRotation = false;
            p.Color = Color(.2, 1, .2, 5);
            Render::AddParticle(p, segId, seg->Center);

            Render::DynamicLight light;
            light.Radius = p.Radius * 2.0f;
            light.LightColor = Color(1, 0, 0.8f, 5.0f);
            light.Position = seg->Center;
            light.Duration = vclip.PlayTime * 2;
            light.FadeTime = vclip.PlayTime;
            light.Segment = segId;
            Render::AddDynamicLight(light);

            if (auto beam = Render::EffectLibrary.GetBeamInfo("matcen")) {
                for (int i = 0; i < 4; i++) {
                    //beam->StartDelay = i * 0.4f + Random() * 0.125f;
                    Render::AddBeam(*beam, vclip.PlayTime, top, bottom);
                }
            }

            if (auto beam = Render::EffectLibrary.GetBeamInfo("matcen arcs")) {
                for (int i = 0; i < 8; i++) {
                    //beam->StartDelay = i * 0.4f + Random() * 0.125f;
                    Render::AddBeam(*beam, vclip.PlayTime, seg->Center, {});
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

            if (matcen.Energy <= 0) {
                // Remove the ambient light if out of energy
                if (auto effect = Render::GetEffect(matcen.Light))
                    effect->Duration = 0;
            }
            return;
        }

        //if (matcen.ActiveTime > 0) {
        //    matcen.ActiveTime -= dt;
        //    if (matcen.ActiveTime <= 0)
        //        matcen.Active = false;
        //}

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
            if (robots >= Game::Difficulty + 3) {
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
            Sound::Play(Sound3D({ vclip.Sound }, seg->Center, matcen.Segment));

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
                return;
            }

            // Merge set robots from both flags
            int8 legalTypes[64]{}; // max of 64 different robots set on matcen

            int numTypes = 0;
            for (int8 i = 0; i < 2; i++) {
                int8 robotIndex = i * 32;
                auto flags = i == 0 ? matcen.Robots : matcen.Robots2;
                while (flags) {
                    if (flags & 1)
                        legalTypes[numTypes++] = robotIndex;
                    flags >>= 1;
                    robotIndex++;
                }
            }

            ASSERT(numTypes != 0);
            auto type = numTypes == 1 ? legalTypes[0] : legalTypes[RandomInt(numTypes - 1)];

            // Create a new robot
            Object obj{};
            Editor::InitObject(Game::Level, obj, ObjectType::Robot, type);
            obj.Position = seg->Center;
            obj.Segment = matcen.Segment;
            obj.SourceMatcen = matcenId;
            obj.PhaseIn(2, Game::MATCEN_PHASING_COLOR);

            auto facing = GetExitVector(Game::Level, *seg, matcen);
            obj.Rotation = VectorToRotation(-facing);
            Game::AddObject(obj);

            matcen.RobotCount--;
            matcen.CreateRobotState = false;
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

        if (matcen->Energy <= 0 || matcen->Active)
            return; // Already active or out of lives

        auto matcenId = MatcenID(matcen - &level.Matcens[0]);
        auto robots = GetLiveRobots(level, matcenId);
        if (GetLiveRobots(level, matcenId) >= Game::Difficulty + 3)
            return; // Maximum robots already alive

        SPDLOG_INFO("Triggering matcen {} Live robots {}", (int)matcenId, robots);
        matcen->Active = true;
        matcen->Timer = 0;
        matcen->Delay = 0;
        matcen->RobotCount = (int8)Game::Difficulty + 3; // 3 to 7
        matcen->TriggerPath = Game::Navigation.NavigateTo(segId, triggerSeg, false, level);
        matcen->Energy--;

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
        // Increase amount of energy on ace and insane.
        // Replaces the infinite spawns on insane that D2 added.
        int8 energy = 3;
        if (Game::Difficulty == 3) energy = 4; // Ace
        if (Game::Difficulty >= 4) energy = 5; // Insane or above

        for (auto& matcen : level.Matcens) {
            matcen.Energy = energy;
            matcen.CreateRobotState = false;
            if (matcen.Light == EffectID::None) {
                if (auto seg = level.TryGetSegment(matcen.Segment)) {
                    // Ambient light while matcen has energy remaining
                    Render::DynamicLight light;
                    light.Radius = seg->GetLongestEdge() * 1.5f;
                    light.LightColor = Color(1, 0, 0.8f, 0.05f);
                    light.Position = seg->Center;
                    light.Segment = matcen.Segment;
                    light.Mode = DynamicLightMode::BigPulse;
                    light.Duration = MAX_OBJECT_LIFE;
                    matcen.Light = Render::AddDynamicLight(light);
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
}
