#include "pch.h"
#include "EditorUI.h"
#include "Shell.h"
#include "Settings.h"
#include "imgui_local.h"
#include "DebugOverlay.h"
#include "Editor/Bindings.h"
#include "Editor/Editor.Clipboard.h"
#include "Editor/Editor.h"
#include "Editor/Editor.Segment.h"
#include "Editor/Editor.Texture.h"
#include "Editor/Editor.Wall.h"

namespace Inferno::Editor {
    constexpr ImU32 TOOLBAR_COLOR = IM_COL32(20, 20, 20, 200);

    void MenuCommandEx(const Command& command, const char* label, EditorAction bind = EditorAction::None, bool selected = false) {
        if (!label) label = command.Name.c_str();
        if (ImGui::MenuItem(label, Bindings::Active.GetShortcut(bind).c_str(), selected, command.CanExecute()))
            command();
    }

    void MenuCommand(const Command& command, EditorAction bind = EditorAction::None) {
        MenuCommandEx(command, command.Name.c_str(), bind);
    }

    void MenuCommand(EditorAction action, const char* label = nullptr) {
        auto& command = GetCommandForAction(action);
        auto name = label ? label : command.Name.c_str();
        MenuCommandEx(command, name, action);
    }

    void FaceEditMenu() {
        if (ImGui::MenuItem("Mark Coplanar", "Ctrl+Shift+Click")) Commands::MarkCoplanar(Editor::Selection.Tag());
        if (ImGui::BeginMenu("Mark By Texture")) {
            if (ImGui::MenuItem("Base")) Commands::SelectTexture(true, false);
            if (ImGui::MenuItem("Overlay")) Commands::SelectTexture(false, true);
            if (ImGui::MenuItem("Both")) Commands::SelectTexture(true, true);
            ImGui::EndMenu();
        }
    }

    void ClipboardMenu() {
        MenuCommand(Commands::Cut, EditorAction::Cut);
        MenuCommand(Commands::Copy, EditorAction::Copy);
        MenuCommand(Commands::Paste, EditorAction::Paste);
    }

    void SplitMenu() {
        if (ImGui::BeginMenu("Split Segment")) {
            MenuCommand(Commands::SplitSegment2, EditorAction::SplitSegment2);
            MenuCommand(Commands::SplitSegment3);
            MenuCommand(Commands::SplitSegment5);
            MenuCommand(Commands::SplitSegment7);
            MenuCommand(Commands::SplitSegment8);
            ImGui::EndMenu();
        }
    }

    void ObjectMenu() {
        MenuCommand(Commands::MoveObjectToSide);
        MenuCommand(Commands::MoveObjectToSegment);
        MenuCommand(Commands::MoveObjectToUserCSys);
        MenuCommand(Commands::AlignObjectToSide);
    }

    void InsertMenuItems() {
        if (ImGui::BeginMenu("Add Segment")) {
            if (ImGui::MenuItem("Energy Center")) Commands::AddEnergyCenter();
            if (ImGui::MenuItem("Matcen")) Commands::AddMatcen();
            if (ImGui::MenuItem("Reactor")) Commands::AddReactor();
            if (ImGui::MenuItem("Secret Exit")) Commands::AddSecretExit();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add Wall")) {
            MenuCommand(Commands::AddGrate);
            MenuCommand(Commands::AddEnergyWall);
            if (Game::Level.IsDescent2()) MenuCommand(Commands::AddForceField);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add Door")) {
            MenuCommand(Commands::AddDoor);
            MenuCommand(Commands::AddEntryDoor);
            MenuCommand(Commands::AddExitDoor);
            MenuCommand(Commands::AddHostageDoor);
            if (Game::Level.IsDescent2()) MenuCommand(Commands::AddGuidebotDoor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add Object")) {
            auto addObjectType = [](const char* name, ObjectType type) {
                if (ImGui::MenuItem(name)) {
                    auto id = AddObject(Game::Level, Editor::Selection.PointTag(), type);
                    if (id != ObjID::None) Editor::History.SnapshotLevel(fmt::format("Add {}", name));
                }
            };

            addObjectType("Player", ObjectType::Player);
            addObjectType("Robot", ObjectType::Robot);
            addObjectType("Powerup", ObjectType::Powerup);
            addObjectType("Co-op", ObjectType::Coop);
            addObjectType("Hostage", ObjectType::Hostage);
            ImGui::EndMenu();
        }

        MenuCommand(Commands::AddTrigger);
    }

    void EditorUI::DrawMenu() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                MenuCommand(EditorAction::NewLevel);
                MenuCommand(EditorAction::Open);

                ImGui::Separator();

                MenuCommand(EditorAction::Save);
                MenuCommand(EditorAction::SaveAs);

                ImGui::Separator();

                MenuCommand(EditorAction::ShowHogEditor, "Edit HOG...");
                MenuCommand(EditorAction::ShowMissionEditor, "Edit Mission...");

                if (ImGui::MenuItem("Rename Level...")) {
                    Events::ShowDialog(DialogType::RenameLevel);
                }

                if (Game::Level.IsDescent2()) {
                    if (ImGui::BeginMenu("Palette")) {
                        bool paletteChanged = false;

                        for (auto& palette : Resources::GetAvailablePalettes()) {
                            if (ImGui::MenuItem(palette.Name.c_str(), nullptr, String::ToUpper(Game::Level.Palette) == palette.FileName)) {
                                Game::Level.Palette = palette.FileName;
                                paletteChanged = true;
                            }
                        }

                        if (paletteChanged) {
                            // Reloading should probably be done elsewhere, but it must be done after drawing the menu items
                            // because loading the level changes the palette list.
                            Resources::LoadLevel(Game::Level);
                            Render::Materials->Reload();
                        }

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Convert")) {
                        MenuCommand(Commands::ConvertToD2);
                        MenuCommand(Commands::ConvertToVertigo);
                        ImGui::EndMenu();
                    }
                }

                if (!Settings::Editor.RecentFiles.empty()) {
                    ImGui::Separator();
                    for (auto& file : Settings::Editor.RecentFiles) {
                        if (file.empty()) continue;
                        if (ImGui::MenuItem(file.filename().string().c_str()))
                            if (CanCloseCurrentFile()) Game::LoadLevel(file, "", true);
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Main Menu")) {
                    if (CanCloseCurrentFile()) {
                        Game::SetState(GameState::MainMenu);
                    }
                }

                if (ImGui::MenuItem("Exit", "Alt+F4"))
                    Commands::Exit();

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::BeginMenu("Go To")) {
                    if (ImGui::MenuItem("Go To Player Start")) Commands::GoToPlayerStart();
                    if (ImGui::MenuItem("Go To Reactor")) Commands::GoToReactor();
                    if (ImGui::MenuItem("Go To Boss")) Commands::GoToBoss();
                    if (ImGui::MenuItem("Go To Exit")) Commands::GoToExit();
                    if (ImGui::MenuItem("Go To Secret Exit", nullptr, nullptr, Game::Level.HasSecretExit())) Commands::GoToSecretExit();
                    if (ImGui::MenuItem("Go To Secret Exit Return", nullptr, nullptr, Game::Level.IsDescent2() && Game::Level.HasSecretExit())) Commands::GoToSecretExitReturn();

                    MenuCommandEx(Commands::GotoObject, "Go To Object...", EditorAction::ShowGotoObject);
                    MenuCommandEx(Commands::GotoSegment, "Go To Segment...", EditorAction::ShowGotoSegment);
                    MenuCommandEx(Commands::GotoWall, "Go To Wall...", EditorAction::ShowGotoWall);

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Mode")) {
                    if (ImGui::MenuItem("Point", "1")) Editor::SetMode(SelectionMode::Point);
                    if (ImGui::MenuItem("Edge", "2")) Editor::SetMode(SelectionMode::Edge);
                    if (ImGui::MenuItem("Face", "3")) Editor::SetMode(SelectionMode::Face);
                    if (ImGui::MenuItem("Segment", "4")) Editor::SetMode(SelectionMode::Segment);
                    if (ImGui::MenuItem("Object", "5")) Editor::SetMode(SelectionMode::Object);
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                MenuCommand(Commands::Delete, EditorAction::Delete);
                MenuCommand(Commands::RemoveWall);

                ImGui::Separator();

                auto undoLabel = fmt::format("Undo {}", Editor::History.GetUndoName());
                MenuCommandEx(Commands::Undo, undoLabel.c_str(), EditorAction::Undo);

                auto redoLabel = fmt::format("Redo {}", Editor::History.GetRedoName());
                MenuCommandEx(Commands::Redo, redoLabel.c_str(), EditorAction::Redo);

                ImGui::Separator();

                ClipboardMenu();
                MenuCommand(Commands::PasteMirrored, EditorAction::PasteMirrored);
                if (ImGui::BeginMenu("Include Segment")) {
                    ImGui::MenuItem("Walls", nullptr, &Settings::Editor.PasteSegmentWalls);
                    ImGui::MenuItem("Objects", nullptr, &Settings::Editor.PasteSegmentObjects);
                    ImGui::MenuItem("Types", nullptr, &Settings::Editor.PasteSegmentSpecial);
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if (ImGui::BeginMenu("Marks")) {
                    MenuCommand(Commands::ToggleMarked, EditorAction::ToggleMark);
                    MenuCommand(Commands::ClearMarked, EditorAction::ClearSelection);
                    MenuCommand(Commands::MarkAll);
                    MenuCommand(Commands::InvertMarked, EditorAction::InvertMarked);

                    FaceEditMenu();

                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ObjectMenu();
                ImGui::Separator();

                if (ImGui::MenuItem("Settings..."))
                    Events::ShowDialog(DialogType::Settings);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Geometry")) {
                MenuCommand(Commands::ConnectSides, EditorAction::ConnectSides);
                MenuCommand(Commands::JoinSides, EditorAction::JoinSides);
                MenuCommand(Commands::JoinPoints, EditorAction::JoinPoints);
                MenuCommand(Commands::JoinTouchingSegments, EditorAction::JoinTouchingSegments);
                MenuCommand(Commands::AveragePoints, EditorAction::AveragePoints);
                ImGui::Separator();
                SplitMenu();
                MenuCommand(Commands::MergeSegment, EditorAction::MergeSegment);
                ImGui::Separator();
                MenuCommand(Commands::DetachSegments, EditorAction::DetachSegments);
                MenuCommand(Commands::DetachSides, EditorAction::DetachSides);
                MenuCommand(Commands::DetachPoints, EditorAction::DetachPoints);
                ImGui::Separator();

                MenuCommand(Commands::MirrorSegments);
                if (ImGui::MenuItem("Weld All Vertices")) Commands::WeldVertices();
                if (ImGui::MenuItem("Snap To Grid")) Commands::SnapToGrid();
                MenuCommand(Commands::MakeCoplanar, EditorAction::MakeCoplanar);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Texturing")) {
                MenuCommand(Commands::ResetUVs, EditorAction::ResetUVs);
                MenuCommand(Commands::FitUVs, EditorAction::FitUVs);
                MenuCommand(Commands::AlignMarked, EditorAction::AlignMarked);
                MenuCommand(Commands::CopyUVsToFaces, EditorAction::CopyUVsToFaces);
                MenuCommand(Commands::PlanarMapping);
                MenuCommand(Commands::CubeMapping);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Insert")) {
                MenuCommandEx(Commands::Insert, "Segment or Object", EditorAction::Insert);
                MenuCommandEx(Commands::InsertMirrored, "Mirrored Segment", EditorAction::InsertMirrored);
                MenuCommand(Commands::InsertSegmentAtOrigin);
                MenuCommand(Commands::InsertAlignedSegment, EditorAction::InsertAlignedSegment);
                ImGui::Separator();
                InsertMenuItems();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")) {
                MenuCommand(Commands::FocusSelection, EditorAction::FocusSelection);
                MenuCommand(Commands::AlignViewToFace, EditorAction::AlignViewToFace);
                MenuCommand(Commands::ToggleMouselook, EditorAction::ToggleMouselook);
                ImGui::Separator();

                //if (ImGui::MenuItem("Wireframe", nullptr, Settings::Editor.RenderMode == RenderMode::Wireframe))
                //    Settings::Editor.RenderMode = RenderMode::Wireframe;

                if (ImGui::MenuItem("No Fill", "F4", Settings::Editor.RenderMode == RenderMode::None)) {
                    Settings::Editor.RenderMode = RenderMode::None;
                    if (!Settings::Editor.ShowWireframe)
                        Settings::Editor.ShowWireframe = true;
                }
                if (ImGui::MenuItem("Flat", "F4", Settings::Editor.RenderMode == RenderMode::Flat))
                    Settings::Editor.RenderMode = RenderMode::Flat;
                if (ImGui::MenuItem("Textured", "F4", Settings::Editor.RenderMode == RenderMode::Textured))
                    Settings::Editor.RenderMode = RenderMode::Textured;
                if (ImGui::MenuItem("Shaded", "F4", Settings::Editor.RenderMode == RenderMode::Shaded))
                    Settings::Editor.RenderMode = RenderMode::Shaded;

                ImGui::Separator();
                MenuCommandEx(Commands::ToggleWireframe, "Show Wireframe", EditorAction::ToggleWireframe, Settings::Editor.ShowWireframe);
                ImGui::Separator();

                ImGui::MenuItem("Objects", nullptr, &Settings::Editor.ShowObjects);
                //ImGui::MenuItem("Walls", nullptr, &Settings::Editor.ShowWalls);
                //ImGui::MenuItem("Triggers", nullptr, &Settings::Editor.ShowTriggers);

                ImGui::Separator();

                if (ImGui::MenuItem("Flickering lights", nullptr, &Settings::Editor.ShowFlickeringLights))
                    if (!Settings::Editor.ShowFlickeringLights) Commands::DisableFlickeringLights();

                ImGui::MenuItem("Animation", nullptr, &Settings::Editor.ShowAnimation);
                ImGui::MenuItem("Matcen Effects", nullptr, &Settings::Editor.ShowMatcenEffects);
                ImGui::MenuItem("Terrain", nullptr, &Settings::Editor.ShowTerrain);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools")) {
                ImGui::MenuItem("Textures", nullptr, &Settings::Editor.Windows.Textures);
                ImGui::MenuItem("Texture Editor", nullptr, &Settings::Editor.Windows.TextureEditor);
                ImGui::MenuItem("Properties", nullptr, &Settings::Editor.Windows.Properties);
                ImGui::MenuItem("Reactor", nullptr, &Settings::Editor.Windows.Reactor);
                ImGui::MenuItem("Lighting", nullptr, &Settings::Editor.Windows.Lighting);
                ImGui::MenuItem("Diagnostics", nullptr, &Settings::Editor.Windows.Diagnostics);
                ImGui::MenuItem("Noise", nullptr, &Settings::Editor.Windows.Noise);
                ImGui::MenuItem("Sounds", nullptr, &Settings::Editor.Windows.Sound);
                ImGui::MenuItem("Tunnel Builder", nullptr, &Settings::Editor.Windows.TunnelBuilder);
                ImGui::MenuItem("Scale", nullptr, &Settings::Editor.Windows.Scale);
                ImGui::MenuItem("Material Editor", nullptr, &Settings::Editor.Windows.MaterialEditor);
                ImGui::MenuItem("Terrain", nullptr, &Settings::Editor.Windows.TerrainEditor);

#ifdef _DEBUG
                ImGui::MenuItem("Briefing Editor", nullptr, &Settings::Editor.Windows.BriefingEditor);
#endif

                ImGui::Separator();
                if (ImGui::MenuItem("Clean level"))
                    Commands::CleanLevel();

                if (ImGui::MenuItem("Fix object positions")) {
                    for (auto& obj : Game::Level.Objects)
                        FixObjectPosition(obj);

                    Editor::History.SnapshotLevel("Fix object positions");
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Bloom", nullptr, _bloomWindow.IsOpen()))
                    _bloomWindow.ToggleIsOpen();

#ifdef _DEBUG
                if (ImGui::MenuItem("Debug", nullptr, _debugWindow.IsOpen()))
                    _debugWindow.ToggleIsOpen();

                ImGui::Separator();
                if (ImGui::MenuItem("Enable Physics", nullptr, &Settings::Editor.EnablePhysics)) {
                    for (auto& obj : Game::Level.Objects) {
                        obj.PrevPosition = obj.Position;
                        obj.PrevRotation = obj.Rotation;
                    }
                }
                ImGui::MenuItem("Show ImGui Demo", nullptr, &_showImguiDemo);
#endif
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Play")) {
                if (ImGui::MenuItem("Play in Inferno"))
                    Game::SetState(GameState::Game);

                ImGui::Separator();

                if (ImGui::MenuItem("Create test mission"))
                    Commands::PlaytestLevel();
                if (ImGui::MenuItem("Launch game"))
                    Commands::StartGame();

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("User Guide"))
                    Events::ShowDialog(DialogType::Help);

                if (ImGui::MenuItem(fmt::format("About {}", APP_TITLE).c_str()))
                    Events::ShowDialog(DialogType::About);

                ImGui::EndMenu();
            }

            _mainMenuHeight = ImGui::GetWindowSize().y;

            ImGui::EndMainMenuBar();
        }
        ImGui::PopStyleVar();
    }

    // Toolbar positioned at the top of the center dock node.
    void DrawTopToolbar(const ImGuiDockNode& node) {
        ImGui::SetNextWindowPos({ node.Pos.x - 1, node.Pos.y });
        ImGui::SetNextWindowSize({ node.Size.x + 2, 0 });

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::Begin("TopToolbar", nullptr, ToolbarFlags);

        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Snap");
            ImGui::SameLine();

            ImGui::SetNextItemWidth(80 * Shell::DpiScale);
            auto snap = Settings::Editor.TranslationSnap;
            if (ImGui::InputFloat("##translation", &snap, 0, 0, "%.2f"))
                Settings::Editor.TranslationSnap = std::clamp(snap, 0.0f, 1000.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translation snapping");

            ImGui::SameLine();
            ImGui::GetCurrentWindow()->DC.CursorPos.x -= 8;

            ImGui::SetNextWindowSize({ 110 * Shell::DpiScale, 0 });
            if (ImGui::BeginCombo("##drp", nullptr, ImGuiComboFlags_NoPreview)) {
                static constexpr float snapValues[] = { 0, 20.0f / 64, 1, 2.5f, 5, 10, 20 };
                for (int i = 0; i < std::size(snapValues); i++) {
                    auto label = i == 1 ? "Pixel" : fmt::format("{:.1f}", snapValues[i]);
                    if (ImGui::Selectable(label.c_str()))
                        Settings::Editor.TranslationSnap = snapValues[i];
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextWindowSize({});
        }

        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80 * Shell::DpiScale);
            auto snap = Settings::Editor.RotationSnap * RadToDeg;

            if (ImGui::InputFloat("##rotation", &snap, 0, 0, (char*)u8"%.3f°"))
                Settings::Editor.RotationSnap = std::clamp(snap, 0.0f, 180.0f) * DegToRad;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotation snapping");

            ImGui::SameLine();
            ImGui::GetCurrentWindow()->DC.CursorPos.x -= 8;

            ImGui::SetNextWindowSize({ 110 * Shell::DpiScale, 0 });
            if (ImGui::BeginCombo("##rdrp", nullptr, ImGuiComboFlags_NoPreview)) {
                constexpr auto pi = (float)std::numbers::pi * RadToDeg;
                static constexpr float snapValues[] = { 0, pi / 32, pi / 24, pi / 16, pi / 12, pi / 8, pi / 6, pi / 4 };
                for (auto& value : snapValues) {
                    auto label = fmt::format(u8"{:.2f}°", value);
                    if (ImGui::Selectable((char*)label.c_str()))
                        Settings::Editor.RotationSnap = value * DegToRad;
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextWindowSize({});
        }

        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + 300 * Shell::DpiScale < node.Size.x) {
            ImGui::SeparatorVertical();
            ImGui::SameLine();
        }
        else {
            ImGui::Dummy({});
        }

        {
            static constexpr std::array insertModes = { "Normal", "Extrude", "Mirror" };

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Insert");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert mode for segments");

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110 * Shell::DpiScale);

            if (ImGui::BeginCombo("##insert", insertModes[(int)Settings::Editor.InsertMode])) {
                for (int i = 0; i < insertModes.size(); i++) {
                    const bool isSelected = (int)Settings::Editor.InsertMode == i;
                    auto itemLabel = std::to_string((int)i);
                    if (ImGui::Selectable(insertModes[i], isSelected)) {
                        Settings::Editor.InsertMode = (InsertMode)i;
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + 400 * Shell::DpiScale < node.Size.x) {
            ImGui::SeparatorVertical();
            ImGui::SameLine();
        }
        else {
            ImGui::Dummy({});
        }

        {
            // Selection settings
            ImGui::SameLine();

            ImGui::SetNextItemWidth(125 * Shell::DpiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));

            if (ImGui::BeginCombo("##selection-dropdown", "Selection")) {
                ImGui::PopStyleColor(2);
                ImGui::Text("Planar tolerance");

                // must use utf8 encoding to properly render glyphs
                auto tolerance = Settings::Editor.Selection.PlanarTolerance;
                auto label = fmt::format(u8"{:.0f}°", tolerance);
                ImGui::SetNextItemWidth(175 * Shell::DpiScale);

                if (ImGui::SliderFloat("##tolerance", &tolerance, 0, 90, (char*)label.c_str())) {
                    Settings::Editor.Selection.PlanarTolerance = std::clamp(tolerance, 0.0f, 90.0f);
                }

                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                //ImGui::Separator();

                ImGui::Text("Stop at");
                ImGui::Checkbox("Texture 1", &Settings::Editor.Selection.UseTMap1);
                ImGui::Checkbox("Texture 2", &Settings::Editor.Selection.UseTMap2);
                ImGui::Checkbox("Walls", &Settings::Editor.Selection.StopAtWalls);
                ImGui::EndPopup();
            }
            else {
                ImGui::PopStyleColor(2);
            }
        }

        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + 150 * Shell::DpiScale < node.Size.x) {
            ImGui::SeparatorVertical();
            ImGui::SameLine();
        }
        else {
            ImGui::Dummy({});
        }

        //{
        //    static const std::array uvAngles = { (char*)u8"0°", (char*)u8"90°", (char*)u8"180°", (char*)u8"270°" };
        //    ImGui::AlignTextToFramePadding();
        //    ImGui::Text("UV Angle");
        //    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extra angle to apply when resetting UVs");

        //    ImGui::SameLine();
        //    ImGui::SetNextItemWidth(100);

        //    if (ImGui::BeginCombo("##uvangle", uvAngles[Settings::Editor.ResetUVsAngle])) {
        //        for (int i = 0; i < uvAngles.size(); i++) {
        //            auto itemLabel = std::to_string((int)i);
        //            if (ImGui::Selectable(uvAngles[i], Settings::Editor.ResetUVsAngle == i)) {
        //                Settings::Editor.ResetUVsAngle = std::clamp(i, 0, 3);
        //            }

        //            if (Settings::Editor.ResetUVsAngle == i)
        //                ImGui::SetItemDefaultFocus();
        //        }

        //        ImGui::EndPopup();
        //    }
        //}


        //ImGui::SameLine();
        //if (ImGui::GetCursorPosX() + 500 < node.Size.x) {
        //    ImGui::SeparatorVertical();
        //    ImGui::SameLine();
        //}
        //else {
        //    ImGui::Dummy({});
        //}

        {
            // Coordinate system settings
            ImGui::SetNextItemWidth(150 * Shell::DpiScale);

            static constexpr std::array csysModes = { "Local", "Global", "User Defined (UCS)" };
            const ImVec2 csysBtnSize = { 150 * Shell::DpiScale, 0 };

            if (ImGui::BeginCombo("##csys-dropdown", csysModes[(int)Settings::Editor.CoordinateSystem], ImGuiComboFlags_HeightLarge)) {
                ImGui::Text("Coordinate system");
                ImGui::Dummy({ 200 * Shell::DpiScale, 0 });
                auto csys = Settings::Editor.CoordinateSystem;

                if (ImGui::RadioButton(csysModes[0], csys == CoordinateSystem::Local))
                    csys = CoordinateSystem::Local;

                if (ImGui::RadioButton(csysModes[1], csys == CoordinateSystem::Global))
                    csys = CoordinateSystem::Global;

                if (ImGui::RadioButton(csysModes[2], csys == CoordinateSystem::User))
                    csys = CoordinateSystem::User;

                // Average csys? uses geometric average of marked

                if (csys != Settings::Editor.CoordinateSystem) {
                    Settings::Editor.CoordinateSystem = csys;
                    Editor::Gizmo.UpdatePosition();
                }

                {
                    const float Indent = 35 * Shell::DpiScale;
                    ImGui::SetCursorPosX(Indent);
                    static SelectionMode previousMode{};
                    bool isEditing = Settings::Editor.SelectionMode == SelectionMode::Transform;
                    if (ImGui::Button(isEditing ? "Finish edit" : "Edit", csysBtnSize)) {
                        if (isEditing) {
                            SetMode(previousMode);
                        }
                        else {
                            previousMode = Settings::Editor.SelectionMode;
                            SetMode(SelectionMode::Transform);
                        }
                    }

                    ImGui::SetCursorPosX(Indent);
                    if (ImGui::Button("Align to gizmo", csysBtnSize))
                        AlignUserCSysToGizmo();

                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the user csys to the gizmo location");

                    ImGui::SetCursorPosX(Indent);
                    if (ImGui::Button("Align to side", csysBtnSize))
                        AlignUserCSysToSide();

                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Align the user csys to the selected side and edge");

                    ImGui::SetCursorPosX(Indent);
                    if (ImGui::Button("Move to marked", csysBtnSize))
                        AlignUserCSysToMarked();

                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the user csys to the center of the marked geometry");
                }

                ImGui::EndPopup();
            }
        }

        Editor::TopToolbarOffset = 32 * Shell::DpiScale + ImGui::GetCursorScreenPos().y;

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    bool BeginContextMenu() {
        if (Editor::Gizmo.State == GizmoState::EndDrag ||
            Input::GetMouseMode() != Input::MouseMode::Normal ||
            (Editor::Gizmo.State == GizmoState::RightClick && Settings::Editor.EnableTextureMode) || // Disable right click in texture mode
            Input::LeftDragState == Input::SelectionState::Dragging ||
            ImGui::GetTopMostPopupModal())
            return false;

        auto id = ImGui::GetID("context-menu");

        // Root dockspace window is pass-through and will return null
        if (ImGui::IsMouseReleased(1) && ImGui::GetCurrentContext()->HoveredWindow == nullptr)
            ImGui::OpenPopupEx(id);

        return ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    }

    void DrawContextMenu() {
        //ImGui::BeginPopupContextWindow();
        if (BeginContextMenu()) {
            auto mode = Settings::Editor.SelectionMode;

            ClipboardMenu();

            if (mode == SelectionMode::Segment)
                MenuCommand(Commands::PasteMirrored, EditorAction::PasteMirrored);

            ImGui::Separator();

            switch (mode) {
                case SelectionMode::Point:
                case SelectionMode::Edge:
                    MenuCommand(Commands::DetachPoints, EditorAction::DetachPoints);
                    ImGui::Separator();
                    break;

                case SelectionMode::Face:
                    MenuCommand(Commands::ConnectSides, EditorAction::ConnectSides);
                    MenuCommand(Commands::JoinSides, EditorAction::JoinSides);
                    MenuCommand(Commands::DetachSides, EditorAction::DetachSides);
                    ImGui::Separator();
                    break;

                case SelectionMode::Segment:
                    SplitMenu();
                    MenuCommand(Commands::MirrorSegments);
                    MenuCommand(Commands::DetachSegments, EditorAction::DetachSegments);
                    ImGui::Separator();
                    break;

                case SelectionMode::Object:
                    ObjectMenu();
                    ImGui::Separator();
                    break;
            }

            if (mode != SelectionMode::Object && mode != SelectionMode::Transform) {
                MenuCommand(Commands::ResetUVs, EditorAction::ResetUVs);
                MenuCommand(Commands::FitUVs, EditorAction::FitUVs);
                MenuCommand(Commands::AlignMarked, EditorAction::AlignMarked);
                MenuCommand(Commands::CopyUVsToFaces, EditorAction::CopyUVsToFaces);
                if (ImGui::MenuItem("Clear Overlay Texture"))
                    Events::SelectTexture(LevelTexID::None, LevelTexID::Unset);

                ImGui::Separator();
            }

            if (mode == SelectionMode::Face) {
                FaceEditMenu();
                ImGui::Separator();
            }

            InsertMenuItems();
            ImGui::Separator();
            MenuCommand(Commands::RemoveWall);

            ImGui::EndPopup();
        }
    }

    void DrawMainToolbar(const ImGuiViewport* node) {
        //statusPos.y += dock->CentralNode->Size.y;
        //_statusBar.Position = statusPos;
        //_statusBar.Width = dock->CentralNode->Size.x;
        float btnWidth = 80 * Shell::DpiScale;
        float width = btnWidth * 7;

        auto pos = node->Pos;
        pos.x += node->Size.x / 2 - width / 2;
        pos.y += node->Size.y - btnWidth - 60 * Shell::DpiScale; // status bar height?

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize({ 0, btnWidth });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.5, 0.5 });
        ImGui::PushStyleColor(ImGuiCol_WindowBg, TOOLBAR_COLOR);

        {
            ImGui::Begin("MainToolbar", nullptr, ToolbarFlags);

            ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 size(btnWidth - style.WindowPadding.x * 2, btnWidth - style.WindowPadding.x * 2);

            auto& mode = Settings::Editor.SelectionMode;
            if (ImGui::Selectable("Point", mode == SelectionMode::Point, 0, size))
                SetMode(SelectionMode::Point);

            ImGui::SameLine();
            if (ImGui::Selectable("Edge", mode == SelectionMode::Edge, 0, size))
                SetMode(SelectionMode::Edge);

            ImGui::SameLine();
            if (ImGui::Selectable("Face", mode == SelectionMode::Face, 0, size))
                SetMode(SelectionMode::Face);

            ImGui::SameLine();
            if (ImGui::Selectable("Seg", mode == SelectionMode::Segment, 0, size))
                SetMode(SelectionMode::Segment);

            ImGui::SameLine();
            if (ImGui::Selectable("Object", mode == SelectionMode::Object, 0, size))
                SetMode(SelectionMode::Object);

            ImGui::SeparatorVertical();

            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 1, 0, 0, 0.55f });
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{ 1, 0, 0, 0.65f }); // over
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{ 1, 0, 0, 0.1f }); // pressed
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{ 0.75f, 0, 0, 1 });

            // Toggle features
            ImGui::SameLine();
            if (ImGui::ToggleButton("Wall", Settings::Editor.EnableWallMode, 0, size, 3))
                ToggleWallMode();

            ImGui::SameLine(0, 10);
            if (ImGui::ToggleButton("Texture", Settings::Editor.EnableTextureMode, 0, size, 3))
                ToggleTextureMode();

            ImGui::PopStyleColor(4);
            ImGui::End();
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
    }

    ImGuiDockNode* EditorUI::CreateDockLayout(ImGuiID dockspaceId, const ImGuiViewport* viewport) const {
        auto dockspaceNode = ImGui::DockBuilderGetNode(dockspaceId);

        if (!dockspaceNode) {
            ImGui::DockBuilderRemoveNode(dockspaceId); // Clear out existing layout
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace); // Add empty node
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID dockMainId = dockspaceId;
            ImGuiID leftPanel = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, nullptr, &dockMainId);
            ImGuiID rightPanel = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.20f, nullptr, &dockMainId);
            //ImGuiID bottomPanel = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.20f, nullptr, &dock_main_id);
            //ImGuiID leftTopSplit, leftBottomSplit;

            //ImGui::DockBuilderSplitNode(leftPanel, ImGuiDir_Down, 0.5f, &leftBottomSplit, &leftTopSplit);

            //ImGuiID rightSplit;
            //ImGui::DockBuilderSplitNode(rightPanel, ImGuiDir_Down, 0.5f, &leftBottomSplit, &leftTopSplit);
            ImGui::DockBuilderDockWindow(_textureBrowser.Name(), leftPanel);
            ImGui::DockBuilderDockWindow(_propertyEditor.Name(), rightPanel);
            ImGui::DockBuilderFinish(dockspaceId);
            return ImGui::DockBuilderGetNode(dockspaceId);
        }
        else {
            return dockspaceNode;
        }
    }

    void EditorUI::DrawDockspace(const ImGuiViewport* viewport) const {
        float toolbarWidth = 0; // = ToolbarWidth;
        ImGui::SetNextWindowPos({ toolbarWidth, 0 });
        ImGui::SetNextWindowSize({ viewport->WorkSize.x - toolbarWidth, viewport->WorkSize.y + _mainMenuHeight - _statusBar.Height });

        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
        ImGui::Begin("DockSpace", nullptr, MainWindowFlags);
        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);

        ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
        auto* dock = CreateDockLayout(dockspaceId, viewport);

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspace_flags);
        //viewport->WorkOffsetMin = { viewport->WorkOffsetMin.x + ToolbarWidth, viewport->WorkOffsetMin.y + TopToolbarHeight };

        //DrawLogWindow();
        DrawContextMenu();

        ImGui::End();

        DrawTopToolbar(*dock->CentralNode);
        Editor::MainViewportWidth = dock->CentralNode->Size.x;
        Editor::MainViewportXOffset = dock->CentralNode->Pos.x;

        if (Game::ShowDebugOverlay) {
            auto pos = ImVec2(dock->CentralNode->Pos.x + dock->CentralNode->Size.x, dock->CentralNode->Pos.y + 40);
            ImGui::SetNextWindowViewport(dock->CentralNode->ID);
            DrawDebugOverlay(pos, { 1, 0 });
        }
        //DrawSelectionToolbar({ viewport->Pos.x + viewportSize.x - 100, dock->CentralNode->Pos.y });
    }

    void DrawGizmoTooltip() {
        ImGui::SetNextWindowPos({ Input::MousePosition.x + 25, Input::MousePosition.y - 15 });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0, 0, 0, 0.7f });
        ImGui::Begin("GizmoStatus", nullptr, ToolbarFlags);
        if (Editor::Gizmo.Mode == TransformMode::Rotation)
            ImGui::Text("%.1f deg", Editor::Gizmo.TotalDelta * RadToDeg);
        else
            ImGui::Text("%.1f units", Editor::Gizmo.TotalDelta);
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(1);
    }

    void DrawSelectionBox() {
        ImVec2 p0 = { Input::DragStart.x, Input::DragStart.y };
        ImVec2 p1 = { Input::MousePosition.x, Input::MousePosition.y };
        ImGui::GetBackgroundDrawList()->AddRect(p0, p1, IM_COL32(0, 255, 0, 255), 0, 0, 2);
    }

    void EditorUI::OnRender() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        //auto viewportSize = viewport->GetWorkSize();

        DrawMenu();
        //DrawLeftToolbar(viewportSize);
        DrawDockspace(viewport);

        //auto statusPos = dock->CentralNode->Pos;
        //statusPos.y += dock->CentralNode->Size.y;
        _statusBar.Position = { 0, viewport->Size.y - _statusBar.Height };
        _statusBar.Width = viewport->Size.x;
        _statusBar.Update();

        DrawMainToolbar(viewport);

        for (auto& dialog : _dialogs | views::values)
            dialog->Update();

        for (auto& window : _windows)
            window->Update();

        if (Editor::Gizmo.State == GizmoState::Dragging) {
            DrawGizmoTooltip();
        }
        else if (Input::LeftDragState == Input::SelectionState::Dragging &&
            !ImGui::GetIO().WantCaptureMouse) {
            DrawSelectionBox();
        }

        if (_showImguiDemo) ImGui::ShowDemoWindow();
    }
}
