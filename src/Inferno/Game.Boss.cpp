#include "pch.h"
#include "Game.Boss.h"

#include "Game.AI.h"
#include "Game.h"
#include "Game.Reactor.h"
#include "Physics.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Game {
    namespace {
        constexpr float BOSS_DEATH_DURATION = 5.5f;
        constexpr float DEATH_SOUND_DURATION = 2.68f;
        constexpr float BOSS_DEATH_SOUND_VOLUME = 2;
        constexpr float BOSS_PHASE_TIME = 1.25f;
        //float3(.2, .2, 25)
        constexpr Color BOSS_PHASE_COLOR = { 25, 0, 0 };

        // Boss state is intentionally shared. Defeating one boss causes the others to start
        // exploding and some custom levels rely on this.
        bool BossDying = false;
        bool BossDyingSoundPlaying = false;
        float BossDyingElapsed = 0;
        List<SegID> TeleportSegments;
        List<SegID> GateSegments;
    }

    bool BossFitsInSegment(Inferno::Level& level, SegID segId, const Object& boss) {
        // Checks if the boss can fit at 9 different locations within the segment,
        // towards each corner and the center.
        auto seg = level.TryGetSegment(segId);
        if (!seg) return false;
        auto vertices = seg->GetVertices(level);
        float radius = boss.Radius * 4 / 3.0f;

        for (int i = 0; i < 9; i++) {
            Vector3 position = seg->Center;
            if (i < vertices.size())
                position = (*vertices[i] + seg->Center) / 2;

            LevelHit hit;
            if (!IntersectLevelSegment(level, position, radius, segId, hit))
                return true;
        }

        return false;
    }

    List<SegID> GetBossSegments(Inferno::Level& level, bool sizeCheck) {
        Object* boss = nullptr;
        List<int8> visited;
        visited.resize(level.Segments.size());
        //ranges::fill(visited, 0);
        List<SegID> segments;


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

        segments.push_back(boss->Segment);
        int index = 0;

        while (index < queue.size()) {
            auto segid = queue[index++];
            auto seg = level.TryGetSegment(segid);
            if (!seg) continue;

            for (auto& sideid : SideIDs) {
                if (seg->SideIsSolid(sideid, level)) continue;

                auto connection = seg->GetConnection(sideid);
                auto& isVisited = visited[(int)connection];
                if (isVisited) continue; // already visited
                isVisited = true;
                queue.push_back(connection);
                if (!sizeCheck || BossFitsInSegment(level, segid, *boss)) {
                    segments.push_back(connection);
                }
            }
        }

        Seq::sort(segments);
        return segments;
    }

    float GetGateInterval() {
        return 4.0f - Game::Difficulty * 2.0f / 3.0f;
    }

    void GateInRobot() {
        // use materialize effect
    }

    void TeleportBoss(Object& boss, AIRuntime& ai, const RobotInfo& info) {
        if (TeleportSegments.empty()) {
            SPDLOG_WARN("No teleport segments found for boss!");
            return;
        }

        auto& player = Game::GetPlayerObject();

        // Avoid warping the boss directly on top of the player or itself
        int random = -1;
        for (int retry = 0; retry < 5; retry++) {
            random = RandomInt((int)TeleportSegments.size() - 1);
            if (player.Segment != TeleportSegments[random] &&
                boss.Segment != TeleportSegments[random])
                break;
        }

        if (random != -1) {
            auto& seg = Level.GetSegment(TeleportSegments[random]);
            boss.Position = boss.PrevPosition = seg.Center;
            RelinkObject(Game::Level, boss, TeleportSegments[random]);
        }
        else {
            SPDLOG_WARN("Boss was unable to find a new segment to warp to");
        }

        // Face towards player after teleporting
        auto facing = player.Position - boss.Position;
        facing.Normalize();
        boss.Rotation = VectorToRotation(facing);
        boss.Rotation.Forward(-boss.Rotation.Forward());

        ai.TeleportDelay = info.TeleportInterval;
        ai.Awareness = 0; // Make unaware of player so teleport doesn't start counting down immediately
        boss.PhaseIn(BOSS_PHASE_TIME, BOSS_PHASE_COLOR);
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
                ExplodeObject(boss, 0.25f);
                BossDying = false; // safeguard
            }
            return false;
        }

        if (Settings::Cheats.DisableAI) 
            return false;

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
        TeleportSegments = GetBossSegments(Game::Level, true);
        GateSegments = GetBossSegments(Game::Level, false);
        BossDying = false;
        BossDyingElapsed = 0;
        BossDyingSoundPlaying = false;

        // Attach sound to boss
        for (auto& obj : Game::Level.Objects) {
            if (obj.IsRobot()) {
                auto& info = Resources::GetRobotInfo(obj);
                if (!info.IsBoss) continue;

                auto& ai = GetAI(obj);
                ai.TeleportDelay = info.TeleportInterval;

                Sound3D sound(info.SeeSound, Game::GetObjectRef(obj));
                sound.Radius = 400;
                sound.Looped = true;
                sound.Volume = 0.85f;
                sound.Occlusion = false;
                sound.AttachToSource = true;
                Sound::Play(sound);
            }
        }
    }
}
