#include "pch.h"
#include "Game.Segment.h"
#include "Editor/Editor.h"
#include "VisualEffects.h"
#include "Game.AI.Pathing.h"
#include "Game.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "logging.h"

namespace Inferno {
    // Returns a vector that exits the segment
    Vector3 GetExitVector(Level& level, Segment& seg, const Matcen& matcen) {
        // Use active path side
        if (matcen.TriggerPath.size() >= 2) {
            auto exit = matcen.TriggerPath[1].Position - matcen.TriggerPath[0].Position;
            exit.Normalize();

            if (exit != Vector3::Zero)
                return exit;
        }

        // Fallback to open side
        Vector3 exit;

        for (auto& sid : SIDE_IDS) {
            if (!seg.SideHasConnection(sid)) continue;
            auto& side = seg.GetSide(sid);

            if (auto wall = level.TryGetWall(side.Wall)) {
                if (wall->IsSolid() && wall->Type != WallType::Door) continue;
            }

            exit = side.Center - seg.Center;
            exit.Normalize();
            break;
        }

        if (exit == Vector3::Zero) {
            SPDLOG_WARN("Zero vector in GetExitVector()");
            exit = Vector3::Forward;
        }

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

            if (auto info = Resources::GetLightInfo("Matcen Create")) {
                LightEffectInfo light;
                light.Radius =  p.Radius * 2.0f;
                light.LightColor = info->Color;
                light.FadeTime = vclip.PlayTime;
                AddLight(light, seg->Center, vclip.PlayTime * 2, segId);
            }

            if (auto beam = EffectLibrary.GetBeamInfo("matcen")) {
                for (int i = 0; i < 4; i++) {
                    //beam->StartDelay = i * 0.4f + Random() * 0.125f;
                    AddBeam(*beam, segId, top, bottom);
                }
            }

            if (auto beam = EffectLibrary.GetBeamInfo("matcen arcs")) {
                for (int i = 0; i < 8; i++) {
                    //beam->StartDelay = i * 0.4f + Random() * 0.125f;
                    AddBeam(*beam, segId, seg->Center, {});
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
        matcen.CooldownTimer -= dt;

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

            // Check if there's something blocking the the matcen
            for (auto& objid : seg->Objects) {
                if (auto obj = level.TryGetObject(objid)) {
                    if (!obj->IsAlive()) continue;

                    if (obj->IsRobot()) {
                        auto dir = GetExitVector(level, *seg, matcen);
                        obj->Physics.Velocity += dir * 50;
                        obj->ApplyDamage(1);

                        ExplosionEffectInfo expl;
                        expl.Clip = VClipID::Explosion;
                        expl.Radius = { obj->Radius * .4f, obj->Radius * 0.6f };
                        CreateExplosion(expl, Game::GetObjectRef(*obj));

                        Sound::Play({ SoundID::Explosion }, *obj);
                        wasBlocked = true;
                    }
                    else if (obj->IsPlayer()) {
                        ExplosionEffectInfo expl;
                        expl.Clip = VClipID::HitPlayer;
                        expl.Radius = obj->Radius;
                        CreateExplosion(expl, Game::GetObjectRef(*obj));

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
            matcen.Delay = 2.5f + Random() * 2.0f;

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

            // Always wait at least 5 seconds after the last robot spawns before activating again.
            // This is in case the spawner gets blocked for some reason
            matcen.CooldownTimer = std::max(matcen.CooldownTimer, 5.0f);

            if (auto newObj = Game::GetObject(ref)) {
                auto minPath = std::min(3, (int)matcen.TriggerPath.size());
                auto maxPath = (int)matcen.TriggerPath.size() - 1;

                // for long paths only travel the first 5 segments with a rare chance to travel the full distance
                if (matcen.TriggerPath.size() >= 10) {
                    auto longChance = Random() <= 0.2f;
                    maxPath = longChance ? maxPath : 5;
                    minPath = longChance ? maxPath / 2 : 2;
                }

                auto length = maxPath > minPath ? RandomInt(minPath, maxPath) : maxPath;

                if (length >= 2) {
                    SPDLOG_INFO("Creating random matcen path of length {} out of {}", length, matcen.TriggerPath.size() - 1);
                    List<NavPoint> path(matcen.TriggerPath.begin(), matcen.TriggerPath.begin() + length);
                    AI::SetPath(*newObj, path);
                }
                else {
                    AI::SetPath(*newObj, matcen.TriggerPath);
                }

                // Path newly created robots to their matcen triggers
                auto& ai = GetAI(*newObj);
                //ai.RemainingSlow = 1.5f;
                ai.State = AIState::Path;
                ai.LastUpdate = Game::Time;
                ai.path.mode = PathMode::StopAtEnd;
                OptimizePath(ai.path.nodes);

                // Special case gophers to start in mine laying mode
                if (obj.ID == 10) {
                    newObj->Control.AI.Behavior = AIBehavior::RunFrom;
                    ai.State = AIState::Alert;
                    ai.Awareness = 1;
                    ai.path.nodes = {};
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
        auto matcenId = seg->Matcen;

        if (!matcen) {
            SPDLOG_WARN("Matcen data is missing for {}", (int)matcenId);
            return;
        }

        if (matcen->CooldownTimer > 0) {
            SPDLOG_INFO("Matcen {} is still cooling down for {}s", (int)matcenId, matcen->CooldownTimer);
            return;
        }

        if (matcen->Activations <= 0 || matcen->Active) {
            SPDLOG_INFO("Matcen {} is out of energy", (int)matcenId);
            return; // Already active or out of activations
        }

        auto robots = GetLiveRobots(level, matcenId);
        if (robots >= (int)Game::Difficulty + 3) {
            SPDLOG_INFO("Matcen {} has {} live robots, which is the maximum", (int)matcenId, robots);
            return; // Maximum robots already alive
        }

        if (!matcen->Robots && !matcen->Robots2) {
            SPDLOG_WARN("Tried activating matcen {} but it has no robots set", (int)matcenId);
            return;
        }

        matcen->CooldownTimer = 30 - (float)Game::Difficulty * 2;
        SPDLOG_INFO("Triggering matcen {} Cooldown {}", (int)matcenId, matcen->CooldownTimer);
        matcen->Active = true;
        matcen->Timer = 0;
        matcen->Delay = 0;
        matcen->RobotCount = (int8)Game::Difficulty + 3; // 3 to 7
        matcen->Activations--;

        if (auto tseg = level.TryGetSegment(triggerSeg)) {
            // Try to generate a path to the trigger, prefering to avoid key doors.
            NavPoint goal = { triggerSeg, tseg->Center };
            matcen->TriggerPath = Game::Navigation.NavigateTo(segId, goal, NavigationFlag::None, level, FLT_MAX, false);

            if (matcen->TriggerPath.empty())
                matcen->TriggerPath = Game::Navigation.NavigateTo(segId, goal, NavigationFlag::OpenKeyDoors, level, FLT_MAX, false);

            if (matcen->TriggerPath.empty())
                matcen->TriggerPath = GenerateRandomPath(level, segId, 8, NavigationFlag::None, SegID::None, false); // No path, generate random nearby location
        }

        DeduplicatePath(matcen->TriggerPath);

        // Light for when matcen is active and producing robots
        Object light{};
        light.Type = ObjectType::Light;
        light.Light.Radius = std::min(45.0f, seg->GetLongestEdge() * 1.5f);
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
        if (Game::Difficulty == DifficultyLevel::Ace) activations = 4;
        if (Game::Difficulty >= DifficultyLevel::Insane) activations = 5;

        for (auto& matcen : level.Matcens) {
            matcen.Activations = activations;
            matcen.CreateRobotState = false;
            if (matcen.Light == EffectID::None) {
                if (auto seg = level.TryGetSegment(matcen.Segment)) {
                    // Ambient light while matcen has energy remaining
                    //LightEffectInfo light;
                    //light.Radius = std::min(40.0f, seg->GetLongestEdge() * 1.5f);
                    //light.LightColor = Color(1, 0, 0.8f, 0.05f);
                    //light.Mode = DynamicLightMode::BigPulse;
                    //matcen.Light = AddLight(light, seg->Center, MAX_OBJECT_LIFE, matcen.Segment);
                }
            }
        }
    }
}
