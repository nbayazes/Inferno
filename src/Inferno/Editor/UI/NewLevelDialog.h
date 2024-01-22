#pragma once
#include "../Editor.h"
#include "Resources.h"
#include "WindowBase.h"

namespace Inferno::Editor {
    class NewLevelDialog : public ModalWindowBase {
    public:
        NewLevelDialog() : ModalWindowBase("New Level") {}

        char _title[35]{};
        char _fileName[9]{};
        int16 Version = 7;
        bool _foundD1 = false, _foundD2 = false, _foundVertigo = false;
        bool _addToHog = false;
    protected:
        bool OnOpen() override {
            _foundD1 = Resources::FoundDescent1();
            _foundD2 = Resources::FoundDescent2();
            _foundVertigo = Resources::FoundVertigo();
            strcpy_s(_title, "untitled");
            strcpy_s(_fileName, "new");
            if (!Game::Mission)
                _addToHog = false;
            return CanCloseCurrentFile();
        }

        void OnUpdate() override {
            ImGui::Text("Title");

            SetInitialFocus();
            ImGui::InputText("##Title", _title, std::size(_title), ImGuiInputTextFlags_AutoSelectAll);
            EndInitialFocus();

            ImGui::Text("File name");
            ImGui::InputText("##Filename", _fileName, std::size(_fileName), ImGuiInputTextFlags_AutoSelectAll);

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Version");
            {
                DisableControls disable(!_foundD1);
                if (ImGui::RadioButton("Descent 1", Version == 1)) Version = 1;
            }
            {
                DisableControls disable(!_foundD2);
                if (ImGui::RadioButton("Descent 2", Version == 7)) Version = 7;
            }
            {
                DisableControls disable(!_foundVertigo);
                if (ImGui::RadioButton("Descent 2 - Vertigo", Version == 8)) Version = 8;
            }

            if (Game::Mission) {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::Checkbox("Add to HOG", &_addToHog);
            }

            bool validSelection =
                (Version == 1 && _foundD1) ||
                (Version == 7 && _foundD2) ||
                (Version == 8 && _foundVertigo);

            bool validFields = _title[0] != '\0' && _fileName[0] != '\0';
            AcceptButtons("OK", "Cancel", validFields && validSelection);
        }

        void OnAccept() override {
            Editor::NewLevel(_title, _fileName, Version, _addToHog);
        }
    };
}
