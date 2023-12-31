#include "pch.h"
#include "Game.Boss.h"

#include <numeric>

#include "Game.AI.h"
#include "Game.h"
#include "Game.Reactor.h"
#include "Game.Segment.h"
#include "Physics.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Editor/Editor.Object.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Game {
    namespace {
        constexpr float BOSS_DEATH_DURATION = 5.5f;
        constexpr float BOSS_DEATH_SOUND_VOLUME = 2;
        constexpr float BOSS_PHASE_TIME = 1.25f;
        //float3(.2, .2, 25)
        constexpr Color BOSS_PHASE_COLOR = { 25, 0, 0 };

        // Boss state is intentionally shared. Defeating one boss causes the others to start
        // exploding and some custom levels rely on this.
        bool BossDying = false;
        bool BossDyingSoundPlaying = false;
        float BossDyingElapsed = 0;
        List<TeleportTarget> TeleportTargets;
        List<SegID> GateSegments;
        float GateInterval = 10; // D1 gate interval
        float GateTimer = 0; // Gates in a robot when timer reaches interval
    }

    span<TeleportTarget> GetTeleportSegments() { return TeleportTargets; }

    Option<Vector3> BossFitsInSegment(Inferno::Level& level, SegID segId, const Object& boss) {
        // Checks if the boss can fit at 9 different locations within the segment,
        // towards each corner and the center.
        auto seg = level.TryGetSegment(segId);
        if (!seg) return {};
        float radius = boss.Radius * 4 / 3.0f;

        {
            LevelHit hit;
            if (!IntersectLevelSegment(level, seg->Center, radius, segId, hit))
                return seg->Center;
        }

        for (auto& sideid : SIDE_IDS) {
            auto position = (seg->GetSide(sideid).Center + seg->Center) / 2;
            LevelHit hit;
            if (!IntersectLevelSegment(level, position, radius, segId, hit))
                return position;
        }

        return {};
    }

    List<TeleportTarget> FindTeleportTargets(Inferno::Level& level, bool sizeCheck) {
        Object* boss = nullptr;
        List<int8> visited;
        visited.resize(level.Segments.size());
        List<TeleportTarget> targets;

        for (int i = 0; i < level.Objects.size(); i++) {
            auto& obj = level.Objects[i];
            if (obj.IsRobot() && Resources::GetRobotInfo(obj).IsBoss) {
                if (boss)
                    SPDLOG_WARN("Level contains multiple bosses. Boss segment logic only supports a single boss for teleporting");
                boss = &obj;
            }
        }

        if (!boss) return {};

        List<SegID> queue;
        queue.reserve(256);
        queue.push_back(boss->Segment);

        int index = 0;

        while (index < queue.size()) {
            auto segid = queue[index++];
            auto seg = level.TryGetSegment(segid);
            if (!seg) continue;

            auto position = BossFitsInSegment(level, segid, *boss);
            if (!sizeCheck || position)
                targets.push_back({ segid, position.value_or(seg->Center) });

            for (auto& sideid : SIDE_IDS) {
                if (seg->SideIsSolid(sideid, level)) continue;

                auto connection = seg->GetConnection(sideid);
                auto& isVisited = visited[(int)connection];
                if (isVisited) continue; // already visited
                isVisited = true;
                queue.push_back(connection);
            }
        }

        return targets;
    }

    float GetGateInterval() {
        return 4.0f - Game::Difficulty * 2.0f / 3.0f;
    }

    void GateInRobotD1(int8 id) {
        if (GateSegments.empty()) {
            SPDLOG_WARN("Gate segments empty, unable to gate in robot");
            return;
        }

        auto segId = GateSegments[RandomInt((int)GateSegments.size() - 1)];
        auto& seg = Game::Level.GetSegment(segId);
        auto& robotInfo = Resources::GetRobotInfo(id);

        int count = 0;
        for (auto& obj : Game::Level.Objects) {
            if (obj.IsRobot() && obj.SourceMatcen == MatcenID::Boss)
                count++;
        }

        if (count > 2 * Game::Difficulty + 3) {
            GateTimer = GateInterval * 0.75f;
            return;
        }

        auto point = RandomPointInSegment(Game::Level, seg);
        auto mask = ObjectMask::Player | ObjectMask::Robot;
        if (NewObjectIntersects(Game::Level, seg, point, robotInfo.Radius, mask)) {
            GateTimer = GateInterval * 0.75f;
            return;
        }

        // use materialize effect
        auto& vclip = Resources::GetVideoClip(VClipID::Matcen);
        Sound3D sound(vclip.Sound);
        sound.Radius = 400.0f;
        Sound::Play(sound, point, segId);

        // Create a new robot
        Object obj{};
        InitObject(Game::Level, obj, ObjectType::Robot, id);
        obj.Position = point;
        obj.Segment = segId;
        obj.SourceMatcen = MatcenID::Boss;
        obj.PhaseIn(2, MATCEN_PHASING_COLOR);

        auto dir = Game::GetPlayerObject().Position - point;
        dir.Normalize();
        obj.Rotation = VectorToObjectRotation(dir);
        Game::AddObject(obj);

        GateTimer = 0;
    }

    void TeleportBoss(Object& boss, AIRuntime& ai, const RobotInfo& info) {
        if (TeleportTargets.empty()) {
            SPDLOG_WARN("No teleport segments found for boss!");
            return;
        }

        auto& player = Game::GetPlayerObject();

        // Find a valid segment to warp to
        TeleportTarget* target = nullptr;

        Shuffle(TeleportTargets);
        for (auto& t : TeleportTargets) {
            if (player.Segment == t.Segment || boss.Segment == t.Segment)
                continue; // Avoid teleporting on top of self or the player

            if (auto seg = Game::Level.TryGetSegment(t.Segment)) {
                auto mask = ObjectMask::Player | ObjectMask::Robot;
                if (NewObjectIntersects(Game::Level, *seg, t.Position, boss.Radius, mask))
                    continue; // Avoid teleporting on top of an existing object

                target = &t;
                break; // Found a valid segment
            }
        }

        if (!target) {
            SPDLOG_WARN("Boss was unable to find a new segment to warp to");
        }
        else {
            boss.Position = boss.PrevPosition = target->Position;
            boss.Physics.PrevVelocity = boss.Physics.Velocity = Vector3();
            boss.Physics.Thrust = Vector3();
            SPDLOG_INFO("Teleporting boss to segment {}", target->Segment);
            RelinkObject(Game::Level, boss, target->Segment);
        }

        // Face towards player after teleporting
        auto facing = player.Position - boss.Position;
        facing.Normalize();
        boss.Rotation = VectorToObjectRotation(facing);
        boss.Rotation.Forward(boss.Rotation.Forward());
        boss.PrevRotation = boss.Rotation;

        ai.TeleportDelay = info.TeleportInterval;
        ai.Awareness = 0; // Make unaware of player so teleport doesn't start counting down immediately
        boss.PhaseIn(BOSS_PHASE_TIME, BOSS_PHASE_COLOR);
        ai.ClearPath();
    }

    bool UpdateBoss(Object& boss, float dt) {
        auto& ri = Resources::GetRobotInfo(boss);
        auto& ai = GetAI(boss);

        if (boss.HitPoints <= 0)
            BossDying = true;

        if (BossDying) {
            // Phase the boss back in if it dies while warping out
            if (HasFlag(boss.Effects.Flags, EffectFlags::PhaseOut))
                boss.PhaseIn(boss.Effects.PhaseTimer / 2, BOSS_PHASE_COLOR);

            BossDyingElapsed += dt;
            bool explode = DeathRoll(boss, BOSS_DEATH_DURATION, BossDyingElapsed, ri.DeathRollSound,
                                     BossDyingSoundPlaying, BOSS_DEATH_SOUND_VOLUME, dt);
            if (explode) {
                SelfDestructMine();
                ExplodeObject(boss);
                BossDying = false; // safeguard
                Sound3D sound(ri.ExplosionSound2);
                sound.Volume = 3;
                sound.Radius = 1000;
                Sound::Play(sound, boss.Position, boss.Segment);

                Render::DynamicLight light;
                light.Radius = 200;
                light.Duration = light.FadeTime = 0.25f;
                light.LightColor = Color(1, 0.45f, 0.25f, 25);
                light.Position = boss.Position;
                light.Segment = boss.Segment;
                Render::AddDynamicLight(light);
            }
            return false;
        }

        if (Settings::Cheats.DisableAI)
            return false;

        if (Game::Level.IsDescent1()) {
            if (!ri.GatedRobots.empty()) {
                GateTimer += dt;
                if (GateTimer >= GateInterval) {
                    auto robotId = ri.GatedRobots[RandomInt((int)ri.GatedRobots.size() - 1)];
                    GateInRobotD1(robotId);
                }
            }
        }

        if (ai.Awareness > 0.3f)
            ai.TeleportDelay -= dt; // Only teleport when aware of player

        if (ai.TeleportDelay <= BOSS_PHASE_TIME && !boss.IsPhasing()) {
            boss.PhaseOut(BOSS_PHASE_TIME, BOSS_PHASE_COLOR);
        }

        if (ai.TeleportDelay <= 0) {
            TeleportBoss(boss, ai, ri);
        }

        return true;
    }

    void StartBossDeath() {
        BossDying = true;
    }

    void InitBoss() {
        // todo: add hack for D2 level 4 boss to check past 1 wall for teleport targets
        GateSegments.clear();
        TeleportTargets = FindTeleportTargets(Game::Level, true);
        if (Game::Level.IsDescent1()) {
            for (auto& [seg, pos] : FindTeleportTargets(Game::Level, false))
                GateSegments.push_back(seg);
            GateInterval = 5.0f - Game::Difficulty / 2.0f;
        }

        BossDying = false;
        BossDyingElapsed = 0;
        BossDyingSoundPlaying = false;
        GateTimer = 0;

        if (Game::GetState() == GameState::Editor) return;

        // Attach sound to boss
        for (auto& obj : Game::Level.Objects) {
            if (obj.IsRobot()) {
                auto& info = Resources::GetRobotInfo(obj);
                if (!info.IsBoss) continue;

                auto& ai = GetAI(obj);
                ai.TeleportDelay = info.TeleportInterval;

                Sound3D sound(info.SeeSound);
                sound.Radius = 400;
                sound.Looped = true;
                sound.Volume = 0.85f;
                sound.Occlusion = false;
                Sound::PlayFrom(sound, obj);
            }
        }
    }
}
