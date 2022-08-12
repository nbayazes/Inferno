#include "pch.h"
#include "Game.Wall.h"
#include "SoundSystem.h"
#include "Physics.h"
#include "Face.h"

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
        frame = std::clamp(frame, 0, (int)clip.NumFrames);

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

    void DoOpenDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);
        auto conn = level.GetConnectedSide(wall.Tag);
        auto& side = level.GetSide(wall.Tag);
        auto& cside = level.GetSide(conn);
        auto& cwall = level.GetWall(cside.Wall);

        // todo: remove objects stuck on door

        door.Time += dt;

        auto& clip = Resources::GetWallClip(wall.Clip);
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(door.Time / frameTime);

        if (i < clip.NumFrames) {
            SetWallTMap(side, cside, clip, i);
        }

        if (i > clip.NumFrames / 2) { // half way open
            wall.SetFlag(WallFlag::DoorOpened);
            cwall.SetFlag(WallFlag::DoorOpened);
        }

        if (i >= clip.NumFrames - 1) {
            SetWallTMap(side, cside, clip, i - 1);

            if (!wall.HasFlag(WallFlag::DoorAuto)) {
                door = {}; // free door slot because it won't close
            }
            else {
                fmt::print("Waiting door\n");
                wall.State = WallState::DoorWaiting;
                cwall.State = WallState::DoorWaiting;
                door.Time = 0;
            }
        }
    }

    void DoCloseDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);

        auto front = level.TryGetWall(door.Front);
        auto back = level.TryGetWall(door.Back);

        auto conn = level.GetConnectedSide(wall.Tag);
        auto& side = level.GetSide(wall.Tag);
        auto& cside = level.GetSide(conn);

        if (wall.HasFlag(WallFlag::DoorAuto)) {
            for (auto& obj : level.Objects | views::filter(Object::IsAlive)) {
                if (obj.Segment == wall.Tag.Segment || obj.Segment == conn.Segment) {
                    DirectX::BoundingSphere sphere(obj.Position, obj.Radius);
                    auto face = Face::FromSide(level, wall.Tag);
                    if (IntersectFaceSphere(face, sphere))
                        return; // object blocking doorway!
                }
            }
        }

        auto& clip = Resources::GetWallClip(wall.Clip);

        if (door.Time == 0) { // play sound at start of closing
            //auto sound = Resources::GetSoundIndex(clip.CloseSound);
            Sound::Sound3D sound(side.Center, wall.Tag.Segment);
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


    void OpenDoor(Level& level, Tag tag) {
        auto& seg = level.GetSegment(tag);
        auto& side = seg.GetSide(tag.Side);
        auto wall = level.TryGetWall(side.Wall);
        if (!wall) throw Exception("Tried to open door on side that has no wall");

        auto conn = level.GetConnectedSide(tag);
        auto cwallId = level.TryGetWallID(conn);
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
            Sound::Sound3D sound(side.Center, tag.Segment);
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
}
