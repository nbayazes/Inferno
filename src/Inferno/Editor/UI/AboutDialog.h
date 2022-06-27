#pragma once

#include "WindowBase.h"
#include "Version.h"

namespace Inferno::Editor {
    class AboutDialog : public ModalWindowBase {
    public:
        AboutDialog() : ModalWindowBase("About Inferno") {}

    protected:
        void OnUpdate() override {
            //ImGui::GetCurrentWindow()->FontWindowScale();
            ImGui::SetWindowFontScale(1.75);
            auto mult = float(std::sin(Game::ElapsedTime * 1.5) + 1) * 0.5f;
            ImVec4 color = { 1, 0.3f * mult, 0.3f * mult, 1 };
            auto pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos({ pos.x + 1, pos.y /*+ 1 */});
            ImGui::TextColored({ color.x, color.y, color.z, color.w }, AppTitle);
            ImGui::SetCursorPos(pos);
            ImGui::TextColored(color, AppTitle);
            ImGui::SetWindowFontScale(1);

            ImGui::Text("Version %s", VersionString);

            ImGui::Dummy({ 0, 10 });
            ImGui::Text((char*)u8"© 2022 Nicholas Bayazes");

            ImGui::BeginChild("closebtns", { 0, 32 });
            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
            if (ImGui::Button("OK", { 100, 0 }))
                Close();
            ImGui::EndChild();
        }
    };
}