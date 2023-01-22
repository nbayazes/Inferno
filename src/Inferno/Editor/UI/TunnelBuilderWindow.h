#pragma once

#include "WindowBase.h"
#include "../TunnelBuilder.h"

namespace Inferno::Editor {
    class TunnelBuilderWindow : public WindowBase {
        TunnelParams _params;

    public:
        TunnelBuilderWindow() : WindowBase("Tunnel Builder", &Settings::Editor.Windows.TunnelBuilder) {
            Events::LevelChanged += [this] { if (IsOpen()) RefreshTunnel(); };

            _params.Start = { (SegID)6, (SideID)5, 1 };
            _params.End = { (SegID)5, (SideID)0, 1 };
        }

    protected:
        void OnUpdate() override {
            if (ImGui::Button("Pick Start", { 100, 0 })) {
                _params.Start = Editor::Selection.PointTag();
                UpdateInitialLengths();
                RefreshTunnel();
            }

            ImGui::SameLine();
            ImGui::Text("%i:%i:%i", _params.Start.Segment, _params.Start.Side, _params.Start.Point);

            if (ImGui::Button("Rotate##Start", { 100, 0 }) && _params.Start) {
                _params.Start.Point = (_params.Start.Point + 1) % 4;
                RefreshTunnel();
            }

            if (ImGui::DragFloat("Length##Start", &_params.StartLength, 0.1f, 10, 200, "%.1f")) {
                _params.ClampInputs();
                RefreshTunnel();
            }

            //if (ImGui::InputFloat("Length##Start", &_startLength, 1, 10, "%.0f")) {
            //    ClampLengths();
            //    RefreshTunnel();
            //}

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::Button("Pick End", { 100, 0 })) {
                _params.End = Editor::Selection.PointTag();
                UpdateInitialLengths();
                RefreshTunnel();
            }

            ImGui::SameLine();
            ImGui::Text("%i:%i:%i", _params.End.Segment, _params.End.Side, _params.End.Point);

            if (ImGui::Button("Rotate##End", { 100, 0 }) && _params.End) {
                _params.End.Point = (_params.End.Point + 1) % 4;
                RefreshTunnel();
            }

            if (ImGui::DragFloat("Length##End", &_params.EndLength, 0.1f, 10, 200, "%.1f")) {
                //if (ImGui::InputFloat("Length##End", &_endLength, 1, 10, "%.0f")) {
                _params.ClampInputs();
                RefreshTunnel();
            }

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::InputInt("Steps", &_params.Steps, 1, 10)) {
                _params.ClampInputs();
                RefreshTunnel();
            }

            if (ImGui::Checkbox("Twist", &EnableTunnelTwist))
                RefreshTunnel();

            if (ImGui::Button("Generate", { 100, 0 })) {
                GenerateTunnel();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear", { 100, 0 })) {
                ClearTunnel();
            }
        }

        void RefreshTunnel() {
            CreateTunnel(Game::Level, _params);
        }

        void GenerateTunnel() {
            CreateTunnelSegments(Game::Level, DebugTunnel, _params.Start, _params.End);
        }

        void UpdateInitialLengths() {
            if (!Game::Level.SegmentExists(_params.Start) || !Game::Level.SegmentExists(_params.End)) 
                return;

            auto start = Face::FromSide(Game::Level, _params.Start);
            auto end = Face::FromSide(Game::Level, _params.End);

            // calculate the initial length of each end of the bezier curve
            _params.StartLength = _params.EndLength = (end.Center() - start.Center()).Length() * 0.5f;
            _params.ClampInputs();
        }
    };
}
