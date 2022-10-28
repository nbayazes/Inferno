#pragma once

#include "Level.h"
#include "Editor.Selection.h"
#include "Game.h"

namespace Inferno::Editor {
    void RemoveTrigger(Level&, TriggerID);
    //void RemoveTriggerTarget(Trigger&, Tag target);
    //bool AddTriggerTarget(Trigger& trigger, Tag tag);
    void AddTriggerTarget(Level& level, TriggerID tid, Tag tag);
    void RemoveTriggerTarget(Level& level, TriggerID id, int index);

    WallID AddWall(Level&, Tag, WallType type, LevelTexID tmap1, LevelTexID tmap2, WallFlag flags = WallFlag::None);
    bool RemoveWall(Level& level, WallID id);

    void AddTriggerTargets(Level& level, TriggerID tid, auto tags) {
        for (auto& tag : tags)
            AddTriggerTarget(level, tid, tag);
    }

    TriggerID AddTrigger(Level&, WallID, TriggerType);
    TriggerID AddTrigger(Level&, WallID, TriggerFlagD1);
    bool FixWallClip(Wall&);

    // Returns ID of the first wall (the one on the source tag)
    WallID AddPairedWall(Level& level, Tag tag, WallType type, LevelTexID tmap1, LevelTexID tmap2, WallFlag flags = WallFlag::None);

    namespace Commands {
        inline Command RemoveWall{
            .SnapshotAction = [] {
                auto wall = Game::Level.GetWallID(Editor::Selection.Tag());
                if (Editor::RemoveWall(Game::Level, wall)) {
                    if (Settings::Editor.EditBothWallSides) {
                        auto other = Game::Level.GetConnectedWallID(Editor::Selection.Tag());
                        Editor::RemoveWall(Game::Level, other);
                    }

                    return "Remove Wall";
                }

                return "";
            },
            .Name = "Remove Wall"
        };

        inline Command AddGrate{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                LevelTexID tmap1{ level.IsDescent1() ? 255 : 267 }; // Grate
                if (AddPairedWall(level, Editor::Selection.Tag(), WallType::Closed, tmap1, {}) == WallID::None)
                    return "";

                return "Add Grate";
            },
            .Name = "Add Grate"
        };

        inline Command AddCloaked{
            .SnapshotAction = [] {
                if (AddPairedWall(Game::Level, Editor::Selection.Tag(), WallType::Cloaked, {}, {}) == WallID::None)
                    return "";
                    
                return "Add Cloaked Wall";
            },
            .Name = "Add Cloaked Wall"
        };

        extern Command AddFlythroughTrigger, AddWallTrigger, AddTrigger;

        inline Command AddDoor{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                LevelTexID tmap2{ level.IsDescent1() ? 376 : 687 };
                if (AddPairedWall(level, tag, WallType::Door, {}, tmap2, WallFlag::DoorAuto) == WallID::None)
                    return "";

                return "Add Door";
            },
            .Name = "Normal Door"
        };

        inline Command AddExitDoor{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                LevelTexID tmap2{ level.IsDescent1() ? 444 : 508 };
                auto entry = AddPairedWall(level, tag, WallType::Door, {}, tmap2, WallFlag::DoorLocked);
                if (entry == WallID::None) return "";

                if (level.IsDescent1())
                    Editor::AddTrigger(level, entry, TriggerFlagD1::Exit);
                else
                    Editor::AddTrigger(level, entry, TriggerType::Exit);

                level.ReactorTriggers.Add(tag);
                return "Add Exit Door";
            },
            .Name = "Exit Door"
        };

        inline Command AddEntryDoor{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                LevelTexID tmap2{ level.IsDescent1() ? 399 : 463 };
                if (AddPairedWall(level, tag, WallType::Door, {}, tmap2, WallFlag::DoorLocked) == WallID::None)
                    return "";

                return "Add Entry Door";
            },
            .Name = "Entry Door"
        };

        inline Command AddHostageDoor{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();
                LevelTexID tmap1{ level.IsDescent1() ? 419 : 483 };
                if (AddPairedWall(level, tag, WallType::Destroyable, tmap1, {}) == WallID::None)
                    return "";

                return "Add Hostage Door";
            },
            .Name = "Hostage Door"
        };

        inline Command AddEnergyWall{
            .SnapshotAction = [] {
                auto& level = Game::Level;
                auto tag = Editor::Selection.Tag();

                LevelTexID tmap1{ level.IsDescent1() ? 328 : 353 };
                if (AddPairedWall(level, tag, WallType::Illusion, tmap1, {}) == WallID::None)
                    return "";

                return "Add Energy Wall";
            },
            .Name = "Energy Wall"
        };

        extern Command AddGuidebotDoor, AddForceField;

        inline void AddWallType(WallType type) {
            switch (type) {
                case WallType::Destroyable: AddHostageDoor(); break;
                case WallType::Door: AddDoor(); break;
                case WallType::Illusion: AddEnergyWall(); break;
                case WallType::FlyThroughTrigger: AddFlythroughTrigger(); break;
                case WallType::Closed: AddGrate(); break;
                case WallType::WallTrigger: AddWallTrigger(); break;
                case WallType::Cloaked: AddCloaked(); break;
            }
        }
    }
}
