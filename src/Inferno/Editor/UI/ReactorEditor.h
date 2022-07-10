#pragma once

#include "WindowBase.h"

namespace Inferno::Editor {

    class ReactorEditor : public WindowBase {
    public:
        ReactorEditor() : WindowBase("Reactor", &Settings::Windows.Reactor) { }
    protected:
        void OnUpdate() override {
            constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("reactor", 2, flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                bool defaultStrength = Game::Level.ReactorStrength == -1;
                auto strengthDesc = "Default strength is 200 + 50 per level.\nSecret levels are 200 + 150.";
                ImGui::TableRowLabelEx("Default strength", strengthDesc);
                if (ImGui::Checkbox("##defaultstrength", &defaultStrength)) {
                    Game::Level.ReactorStrength = defaultStrength ? -1 : 200;
                }

                {
                    DisableControls disable(defaultStrength);
                    ImGui::TableRowLabel("Strength");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputInt("##Strength", &Game::Level.ReactorStrength, 10)) {
                        if (Game::Level.ReactorStrength <= 0)
                            Game::Level.ReactorStrength = 1;
                    }
                }

                auto countdownDesc = "Insane: 1x\nAce: 1.5x\nHotshot: 2x\nRookie: 2.5x\nTrainee: 3x";
                ImGui::TableRowLabelEx("Countdown", countdownDesc);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputInt("##Countdown", &Game::Level.BaseReactorCountdown, 5)) {
                    if (Game::Level.BaseReactorCountdown <= 0)
                        Game::Level.BaseReactorCountdown = 1;
                }

                ReactorTriggers();

                ImGui::EndTable();
            }
        }

    private:
        void ReactorTriggers() {
            ImGui::TableRowLabelEx("Targets to open\nwhen destroyed", "Only doors or destroyable walls are valid targets");
            
            ImGui::BeginChild("##cctriggers", { -1, 200 }, true);

            static int selection = 0;
            for (int i = 0; i < Game::Level.ReactorTriggers.Count(); i++) {
                auto& target = Game::Level.ReactorTriggers[i];

                string targetLabel = fmt::format("{}:{}", target.Segment, target.Side);
                if (ImGui::Selectable(targetLabel.c_str(), selection == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selection = i;
                    if (ImGui::IsMouseDoubleClicked(0))
                        Editor::Selection.SetSelection(target);
                }
            }

            ImGui::EndChild();

            if (ImGui::Button("Add##ReactorTriggerTarget", { 100, 0 })) {
                if (Editor::Marked.Faces.empty())
                    ShowWarningMessage(L"Please mark faces to add as targets.");

                for (auto& mark : Editor::Marked.Faces)
                    Game::Level.ReactorTriggers.Add(mark);

                Editor::History.SnapshotLevel("Add reactor trigger");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete##ReactorTriggerTarget", { 100, 0 })) {
                if (Game::Level.ReactorTriggers.Remove(selection))
                    Editor::History.SnapshotLevel("Remove reactor trigger");

                if (selection > Game::Level.ReactorTriggers.Count()) selection--;
            }
        }
    };
}