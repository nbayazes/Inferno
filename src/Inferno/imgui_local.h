#pragma once

#include <codeanalysis\warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl.h>
#include "Graphics/Render.h"
#include "Graphics/Buffers.h"

namespace ImGui {
    // Copy of Selectable() with a border when selected
    inline bool ToggleButton(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size_arg, float borderSize = 1) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        // Submit label or explicit size to ItemSize(), whereas ItemAdd() will submit a larger/spanning rectangle.
        ImGuiID id = window->GetID(label);
        ImVec2 label_size = CalcTextSize(label, NULL, true);
        ImVec2 size(size_arg.x != 0.0f ? size_arg.x : label_size.x, size_arg.y != 0.0f ? size_arg.y : label_size.y);
        ImVec2 pos = window->DC.CursorPos;
        pos.y += window->DC.CurrLineTextBaseOffset;
        ItemSize(size, 0.0f);

        // Fill horizontal space
        // We don't support (size < 0.0f) in Selectable() because the ItemSpacing extension would make explicitly right-aligned sizes not visibly match other widgets.
        const bool span_all_columns = (flags & ImGuiSelectableFlags_SpanAllColumns) != 0;
        const float min_x = span_all_columns ? window->ParentWorkRect.Min.x : pos.x;
        const float max_x = span_all_columns ? window->ParentWorkRect.Max.x : window->WorkRect.Max.x;
        if (size_arg.x == 0.0f || (flags & ImGuiSelectableFlags_SpanAvailWidth))
            size.x = ImMax(label_size.x, max_x - min_x);

        // Text stays at the submission position, but bounding box may be extended on both sides
        const ImVec2 text_min = pos;
        const ImVec2 text_max(min_x + size.x, pos.y + size.y);

        // Selectables are meant to be tightly packed together with no click-gap, so we extend their box to cover spacing between selectable.
        ImRect bb(min_x, pos.y, text_max.x, text_max.y);
        if ((flags & ImGuiSelectableFlags_NoPadWithHalfSpacing) == 0) {
            const float spacing_x = span_all_columns ? 0.0f : style.ItemSpacing.x;
            const float spacing_y = style.ItemSpacing.y;
            const float spacing_L = IM_FLOOR(spacing_x * 0.50f);
            const float spacing_U = IM_FLOOR(spacing_y * 0.50f);
            bb.Min.x -= spacing_L;
            bb.Min.y -= spacing_U;
            bb.Max.x += (spacing_x - spacing_L);
            bb.Max.y += (spacing_y - spacing_U);
        }
        //if (g.IO.KeyCtrl) { GetForegroundDrawList()->AddRect(bb.Min, bb.Max, IM_COL32(0, 255, 0, 255)); }

        // Modify ClipRect for the ItemAdd(), faster than doing a PushColumnsBackground/PushTableBackground for every Selectable..
        const float backup_clip_rect_min_x = window->ClipRect.Min.x;
        const float backup_clip_rect_max_x = window->ClipRect.Max.x;
        if (span_all_columns) {
            window->ClipRect.Min.x = window->ParentWorkRect.Min.x;
            window->ClipRect.Max.x = window->ParentWorkRect.Max.x;
        }

        const bool disabled_item = (flags & ImGuiSelectableFlags_Disabled) != 0;
        const bool item_add = ItemAdd(bb, id, NULL, disabled_item ? ImGuiItemFlags_Disabled : ImGuiItemFlags_None);
        if (span_all_columns) {
            window->ClipRect.Min.x = backup_clip_rect_min_x;
            window->ClipRect.Max.x = backup_clip_rect_max_x;
        }

        if (!item_add)
            return false;

        const bool disabled_global = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        if (disabled_item && !disabled_global) // Only testing this as an optimization
            BeginDisabled();

        // FIXME: We can standardize the behavior of those two, we could also keep the fast path of override ClipRect + full push on render only,
        // which would be advantageous since most selectable are not selected.
        if (span_all_columns && window->DC.CurrentColumns)
            PushColumnsBackground();
        else if (span_all_columns && g.CurrentTable)
            TablePushBackgroundChannel();

        // We use NoHoldingActiveID on menus so user can click and _hold_ on a menu then drag to browse child entries
        ImGuiButtonFlags button_flags = 0;
        if (flags & ImGuiSelectableFlags_NoHoldingActiveID) { button_flags |= ImGuiButtonFlags_NoHoldingActiveId; }
        if (flags & ImGuiSelectableFlags_SelectOnClick) { button_flags |= ImGuiButtonFlags_PressedOnClick; }
        if (flags & ImGuiSelectableFlags_SelectOnRelease) { button_flags |= ImGuiButtonFlags_PressedOnRelease; }
        if (flags & ImGuiSelectableFlags_AllowDoubleClick) { button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick; }
        if (flags & ImGuiSelectableFlags_AllowItemOverlap) { button_flags |= ImGuiButtonFlags_AllowItemOverlap; }

        const bool was_selected = selected;
        bool hovered, held;
        bool pressed = ButtonBehavior(bb, id, &hovered, &held, button_flags);

        // Auto-select when moved into
        // - This will be more fully fleshed in the range-select branch
        // - This is not exposed as it won't nicely work with some user side handling of shift/control
        // - We cannot do 'if (g.NavJustMovedToId != id) { selected = false; pressed = was_selected; }' for two reasons
        //   - (1) it would require focus scope to be set, need exposing PushFocusScope() or equivalent (e.g. BeginSelection() calling PushFocusScope())
        //   - (2) usage will fail with clipped items
        //   The multi-select API aim to fix those issues, e.g. may be replaced with a BeginSelection() API.
        if ((flags & ImGuiSelectableFlags_SelectOnNav) && g.NavJustMovedToId != 0 && g.NavJustMovedToFocusScopeId == window->DC.NavFocusScopeIdCurrent)
            if (g.NavJustMovedToId == id)
                selected = pressed = true;

        // Update NavId when clicking or when Hovering (this doesn't happen on most widgets), so navigation can be resumed with gamepad/keyboard
        if (pressed || (hovered && (flags & ImGuiSelectableFlags_SetNavIdOnHover))) {
            if (!g.NavDisableMouseHover && g.NavWindow == window && g.NavLayer == window->DC.NavLayerCurrent) {
                SetNavID(id, window->DC.NavLayerCurrent, window->DC.NavFocusScopeIdCurrent, WindowRectAbsToRel(window, bb)); // (bb == NavRect)
                g.NavDisableHighlight = true;
            }
        }
        if (pressed)
            MarkItemEdited(id);

        if (flags & ImGuiSelectableFlags_AllowItemOverlap)
            SetItemAllowOverlap();

        // In this branch, Selectable() cannot toggle the selection so this will never trigger.
        if (selected != was_selected) //-V547
            g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_ToggledSelection;
        if (selected) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, borderSize);
            bb.Min.x += borderSize / 2;
            bb.Min.y += borderSize / 2;
            bb.Max.x -= borderSize / 2;
            bb.Max.y -= borderSize / 2;
            RenderFrame(bb.Min, bb.Max, 0, true, 0.0f);
            ImGui::PopStyleVar();
        }

        // Render
        if (held && (flags & ImGuiSelectableFlags_DrawHoveredWhenHeld))
            hovered = true;
        if (hovered || selected) {
            const ImU32 col = GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
            RenderFrame(bb.Min, bb.Max, col, false, 0.0f);
        }
        RenderNavHighlight(bb, id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);

        if (span_all_columns && window->DC.CurrentColumns)
            PopColumnsBackground();
        else if (span_all_columns && g.CurrentTable)
            TablePopBackgroundChannel();

        RenderTextClipped(text_min, text_max, label, NULL, &label_size, style.SelectableTextAlign, &bb);

        // Automatically close popups
        if (pressed && (window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiSelectableFlags_DontClosePopups) && !(g.LastItemData.InFlags & ImGuiItemFlags_SelectableDontClosePopup))
            CloseCurrentPopup();

        if (disabled_item && !disabled_global)
            EndDisabled();

        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
        return pressed; //-V1020
    }

    inline void SeparatorVertical() {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    }
}

namespace Inferno {
    struct FrameResources {
        UploadBuffer<ImDrawVert> VertexBuffer = { 10000 };
        UploadBuffer<ImDrawIdx> IndexBuffer = { 20000 };
    };

    struct FrameContext {
        ComPtr<ID3D12CommandAllocator> CommandAllocator;
        ComPtr<ID3D12Resource> RenderTarget;
        D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetCpuDescriptors = {};
    };

    // Helper structure we store in the void* RenderUserData field of each ImGuiViewport to easily retrieve our backend data.
    struct ImGuiViewportData {
        ComPtr<ID3D12CommandQueue> CommandQueue;
        ComPtr<ID3D12GraphicsCommandList> CommandList;
        ComPtr<ID3D12DescriptorHeap> RtvDescHeap;
        ComPtr<IDXGISwapChain3> SwapChain;

        ComPtr<ID3D12Fence> Fence;
        UINT64 FenceSignaledValue = 0;
        HANDLE FenceEvent = nullptr;

        UINT FrameIndex = UINT_MAX;
        List<FrameContext> FrameCtx;
        List<FrameResources> Resources;

        ImGuiViewportData(int backBufferCount) :
            FrameCtx(backBufferCount),
            Resources(backBufferCount) {}

        ~ImGuiViewportData() {
            CloseHandle(FenceEvent);
        }

        ImGuiViewportData(const ImGuiViewportData&) = delete;
        ImGuiViewportData(ImGuiViewportData&&) = delete;
        ImGuiViewportData& operator=(const ImGuiViewportData&) = delete;
        ImGuiViewportData& operator=(ImGuiViewportData&&) = delete;
    };

    class ImGuiBatch {
        //std::unique_ptr<ImGuiEffect> _effect;
        //DX::UIEffect* _effect;
        int _backBufferCount;


    public:
        ImGuiBatch(int backBufferCount)
            : _backBufferCount(backBufferCount) {
            // Setup back-end capabilities flags
            ImGuiIO& io = ImGui::GetIO();
            io.BackendRendererName = "imgui_impl_dx12";
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
            io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional) // FIXME-VIEWPORT: Actually unfinished..

            ImGuiViewport* main_viewport = ImGui::GetMainViewport();
#pragma warning(push)
#pragma warning(disable: 26409)
            main_viewport->RendererUserData = IM_NEW(ImGuiViewportData)(backBufferCount);
#pragma warning(pop)
            // Setup back-end capabilities flags
            io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;    // We can create multi-viewports on the Renderer side (optional)
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
#if IM_MULTI_VIEWPORT
                ImGui_ImplDX12_InitPlatformInterface();
#endif
            }
            else {
                ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
                platform_io.Renderer_DestroyWindow = DestroyWindow;
            }

            ImGuiStyle& style = ImGui::GetStyle();
            style.FrameRounding = 3;
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.65f);
            CreateFontsTexture();
        }

        ~ImGuiBatch() {
            ImGuiViewport* main_viewport = ImGui::GetMainViewport();
            if (ImGuiViewportData* data = (ImGuiViewportData*)main_viewport->RendererUserData)
                IM_DELETE(data);
            main_viewport->RendererUserData = nullptr;
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        ImGuiBatch(const ImGuiBatch&) = delete;
        ImGuiBatch(ImGuiBatch&&) = delete;
        ImGuiBatch& operator=(const ImGuiBatch&) = delete;
        ImGuiBatch& operator=(ImGuiBatch&&) = delete;

        bool Enabled = true;

        static void Initialize(HWND hwnd, float fontSize = 24) {
            // Setup Dear ImGui context
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
            //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
            // BUG: default impl. causes exception on shutdown if viewports are disabled
            //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
            //io.ConfigViewportsNoAutoMerge = true;
            //io.ConfigViewportsNoTaskBarIcon = true;

            // Setup Dear ImGui style
            ImGui::StyleColorsDark();
            //ImGui::StyleColorsClassic();

            //// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
            //ImGuiStyle& style = ImGui::GetStyle();
            //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            //    style.WindowRounding = 0.0f;
            //    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            //}

            // Setup Platform/Renderer bindings
            ImGui_ImplWin32_Init(hwnd);

            //static const ImWchar ranges[] = { 0x0020, 0x00FF, 0 };

            /*ImFont* font = */io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\SegoeUI.ttf", fontSize, nullptr, nullptr);
        }

        void BeginFrame() {
            if (!Enabled) return;
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
        }

        void EndFrame() {
            if (!Enabled) return;
            ImGui::Render();
        }

        void Render(ID3D12GraphicsCommandList* commandList) {
            RenderDrawData(ImGui::GetDrawData(), commandList);
        }

    private:
        void RenderDrawData(ImDrawData* draw_data, ID3D12GraphicsCommandList* ctx) {
            if (!draw_data || !ctx) return;

            // Avoid rendering when minimized
            if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
                return;

            ImGuiViewportData* render_data = (ImGuiViewportData*)draw_data->OwnerViewport->RendererUserData;
            render_data->FrameIndex++;
            FrameResources* fr = &render_data->Resources[render_data->FrameIndex % _backBufferCount];

            fr->VertexBuffer.Begin();
            fr->IndexBuffer.Begin();

            for (int n = 0; n < draw_data->CmdListsCount; n++) {
                const ImDrawList* cmdList = draw_data->CmdLists[n];
                fr->VertexBuffer.Copy({ cmdList->VtxBuffer.Data, (size_t)cmdList->VtxBuffer.Size });
                fr->IndexBuffer.Copy({ cmdList->IdxBuffer.Data, (size_t)cmdList->IdxBuffer.Size });
            }

            bool resized = false;
            resized |= !fr->VertexBuffer.End();
            resized |= !fr->IndexBuffer.End();
            if (resized) return;

            // Setup desired DX state
            SetRenderState(draw_data, ctx, fr);

            // Render command lists
            // (Because we merged all buffers into a single one, we maintain our own offset into them)
            int global_vtx_offset = 0;
            int global_idx_offset = 0;
            ImVec2 clip_off = draw_data->DisplayPos;
            for (int n = 0; n < draw_data->CmdListsCount; n++) {
                const ImDrawList* cmd_list = draw_data->CmdLists[n];
                for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
                    const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                    if (pcmd->UserCallback != nullptr) {
                        // User callback, registered via ImDrawList::AddCallback()
                        // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                        if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                            SetRenderState(draw_data, ctx, fr);
                        else
                            pcmd->UserCallback(cmd_list, pcmd);
                    }
                    else {
                        // Apply Scissor, Bind texture, Draw
                        const D3D12_RECT r = { (LONG)(pcmd->ClipRect.x - clip_off.x), (LONG)(pcmd->ClipRect.y - clip_off.y), (LONG)(pcmd->ClipRect.z - clip_off.x), (LONG)(pcmd->ClipRect.w - clip_off.y) };
                        Render::Shaders->UserInterface.SetDiffuse(ctx, *(D3D12_GPU_DESCRIPTOR_HANDLE*)&pcmd->TextureId);
                        ctx->RSSetScissorRects(1, &r);
                        ctx->DrawIndexedInstanced(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                    }
                }
                global_idx_offset += cmd_list->IdxBuffer.Size;
                global_vtx_offset += cmd_list->VtxBuffer.Size;
            }
        }

        void CreateFontsTexture() {
            // Build texture atlas
            ImGuiIO& io = ImGui::GetIO();
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

            auto batch = Render::BeginTextureUpload();
            Render::StaticTextures->ImguiFont.Load(batch, (void*)pixels, width, height, L"ImGui Font");
            Render::StaticTextures->ImguiFont.AddShaderResourceView();
            Render::EndTextureUpload(batch);

            // Store our identifier
            auto ptr = Render::StaticTextures->ImguiFont.GetSRV().ptr;
            static_assert(sizeof(ImTextureID) >= sizeof(ptr), "Can't pack descriptor handle into TexID, 32-bit not supported yet.");
            io.Fonts->TexID = (ImTextureID)ptr;
        }

        void SetRenderState(ImDrawData* draw_data, ID3D12GraphicsCommandList* ctx, FrameResources* fr) {
            // Setup orthographic projection matrix into our constant buffer
            // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
            float L = draw_data->DisplayPos.x;
            float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
            float T = draw_data->DisplayPos.y;
            float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

            auto proj = Matrix::CreateOrthographicOffCenter(L, R, B, T, 0.0, -2.0f);
            // Setup viewport
            CD3DX12_VIEWPORT vp(0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y);
            ctx->RSSetViewports(1, &vp);

            // Bind shader and vertex buffers
            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = fr->VertexBuffer.GetGPUVirtualAddress();
            vbv.SizeInBytes = (uint)fr->VertexBuffer.GetSizeInBytes();
            vbv.StrideInBytes = fr->VertexBuffer.Stride;
            ctx->IASetVertexBuffers(0, 1, &vbv);
            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = fr->IndexBuffer.GetGPUVirtualAddress();
            ibv.SizeInBytes = (uint)fr->IndexBuffer.GetSizeInBytes();
            ibv.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            ctx->IASetIndexBuffer(&ibv);
            ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Render::Effects->UserInterface.Apply(ctx);
            Render::Shaders->UserInterface.SetWorldViewProjection(ctx, proj);

            // Setup blend factor
            const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
            ctx->OMSetBlendFactor(blend_factor);
        }

        static void ImGui_WaitForPendingOperations(ImGuiViewportData* data) {
            HRESULT hr = S_FALSE;
            if (data && data->CommandQueue && data->Fence && data->FenceEvent) {
                hr = data->CommandQueue->Signal(data->Fence.Get(), ++data->FenceSignaledValue);
                IM_ASSERT(hr == S_OK);
                ::WaitForSingleObject(data->FenceEvent, 0); // Reset any forgotten waits
                hr = data->Fence->SetEventOnCompletion(data->FenceSignaledValue, data->FenceEvent);
                IM_ASSERT(hr == S_OK);
                ::WaitForSingleObject(data->FenceEvent, INFINITE);
            }
        }

        static void DestroyWindow(ImGuiViewport* viewport) {
            // The main viewport (owned by the application) will always have RendererUserData == nullptr since we didn't create the data for it.
            if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->RendererUserData) {
                ImGui_WaitForPendingOperations(data);
                IM_DELETE(data);
            }
            viewport->RendererUserData = nullptr;
        }
    };

    inline Ptr<ImGuiBatch> g_ImGuiBatch;
}

#pragma warning(pop)
