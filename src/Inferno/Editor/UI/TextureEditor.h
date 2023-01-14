#pragma once
#include "WindowBase.h"
#include "Editor/Editor.h"
#include "Resources.h"

namespace Inferno::Editor {
    enum class BitmapTransparencyMode {
        NoTransparency,
        ByPaletteIndex,
        ByColor
    };

    void WriteBmp(const filesystem::path& path, const Palette& gamePalette, const PigBitmap& bmp);

    // A user defined texture in a POG or DTX
    struct CustomTexture : PigEntry {
        //List<ubyte> Data; // Indexed color data
        List<Palette::Color> Data;
    };

    // Editor for importing custom textures
    class TextureEditor final : public WindowBase {
        bool _showModified = true;
        bool _showLevel = false;
        bool _showPowerups = false;
        bool _showRobots = false;
        bool _showMisc = false;
        bool _showInUse = true;
        TexID _selection = TexID{ 1 };
        bool _useTransparency = false;
        Set<TexID> _levelTextures;
        List<TexID> _visibleTextures;
        bool _initialized = false;

    public:
        TextureEditor() : WindowBase("Texture Editor", &Settings::Editor.Windows.TextureEditor) {
            Events::SelectSegment += [this] {
                const auto& seg = Game::Level.GetSegment(Editor::Selection.Segment);
                auto [t1, t2] = seg.GetTexturesForSide(Editor::Selection.Side);
                _selection = Resources::LookupLevelTexID(t1);
            };

            Events::LevelLoaded += [this] {
                _initialized = false;
            };
        }

    protected:
        void OnUpdate() override {
            if (!_initialized) {
                UpdateTextureList();
                _initialized = true;
            }

            const float detailWidth = 250 * Shell::DpiScale;
            const float bottomHeight = 200 * Shell::DpiScale;

            auto contentMax = ImGui::GetWindowContentRegionMax();

            {
                ImGui::BeginChild("list", { contentMax.x - detailWidth, contentMax.y - bottomHeight });
                constexpr auto flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;

                if (ImGui::BeginTable("properties", 5, flags)) {
                    ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                    ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Dimensions", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Transparent", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (auto& id : _visibleTextures) {
                        auto ti = Resources::GetTextureInfo(id);
                        auto& bmp = Resources::GetBitmap(id);

                        auto& material = Render::Materials->Get(id);
                        bool selected = id == _selection;

                        const float rowHeight = 32 * Shell::DpiScale;
                        auto ratio = ti.Width > 0 && ti.Height > 0 ? (float)ti.Width / (float)ti.Height : 1.0f;
                        ImVec2 tileSize{ rowHeight, rowHeight };
                        if (ratio > 1) tileSize.y /= ratio;
                        if (ratio < 1) tileSize.x *= ratio;

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        ImGui::PushID((int)id);
                        constexpr auto selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                        if (ImGui::Selectable("", selected, selectable_flags, ImVec2(0, rowHeight))) {
                            _selection = id;
                        }
                        ImGui::PopID();
                        if (material.ID != TexID::Invalid) {
                            ImGui::SameLine();
                            ImGui::Image((ImTextureID)material.Handles[0].ptr, tileSize, { 0, 0 }, { 1, 1 });
                        }

                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text(bmp.Info.Name.c_str());
                        if (ImGui::IsItemVisible()) {
                            std::array ids{ id };
                            Render::Materials->LoadMaterialsAsync(ids);
                        }

                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        auto sizeText = fmt::format("{} x {}", ti.Width, ti.Height);
                        ImGui::Text(sizeText.c_str());

                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        auto transparent = ti.Transparent ? (ti.SuperTransparent ? "Yes+" : "Yes") : "No";
                        ImGui::Text(transparent);

                        ImGui::TableNextColumn();
                        ImGui::Text(bmp.Info.Custom ? "Yes" : "No");
                    }

                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }

            {
                ImGui::SameLine();
                ImGui::BeginChild("details", { detailWidth, ImGui::GetWindowSize().y - bottomHeight });

                auto& bmp = Resources::GetBitmap(_selection);
                auto& ti = bmp.Info;
                if (ti.ID > TexID::Invalid) {
                    //auto ti = Resources::GetTextureInfo(_selection);
                    auto& material = Render::Materials->Get(_selection);

                    ImGui::Text(ti.Name.c_str());
                    auto label = fmt::format("Tex ID: {}", ti.ID);
                    ImGui::Text(label.c_str());

                    ImVec2 tileSize{ 128 * Shell::DpiScale, 128 * Shell::DpiScale };
                    {
                        ImGui::BeginChild("preview", tileSize);
                        auto ratio = ti.Width > 0 && ti.Height > 0 ? (float)ti.Width / (float)ti.Height : 1.0f;
                        if (ratio > 1) tileSize.y /= ratio;
                        if (ratio < 1) tileSize.x *= ratio;
                        ImGui::Image((ImTextureID)material.Handles[0].ptr, tileSize, { 0, 0 }, { 1, 1 });
                        ImGui::EndChild();
                    }

                    {
                        if (ImGui::Button("Import", { 100 * Shell::DpiScale, 0 })) {
                            OnImport(ti);
                        }

                        ImGui::Dummy({ 5 * Shell::DpiScale, 0 });
                        ImGui::Dummy({ 0, 5 * Shell::DpiScale });
                        ImGui::SameLine();
                        ImGui::Checkbox("Use transparency", &_useTransparency);

                        ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                        if (ImGui::Button("Export", { 100 * Shell::DpiScale, 0 })) {
                            OnExport(bmp.Info.ID);
                        }

                        {
                            DisableControls disable(!ti.Custom);
                            if (ImGui::Button("Revert", { 100 * Shell::DpiScale, 0 }))
                                OnRevert(ti.ID);
                        }
                    }
                }

                ImGui::EndChild();
            }

            const auto colWidth = 175 * Shell::DpiScale;
            ImGui::BeginChild("filters", { colWidth * 2, 0 });
            ImGui::Text("Filters:");

            ImGui::BeginChild("filtersCol1", { colWidth, 0 });
            if (ImGui::Checkbox("Level", &_showLevel)) UpdateTextureList();
            if (ImGui::Checkbox("Misc", &_showMisc)) UpdateTextureList();
            if (ImGui::Checkbox("Modified", &_showModified)) UpdateTextureList();
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("filtersCol2", { colWidth, 0 });
            if (ImGui::Checkbox("Powerup", &_showPowerups)) UpdateTextureList();
            if (ImGui::Checkbox("Robots", &_showRobots)) UpdateTextureList();
            if (ImGui::Checkbox("In Use", &_showInUse)) UpdateTextureList();
            ImGui::EndChild();

            ImGui::EndChild();
        }

    private:
        static void OnExport(TexID id) {
            try {
                auto& bmp = Resources::GetBitmap(id);
                static constexpr COMDLG_FILTERSPEC filter[] = {
                    { L"256 Color Bitmap", L"*.BMP" }
                };

                if (auto path = SaveFileDialog(filter, 0, Convert::ToWideString(bmp.Info.Name + ".bmp"), L"Export BMP")) {
                    WriteBmp(*path, Resources::GetPalette(), bmp);
                }
            }
            catch (...) {}
        }

        void OnImport(const PigEntry& entry) {
            try {
                static constexpr COMDLG_FILTERSPEC filter[] = { { L"256 Color Bitmap", L"*.BMP" }, };
                if (auto file = OpenFileDialog(filter, L"Import custom texture")) {
                    // todo: check game version, limits, and if level textures are square
                    Resources::CustomTextures.ImportBmp(*file, _useTransparency, entry);
                    std::array ids{ _selection };
                    Render::Materials->LoadMaterialsAsync(ids, true);
                    UpdateTextureList();
                }
            }
            catch (...) {}
        }

        void OnRevert(TexID id) {
            if (Resources::CustomTextures.Get(id)) {
                Resources::CustomTextures.Delete(id);
                std::array ids{ id };
                Render::Materials->LoadMaterialsAsync(ids, true);
                UpdateTextureList();
            }
        }

        void UpdateTextureList();
    };
}
