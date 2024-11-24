#include "pch.h"
#define NOMINMAX
#include "Game.Wall.h"
#include "SoundSystem.h"
#include "Physics.h"
#include <Game.Segment.h>
#include "Face.h"
#include "Game.h"
#include "HUD.h"
#include "DataPool.h"
#include "Game.AI.h"
#include "Resources.h"
#include "Editor/Events.h"
#include "VisualEffects.h"
#include "Graphics.h"

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
        RemoveDecals(tag);
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
                //fmt::print("Waiting door\n");
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
                auto face = ConstFace::FromSide(level, tag);
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
            Sound3D sound(clip.CloseSound);
            Sound::Play(sound, side.Center, wall.Tag.Segment);
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
    void OpenDoor(Level& level, Tag tag, Faction source) {
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

        //fmt::print("Opening door {}:{}\n", tag.Segment, tag.Side);
        wall->State = WallState::DoorOpening;
        door->Front = side.Wall;

        if (cwall) {
            door->Back = cwallId;
            cwall->State = WallState::DoorOpening;
        }

        if (clip.OpenSound != SoundID::None) {
            Sound::Play({ clip.OpenSound }, side.Center, tag.Segment);
        }

        // Have robots look at opened doors on Hotshot and above that they didn't open
        if (Game::Difficulty >= DifficultyLevel::Hotshot && HasFlag(source, Faction::Player)) {
            // Alert both sides of the door (sound stops at closed doors)
            //AlertRobotsOfNoise({ tag.Segment, side.Center + side.AverageNormal }, AI_DOOR_AWARENESS_RADIUS, 2);
            //AlertRobotsOfNoise({ conn.Segment, side.Center - side.AverageNormal }, AI_DOOR_AWARENESS_RADIUS, 2);

            NavPoint soundSource = { tag.Segment, side.Center + side.AverageNormal };

            IterateNearbySegments(Game::Level, soundSource, AI_DOOR_AWARENESS_RADIUS, TraversalFlag::None, [&soundSource](const Segment& nearbySeg, bool) {
                AlertEnemiesInSegment(Game::Level, nearbySeg, soundSource, AI_DOOR_AWARENESS_RADIUS, 1);
            });
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
                    //fmt::print("Closing door {}\n", door.Front);
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
                    Sound3D sound(SoundID::ForcefieldOff);
                    Sound::Play(sound, wside->Center, wall.Tag.Segment);
                    Sound::Stop(wall.Tag); // stop the humming sound
                    wall.Type = wallType;
                    fmt::print("Turned off forcefield {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                else {
                    // do wall uncloak
                    Sound3D sound(SoundID::CloakOn);
                    Sound::Play(sound, wside->Center, wall.Tag.Segment);
                    wall.Type = wallType; // would be delayed by animation
                    fmt::print("Opened wall {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                break;

            case TriggerType::CloseWall:
                if (Resources::GetLevelTextureInfo(wside->TMap).HasFlag(TextureFlag::ForceField)) {
                    Sound3D sound(SoundID::ForcefieldHum);
                    sound.Looped = true;
                    sound.Volume = 0.5f;
                    Sound::Play(sound, wside->Center, wall.Tag.Segment);
                    wall.Type = wallType;
                    fmt::print("Activated forcefield {}:{}\n", wall.Tag.Segment, wall.Tag.Side);
                }
                else {
                    // do wall cloak
                    Sound3D sound(SoundID::CloakOff);
                    Sound::Play(sound, wside->Center, wall.Tag.Segment);
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
        //Game::SetState(GameState::ExitSequence);
    }

    void EnterSecretLevel() {}

    void ToggleWall(Segment& /*seg*/, SideID /*side*/) {}

    Option<SideID> GetConnectedSide(Segment& base, SegID conn) {
        for (auto& side : SIDE_IDS) {
            if (base.GetConnection(side) == conn)
                return side;
        }

        return {};
    }

    struct ExplodingWall {
        Tag Tag;
        RoomID Room = RoomID::None;
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

                if (e % 4 == 0) {
                    // Create a damaging explosion 1/4th of the time
                    GameExplosion expl{};
                    expl.Damage = 4;
                    expl.Radius = 20;
                    expl.Force = 50;
                    expl.Position = pos;
                    expl.Segment = wall.Tag.Segment;
                    expl.Room = wall.Room;
                    CreateExplosion(level, nullptr, expl);
                }

                ParticleInfo p{};
                p.Clip = VClipID::SmallExplosion;
                p.Radius = size / 2;
                p.Color = Color(1, .75f, .75f, 2.0f);
                AddParticle(p, wall.Tag.Segment, pos);
            }

            if (wall.Time >= EXPLODE_TIME)
                wall.Tag = {}; // Free the slot
        }
    }

    void ExplodeWall(Level& level, Tag tag) {
        // create small explosions on the face
        auto& side = level.GetSide(tag);
        Sound3D sound(SoundID::ExplodingWall);
        Sound::Play(sound, side.Center, tag.Segment);

        auto room = level.GetRoomID(tag.Segment);
        ExplodingWalls.Add({ tag, room });
        //if (auto conn = level.GetConnectedSide(tag))
        //    ExplodingWalls.Add({ conn, room }); // Create explosions on both sides
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

    bool RobotCanOpenDoor(Level& /*level*/, const Wall& wall, const Object& robot) {
        // Don't allow sleeping robots to open walls. Important because several
        // robots in official levels are positioned on top of secret doors.
        auto& ai = GetAI(robot);
        if (ai.Awareness <= 0)
            return false;

        auto& robotInfo = Resources::GetRobotInfo(robot);

        if (wall.Type != WallType::Door || wall.HasFlag(WallFlag::DoorLocked))
            return false;

        if (wall.IsKeyDoor()) {
            if (!robotInfo.OpenKeyDoors) return false; // Robot can't open key doors
            if (!Game::Player.CanOpenDoor(wall)) return false; // Player doesn't have the key, so neither does the robot
        }

        // Don't allow robots to open locked doors from the back even if they are open.
        // Can cause sequence breaking or undesired behavior. Note that the thief
        // could originally open locked doors from the back.
        // Note: some user levels rely on this behavior
        //if (auto cwall = level.GetConnectedWall(wall)) {
        //    if (cwall->Type != WallType::Door || cwall->HasFlag(WallFlag::DoorLocked))
        //        return false;

        //    bool isKeyDoor = HasFlag(cwall->Keys, WallKey::Red) || HasFlag(cwall->Keys, WallKey::Gold) || HasFlag(cwall->Keys, WallKey::Blue);
        //    if (!robotInfo.OpenKeyDoors && isKeyDoor)
        //        return false;

        //    if (isKeyDoor && !Game::Player.CanOpenDoor(*cwall))
        //        return false;
        //}

        return true;
    }

    void HitWall(Level& level, const Vector3& point, const Object& src, const Wall& wall) {
        auto parent = level.TryGetObject(src.Parent);
        bool isPlayerSource = src.IsPlayer() || (parent && parent->IsPlayer());
        bool isRobotSource = src.IsRobot() || (parent && parent->IsRobot());
        // Should robots only be able to open doors by touching them?
        //const Object* pRobot = src.IsRobot() ? &src : nullptr; // Only allow touching
        const Object* pRobot = src.IsRobot() ? &src : (parent && parent->IsRobot() ? parent : nullptr);

        if (wall.Type == WallType::Destroyable && isPlayerSource && src.Type == ObjectType::Weapon) {
            auto& weapon = Resources::GetWeapon((WeaponID)src.ID);
            DamageWall(level, wall.Tag, GetDamage(weapon));
        }
        else if (wall.Type == WallType::Door) {
            if (pRobot && RobotCanOpenDoor(level, wall, *pRobot)) {
                // Allow robots to open normal doors
                OpenDoor(level, wall.Tag, src.Faction);
            }
            else if (isPlayerSource && Game::Player.CanOpenDoor(wall)) {
                OpenDoor(level, wall.Tag, src.Faction);
            }
            else if (src.Type == ObjectType::Weapon || src.Type == ObjectType::Player) {
                // Can't open door
                if ((isPlayerSource || isRobotSource) && src.Type == ObjectType::Weapon) {
                    Sound::Play({ SoundID::HitLockedDoor }, point, wall.Tag.Segment);
                }

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

    void AddPlanarExplosion(const Weapon& weapon, const LevelHit& hit) {
        auto rotation = Matrix::CreateFromAxisAngle(hit.Normal, Random() * DirectX::XM_2PI);

        // Add the planar explosion effect
        Decal decal{};
        auto tangent = Vector3::Transform(hit.Tangent, rotation);
        decal.Texture = weapon.Extended.ExplosionTexture;
        decal.Radius = weapon.Extended.ExplosionSize;
        decal.FadeTime = weapon.Extended.ExplosionTime;
        decal.FadeRadius = weapon.GetDecalSize() * 2.4f;
        decal.Additive = true;
        decal.Color = Color{ 1.5f, 1.5f, 1.5f };
        AddDecal(decal, hit.Tag, hit.Point, hit.Normal, tangent, decal.FadeTime);

        //Render::DynamicLight light{};
        //light.LightColor = weapon.Extended.ExplosionColor;
        //light.Radius = weapon.Extended.LightRadius;
        //light.Position = hit.Point + hit.Normal * weapon.Extended.ExplosionSize;
        //light.Duration = light.FadeTime = weapon.Extended.ExplosionTime;
        //light.Segment = hit.Tag.Segment;
        //Render::AddDynamicLight(light);
    }


    // returns true if overlay was destroyed
    bool CheckDestroyableOverlay(Level& level, const Vector3& point, Tag tag, int tri, bool isPlayer) {
        tri = std::clamp(tri, 0, 1);

        auto seg = level.TryGetSegment(tag);
        if (!seg) return false;

        auto& side = seg->GetSide(tag.Side);
        if (side.TMap2 <= LevelTexID::Unset) return false;

        auto& tmi = Resources::GetLevelTextureInfo(side.TMap2);
        if (tmi.EffectClip == EClipID::None && tmi.DestroyedTexture == LevelTexID::None)
            return false;

        auto& eclip = Resources::GetEffectClip(tmi.EffectClip);
        if (eclip.OneShotTag) return false; // don't trigger from one-shot effects

        bool hasEClip = eclip.DestroyedTexture != LevelTexID::None || eclip.DestroyedEClip != EClipID::None;
        if (!hasEClip && tmi.DestroyedTexture == LevelTexID::None)
            return false;

        // Don't allow non-players to destroy triggers
        if (!isPlayer) {
            if (auto wall = level.TryGetWall(tag)) {
                if (wall->Trigger != TriggerID::None)
                    return false;
            }
        }

        auto face = ConstFace::FromSide(level, *seg, tag.Side);
        auto uv = IntersectFaceUVs(point, face, tri);

        auto& bitmap = Resources::GetBitmap(Resources::LookupTexID(side.TMap2));
        auto& info = bitmap.Info;
        auto x = uint(uv.x * info.Width) % info.Width;
        auto y = uint(uv.y * info.Height) % info.Height;
        FixOverlayRotation(x, y, info.Width, info.Height, side.OverlayRotation);

        if (!bitmap.Mask.empty() && bitmap.Mask[y * info.Width + x] == Palette::SUPER_MASK)
            return false; // portion hit was supertransparent

        if (bitmap.Data[y * info.Width + x].a == 0)
            return false; // portion hit was transparent

        // Hit opaque overlay!
        //Inferno::SubtractLight(level, tag, *seg);

        bool usedEClip = false;

        if (eclip.DestroyedEClip != EClipID::None) {
            // Hack storing exploding side state into the global effect.
            // The original game did this, but should be replaced with a more robust system.
            // If more than one monitor breaks with different times the animation wouldn't play properly.
            if (Seq::inRange(Resources::GameData.Effects, (int)eclip.DestroyedEClip)) {
                auto& destroyed = Resources::GameData.Effects[(int)eclip.DestroyedEClip];
                if (!destroyed.OneShotTag) {
                    side.TMap2 = Resources::LookupLevelTexID(destroyed.VClip.Frames[0]);
                    destroyed.TimeLeft = destroyed.VClip.PlayTime;
                    destroyed.OneShotTag = tag;
                    destroyed.DestroyedTexture = eclip.DestroyedTexture;
                    usedEClip = true;
                    Graphics::LoadTexture(eclip.DestroyedTexture);
                    Graphics::LoadTexture(side.TMap2);
                }
            }
        }

        if (!usedEClip) {
            // Skip to the destroyed fully texture
            side.TMap2 = hasEClip ? eclip.DestroyedTexture : tmi.DestroyedTexture;
            Graphics::LoadTexture(side.TMap2);
        }

        //Editor::Events::LevelChanged();

        //Render::ExplosionInfo ei;
        //ei.Clip = hasEClip ? eclip.DestroyedVClip : VClipID::LightExplosion;
        //ei.MinRadius = ei.MaxRadius = hasEClip ? eclip.ExplosionSize : 20.0f;
        //ei.FadeTime = 0.25f;
        //ei.Position = point;
        //ei.Segment = tag.Segment;
        //Render::CreateExplosion(ei);
        if (auto e = EffectLibrary.GetSparks("overlay_destroyed")) {
            e->Direction = side.AverageNormal;
            e->Up = side.Tangents[0];
            auto position = point + side.AverageNormal * 0.1f;
            AddSparkEmitter(*e, tag.Segment, position);
        }

        auto& vclip = Resources::GetVideoClip(eclip.DestroyedVClip);
        auto soundId = vclip.Sound != SoundID::None ? vclip.Sound : SoundID::LightDestroyed;
        Sound3D sound(soundId);
        Sound::Play(sound, point, tag.Segment);

        if (auto trigger = level.TryGetTrigger(side.Wall)) {
            SPDLOG_INFO("Activating switch {}:{}\n", tag.Segment, tag.Side);
            //fmt::print("Activating switch {}:{}\n", tag.Segment, tag.Side);
            ActivateTrigger(level, *trigger, tag);
        }

        return true; // was destroyed!
    }

    // There are four possible outcomes when hitting a wall:
    // 1. Hit a normal wall
    // 2. Hit water. Reduces damage of explosion and changes sound effect
    // 3. Hit lava. Creates explosion for all weapons and changes sound effect
    // 4. Hit forcefield. Bounces non-matter weapons.
    void WeaponHitWall(const LevelHit& hit, Object& obj, Inferno::Level& level, ObjID objId) {
        if (!hit.Tag) return;
        if (obj.Lifespan <= 0) return; // Already dead
        bool isPlayer = obj.Control.Weapon.ParentType == ObjectType::Player;
        CheckDestroyableOverlay(level, hit.Point, hit.Tag, hit.Tri, isPlayer);

        auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
        float damage = GetDamage(weapon); // Damage used when hitting lava
        float splashRadius = weapon.SplashRadius;
        float force = damage;
        float impactSize = weapon.ImpactSize;

        // don't use volatile hits on large explosions like megas
        constexpr float VOLATILE_DAMAGE_RADIUS = 30;
        bool isLargeExplosion = splashRadius >= VOLATILE_DAMAGE_RADIUS / 2;

        // weapons with splash damage (explosions) always use robot hit effects
        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;

        auto& side = level.GetSide(hit.Tag);
        auto& ti = Resources::GetLevelTextureInfo(side.TMap);
        bool hitForcefield = ti.HasFlag(TextureFlag::ForceField);
        bool hitLava = ti.HasFlag(TextureFlag::Volatile);
        bool hitWater = ti.HasFlag(TextureFlag::Water);

        // Special case for flares
        if (HasFlag(obj.Physics.Flags, PhysicsFlag::Stick) && !hitLava && !hitWater && !hitForcefield) {
            // sticky flare behavior
            Vector3 vec;
            obj.Physics.Velocity.Normalize(vec);
            //obj.Position -= vec * obj.Radius; // move out of wall
            obj.Physics.Velocity = Vector3::Zero;
            StuckObjects.Add(hit.Tag, objId);
            obj.Flags |= ObjectFlag::Attached;
            return;
        }

        auto bounce = hit.Bounced;
        if (hitLava && weapon.SplashRadius > 0)
            bounce = false; // Explode bouncing explosive weapons (mines) when touching lava

        if (!bounce) {
            // Move object to the desired explosion location
            auto dir = obj.Physics.PrevVelocity;
            dir.Normalize();

            if (impactSize < 5)
                obj.Position = hit.Point - dir * impactSize * 0.25f;
            else
                obj.Position = hit.Point - dir * 2.5;
        }

        if (hitForcefield) {
            if (!weapon.IsMatter) {
                // Bounce energy weapons
                obj.Physics.Bounces++;
                obj.Parent = {}; // Make hostile to owner!
                Sound::Play({ SoundID::WeaponHitForcefield }, hit.Point, hit.Tag.Segment);
            }
        }
        else if (hitLava) {
            if (!isLargeExplosion) {
                // add volatile size and damage bonuses to smaller explosions
                vclip = VClipID::HitLava;
                constexpr float VOLATILE_DAMAGE = 10;
                constexpr float VOLATILE_FORCE = 5;

                damage = damage / 4 + VOLATILE_DAMAGE;
                splashRadius += VOLATILE_DAMAGE_RADIUS;
                force = force / 2 + VOLATILE_FORCE;
                impactSize += 1;
            }

            // Create a damaging and visual explosion
            GameExplosion ge{};
            ge.Segment = hit.Tag.Segment;
            ge.Position = obj.Position;
            ge.Damage = damage;
            ge.Force = force;
            ge.Radius = splashRadius;
            ge.Room = level.GetRoomID(obj);
            CreateExplosion(level, &obj, ge);

            ExplosionEffectInfo e;
            e.Radius = { weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f };
            e.Clip = vclip;
            e.FadeTime = weapon.Extended.ExplosionTime;
            e.Color = Color{ 1, .7f, .7f, 2 };
            e.LightColor = Color{ 1.0f, 0.6f, 0.05f, .5 };
            e.LightRadius = splashRadius;
            CreateExplosion(e, obj.Segment, obj.Position);

            Sound::Play({ SoundID::HitLava }, hit.Point, hit.Tag.Segment);
        }
        else if (hitWater) {
            if (isLargeExplosion) {
                // reduce strength of megas and shakers in water, but don't cancel them
                splashRadius *= 0.5f;
                damage *= 0.25f;
                force *= 0.5f;
                impactSize *= 0.5f;
            }
            else {
                vclip = VClipID::HitWater;
                splashRadius = 0; // Cancel explosions when hitting water
            }

            if (splashRadius > 0) {
                // Create damage for large explosions
                GameExplosion ge{};
                ge.Segment = hit.Tag.Segment;
                ge.Position = obj.Position;
                ge.Damage = damage;
                ge.Force = force;
                ge.Radius = splashRadius;
                CreateExplosion(level, &obj, ge);
            }

            ParticleInfo e;
            e.Radius = NumericRange(weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f).GetRandom();
            e.Clip = vclip;
            e.FadeTime = weapon.Extended.ExplosionTime;
            e.Color = Color(1, 1, 1);
            AddParticle(e, obj.Segment, obj.Position);

            auto splashId = weapon.IsMatter ? SoundID::MissileHitWater : SoundID::HitWater;
            Sound::Play({ splashId }, hit.Point, hit.Tag.Segment);
        }
        else {
            // Hit normal wall
            Game::AddWeaponDecal(hit, weapon);

            // Explosive weapons play their effects on death instead of here
            if (!bounce && splashRadius <= 0) {
                if (vclip != VClipID::None)
                    Game::DrawWeaponExplosion(obj, weapon);

                SoundResource resource = { soundId };
                resource.D3 = weapon.Extended.ExplosionSound; // Will take priority if D3 is loaded
                Sound3D sound(resource);
                sound.Volume = Game::WEAPON_HIT_WALL_VOLUME;
                Sound::Play(sound, hit.Point, hit.Tag.Segment);
            }
        }

        if (!bounce)
            obj.Lifespan = 0; // remove weapon after hitting a wall
    }

    // Opens doors targeted by a trigger (or destroys them)
    void OpenDoorTrigger(Level& level, Trigger& trigger) {
        for (auto& target : trigger.Targets) {
            //ToggleWall(target);
            if (auto wall = level.TryGetWall(target)) {
                if (wall->Type == WallType::Destroyable)
                    DestroyWall(level, *wall);

                if (wall->Type == WallType::Door || wall->Type == WallType::Closed)
                    OpenDoor(level, target, Faction::Neutral);
            }
        }
    }

    void IllusionOn(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (wall) wall->SetFlag(WallFlag::IllusionOff);
        if (cwall) wall->SetFlag(WallFlag::IllusionOff);

        if (auto side = level.TryGetSide(tag)) {
            Sound::Play({ SoundID::CloakOff }, side->Center, tag.Segment);
        }
    }

    void IllusionOff(Level& level, Tag tag) {
        auto [wall, cwall] = level.TryGetWalls(tag);
        if (wall) wall->ClearFlag(WallFlag::IllusionOff);
        if (cwall) wall->ClearFlag(WallFlag::IllusionOff);

        if (auto side = level.TryGetSide(tag)) {
            Sound::Play({ SoundID::CloakOn }, side->Center, tag.Segment);
        }
    }

    void ActivateTriggerD1(Level& level, Trigger& trigger, Tag src) {
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
            fmt::print("Trigger Matcen\n");
            for (auto& tag : trigger.Targets)
                TriggerMatcen(level, tag.Segment, src.Segment);
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

    void ActivateTriggerD2(Level& level, Trigger& trigger, Tag src) {
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
                for (auto& tag : trigger.Targets)
                    TriggerMatcen(level, tag.Segment, src.Segment);

                break;
        }
    }

    void ActivateTrigger(Level& level, Trigger& trigger, Tag src) {
        if (level.IsDescent1())
            ActivateTriggerD1(level, trigger, src);
        else
            ActivateTriggerD2(level, trigger, src);
    }

    bool WallIsTransparent(const Level& level, const Wall& wall) {
        //if (wall.Type == WallType::WallTrigger)
        //    return false;

        //if (wall.Type == WallType::Open)
        //    return true;

        if (auto side = level.TryGetSide(wall.Tag)) {
            auto& tmap1 = Resources::GetTextureInfo(side->TMap);
            if (tmap1.Transparent) return true;

            if (side->TMap2 > LevelTexID::Unset) {
                auto& tmap2 = Resources::GetTextureInfo(side->TMap2);
                if (tmap2.SuperTransparent) return true;
            }
        }

        return false;
    }

    bool SideIsTransparent(const Level& level, Tag tag) {
        auto seg = level.TryGetSegment(tag);
        if (!seg) return false;
        auto& side = seg->GetSide(tag.Side);

        if (auto wall = level.TryGetWall(side.Wall)) {
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
