#pragma once

#include "WindowBase.h"
#include "../TunnelBuilder.h"

namespace Inferno::Editor {
    class TunnelBuilderWindow final : public WindowBase {
    public:
        TunnelBuilderWindow() : WindowBase("Tunnel Builder", &Settings::Editor.Windows.TunnelBuilder) {
            Events::LevelChanged += [this] { if (IsOpen()) UpdateTunnelPreview(); };
        }

    protected:
        void OnUpdate() override {
            auto& args = TunnelBuilderArgs;

            if (ImGui::Button("Pick Start", { 100, 0 })) {
                args.Start.Tag = Editor::Selection.PointTag();
                UpdateInitialLengths();
                UpdateTunnelPreview();
            }

            ImGui::SameLine();
            if (!args.Start.Tag)
                ImGui::Text("None");
            else
                ImGui::Text("%i:%i:%i", args.Start.Tag.Segment, args.Start.Tag.Side, args.Start.Tag.Point);

            if (ImGui::Button("Rotate##Start", { 100, 0 }) && args.Start.Tag) {
                args.Start.Tag.Point = (args.Start.Tag.Point + 1) % 4;
                UpdateTunnelPreview();
            }

            if (ImGui::DragFloat("Length##Start", &args.Start.Length, 1.0f, TunnelHandle::MIN_LENGTH, TunnelHandle::MAX_LENGTH, "%.1f")) {
                args.ClampInputs();
                UpdateTunnelPreview();
            }

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::Button("Pick End", { 100, 0 })) {
                args.End.Tag = Editor::Selection.PointTag();
                UpdateInitialLengths();
                UpdateTunnelPreview();
            }

            ImGui::SameLine();
            if (!args.End.Tag)
                ImGui::Text("None");
            else
                ImGui::Text("%i:%i:%i", args.End.Tag.Segment, args.End.Tag.Side, args.End.Tag.Point);

            if (ImGui::Button("Rotate##End", { 100, 0 }) && args.End.Tag) {
                args.End.Tag.Point = (args.End.Tag.Point + 1) % 4;
                UpdateTunnelPreview();
            }

            if (ImGui::DragFloat("Length##End", &args.End.Length, 1.0f, TunnelHandle::MIN_LENGTH, TunnelHandle::MAX_LENGTH, "%.1f")) {
                args.ClampInputs();
                UpdateTunnelPreview();
            }

            ImGui::Dummy({ 0, 5 });
            ImGui::Separator();
            ImGui::Dummy({ 0, 5 });

            if (ImGui::InputInt("Steps", &args.Steps, 1, 5)) {
                args.ClampInputs();
                UpdateTunnelPreview();
            }

            if (ImGui::Checkbox("Twist", &args.Twist))
                UpdateTunnelPreview();

            if (ImGui::Button("Swap Ends", { 100, 0 })) {
                std::swap(args.Start, args.End);
                UpdateTunnelPreview();
            }
            ImGui::HelpMarker("Sometimes the solver does not exactly match the tunnel end.\nSwapping ends might fix this.");

            ImGui::Dummy({ 0, 20 });

            if (ImGui::Button("Reset", { 100, 0 }))
                Reset();
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - 100 - 20);
                DisableControls disable(!args.IsValid());
                if (ImGui::Button("Generate", { 100, 0 }))
                    GenerateTunnel();
            }
        }

        static void GenerateTunnel() {
            CreateTunnelSegments(Game::Level, TunnelBuilderArgs);
            PreviewTunnel = {};
        }

        static void UpdateInitialLengths() {
            auto& args = TunnelBuilderArgs;
            if (!Game::Level.SegmentExists(args.Start.Tag) || !Game::Level.SegmentExists(args.End.Tag))
                return;

            auto start = Face::FromSide(Game::Level, args.Start.Tag);
            auto end = Face::FromSide(Game::Level, args.End.Tag);

            // calculate the initial length of each end of the bezier curve
            args.Start.Length = args.End.Length = (end.Center() - start.Center()).Length() * 0.5f;

            // Estimate the number of segments based on the length
            BezierCurve curve;
            curve.Points[0] = start.Center();
            curve.Points[1] = start.Center() + start.AverageNormal() * args.Start.Length;
            curve.Points[2] = end.Center() - end.AverageNormal() * args.End.Length;
            curve.Points[3] = end.Center();
            args.Steps = int(curve.EstimateLength(20) / 20);

            args.ClampInputs();
        }

        void Reset() {
            TunnelBuilderArgs = {};
            UpdateTunnelPreview();
        }
    };
}
