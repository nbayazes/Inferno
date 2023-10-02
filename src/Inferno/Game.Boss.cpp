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
        constexpr float BOSS_DEATH_DURATION = 6;
        constexpr float DEATH_SOUND_DURATION = 2.68f;
        constexpr float BOSS_CLOAK_DURATION = 7;
        constexpr float BOSS_DEATH_SOUND_VOLUME = 2;
        constexpr float BOSS_PHASE_TIME = 1.25f;
        constexpr Color BOSS_PHASE_COLOR = { 20, 0, 0 };

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

    // Causes an object to start exploding with a delay
    void ExplodeObject(Object& obj, float delay) {
        if (HasFlag(obj.Flags, ObjectFlag::Exploding)) return;

        obj.Lifespan = delay;
        SetFlag(obj.Flags, ObjectFlag::Exploding);
    }

    bool DeathRoll(Object& obj, float rollDuration, float elapsedTime, SoundID soundId, bool& dyingSoundPlaying, float volume, float dt) {
        auto& angularVel = obj.Physics.AngularVelocity;

        //auto roll = elapsedTime / rollDuration;
        //angularVel.x = cos(roll * roll);
        //angularVel.y = sin(roll);
        //angularVel.z = cos(roll - 1 / 8.0f);

        angularVel.x = elapsedTime / 9.0f;
        angularVel.y = elapsedTime / 5.0f;
        angularVel.z = elapsedTime / 7.0f;

        SoundResource resource(soundId);
        auto soundDuration = resource.GetDuration();

        if (elapsedTime > rollDuration - soundDuration) {
            // Going critical!
            if (!dyingSoundPlaying) {
                Sound3D sound3d(resource, Game::GetObjectRef(obj));
                sound3d.Volume = volume;
                sound3d.Radius = 400; // Should be a global radius for bosses
                Sound::Play(sound3d);
                dyingSoundPlaying = true;
            }

            if (Random() < dt * 16) {
                if (auto e = Render::EffectLibrary.GetExplosion("large fireballs")) {
                    // Larger periodic explosions with sound
                    e->Variance = obj.Radius * 0.45f;
                    e->Instances = (int)soundDuration;
                    e->Duration = soundDuration;
                    Render::CreateExplosion(*e, obj.Segment, obj.Position);
                }
            }
        }
        else if (Random() < dt * 8) {
            // Winding up, create fireballs on object
            if (auto e = Render::EffectLibrary.GetExplosion("small fireballs")) {
                e->Variance = obj.Radius * 0.55f;
                e->Duration = rollDuration;
                e->Instances = (int)rollDuration;
                Render::CreateExplosion(*e, obj.Segment, obj.Position);
            }
        }

        return rollDuration < elapsedTime;
    }

    void GateInRobot() {
        // use materialize effect
    }

    void TeleportBoss(Object& boss, AIRuntime& ai) {
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
        } else {
            SPDLOG_WARN("Boss was unable to find a new segment to warp to");
        }

        // Face towards player after teleporting
        auto facing = player.Position - boss.Position;
        facing.Normalize();
        boss.Rotation = VectorToRotation(facing);
        boss.Rotation.Forward(-boss.Rotation.Forward());

        ai.TeleportDelay = 7;
        ai.Awareness = 0; // Make unaware of player so teleport doesn't start counting down immediately
        boss.PhaseIn(BOSS_PHASE_TIME, BOSS_PHASE_COLOR);
    }

    void UpdateBoss(Object& boss, float dt) {
        auto& ri = Resources::GetRobotInfo(boss);
        auto& ai = GetAI(boss);

        if (ai.Awareness > 0.3f)
            ai.TeleportDelay -= dt; // Only teleport when aware of player

        if (ai.TeleportDelay <= BOSS_PHASE_TIME && !boss.IsPhasing()) {
            //float3(.2, .2, 25)
            boss.PhaseOut(BOSS_PHASE_TIME, BOSS_PHASE_COLOR);
        }

        if (ai.TeleportDelay <= 0) {
            TeleportBoss(boss, ai);
        }

        if (BossDying) {
            BossDyingElapsed += dt;
            DeathRoll(boss, BOSS_DEATH_DURATION, BossDyingElapsed, ri.DeathrollSound,
                      BossDyingSoundPlaying, BOSS_DEATH_SOUND_VOLUME, dt);

            if (BossDyingElapsed > BOSS_DEATH_DURATION) {
                SelfDestruct();
                ExplodeObject(boss, 0.25f);
                BossDying = false; // safeguard
            }
            return;
        }


        if (HasFlag(boss.Effects.Flags, EffectFlags::PhaseIn)) {
            // boss phases in / out over 1/3 of the cloak duration and then teleports

            // cloaking is shared for all bosses, but should be per-robot

            // teleports are stored per-boss type, but should probably be per robot
            // when the boss is hit, the teleport delay is reduced by 1/4

            // d2 level 4 boss has a special case for teleporting out of a room

            //if(ElapsedBossCloak > BOSS_CLOAK_DURATION / 3
        }
        else { }
    }

    void InitBoss() {
        TeleportSegments = GetBossSegments(Game::Level, true);
        GateSegments = GetBossSegments(Game::Level, false);
    }
}
