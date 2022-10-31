#include "pch.h"
#define NOMINMAX
#include "Game.Wall.h"
#include "SoundSystem.h"
#include "Physics.h"
#include <Game.Segment.h>
#include "Face.h"
#include "Game.h"
#include "Graphics/Render.Particles.h"
#include "HUD.h"

namespace Inferno {
    //template<class TData, class TKey = int>
    //class SlotMap {
    //    std::vector<TData> _data;
    //    std::function<bool(const TData&)> _aliveFn;

    //public:
    //    SlotMap(std::function<bool(const TData&)> aliveFn) : _aliveFn(aliveFn) {}

    //    TData& Get(TKey key) {
    //        assert(InRange(key));
    //        return _data[(int64)key];
    //    }

    //    [[nodiscard]] TKey Add(TData&& data) {
    //        for (size_t i = 0; i < _data.size(); i++) {
    //            if (!_aliveFn(_data[i])) {
    //                _data = data;
    //                return (TKey)i;
    //            }
    //        }

    //        _data.push_back(data);
    //        return TKey(_data.size() - 1);
    //    }

    //    [[nodiscard]] TData& Alloc() {
    //        for (auto& v : _data) {
    //            if (_aliveFn(v))
    //                return v;
    //        }

    //        return _data.emplace_back();
    //    }

    //    bool InRange(TKey index) const { return index >= (TKey)0 && index < (TKey)_data.size(); }

    //    [[nodiscard]] auto at(size_t index) { return _data.at(index); }
    //    [[nodiscard]] auto begin() { return _data.begin(); }
    //    [[nodiscard]] auto end() { return _data.end(); }
    //    [[nodiscard]] const auto begin() const { return _data.begin(); }
    //    [[nodiscard]] const auto end() const { return _data.end(); }
    //};

    constexpr float DOOR_WAIT_TIME = 2;

    ActiveDoor* FindDoor(Level& level, WallID id) {
        for (auto& door : level.ActiveDoors) {
            if (door.Front == id || door.Back == id) return &door;
        }

        return nullptr;
    }

    void SetWallTMap(SegmentSide& side1, SegmentSide& side2, const WallClip& clip, int frame) {
        frame = std::clamp(frame, 0, (int)clip.NumFrames - 1);
        auto tmap = clip.Frames[frame];
        bool changed = false;

        if (clip.UsesTMap1()) {
            changed = side1.TMap != tmap || side2.TMap != tmap;
            side1.TMap = side2.TMap = tmap;
        }
        else {
            // assert side.tmap1 && tmap2 != 0
            changed = side1.TMap2 != tmap || side2.TMap2 != tmap;
            side1.TMap2 = side2.TMap2 = tmap;
        }

        if (changed) Editor::Events::LevelChanged();
    }

    void SetWallTMap(Level& level, Tag tag, const WallClip& clip, int frame) {
        auto conn = level.GetConnectedSide(tag);
        auto& side = level.GetSide(tag);
        auto& cside = level.GetSide(conn);
        SetWallTMap(side, cside, clip, frame);
    }

    void DoOpenDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);
        auto cwall = level.TryGetConnectedWall(wall.Tag);

        // todo: remove objects stuck on door
        Render::RemoveDecals(wall.Tag);

        door.Time += dt;

        auto& clip = Resources::GetWallClip(wall.Clip);
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(door.Time / frameTime);

        if (i < clip.NumFrames) {
            SetWallTMap(level, wall.Tag, clip, i);
        }

        if (i > clip.NumFrames / 2) { // half way open
            wall.SetFlag(WallFlag::DoorOpened);
            if (cwall) cwall->SetFlag(WallFlag::DoorOpened);
        }

        if (i >= clip.NumFrames - 1) {
            SetWallTMap(level, wall.Tag, clip, i - 1);

            if (!wall.HasFlag(WallFlag::DoorAuto)) {
                door = {}; // free door slot because it won't close
            }
            else {
                fmt::print("Waiting door\n");
                wall.State = WallState::DoorWaiting;
                if (cwall) cwall->State = WallState::DoorWaiting;
                door.Time = 0;
            }
        }
    }

    bool DoorIsObstructed(Level& level, Tag tag) {
        auto other = level.GetConnectedSide(tag);
        for (auto& obj : level.Objects | views::filter(Object::IsAliveFn)) {
            if (obj.Segment == tag.Segment || obj.Segment == other.Segment) {
                DirectX::BoundingSphere sphere(obj.Position, obj.Radius);
                auto face = Face::FromSide(level, tag);
                if (IntersectFaceSphere(face, sphere))
                    return true; // object blocking doorway!
            }
        }

        return false;
    }

    void DoCloseDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);

        auto front = level.TryGetWall(door.Front);
        auto back = level.TryGetWall(door.Back);

        auto conn = level.GetConnectedSide(wall.Tag);
        auto& side = level.GetSide(wall.Tag);
        auto& cside = level.GetSide(conn);

        if (wall.HasFlag(WallFlag::DoorAuto)) {
            if (DoorIsObstructed(level, wall.Tag)) return;
        }

        auto& clip = Resources::GetWallClip(wall.Clip);

        if (door.Time == 0) { // play sound at start of closing
            //auto sound = Resources::GetSoundIndex(clip.CloseSound);
            Sound3D sound(side.Center, wall.Tag.Segment);
            sound.Resource = Resources::GetSoundResource(clip.CloseSound);
            Sound::Play(sound);
        }

        door.Time += dt;
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(clip.NumFrames - door.Time / frameTime - 1);

        if (i < clip.NumFrames / 2) { // Half way closed
            //SPDLOG_INFO("Set door {}:{} state to opened", wall.Tag.Segment, wall.Tag.Side);
            front->ClearFlag(WallFlag::DoorOpened);
            if (back) back->ClearFlag(WallFlag::DoorOpened);
        }

        if (i > 0) {
            SetWallTMap(side, cside, clip, i);
            //fmt::print("{}:{} Set wall state to closing\n", wall.Tag.Segment, wall.Tag.Side);
            front->State = WallState::DoorClosing;
            if (back) back->State = WallState::DoorClosing;
            //door.Time = 0;
        }
        else {
            // CloseDoor()
            fmt::print("{}:{} Set wall state to closed\n", wall.Tag.Segment, wall.Tag.Side);
            front->State = WallState::Closed;
            if (back) back->State = WallState::Closed;
            SetWallTMap(side, cside, clip, 0);
            door = {};
        }
    }

    // Commands a door to open
    void OpenDoor(Level& level, Tag tag) {
        auto& seg = level.GetSegment(tag);
        auto& side = seg.GetSide(tag.Side);
        auto wall = level.TryGetWall(side.Wall);
        if (!wall) throw Exception("Tried to open door on side that has no wall");

        auto conn = level.GetConnectedSide(tag);
        auto cwallId = level.GetWallID(conn);
        auto cwall = level.TryGetWall(cwallId);

        if (wall->State == WallState::DoorOpening ||
            wall->State == WallState::DoorWaiting)
            return;

        ActiveDoor* door = nullptr;
        auto& clip = Resources::GetWallClip(wall->Clip);

        if (wall->State != WallState::Closed) {
            // Reuse door
            door = FindDoor(level, side.Wall);
            if (door)
                door->Time = std::max(clip.PlayTime - door->Time, 0.0f);
        }

        if (!door) {
            door = &level.ActiveDoors.Alloc();
            door->Time = 0;
        }

        fmt::print("Opening door {}:{}\n", tag.Segment, tag.Side);
        wall->State = WallState::DoorOpening;
        door->Front = side.Wall;

        if (cwall) {
            door->Back = cwallId;
            cwall->State = cwall->State = WallState::DoorOpening;
        }

        if (clip.OpenSound != SoundID::None) {
            Sound3D sound(side.Center, tag.Segment);
            sound.Resource = Resources::GetSoundResource(clip.OpenSound);
            Sound::Play(sound);
        }

        //if (wall->LinkedWall == WallID::None) {
        //    door->Parts = 1;
        //}
        //else {
        //    auto lwall = level.TryGetWall(wall->LinkedWall);
        //    auto& seg2 = level.GetSegment(lwall->Tag);

        //    assert(lwall->LinkedWall == seg.GetSide(tag.Side).Wall);
        //    lwall->State = WallState::DoorOpening;

        //    auto& csegp = level.GetSegment(seg2.GetConnection(lwall->Tag.Side));

        //    door->Parts = 2;

        //}
    }

    // Commands a door to close
    void CloseDoor(Level& level, Tag tag) {
        auto wall = level.TryGetWall(tag);
        if (!wall) return;

        if (wall->State == WallState::DoorClosing ||
            wall->State == WallState::DoorWaiting ||
            wall->State == WallState::Closed)
            return;
    }

    void UpdateDoors(Level& level, float dt) {
        for (auto& door : level.ActiveDoors) {
            auto wall = level.TryGetWall(door.Front);
            if (!wall) continue;

            if (wall->State == WallState::DoorOpening) {
                DoOpenDoor(level, door, dt);
            }
            else if (wall->State == WallState::DoorClosing) {
                DoCloseDoor(level, door, dt);
            }
            else if (wall->State == WallState::DoorWaiting) {
                door.Time += dt;
                if (door.Time > DOOR_WAIT_TIME) {
                    fmt::print("Closing door {}\n", door.Front);
                    wall->State = WallState::DoorClosing;
                    door.Time = 0;
                }
            }
        }
    }


    void PrintTriggerMessage(const Trigger& trigger, string message) {
        if (trigger.HasFlag(TriggerFlag::NoMessage)) return;

        auto msg = fmt::format(message, trigger.Targets.Count() > 1 ? "s" : "");
        PrintHudMessage(msg);
    }

    void ActivateTriggerD1(Level& level, Trigger& trigger) {}

    bool WallIsForcefield(Level& level, Trigger& trigger) {
        for (auto& tag : trigger.Targets) {
            if (auto seg = level.TryGetSide(tag)) {
                if (Resources::GetLevelTextureInfo(seg->TMap).HasFlag(TextureFlag::ForceField))
                    return true;
            }
        }
        return false;
    }

    bool ChangeWalls(Trigger& trigger) {
        bool changed = false;

        return changed;
    }

    void StartExitSequence(Level&) {

    }

    void EnterSecretLevel() {}

    void ToggleWall(Segment& seg, SideID side) {

    }

    Option<SideID> GetConnectedSide(Segment& base, SegID conn) {
        for (auto& side : SideIDs) {
            if (base.GetConnection(side) == conn)
                return side;
        }

        return {};
    }

    struct ExplodingWall {
        Tag Tag;
        float Time = 0;
        static bool IsAlive(const ExplodingWall& w) { return w.Tag.HasValue(); }
    };

    DataPool<ExplodingWall> ExplodingWalls(ExplodingWall::IsAlive, 10);

    void UpdateExplodingWalls(Level& level, float dt) {
        constexpr float EXPLODE_TIME = 1.0f;
        constexpr int TOTAL_FIREBALLS = 32;

        for (auto& wall : ExplodingWalls) {
            if (!ExplodingWall::IsAlive(wall)) continue;

            auto prevFrac = wall.Time / EXPLODE_TIME;
            wall.Time += dt;

            if (wall.Time > EXPLODE_TIME) wall.Time = EXPLODE_TIME;

            if (wall.Time > EXPLODE_TIME * 0.75f) {
                if (auto w = level.TryGetWall(wall.Tag)) {
                    // todo: remove objects stuck on side (flares)
                    Render::RemoveDecals(wall.Tag);
                    auto& clip = Resources::GetWallClip(w->Clip);
                    SetWallTMap(level, wall.Tag, clip, clip.NumFrames - 1);
                }
            }

            auto frac = wall.Time / EXPLODE_TIME;
            auto oldCount = int(TOTAL_FIREBALLS * prevFrac * prevFrac);
            auto count = int(TOTAL_FIREBALLS * frac * frac);

            for (int e = oldCount; e < count; e++) {
                auto verts = level.VerticesForSide(wall.Tag);
                auto pos = verts[1] + (verts[0] - verts[1]) * Random();
                pos += (verts[2] - verts[1]) * Random();

                constexpr float FIREBALL_SIZE = 4.5f;
                auto size = FIREBALL_SIZE + (2.0f * FIREBALL_SIZE * e / TOTAL_FIREBALLS);

                // fireballs start away from door then move closer
                auto& side = level.GetSide(wall.Tag);
                pos += side.AverageNormal * size * float(TOTAL_FIREBALLS - e) / TOTAL_FIREBALLS;

                if (!(e & 3)) {
                    // Create a damaging explosion 1/4th of the time
                    GameExplosion expl{};
                    expl.Damage = 4;
                    expl.Radius = 20;
                    expl.Force = 50;
                    CreateExplosion(level, nullptr, expl);
                }

                Render::Particle p{};
                p.Clip = VClipID::SmallExplosion;
                p.Position = pos;
                p.Radius = size / 2;
                Render::AddParticle(p);
            }

            if (wall.Time >= EXPLODE_TIME)
                wall.Tag = {}; // Free the slot
        }
    };

    void ExplodeWall(Level& level, Tag tag) {
        // create small explosions on the face
        auto& side = level.GetSide(tag);
        Sound3D sound(side.Center, tag.Segment);
        sound.Resource = Resources::GetSoundResource(SoundID::ExplodingWall);
        Sound::Play(sound);

        ExplodingWalls.Add({ tag });
    }

    void DestroyWall(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (!wall) return;

        wall->HitPoints = -1;
        if (cwall) cwall->HitPoints = -1;

        auto& wclip = Resources::GetWallClip(wall->Clip);
        if (wclip.HasFlag(WallClipFlag::Explodes))
            ExplodeWall(level, wall->Tag);

        wall->SetFlag(WallFlag::Destroyed);
        if (cwall) cwall->SetFlag(WallFlag::Destroyed);
    }

    void DamageWall(Level& level, Tag tag, float damage) {
        auto wall = level.TryGetWall(tag);
        if (!wall) return;

        if (wall->Type != WallType::Destroyable ||
            wall->HasFlag(WallFlag::Destroyed)) return;

        wall->HitPoints -= damage;
        auto cwall = level.TryGetConnectedWall(tag);
        if (cwall) cwall->HitPoints -= damage;

        //a = Walls[seg->sides[side].wall_num].clip_num;
        auto& clip = Resources::GetWallClip(wall->Clip);

        //if (Walls[seg->sides[side].wall_num].hps < WALL_HPS * 1 / n)
        if (wall->HitPoints < 100.0f / clip.NumFrames) {
            DestroyWall(level, tag);
        }
        else if (wall->HitPoints < 100) {
            int frame = clip.NumFrames - (int)std::ceil(wall->HitPoints / 100.0f * clip.NumFrames);

            auto& side = level.GetSide(tag);
            auto cside = level.TryGetConnectedSide(tag);
            assert(cside); // a door must be on a connected side
            SetWallTMap(side, *cside, clip, frame);

            // 10 - (100 - 0) / 10) -> 0;
            // 10 - (100 - 50) / 10) -> 5
            //auto frame = (int)(100.0f * (clip.NumFrames - i) / clip.NumFrames);

            //for (int i = 0; i < clip.NumFrames; i++) {
            //    if (wall->HitPoints < 100.0f * (clip.NumFrames - i) / clip.NumFrames) {
            //        SetWallTMap(side, cside, clip, i);
            //        //wall_set_tmap_num(seg, side, csegp, Connectside, a, i);
            //    }
            //}

            //if (frame == clip.NumFrames - 1) // final frame is destroyed state
            //    DestroyWall(level, tag);
        }
    }

    // Opens doors targeted by a trigger (or destroys them)
    void OpenDoorTrigger(Level& level, Trigger& trigger) {
        for (auto& target : trigger.Targets) {
            //ToggleWall(target);
            if (auto wall = level.TryGetWall(target)) {
                if (wall->Type == WallType::Destroyable)
                    DestroyWall(level, target);

                if (wall->Type == WallType::Door || wall->Type == WallType::Closed)
                    OpenDoor(level, target);
            }
        }
    }

    void TriggerMatcen(SegID seg) {
        // do matcen stuff
    }

    void IllusionOn(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (wall) wall->SetFlag(WallFlag::IllusionOff);
        if (cwall) wall->SetFlag(WallFlag::IllusionOff);
    }

    void IllusionOff(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (wall) wall->ClearFlag(WallFlag::IllusionOff);
        if (cwall) wall->ClearFlag(WallFlag::IllusionOff);
    }

    void ActivateTriggerD2(Level& level, Trigger& trigger) {
        if (trigger.HasFlag(TriggerFlag::Disabled))
            return;

        if (trigger.HasFlag(TriggerFlag::OneShot))
            trigger.Flags |= TriggerFlag::Disabled;

        switch (trigger.Type) {
            case TriggerType::Exit:
                StartExitSequence(level);
                break;

            case TriggerType::SecretExit:
                // warp to secret level unless destroyed
                // stop sounds
                // play secret exit sound 249

                if (Game::SecretLevelDestroyed)
                    PrintHudMessage("Secret Level destroyed. Exit disabled.");
                else
                    EnterSecretLevel();
                break;

            case TriggerType::OpenDoor:
                OpenDoorTrigger(level, trigger);
                PrintTriggerMessage(trigger, "Door{} opened");
                break;

            case TriggerType::CloseDoor:
                PrintTriggerMessage(trigger, "Door{} closed");
                for (auto& target : trigger.Targets) {
                    CloseDoor(level, target);
                }
                break;

            case TriggerType::UnlockDoor:
                PrintTriggerMessage(trigger, "Door{} unlocked");
                for (auto& tag : trigger.Targets) {
                    if (auto wall = level.TryGetWall(tag)) {
                        wall->ClearFlag(WallFlag::DoorLocked);
                        wall->Keys = WallKey::None;
                    }
                }
                break;

            case TriggerType::LockDoor:
                PrintTriggerMessage(trigger, "Door{} locked");
                for (auto& tag : trigger.Targets) {
                    if (auto wall = level.TryGetWall(tag)) {
                        wall->SetFlag(WallFlag::DoorLocked);
                    }
                }
                break;

            case TriggerType::CloseWall:
                if (ChangeWalls(trigger)) {
                    if (WallIsForcefield(level, trigger))
                        PrintTriggerMessage(trigger, "Force field{} deactivated!");
                    else
                        PrintTriggerMessage(trigger, "Wall{} opened!");
                }
                break;

            case TriggerType::OpenWall:
                if (ChangeWalls(trigger)) {
                    if (WallIsForcefield(level, trigger))
                        PrintTriggerMessage(trigger, "Force field{} activated!");
                    else
                        PrintTriggerMessage(trigger, "Wall{} closed!");
                }
                break;

            case TriggerType::IllusoryWall:
                ChangeWalls(trigger); // not sure what message to print
                break;

            case TriggerType::IllusionOn:
                PrintTriggerMessage(trigger, "Illusion{} on!");
                for (auto& tag : trigger.Targets)
                    IllusionOn(level, tag);
                break;

            case TriggerType::IllusionOff:
                PrintTriggerMessage(trigger, "Illusion{} off!");
                for (auto& tag : trigger.Targets) {
                    // todo: play SOUND::WallRemoved
                    IllusionOff(level, tag);
                }
                break;

            case TriggerType::LightOff:
                PrintTriggerMessage(trigger, "Light{} off!");
                for (auto& tag : trigger.Targets) {
                    if (auto seg = level.TryGetSegment(tag))
                        SubtractLight(level, tag, *seg);
                }
                break;

            case TriggerType::LightOn:
                PrintTriggerMessage(trigger, "Light{} on!");
                for (auto& tag : trigger.Targets) {
                    if (auto seg = level.TryGetSegment(tag))
                        AddLight(level, tag, *seg);
                }
                break;

            case TriggerType::Matcen:
                fmt::print("Trigger Matcen\n");
                PrintHudMessage("Trigger matcen");
                for (auto& tag : trigger.Targets) {
                    TriggerMatcen(tag.Segment);
                }
                break;
        }
    }

    bool WallIsTransparent(Level& level, Tag tag) {
        auto side = level.TryGetSide(tag);
        auto wall = level.TryGetWall(tag);

        if (!side || !wall) return false;

        if (wall->Type == WallType::WallTrigger)
            return false;

        auto& tmap1 = Resources::GetTextureInfo(side->TMap);
        if (tmap1.Transparent) return true;

        if (side->TMap > LevelTexID::Unset) {
            auto& tmap2 = Resources::GetTextureInfo(side->TMap);
            if (tmap2.SuperTransparent) return true;
        }

        return false;
    }
}
