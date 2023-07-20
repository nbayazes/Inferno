#pragma once

#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#include <imgui.h>
#include <imgui_impl.h>
#include <imgui_internal.h>

namespace ImGui {
    // Copy of Selectable() with a border when selected
    bool ToggleButton(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size_arg, float borderSize = 1);

    void SeparatorVertical();
}

namespace Inferno {
    class ImGuiBatch {
        int _backBufferCount;

    public:
        ImGuiBatch(int backBufferCount);
        ~ImGuiBatch();

        ImGuiBatch(const ImGuiBatch&) = delete;
        ImGuiBatch(ImGuiBatch&&) = delete;
        ImGuiBatch& operator=(const ImGuiBatch&) = delete;
        ImGuiBatch& operator=(ImGuiBatch&&) = delete;

        bool Enabled = true;

        void BeginFrame() const {
            if (!Enabled) return;
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
        }

        void EndFrame() const {
            if (!Enabled) return;
            ImGui::Render();
        }

        void Render(ID3D12GraphicsCommandList* commandList) const {
            RenderDrawData(ImGui::GetDrawData(), commandList);
        }

    private:
        void RenderDrawData(const ImDrawData* drawData, ID3D12GraphicsCommandList* ctx) const;
    };

    inline Ptr<ImGuiBatch> g_ImGuiBatch;
    void InitializeImGui(HWND hwnd, float fontSize);
}

#pragma warning(pop)
