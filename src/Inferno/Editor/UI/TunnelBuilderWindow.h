#pragma once

#include "WindowBase.h"
#include "Graphics/Render.h"
#include "../TunnelBuilder.h"

namespace Inferno::Editor {
    class TunnelBuilderWindow : public WindowBase {
        PointTag _start = { (SegID)6, (SideID)5, 0 }, _end = { (SegID)5, (SideID)0, 0 };
        int _steps = 10;
        float _startLength = 20, _endLength = 20;
    public:
        TunnelBuilderWindow() : WindowBase("Tunnel Builder", &Settings::Editor.Windows.TunnelBuilder) {
            Events::LevelChanged += [this] { if (IsOpen()) RefreshTunnel(); };
        }

    protected:
        void OnUpdate() override {
            if (ImGui::Button("Pick Start", { 100, 0 })) {
                _start = Editor::Selection.PointTag();
                UpdateInitialLengths();
                RefreshTunnel();
            }

            ImGui::SameLine();
            ImGui::Text("%i:%i:%i", _start.Segment, _start.Side, _start.Point);

            if (ImGui::Button("Rotate##Start", { 100, 0 }) && _start) {
                _start.Point = (_start.Point + 1) % 4;
                RefreshTunnel();
            }

            if (ImGui::DragFloat("Length##Start", &_startLength, 0.1f, 10, 200, "%.1f")) {
                ClampLengths();
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
                _end = Editor::Selection.PointTag();
                UpdateInitialLengths();
                RefreshTunnel();
            }

            ImGui::SameLine();
            ImGui::Text("%i:%i:%i", _end.Segment, _end.Side, _end.Point);

            if (ImGui::Button("Rotate##End", { 100, 0 }) && _start) {
                _end.Point = (_end.Point + 1) % 4;
                RefreshTunnel();
            }

            if (ImGui::DragFloat("Length##End", &_endLength, 0.1f, 10, 200, "%.1f")) {
                //if (ImGui::InputFloat("Length##End", &_endLength, 1, 10, "%.0f")) {
                ClampLengths();
                RefreshTunnel();
            }

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::InputInt("Steps", &_steps, 1, 10)) {
                _steps = std::clamp(_steps, 1, 50);
                RefreshTunnel();
            }
        }

        void RefreshTunnel() {
            CreateTunnel(Game::Level, _start, _end, _steps, _startLength, _endLength);
        }

        void UpdateInitialLengths() {
            if (!Game::Level.SegmentExists(_start) || !Game::Level.SegmentExists(_end)) return;
            auto start = Face::FromSide(Game::Level, _start);
            auto end = Face::FromSide(Game::Level, _end);

            // calculate the initial length of each end of the bezier curve
            _startLength = _endLength = (end.Center() - start.Center()).Length() * 0.5f;
            ClampLengths();
        }

        void ClampLengths() {
            _endLength = std::clamp(_endLength, MinTunnelLength, MaxTunnelLength);
            _startLength = std::clamp(_startLength, MinTunnelLength, MaxTunnelLength);
        }
    };
}