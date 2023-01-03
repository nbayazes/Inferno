#pragma once

#include "WindowBase.h"
#include "Version.h"

namespace Inferno::Editor {
    class AboutDialog : public ModalWindowBase {
    public:
        AboutDialog() : ModalWindowBase("About Inferno") {}

    protected:
        void OnUpdate() override {
            ImGui::SetWindowFontScale(1.75);
            auto mult = float(std::sin(Game::ElapsedTime * 1.5) + 1) * 0.5f;
            ImVec4 color = { 1, 0.3f * mult, 0.3f * mult, 1 };
            auto pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos({ pos.x + 1, pos.y /*+ 1 */});
            ImGui::TextColored({ color.x, color.y, color.z, color.w }, APP_TITLE);
            ImGui::SetCursorPos(pos);
            ImGui::TextColored(color, APP_TITLE);
            ImGui::SetWindowFontScale(1);

            ImGui::Text("Version %s", VERSION_STRING);

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text((char*)u8"© 2022 Nicholas Bayazes");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 0.75f, 1, 1 });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.5f, 0.75f, 1, 0.15f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.5f, 0.75f, 1, 0.30f });
            if (ImGui::SmallButton("Visit Project Page"))
                ShellExecute(nullptr, L"open", L"https://github.com/nbayazes/Inferno", nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::PopStyleColor(4);

            ImGui::BeginChild("closebtns", { 0, 32 * Shell::DpiScale });
            ImGui::SameLine(ImGui::GetWindowWidth() - 100 * Shell::DpiScale);
            if (ImGui::Button("OK", { 100 * Shell::DpiScale, 0 }))
                Close();
            ImGui::EndChild();
        }
    };
}