#pragma once

#include "WindowBase.h"
#include "FileSystem.h"
#include "Game.h"
#include "WindowsDialogs.h"

namespace Inferno::Editor {
    class FileBrowserWindow : public WindowBase {
        List<string> _files;
    public:
        FileBrowserWindow() : WindowBase("File Browser") {}
    protected:
        void OnUpdate() override {
            // todo: only update list when mission changes
            // hack: should filter for both
            auto contents = Game::Mission->GetContents(".rl2");
            if (contents.empty())
                contents = Game::Mission->GetContents(".rdl");

            ImGui::Text(Game::Mission->Path.string().c_str());

            static string selection;
            auto window = ImGui::GetCurrentWindow();
            
            {
                ImGui::BeginChildFrame(ImGui::GetID("###Contents"), { 0, std::max(window->Size.y - 125, 100.0f) });
                for (auto& file : contents) {
                    if (ImGui::Selectable(file.c_str(), file == selection, ImGuiSelectableFlags_AllowDoubleClick)) {
                        selection = file;
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            try {
                                Game::LoadLevel(Resources::ReadLevel(selection));
                            }
                            catch (const std::exception& e) {
                                ShowErrorMessage(e);
                            }
                        }
                    }
                }
                ImGui::EndChildFrame();
            }


            ImGuiButtonFlags flags = selection == "" ? ImGuiButtonFlags_Disabled : ImGuiButtonFlags_None;
            if (ImGui::ButtonEx("Open", {}, flags)) {
                try {
                    Game::LoadLevel(Resources::ReadLevel(selection));
                }
                catch (const std::exception& e) {
                    ShowErrorMessage(e);
                }
            }

            ImGui::SameLine();
            ImGui::Button("Delete");
        }
    };
}