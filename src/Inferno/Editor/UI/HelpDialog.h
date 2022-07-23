#pragma once

#include "WindowBase.h"
#include "../Editor.h"

namespace Inferno::Editor {
    class HelpDialog : public ModalWindowBase {
    public:
        HelpDialog() : ModalWindowBase("Help") {
            Width = 900;
        }

    protected:
        void OnUpdate() override {
            ImGui::Text("Basic Usage");
            ImGui::BulletText("Use WASD for camera movement. Q and E are slide down and up. Shift Q E for roll.");
            ImGui::BulletText("Press Z to enter mouselook mode. Can make selections while in this mode.");
            ImGui::BulletText("The gizmo is the primary way to modify objects. It is the circle with three arrows coming.\nThe circle rotate, the arrows translate and the blocks scale.");
            ImGui::BulletText("The editor has 5 different edit modes. Point, Edge, Face, Segment and Object.\nFunctionality changes based on the mode. Use 1-5 keys to switch mode.");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Selection");
            ImGui::BulletText("Selection refers to the selected segment and side");
            ImGui::BulletText("Marks refer to multiple points, faces or segments");
            ImGui::BulletText("Left click to change the selection");
            ImGui::BulletText("Hold Alt to click invisible segment faces");
            ImGui::BulletText("Escape key to unmark everything");
            ImGui::BulletText("Ctrl+click to mark points, faces, segments and objects");
            ImGui::BulletText("Ctrl+shift+click to mark connected surfaces or segments");
            
            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Extrude and Duplicate");
            ImGui::BulletText("Right click dragging the gizmo will extrude in face mode");
            ImGui::BulletText("Right click dragging the gizmo will create copies in object and segment mode.");
            ImGui::BulletText("Multiple faces can be extruded at once");
            ImGui::BulletText("Use this feature to quickly place objects in the level");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Texturing");
            ImGui::BulletText("Enable texture mode on the main toolbar");
            ImGui::BulletText("The gizmo will affect point/edge/face UVs");
            ImGui::BulletText("Right click the X or Y axis to mirror the texture");
            ImGui::BulletText("Right click the rotation circle to rotate overlay");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Copy and Paste");
            ImGui::BulletText("Segments use the selected side and edges when copying and pasting to orient the new segments");
            ImGui::BulletText("Segments copy all walls and objects contained within them");
            ImGui::BulletText("Segments can be mirrored when pasted");
            ImGui::Dummy({ 0, 5 * Shell::DpiScale });
            ImGui::BulletText("Only the selected object will be copy and pasted. Use right click drag to duplicate multiple.");
            ImGui::BulletText("In face mode the selected side textures will be copied and pasted to marked faces");
        }

    };
}