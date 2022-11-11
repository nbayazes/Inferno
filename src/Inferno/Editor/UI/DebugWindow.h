#pragma once

#include "WindowBase.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Debug.h"
#include "Input.h"
#include "../Editor.h"
#include "Physics.h"

namespace Inferno::Editor {
    class DebugWindow : public WindowBase {
        float _frameTime = 0, _timeCounter = 1;
    public:
        DebugWindow() : WindowBase("Debug") { IsOpen(false); }
    protected:
        void OnUpdate() override {
            _timeCounter += (float)Render::FrameTime;

            if (_timeCounter > 0.5f) {
                _frameTime = (float)Render::FrameTime;
                _timeCounter = 0;
            }

            ImGui::Text("Ship pos: %.2f, %.2f, %.2f", Debug::ShipPosition.x, Debug::ShipPosition.y, Debug::ShipPosition.z);
            ImGui::Text("Ship vel: %.2f, %.2f, %.2f", Debug::ShipVelocity.x, Debug::ShipVelocity.y, Debug::ShipVelocity.z);
            ImGui::Text("Ship accel: %.2f, %.2f, %.2f", Debug::ShipAcceleration.x, Debug::ShipAcceleration.y, Debug::ShipAcceleration.z);
            //ImGui::Text("Ship thrust: %.3f, %.3f, %.3f", Debug::ShipThrust.x, Debug::ShipThrust.y, Debug::ShipThrust.z);
            ImGui::Text("steps: %.2f  R: %.4f  K: %.2f", Debug::Steps, Debug::R, Debug::K);

            ImGui::PlotLines("##vel", Debug::ShipVelocities.data(), (int)Debug::ShipVelocities.size(), 0, nullptr, 0, 60, ImVec2(0, 120.0f));

            ImGui::Text("Frame Time: %.2f ms FPS: %.0f Calls %d", _frameTime * 1000, 1 / _frameTime, Render::Stats::DrawCalls);

            ImGui::Text("Present Total: %.2f", Render::Metrics::Present / 1000.0f);
            ImGui::Text("Present(): %.2f", Render::Metrics::PresentCall / 1000.0f);
            ImGui::Text("Execute Render Cmds: %.2f", Render::Metrics::ExecuteRenderCommands / 1000.0f);

            ImGui::Text("Debug: %.2f", Render::Metrics::Debug / 1000.0f);
            //ImGui::Text("Find nearest light: %.2f", Render::Metrics::FindNearestLight / 1000.0f);
            ImGui::Text("QueueLevel: %.2f", Render::Metrics::QueueLevel / 1000.0f);
            ImGui::Text("ImGui: %.2f", Render::Metrics::ImGui / 1000.0f);

            ImGuiIO& io = ImGui::GetIO();
            //ImGui::Text("Capture - Mouse: %d Keyboard: %d", io.WantCaptureMouse, io.WantCaptureKeyboard);
            ImGui::Text("Mouse (Screen Space): %.0f, %.0f", io.MousePos.x, io.MousePos.y);

            ImGui::Text("Shift: %i Ctrl: %i Alt: %i", Input::ShiftDown, Input::ControlDown, Input::AltDown);

            ImGui::Text("LMB: %i RMB %i Drag: %i Gizmo Drag: %i", Input::LeftDragState, Input::RightDragState, Editor::DragMode, Editor::Gizmo.State);

            //auto ray = Render::Camera.UnprojectRay({ io.MousePos.x, io.MousePos.y });
            /*ImGui::Text("Ray pos: %.2f, %.2f, %.2f", ray.position.x, ray.position.y, ray.position.z);
            ImGui::Text("Ray dir: %.2f, %.2f, %.2f", ray.direction.x, ray.direction.y, ray.direction.z);*/

            /* auto& beginDrag = Editor::Selection.BeginDrag;
                ImGui::Text("Begin drag: %.2f, %.2f, %.2f", beginDrag.x, beginDrag.y, beginDrag.z);

                auto& endDrag = Editor::Selection.EndDrag;
                ImGui::Text("End drag: %.2f, %.2f, %.2f", endDrag.x, endDrag.y, endDrag.z);

                auto& dragVec = Editor::Selection.DragVector;
                auto& mag = Editor::Selection.DragMagnitude;
                ImGui::Text("Drag: %.2f, %.2f, %.2f", dragVec.x, dragVec.y, dragVec.z);
                ImGui::Text("Drag mag: %.2f, %.2f, %.2f", mag.x, mag.y, mag.z);*/

            for (auto& hit : Editor::Selection.Hits)
                ImGui::Text("Hit seg %d:%d normal: %.2f, %.2f, %.2f", hit.Tag.Segment, hit.Tag.Side, hit.Normal.x, hit.Normal.y, hit.Normal.z);

            if (Editor::Selection.Segment != SegID::None)
                ImGui::Text("Selection %d:%d P: %d", Editor::Selection.Segment, Editor::Selection.Side, Editor::Selection.Point);


            auto& hit = Editor::DebugNearestHit;
            ImGui::Text("Nearest hit: %.2f, %.2f, %.2f", hit.x, hit.y, hit.z);
            ImGui::Text("Nearest dist: %.2f", Editor::DebugHitDistance);
            ImGui::Text("Drag angle: %.2f", Editor::DebugAngle);
        }
    };
}