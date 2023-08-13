#include "pch.h"
#include "DebugWindow.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Debug.h"
#include "Input.h"
#include "../Editor.h"
#include "Physics.h"
#include "Procedural.h"
#include "Editor/Gizmo.h"
#include "Game.h"

namespace Inferno {
    Tag GetNextConnection(span<SegID> _path, Level& level, SegID segId);
}

namespace Inferno::Editor {
    DebugWindow::DebugWindow(): WindowBase("Debug", &Settings::Editor.Windows.Debug) {}

    void DebugWindow::OnUpdate() {
        ImGui::Checkbox("Disable weapon damage", &Settings::Cheats.DisableWeaponDamage);
        ImGui::Checkbox("Disable AI", &Settings::Cheats.DisableAI);
        ImGui::Combo("Difficulty", &Game::Difficulty, "Trainee\0Rookie\0Hotshot\0Ace\0Insane");
        ImGui::Checkbox("No wall collision", &Settings::Cheats.DisableWallCollision);
        ImGui::Checkbox("Generate spec and normal maps", &Settings::Inferno.GenerateMaps);
        ImGui::Checkbox("Load D3 data", &Settings::Inferno.Descent3Enhanced);
        ImGui::Checkbox("Draw Portals", &Settings::Editor.ShowPortals);
        if (ImGui::Checkbox("Procedural Textures", &Settings::Graphics.EnableProcedurals)) {
            EnableProceduralTextures(Settings::Graphics.EnableProcedurals);
        }

        ImGui::Combo("Filtering", (int*)&Settings::Graphics.FilterMode, "Point\0Enhanced point\0Smooth");

        ImGui::Separator();

        if (ImGui::Button("Set path target")) {
            if (auto obj = Game::Level.TryGetObject(Editor::Selection.Object)) {
                auto path = Game::Navigation.NavigateTo(obj->Segment, Editor::Selection.Segment, Game::Level);

                if (obj->IsRobot()) {
                    auto& seg = Game::Level.GetSegment(Editor::Selection.Segment);
                    obj->NextThinkTime = 0;

                    /*auto& ai = GetAI(obj);
                    ai.GoalSegment = Editor::Selection.Segment;
                    ai.GoalPosition = seg.Center;
                    ai.GoalRoom = Game::Level.FindRoomBySegment(Editor::Selection.Segment);
                    obj->GoalPath = path;
                    obj->GoalPathIndex = 0;
                    obj->Room = Game::Level.FindRoomBySegment(obj->Segment);*/

                    //auto tag = GetNextConnection(path, Game::Level, obj->Segment);
                    //auto& side = Game::Level.GetSide(tag);
                    //auto delta = (side.Center - obj->Position) / 4;

                    //ai.RoomCurve.Points = {
                    //    obj->Position,
                    //    obj->Position + delta,
                    //    obj->Position + delta * 2,
                    //    obj->Position + delta * 3
                    //};
                    //ai.CurveProgress = 1;
                }

                Debug::NavigationPath.clear();

                for (auto& node : path) {
                    auto& seg = Game::Level.GetSegment(node);
                    Debug::NavigationPath.push_back(seg.Center);
                }
            }
        }

        if (ImGui::Button("Update rooms")) {
            Game::Level.Rooms = Game::CreateRooms(Game::Level);
        }

        if (ImGui::Button("Mark room")) {
            if (auto room = Game::Level.GetRoom(Editor::Selection.Segment)) {
                Editor::Marked.Segments.clear();
                Seq::insert(Editor::Marked.Segments, room->Segments);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Mark connected room")) {
            if (auto room = Game::Level.GetConnectedRoom(Editor::Selection.Tag())) {
                Editor::Marked.Segments.clear();
                Seq::insert(Editor::Marked.Segments, room->Segments);
            }
        }

        ImGui::Separator();

        _timeCounter += Render::FrameTime;

        if (_timeCounter > 0.5f) {
            _frameTime = Render::FrameTime;
            _timeCounter = 0;
        }

        ImGui::Text("Frame Time: %.2f ms FPS: %.0f Calls %d", _frameTime * 1000, 1 / _frameTime, Render::Stats::DrawCalls);
        ImGui::Text("Procedural time: %.2f ms", Debug::ProceduralUpdateRate * 1000);

        //ImGui::Text("Light View pos: %.2f, %.2f, %.2f", Debug::LightPosition.x, Debug::LightPosition.y, Debug::LightPosition.z);
        //ImGui::Text("Inside frustum: %i", Debug::InsideFrustum);

        ImGui::Text("Ship pos: %.2f, %.2f, %.2f", Debug::ShipPosition.x, Debug::ShipPosition.y, Debug::ShipPosition.z);
        ImGui::Text("Ship vel: %.2f, %.2f, %.2f", Debug::ShipVelocity.x, Debug::ShipVelocity.y, Debug::ShipVelocity.z);
        //ImGui::Text("Ship accel: %.2f, %.2f, %.2f", Debug::ShipAcceleration.x, Debug::ShipAcceleration.y, Debug::ShipAcceleration.z);
        ImGui::Text("Ship thrust: %.3f, %.3f, %.3f", Debug::ShipThrust.x, Debug::ShipThrust.y, Debug::ShipThrust.z);
        ImGui::Text("steps: %.2f  R: %.4f  K: %.2f", Debug::Steps, Debug::R, Debug::K);

        ImGui::PlotLines("##vel", Debug::ShipVelocities.data(), (int)Debug::ShipVelocities.size(), 0, nullptr, 0, 60, ImVec2(0, 120.0f));


        //ImGui::Text("Present Total: %.2f", Render::Metrics::Present / 1000.0f);
        //ImGui::Text("Present(): %.2f", Render::Metrics::PresentCall / 1000.0f);
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
}
