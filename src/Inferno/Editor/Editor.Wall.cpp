#include "pch.h"
#include "Editor.h"
#include "Editor.Wall.h"
#include "Editor.Geometry.h"
#include "Editor.Texture.h"
#include "Resources.h"

namespace Inferno::Editor {
    bool FixWallClip(Level& level, Wall& wall) {
        if (!level.SegmentExists(wall.Tag)) return false;
        auto& side = level.GetSide(wall.Tag);

        if (wall.Type == WallType::Door || wall.Type == WallType::Destroyable) {
            // If a clip is selected assign it
            auto id1 = Resources::GetDoorClipID(side.TMap);
            auto& wc = Resources::GetDoorClip(id1);
            if (wc.HasFlag(DoorClipFlag::TMap1) && id1 != DClipID::None) {
                wall.Clip = id1;
                return true;
            }

            auto id2 = Resources::GetDoorClipID(side.TMap2);
            auto& wc2 = Resources::GetDoorClip(id2);
            if (!wc2.HasFlag(DoorClipFlag::TMap1) && id2 != DClipID::None) {
                wall.Clip = id2;
                return true;
            }

            SPDLOG_WARN("Door at {}:{} has no texture applied with a valid clip. Defaulting to 0", wall.Tag.Segment, wall.Tag.Side);
            wall.Clip = DClipID(0);
        }
        else {
            wall.Clip = DClipID::None;
        }

        return true;
    }

    WallID AddPairedWall(Level& level, Tag tag, WallType type, LevelTexID tmap1, LevelTexID tmap2, WallFlag flags) {
        auto id = Editor::AddWall(level, tag, type, tmap1, tmap2, flags);

        if (auto other = level.GetConnectedSide(tag)) {
            Editor::AddWall(level, other, type, tmap1, tmap2, flags);
            if (level.GetConnectedWall(id) != WallID::None) {
                auto pairedEdge = GetPairedEdge(level, tag, Editor::Selection.Point);
                ResetUVs(level, other, pairedEdge);
            }
        }

        return id;
    }

    WallID AddWall(Level& level, Tag tag) {
        if (!level.SegmentExists(tag.Segment)) return WallID::None;
        auto [seg, side] = level.GetSegmentAndSide(tag);

        WallID wallId = [&] {
            // Find an unused wall slot
            for (int i = 0; i < level.Walls.size(); i++) {
                auto& wall = level.GetWall(WallID(i));
                if (wall.Tag.Segment == SegID::None)
                    return WallID(i);
            }

            // Allocate a new wall
            level.Walls.emplace_back();
            return WallID(level.Walls.size() - 1);
        }();

        auto& wall = level.GetWall(wallId);
        wall.Tag = tag;
        side.Wall = wallId;
        return wallId;
    }

    TriggerID AddTrigger(Level& level, WallID wallId, TriggerType type) {
        if (auto wall = level.TryGetWall(wallId)) {
            wall->Trigger = (TriggerID)level.Triggers.size();
            auto& trigger = level.Triggers.emplace_back();
            trigger.Type = type;
            return wall->Trigger;
        }

        return TriggerID::None;
    }

    TriggerID AddTrigger(Level& level, WallID wallId, TriggerFlagD1 flags) {
        if (auto wall = level.TryGetWall(wallId)) {
            wall->Trigger = (TriggerID)level.Triggers.size();
            auto& trigger = level.Triggers.emplace_back();
            trigger.FlagsD1 = flags;
            return wall->Trigger;
        }

        return TriggerID::None;
    }

    void RemoveTrigger(Level& level, TriggerID id) {
        if (id == TriggerID::None) return;

        // remove all references
        for (auto& wall : level.Walls) {
            if (wall.ControllingTrigger == id)
                wall.ControllingTrigger = TriggerID::None;

            if (wall.ControllingTrigger > id)
                wall.ControllingTrigger = TriggerID((int)wall.ControllingTrigger - 1);

            if (wall.Trigger == id)
                wall.Trigger = TriggerID::None;
        }

        for (auto& wall : level.Walls) {
            if (wall.Trigger != TriggerID::None && wall.Trigger > id)
                wall.Trigger--;
        }

        Seq::removeAt(level.Triggers, (uint)id);
    }

    void RemoveTriggerTarget(Level& level, TriggerID id, int index) {
        auto trigger = level.TryGetTrigger(id);
        if (!trigger || !trigger->Targets.InRange(index)) return;

        // clear source trigger from wall it was targeting
        if (auto wall = level.TryGetWall(trigger->Targets[index])) {
            if (wall->ControllingTrigger == id)
                wall->ControllingTrigger = TriggerID::None;
        }

        trigger->Targets.Remove(index);
    }

    void AddTriggerTarget(Level& level, TriggerID id, Tag target) {
        auto trigger = level.TryGetTrigger(id);
        if (!trigger) return;
        trigger->Targets.Add(target);

        // clear source trigger from wall it was targeting
        if (auto wall = level.TryGetWall(target)) {
            if (wall->ControllingTrigger == id)
                wall->ControllingTrigger = TriggerID::None;
        }
    }

    bool RemoveWall(Level& level, WallID id) {
        if (id == WallID::None) return false;

        {
            auto wall = level.TryGetWall(id);
            if (!wall) return false;
            auto seg = level.TryGetSegment(wall->Tag.Segment);
            if (!seg) return false;
            auto& side = seg->GetSide(wall->Tag.Side);

            // Unlink walls
            for (auto& w : level.Walls)
                if (w.LinkedWall == id) w.LinkedWall = WallID::None;

            // Remove wall from any triggers that might be targeting it
            for (auto& trigger : level.Triggers)
                for (int i = (int)trigger.Targets.Count() - 1; i > 0; i--) {
                    if (trigger.Targets[i] == wall->Tag)
                        trigger.Targets.Remove(i);
                }

            RemoveTrigger(level, wall->Trigger);
            side.Wall = WallID::None;
        }

        for (auto& seg : level.Segments) {
            for (auto& side : seg.Sides) {
                if (side.Wall > id && side.Wall != WallID::None) side.Wall--;
            }
        }

        level.Walls.erase(level.Walls.begin() + (int)id);
        Events::LevelChanged();
        return true;
    }

    void InitWall(Level& level, Wall& wall, WallType type) {
        if (wall.Type == type) return;

        if (type == WallType::Destroyable)
            wall.HitPoints = 100;

        if (type == WallType::Cloaked)
            wall.CloakValue(0.5f);

        if (type == WallType::Door && wall.Type != WallType::Destroyable) {
            if (auto side = level.TryGetSide(wall.Tag)) {
                side->TMap2 = LevelTexID{ level.IsDescent1() ? 376 : 687 }; // Door
            }
        }

        if (type == WallType::Destroyable && wall.Type != WallType::Door) {
            if (auto side = level.TryGetSide(wall.Tag)) {
                side->TMap2 = LevelTexID{ level.IsDescent1() ? 419 : 483 }; // Prison door
            }
        }

        wall.Type = type;
        FixWallClip(level, wall);
    }

    WallID AddWall(Level& level, Tag tag, WallType type, LevelTexID tmap1, LevelTexID tmap2, WallFlag flags) {
        if (level.Walls.size() + 1 >= (int)WallID::Max) {
            SetStatusMessageWarn("Cannot have more than {} walls in a level", WallID::Max);
            return WallID::None;
        }

        if (!level.SegmentExists(tag.Segment)) {
            SetStatusMessageWarn("Segment is invalid");
            return WallID::None;
        }

        if (level.IsDescent1() && type == WallType::WallTrigger) {
            SetStatusMessageWarn("Cannot add wall triggers to D1 levels");
            return WallID::None;
        }

        auto [seg, side] = level.GetSegmentAndSide(tag);
        if (side.Wall != WallID::None) {
            SetStatusMessage("Side already has a wall");
            return WallID::None;
        }

        if (seg.GetConnection(tag.Side) == SegID::None && type != WallType::WallTrigger) {
            SetStatusMessage("Cannot add a non-trigger wall to a closed side");
            return WallID::None;
        }

        auto wallId = AddWall(level, tag);
        if (!wallId) {
            SetStatusMessageWarn("Error adding wall to level");
            return WallID::None;
        }

        auto& wall = level.GetWall(wallId);
        wall.Tag = tag;
        wall.Flags = flags;
        InitWall(level, wall, type);
        side.TMap = tmap1;
        side.TMap2 = tmap2;
        FixWallClip(level, wall);

        if (type != WallType::WallTrigger)
            ResetUVs(level, tag, Editor::Selection.Point);

        Events::LevelChanged();
        Events::TexturesChanged();
        return wallId;
    }

    TriggerType GetTriggerTypeForTargetD2(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return TriggerType::OpenDoor;

        auto& seg = level.GetSegment(tag);
        auto wall = level.TryGetWall(tag);

        if (!wall && seg.Type == SegmentType::Matcen)
            return TriggerType::Matcen;

        if (!wall)
            return TriggerType::LightOff;

        switch (wall->Type) {
            case Inferno::WallType::Destroyable:
            case Inferno::WallType::Door:
                return TriggerType::OpenDoor;

            case Inferno::WallType::Illusion:
                return TriggerType::IllusionOff;

            case Inferno::WallType::Cloaked:
            case Inferno::WallType::Closed:
                return TriggerType::OpenWall;

            default:
                return TriggerType::LightOff;
        }
    }

    TriggerFlagD1 GetTriggerTypeForTargetD1(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return TriggerFlagD1::OpenDoor;

        auto& seg = level.GetSegment(tag);
        auto wall = level.TryGetWall(tag);

        if (!wall && seg.Type == SegmentType::Matcen)
            return TriggerFlagD1::Matcen;

        if (!wall)
            return {};

        return TriggerFlagD1::OpenDoor;
    }

    void SetupTriggerOnWall(Level& level, WallID wallId, Set<Tag> targets) {
        TriggerID tid{};

        if (level.IsDescent1()) {
            auto type = targets.empty() ? TriggerFlagD1::OpenDoor : GetTriggerTypeForTargetD1(level, *targets.begin());
            tid = AddTrigger(level, wallId, type);
        }
        else {
            auto type = targets.empty() ? TriggerType::OpenDoor : GetTriggerTypeForTargetD2(level, *targets.begin());
            tid = AddTrigger(level, wallId, type);
        }

        if (Settings::Editor.SelectionMode == SelectionMode::Face)
            AddTriggerTargets(level, tid, targets);
    }


    WallID AddWallHelper(Level& level, Tag tag, WallType type) {
        switch (type) {
            case WallType::Destroyable:
            {
                LevelTexID tmap1{ level.IsDescent1() ? 419 : 483 }; // Prison door
                return AddPairedWall(level, tag, WallType::Destroyable, tmap1, {});
            }
            case WallType::Door:
            {
                LevelTexID tmap2{ level.IsDescent1() ? 376 : 687 }; // Door
                return AddPairedWall(level, tag, WallType::Door, {}, tmap2, WallFlag::DoorAuto);
            }
            case WallType::Illusion:
            {
                LevelTexID tmap1{ level.IsDescent1() ? 328 : 353 }; // Energy field
                return AddPairedWall(level, tag, WallType::Illusion, tmap1, {});
            }
            case WallType::Open:
            {
                auto seg = level.TryGetSegment(tag);
                if (!seg) return WallID::None;

                auto& side = seg->GetSide(tag.Side);
                if (!side.HasWall())
                    Editor::AddWall(level, tag, WallType::Open, {}, {});

                if (!side.HasWall()) return WallID::None;

                SetupTriggerOnWall(level, side.Wall, Marked.Faces);
                return side.Wall;
            }
            case WallType::Closed:
            {
                LevelTexID tmap1{ level.IsDescent1() ? 255 : 267 }; // Grate
                return AddPairedWall(level, tag, WallType::Closed, tmap1, {});
            }
            case WallType::WallTrigger:
            {
                auto seg = level.TryGetSegment(tag);
                if (!seg) return WallID::None;

                auto& side = seg->GetSide(tag.Side);
                auto tmap2 = side.TMap2 == LevelTexID::Unset ? LevelTexID(414) : side.TMap2; // Switch
                auto wallId = Editor::AddWall(level, tag, WallType::WallTrigger, side.TMap, tmap2);
                if (wallId == WallID::None) return WallID::None;

                SetupTriggerOnWall(level, wallId, Marked.Faces);
                return wallId;
            }
            case WallType::Cloaked:
                return AddPairedWall(level, tag, WallType::Cloaked, {}, {});
        }

        return WallID::None;
    }

    namespace Commands {
        Command AddFlythroughTrigger{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                auto seg = level.TryGetSegment(tag);
                if (!seg) return "";

                auto& side = seg->GetSide(tag.Side);
                if (!side.HasWall())
                    Editor::AddWall(level, tag, WallType::Open, {}, {});

                if (!side.HasWall()) return ""; // failed to add a wall

                SetupTriggerOnWall(Game::Level, side.Wall, Marked.Faces);
                return "Add Flythrough Trigger";
            },
            .Name = "Add Flythrough Trigger"
        };

        Command AddWallTrigger{
            .SnapshotAction = [] {
                auto tag = Editor::Selection.Tag();
                auto& seg = Game::Level.GetSegment(tag.Segment);
                auto& side = seg.GetSide(tag.Side);

                auto tmap2 = side.TMap2 == LevelTexID::Unset ? LevelTexID(414) : side.TMap2; // Switch
                auto wallId = Editor::AddWall(Game::Level, tag, WallType::WallTrigger, side.TMap, tmap2);
                if (wallId == WallID::None) return "";
                SetupTriggerOnWall(Game::Level, wallId, Marked.Faces);
                return "Add Wall Trigger";
            },
            .Name = "Add Wall Trigger"
        };

        Command AddTrigger{
            .Action = [] {
                if (auto seg = GetSelectedSegment()) {
                    if (seg->SideHasConnection(Editor::Selection.Side))
                        AddFlythroughTrigger();
                    else
                        AddWallTrigger();
                }
            },
            .Name = "Add Trigger"
        };

        Command AddForceField{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                if (level.IsDescent1()) {
                    SetStatusMessage("Cannot add forcefields to D1 levels");
                    return "";
                }

                LevelTexID tmap1{ 420 };
                if (AddPairedWall(level, tag, WallType::Closed, tmap1, {}) == WallID::None)
                    return "";

                return "Add Force Field";
            },
            .Name = "Force Field"
        };

        Command AddGuidebotDoor{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                if (level.IsDescent1()) {
                    SetStatusMessage("Cannot add guidebot doors to D1 levels");
                    return "";
                }

                LevelTexID tmap1{ 858 };
                if (AddPairedWall(level, tag, WallType::Destroyable, tmap1, {}) == WallID::None)
                    return "";

                return "Add Guidebot Door";
            },
            .Name = "Guidebot Door"
        };
    }
}
