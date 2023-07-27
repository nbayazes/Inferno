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
#include "DataPool.h"
#include "Resources.h"
#include "Editor/Events.h"

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

    // Removes all effects and objects stuck to a wall
    void RemoveAttachments(Level& level, Tag tag) {
        Render::RemoveDecals(tag);
        StuckObjects.Remove(level, tag);
    }

    ActiveDoor* FindDoor(Level& level, WallID id) {
        for (auto& door : level.ActiveDoors) {
            if (door.Front == id || door.Back == id) return &door;
        }

        return nullptr;
    }

    void SetSideClip(SegmentSide& side, const DoorClip& clip, int frame) {
        if (clip.NumFrames == 0) return;
        frame = std::clamp(frame, 0, (int)clip.NumFrames - 1);
        auto tmap = clip.Frames[frame];

        if (clip.HasFlag(DoorClipFlag::TMap1))
            side.TMap = tmap;
        else
            side.TMap2 = tmap;
    }

    void SetDoorClip(Level& level, Tag tag, const DoorClip& clip, int frame) {
        auto conn = level.GetConnectedSide(tag);
        auto& side = level.GetSide(tag);
        SetSideClip(side, clip, frame);
        if (auto cside = level.TryGetSide(conn))
            SetSideClip(*cside, clip, frame);
    }

    void DoOpenDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);
        auto cwall = level.TryGetConnectedWall(wall.Tag);

        RemoveAttachments(level, wall.Tag);

        door.Time += dt;

        auto& clip = Resources::GetDoorClip(wall.Clip);
        if (clip.PlayTime == 0) {
            SPDLOG_WARN("Tried to open door {}:{} with invalid wall clip", wall.Tag.Segment, wall.Tag.Side);
            return;
        }
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(door.Time / frameTime);

        if (i < clip.NumFrames) {
            SetDoorClip(level, wall.Tag, clip, i);
        }

        if (i > clip.NumFrames / 2) {
            // half way open
            wall.SetFlag(WallFlag::DoorOpened);
            if (cwall) cwall->SetFlag(WallFlag::DoorOpened);
        }

        if (i >= clip.NumFrames - 1) {
            SetDoorClip(level, wall.Tag, clip, clip.NumFrames - 1);

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
        for (auto& obj : level.Objects | views::filter(&Object::IsAlive)) {
            if (obj.Segment == tag.Segment || obj.Segment == other.Segment) {
                // Add a small buffer because physics will reposition a robot slightly outside of the door
                DirectX::BoundingSphere sphere(obj.Position, obj.Radius + 0.1f);
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

        auto& side = level.GetSide(wall.Tag);

        if (wall.HasFlag(WallFlag::DoorAuto)) {
            if (DoorIsObstructed(level, wall.Tag)) return;
        }

        auto& clip = Resources::GetDoorClip(wall.Clip);

        if (door.Time == 0) {
            // play sound at start of closing
            //auto sound = Resources::GetSoundIndex(clip.CloseSound);
            Sound3D sound(side.Center, wall.Tag.Segment);
            sound.Resource = Resources::GetSoundResource(clip.CloseSound);
            Sound::Play(sound);
        }

        door.Time += dt;
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(clip.NumFrames - door.Time / frameTime - 1);

        if (i < clip.NumFrames / 2) {
            // Half way closed
            //SPDLOG_INFO("Set door {}:{} state to opened", wall.Tag.Segment, wall.Tag.Side);
            front->ClearFlag(WallFlag::DoorOpened);
            if (back) back->ClearFlag(WallFlag::DoorOpened);
        }

        if (i > 0) {
            SetDoorClip(level, wall.Tag, clip, i);
            //fmt::print("{}:{} Set wall state to closing\n", wall.Tag.Segment, wall.Tag.Side);
            front->State = WallState::DoorClosing;
            if (back) back->State = WallState::DoorClosing;
            //door.Time = 0;
        }
        else {
            // CloseDoor()
            SetDoorClip(level, wall.Tag, clip, 0);
            //fmt::print("{}:{} Set wall state to closed\n", wall.Tag.Segment, wall.Tag.Side);
            front->State = WallState::Closed;
            if (back) back->State = WallState::Closed;
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
        auto& clip = Resources::GetDoorClip(wall->Clip);

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
            cwall->State = WallState::DoorOpening;
        }

        if (clip.OpenSound != SoundID::None) {
            Sound3D sound(side.Center, tag.Segment);
            sound.Resource = Resources::GetSoundResource(clip.OpenSound);
            Sound::Play(sound);
        }
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
                if (door.Time > Game::DOOR_WAIT_TIME) {
                    fmt::print("Closing door {}\n", door.Front);
                    wall->State = WallState::DoorClosing;
                    door.Time = 0;
                }
            }
        }
    }

    void PrintTriggerMessage(const Trigger& trigger, string_view message) {
        if (trigger.HasFlag(TriggerFlag::NoMessage)) return;

        auto suffix = trigger.Targets.Count() > 1 ? "s" : "";
        auto msg = fmt::vformat(message, fmt::make_format_args(suffix));
        PrintHudMessage(msg);
    }

    bool WallIsForcefield(Level& level, Trigger& trigger) {
        for (auto& tag : trigger.Targets) {
            if (auto side = level.TryGetSide(tag)) {
                if (Resources::GetLevelTextureInfo(side->TMap).HasFlag(TextureFlag::ForceField))
                    return true;
            }
        }
        return false;
    }

    bool ChangeWall(Level& level, Wall& wall, TriggerType type, WallType wallType) {
        if (wall.Type == wallType) return false; // already the right type

        auto wside = level.TryGetSide(wall.Tag);
        if (!wside) return false;

        switch (type) {
            case TriggerType::OpenWall:
                if (Resources::GetLevelTextureInfo(wside->TMap).HasFlag(TextureFlag::ForceField)) {
                    Sound3D sound(wside->Center, wall.Tag.Segment);
                    sound.Resource = Resources::GetSoundResource(SoundID::ForcefieldOff);
                    Sound::Play(sound);
                    Sound::Stop(wall.Tag); // stop the humming sound
                    wall.Type = wallType;
                    fmt::print("Turned off forcefield {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                else {
                    // do wall uncloak
                    Sound3D sound(wside->Center, wall.Tag.Segment);
                    sound.Resource = Resources::GetSoundResource(SoundID::CloakOn);
                    Sound::Play(sound);
                    wall.Type = wallType; // would be delayed by animation
                    fmt::print("Opened wall {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                break;

            case TriggerType::CloseWall:
                if (Resources::GetLevelTextureInfo(wside->TMap).HasFlag(TextureFlag::ForceField)) {
                    Sound3D sound(wside->Center, wall.Tag.Segment);
                    sound.Resource = Resources::GetSoundResource(SoundID::ForcefieldHum);
                    sound.Looped = true;
                    sound.Volume = 0.5f;
                    Sound::Play(sound);
                    wall.Type = wallType;
                    fmt::print("Activated forcefield {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                else {
                    // do wall cloak
                    Sound3D sound(wside->Center, wall.Tag.Segment);
                    sound.Resource = Resources::GetSoundResource(SoundID::CloakOff);
                    Sound::Play(sound);
                    wall.Type = wallType; // would be delayed by animation
                    fmt::print("Closed wall {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                break;

            case TriggerType::IllusoryWall:
                wall.Type = wallType;
                break;
        }

        RemoveAttachments(level, wall.Tag);
        Editor::Events::LevelChanged();
        return true;
    }

    bool ChangeWalls(Level& level, Trigger& trigger) {
        bool changed = false;

        for (auto& target : trigger.Targets) {
            auto wallType = [&trigger] {
                switch (trigger.Type) {
                    default:
                    case TriggerType::OpenWall: return WallType::Open;
                    case TriggerType::CloseWall: return WallType::Closed;
                    case TriggerType::IllusoryWall: return WallType::Illusion;
                }
            }();

            if (auto wall = level.TryGetWall(target))
                changed |= ChangeWall(level, *wall, trigger.Type, wallType);

            if (auto wall = level.TryGetConnectedWall(target))
                changed |= ChangeWall(level, *wall, trigger.Type, wallType);
        }

        return changed;
    }

    void StartExitSequence(Level&) {
        Game::SetState(GameState::ExitSequence);
    }

    void EnterSecretLevel() {}

    void ToggleWall(Segment& /*seg*/, SideID /*side*/) { }

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
        bool IsAlive() const { return Tag.HasValue(); }
    };

    DataPool<ExplodingWall> ExplodingWalls(&ExplodingWall::IsAlive, 10);

    void UpdateExplodingWalls(Level& level, float dt) {
        constexpr float EXPLODE_TIME = 1.0f;
        constexpr int TOTAL_FIREBALLS = 32;

        for (auto& wall : ExplodingWalls) {
            if (!wall.IsAlive()) continue;

            auto prevFrac = wall.Time / EXPLODE_TIME;
            wall.Time += dt;

            if (wall.Time > EXPLODE_TIME) wall.Time = EXPLODE_TIME;

            if (wall.Time > EXPLODE_TIME * 0.75f) {
                if (auto w = level.TryGetWall(wall.Tag)) {
                    RemoveAttachments(level, wall.Tag);
                    auto& clip = Resources::GetDoorClip(w->Clip);
                    SetDoorClip(level, wall.Tag, clip, clip.NumFrames - 1);
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
                    expl.Position = pos;
                    CreateExplosion(level, nullptr, expl);
                }

                Render::Particle p{};
                p.Clip = VClipID::SmallExplosion;
                p.Position = pos;
                p.Radius = size / 2;
                Render::AddParticle(p, wall.Tag.Segment);
            }

            if (wall.Time >= EXPLODE_TIME)
                wall.Tag = {}; // Free the slot
        }
    }

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

        if (wall->Type != WallType::Destroyable) {
            SPDLOG_WARN("Tried to destroy a non-destroyable wall {}:{}", tag.Segment, tag.Side);
            return;
        }

        wall->HitPoints = -1;
        if (cwall) cwall->HitPoints = -1;

        auto& wclip = Resources::GetDoorClip(wall->Clip);
        if (wclip.HasFlag(DoorClipFlag::Explodes))
            ExplodeWall(level, wall->Tag);

        wall->SetFlag(WallFlag::Destroyed);
        if (cwall) cwall->SetFlag(WallFlag::Destroyed);
    }

    void DamageWall(Level& level, Tag tag, float damage) {
        auto wall = level.TryGetWall(tag);
        if (!wall) return;

        if (wall->Type != WallType::Destroyable ||
            wall->HasFlag(WallFlag::Destroyed))
            return;

        wall->HitPoints -= damage;
        auto cwall = level.TryGetConnectedWall(tag);
        if (cwall) cwall->HitPoints -= damage;

        auto& clip = Resources::GetDoorClip(wall->Clip);

        if (wall->HitPoints < 100.0f / clip.NumFrames + 1) {
            DestroyWall(level, tag);
        }
        else if (wall->HitPoints < 100) {
            int frame = clip.NumFrames - (int)std::ceil(wall->HitPoints / 100.0f * clip.NumFrames);
            SetDoorClip(level, tag, clip, frame);
        }
    }

    void DestroyWall(Level& level, Wall& wall) {
        wall.HitPoints = -1;

        auto& wclip = Resources::GetDoorClip(wall.Clip);
        if (wclip.HasFlag(DoorClipFlag::Explodes))
            ExplodeWall(level, wall.Tag);

        wall.SetFlag(WallFlag::Destroyed);
    }

    void DamageWall(Level& level, Wall& wall, float damage) {
        if (wall.Type != WallType::Destroyable ||
            wall.HasFlag(WallFlag::Destroyed))
            return;

        wall.HitPoints -= damage;

        auto& clip = Resources::GetDoorClip(wall.Clip);

        if (wall.HitPoints < 100.0f / (float)clip.NumFrames + 1) {
            DestroyWall(level, wall);
        }
        else if (wall.HitPoints < 100) {
            int frame = clip.NumFrames - (int)std::ceil(wall.HitPoints / 100.0f * (float)clip.NumFrames);
            SetDoorClip(level, wall.Tag, clip, frame);
        }
    }

    bool RobotCanOpenDoor(const Wall& wall) {
        if (wall.Type != WallType::Door || wall.HasFlag(WallFlag::DoorLocked))
            return false;

        if (HasFlag(wall.Keys, WallKey::Red) || HasFlag(wall.Keys, WallKey::Gold) || HasFlag(wall.Keys, WallKey::Blue))
            return false;

        return true;
    }

    void HitWall(Level& level, const Vector3& point, const Object& src, const Wall& wall) {
        auto parent = level.TryGetObject(src.Parent);
        bool isPlayerSource = src.IsPlayer() || (parent && parent->IsPlayer());

        if (wall.Type == WallType::Destroyable && isPlayerSource && src.Type == ObjectType::Weapon) {
            auto& weapon = Resources::GetWeapon((WeaponID)src.ID);
            DamageWall(level, wall.Tag, weapon.Damage[Game::Difficulty]);
        }
        else if (wall.Type == WallType::Door) {
            if (src.IsRobot()) {
                // Allow robots to open normal doors
                if (RobotCanOpenDoor(wall))
                    OpenDoor(level, wall.Tag);
            }
            else if (isPlayerSource && Game::Player.CanOpenDoor(wall)) {
                OpenDoor(level, wall.Tag);
            }
            else if (src.Type == ObjectType::Weapon) {
                // Can't open door
                Sound3D sound(point, wall.Tag.Segment);
                sound.Resource = Resources::GetSoundResource(SoundID::HitLockedDoor);
                sound.Source = src.Parent;
                sound.FromPlayer = true;
                Sound::Play(sound);

                if (isPlayerSource) {
                    string msg;
                    const auto accessDenied = Resources::GetString(GameString::AccessDenied);
                    if (HasFlag(wall.Keys, WallKey::Red) && !Game::Player.HasPowerup(PowerupFlag::RedKey))
                        msg = fmt::format("{} {}", Resources::GetString(GameString::Red), accessDenied);
                    else if (HasFlag(wall.Keys, WallKey::Blue) && !Game::Player.HasPowerup(PowerupFlag::BlueKey))
                        msg = fmt::format("{} {}", Resources::GetString(GameString::Blue), accessDenied);
                    else if (HasFlag(wall.Keys, WallKey::Gold) && !Game::Player.HasPowerup(PowerupFlag::GoldKey))
                        msg = fmt::format("{} {}", Resources::GetString(GameString::Yellow), accessDenied);
                    else if (wall.HasFlag(WallFlag::DoorLocked))
                        msg = Resources::GetString(level.IsDescent1() ? GameString::CantOpenDoorD1 : GameString::CantOpenDoor);

                    if (!msg.empty())
                        PrintHudMessage(msg);
                }
            }
        }
    }

    // Opens doors targeted by a trigger (or destroys them)
    void OpenDoorTrigger(Level& level, Trigger& trigger) {
        for (auto& target : trigger.Targets) {
            //ToggleWall(target);
            if (auto wall = level.TryGetWall(target)) {
                if (wall->Type == WallType::Destroyable)
                    DestroyWall(level, *wall);

                if (wall->Type == WallType::Door || wall->Type == WallType::Closed)
                    OpenDoor(level, target);
            }
        }
    }

    void TriggerMatcen(SegID /*seg*/) {
        // do matcen stuff
    }

    void IllusionOn(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (wall) wall->SetFlag(WallFlag::IllusionOff);
        if (cwall) wall->SetFlag(WallFlag::IllusionOff);

        if (auto side = level.TryGetSide(tag)) {
            Sound3D sound(side->Center, tag.Segment);
            sound.Resource = Resources::GetSoundResource(SoundID::CloakOff);
            Sound::Play(sound);
        }
    }

    void IllusionOff(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (wall) wall->ClearFlag(WallFlag::IllusionOff);
        if (cwall) wall->ClearFlag(WallFlag::IllusionOff);

        if (auto side = level.TryGetSide(tag)) {
            Sound3D sound(side->Center, tag.Segment);
            sound.Resource = Resources::GetSoundResource(SoundID::CloakOn);
            Sound::Play(sound);
        }
    }

    void ActivateTriggerD1(Level& level, Trigger& trigger) {
        if (trigger.HasFlag(TriggerFlagD1::OneShot)) {
            if (!trigger.HasFlag(TriggerFlagD1::On))
                return;
            // should also disable the other side
            ClearFlag(trigger.FlagsD1, TriggerFlagD1::On);
        }

        if (trigger.HasFlag(TriggerFlagD1::Exit)) {
            StartExitSequence(level);
        }

        if (trigger.HasFlag(TriggerFlagD1::OpenDoor)) {
            OpenDoorTrigger(level, trigger);
            PrintTriggerMessage(trigger, "Door{} opened");
        }

        if (trigger.HasFlag(TriggerFlagD1::Matcen)) {
            // todo: matcen trigger
        }

        if (trigger.HasFlag(TriggerFlagD1::IllusionOn)) {
            PrintTriggerMessage(trigger, "Illusion{} on!");
            for (auto& tag : trigger.Targets)
                IllusionOn(level, tag);
        }

        if (trigger.HasFlag(TriggerFlagD1::IllusionOff)) {
            PrintTriggerMessage(trigger, "Illusion{} off!");
            for (auto& tag : trigger.Targets)
                IllusionOff(level, tag);
        }

        // omitted: energy and shield drain
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
                if (ChangeWalls(level, trigger)) {
                    if (WallIsForcefield(level, trigger))
                        PrintTriggerMessage(trigger, "Force field{} deactivated!");
                    else
                        PrintTriggerMessage(trigger, "Wall{} closed!");
                }
                break;

            case TriggerType::OpenWall:
                if (ChangeWalls(level, trigger)) {
                    if (WallIsForcefield(level, trigger))
                        PrintTriggerMessage(trigger, "Force field{} activated!");
                    else
                        PrintTriggerMessage(trigger, "Wall{} opened!");
                }
                break;

            case TriggerType::IllusoryWall:
                ChangeWalls(level, trigger); // not sure what message to print
                break;

            case TriggerType::IllusionOn:
                PrintTriggerMessage(trigger, "Illusion{} on!");
                for (auto& tag : trigger.Targets)
                    IllusionOn(level, tag);
                break;

            case TriggerType::IllusionOff:
                PrintTriggerMessage(trigger, "Illusion{} off!");
                for (auto& tag : trigger.Targets)
                    IllusionOff(level, tag);
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

    void ActivateTrigger(Level& level, Trigger& trigger) {
        if (level.IsDescent1())
            ActivateTriggerD1(level, trigger);
        else
            ActivateTriggerD2(level, trigger);
    }

    bool WallIsTransparent(Level& level, Tag tag) {
        auto seg = level.TryGetSegment(tag);
        if (!seg) return false;
        auto& side = seg->GetSide(tag.Side);

        if (auto wall = level.TryGetWall(tag)) {
            if (wall->Type == WallType::WallTrigger)
                return false;

            if (wall->Type == WallType::Open)
                return true;

            auto& tmap1 = Resources::GetTextureInfo(side.TMap);
            if (tmap1.Transparent) return true;

            if (side.TMap2 > LevelTexID::Unset) {
                auto& tmap2 = Resources::GetTextureInfo(side.TMap2);
                if (tmap2.SuperTransparent) return true;
            }

            return false;
        }
        else {
            // No wall on this side, test if it's open
            return seg->SideHasConnection(tag.Side);
        }
    }
}
