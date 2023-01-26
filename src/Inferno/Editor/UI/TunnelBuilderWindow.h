#pragma once

#include "WindowBase.h"
#include "../TunnelBuilder.h"

namespace Inferno::Editor {
    class TunnelBuilderWindow final : public WindowBase {
        TunnelArgs _args;

    public:
        TunnelBuilderWindow() : WindowBase("Tunnel Builder", &Settings::Editor.Windows.TunnelBuilder) {
            Events::LevelChanged += [this] { if (IsOpen()) RefreshTunnel(); };
        }

    protected:
        void OnUpdate() override {
            if (ImGui::Button("Pick Start", { 100, 0 })) {
                _args.Start.Tag = Editor::Selection.PointTag();
                UpdateInitialLengths();
                RefreshTunnel();
            }

            ImGui::SameLine();
            if (!_args.Start.Tag)
                ImGui::Text("None");
            else
                ImGui::Text("%i:%i:%i", _args.Start.Tag.Segment, _args.Start.Tag.Side, _args.Start.Tag.Point);

            if (ImGui::Button("Rotate##Start", { 100, 0 }) && _args.Start.Tag) {
                _args.Start.Tag.Point = (_args.Start.Tag.Point + 1) % 4;
                RefreshTunnel();
            }

            if (ImGui::DragFloat("Length##Start", &_args.Start.Length, 1.0f, TunnelHandle::MIN_LENGTH, TunnelHandle::MAX_LENGTH, "%.1f")) {
                _args.ClampInputs();
                RefreshTunnel();
            }

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::Button("Pick End", { 100, 0 })) {
                _args.End.Tag = Editor::Selection.PointTag();
                UpdateInitialLengths();
                RefreshTunnel();
            }

            ImGui::SameLine();
            if (!_args.End.Tag)
                ImGui::Text("None");
            else
                ImGui::Text("%i:%i:%i", _args.End.Tag.Segment, _args.End.Tag.Side, _args.End.Tag.Point);

            if (ImGui::Button("Rotate##End", { 100, 0 }) && _args.End.Tag) {
                _args.End.Tag.Point = (_args.End.Tag.Point + 1) % 4;
                RefreshTunnel();
            }

            if (ImGui::DragFloat("Length##End", &_args.End.Length, 1.0f, TunnelHandle::MIN_LENGTH, TunnelHandle::MAX_LENGTH, "%.1f")) {
                _args.ClampInputs();
                RefreshTunnel();
            }

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::InputInt("Steps", &_args.Steps, TunnelArgs::MIN_STEPS, TunnelArgs::MAX_STEPS)) {
                _args.ClampInputs();
                RefreshTunnel();
            }

            if (ImGui::Checkbox("Twist", &_args.Twist))
                RefreshTunnel();

            if (ImGui::Button("Swap Ends", { 100, 0 })) {
                std::swap(_args.Start, _args.End);
                RefreshTunnel();
            }
            ImGui::HelpMarker("Sometimes the solver does not exactly match the tunnel end.\nSwapping ends might fix this.");

            ImGui::Dummy({ 0, 20 });

            if (ImGui::Button("Reset", { 100, 0 }))
                Reset();
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - 100 - 20);
                DisableControls disable(!_args.IsValid());
                if (ImGui::Button("Generate", { 100, 0 }))
                    GenerateTunnel();
            }

        }

        void RefreshTunnel() {
            PreviewTunnel = CreateTunnel(Game::Level, _args);
            PreviewTunnelStart = _args.Start;
            PreviewTunnelEnd = _args.End;
        }

        void GenerateTunnel() {
            CreateTunnelSegments(Game::Level, _args);
            PreviewTunnel = {};
        }

        void UpdateInitialLengths() {
            if (!Game::Level.SegmentExists(_args.Start.Tag) || !Game::Level.SegmentExists(_args.End.Tag))
                return;

            auto start = Face::FromSide(Game::Level, _args.Start.Tag);
            auto end = Face::FromSide(Game::Level, _args.End.Tag);

            // calculate the initial length of each end of the bezier curve
            _args.Start.Length = _args.End.Length = (end.Center() - start.Center()).Length() * 0.5f;

            // Estimate the number of segments based on the length
            BezierCurve curve;
            curve.Points[0] = start.Center();
            curve.Points[1] = start.Center() + start.AverageNormal() * _args.Start.Length;
            curve.Points[2] = end.Center() - end.AverageNormal() * _args.End.Length;
            curve.Points[3] = end.Center();
            _args.Steps = int(curve.EstimateLength(20) / 20);

            _args.ClampInputs();
        }

        void Reset() {
            _args = {};
            RefreshTunnel();
        }
    };
}
