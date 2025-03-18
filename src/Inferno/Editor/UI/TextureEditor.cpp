#include "pch.h"
#include "TextureEditor.h"
#include "Editor/Editor.Undo.h"
#include "Editor/Events.h"
#include "Graphics/MaterialLibrary.h"

namespace Inferno::Editor {
    void OnExport(TexID id) {
        try {
            auto& bmp = Resources::GetBitmap(id);
            static constexpr COMDLG_FILTERSPEC filter[] = { { L"256 Color Bitmap", L"*.BMP" } };
            if (auto path = SaveFileDialog(filter, 0, bmp.Info.Name + ".bmp", "Export BMP")) {
                WriteBmp(*path, Resources::GetPalette(), bmp);
            }
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }

    void WriteBmp(const filesystem::path& path, const Palette& gamePalette, const PigBitmap& bmp) {
        std::ofstream stream(path, std::ios::binary);
        StreamWriter writer(stream, false);

        constexpr DWORD offset = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 256 * 4;
        constexpr int bpp = 8;
        const int padding = (bmp.Info.Width * bpp + 31) / 32 * 4 - bmp.Info.Width;

        BITMAPFILEHEADER bmfh{
            .bfType = 'MB',
            .bfSize = offset + (bmp.Info.Width + padding) * bmp.Info.Height,
            .bfOffBits = offset
        };

        BITMAPINFOHEADER bmih{
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = bmp.Info.Width,
            .biHeight = -bmp.Info.Height, // Top down
            .biPlanes = 1,
            .biBitCount = bpp,
            .biCompression = BI_RGB,
            .biSizeImage = 0,
            .biXPelsPerMeter = 0,
            .biYPelsPerMeter = 0,
            .biClrUsed = 256,
            .biClrImportant = 0
        };

        writer.WriteBytes(span{ (ubyte*)&bmfh, sizeof bmfh });
        writer.WriteBytes(span{ (ubyte*)&bmih, sizeof bmih });

        List<RGBQUAD> palette(256);

        for (auto& color : gamePalette.Data) {
            writer.Write<RGBQUAD>({ color.b, color.g, color.r });
        }

        for (size_t i = 0; i < bmp.Info.Height; i++) {
            for (size_t j = 0; j < bmp.Info.Width; j++) {
                writer.Write<ubyte>(bmp.Indexed[i * bmp.Info.Width + j]);
            }
            for (size_t j = 0; j < padding; j++) {
                writer.Write<ubyte>(0); // pad rows of data to an alignment of 4
            }
        }
    }

    TextureEditor::TextureEditor(): WindowBase("Texture Editor", &Settings::Editor.Windows.TextureEditor) {
        Events::SelectSegment += [this] {
            if (auto seg = Game::Level.TryGetSegment(Editor::Selection.Segment)) {
                auto [t1, t2] = seg->GetTexturesForSide(Editor::Selection.Side);
                _selection = Resources::LookupTexID(t1);
            }
        };

        Events::LevelLoaded += [this] {
            _initialized = false;
        };

        _search.resize(20);
        ranges::fill(_search, 0);
    }

    void TextureEditor::OnUpdate() {
        if (!_initialized) {
            UpdateTextureList();
            _initialized = true;
        }

        const float detailWidth = 250 * Shell::DpiScale;
        const float bottomHeight = 200 * Shell::DpiScale;

        auto contentMax = ImGui::GetWindowContentRegionMax();
        auto searchstr = String::ToLower(string(_search.data()));

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

                auto tableRect = ImGui::GetCurrentWindow()->ClipRect;

                for (auto& id : _visibleTextures) {
                    auto& ti = Resources::GetTextureInfo(id);

                    if (!searchstr.empty()) {
                        if (!String::Contains(String::ToLower(ti.Name), searchstr))
                            continue;
                    }

                    auto& bmp = Resources::GetBitmap(id);

                    auto& material = Render::Materials->Get(id);
                    bool selected = id == _selection;

                    const float rowHeight = 32 * Shell::DpiScale;
                    auto ratio = ti.Width > 0 && ti.Height > 0 ? (float)ti.Width / (float)ti.Height : 1.0f;
                    ImVec2 tileSize{ rowHeight, rowHeight };
                    if (ratio > 1) tileSize.y /= ratio;
                    if (ratio < 1) tileSize.x *= ratio;

                    ImGui::TableNextRow();

                    auto cursor = ImGui::GetCursorScreenPos();
                    ImRect rowRect = { cursor, { cursor.x + tileSize.x, cursor.y + tileSize.y} };

                    ImGui::TableNextColumn();

                    ImGui::PushID((int)id);
                    constexpr auto selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                    if (ImGui::Selectable("", selected, selectable_flags, ImVec2(0, rowHeight))) {
                        _selection = id;
                    }
                    ImGui::PopID();

                    if (material && tableRect.Overlaps(rowRect)) {
                        ImGui::SameLine();
                        ImGui::Image((ImTextureID)material.Pointer(), tileSize, { 0, 0 }, { 1, 1 });
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
                    ImGui::Image((ImTextureID)material.Pointer(), tileSize, { 0, 0 }, { 1, 1 });
                    ImGui::EndChild();
                }

                {
                    if (ImGui::Button("Import", { 100 * Shell::DpiScale, 0 })) {
                        OnImport(ti);
                    }

                    ImGui::Dummy({ 5 * Shell::DpiScale, 0 });

                    ImGui::Dummy({ 0, 5 * Shell::DpiScale });
                    ImGui::SameLine();
                    ImGui::Checkbox("Transparent palette", &_useTransparency);
                    ImGui::HelpMarker("Loads palette index 254 as super transparent and\nindex 255 as transparent");

                    ImGui::Dummy({ 0, 5 * Shell::DpiScale });
                    ImGui::SameLine();
                    ImGui::Checkbox("Transparent white", &_whiteAsTransparent);
                    ImGui::HelpMarker("Loads the color nearest to white as transparent");

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
        ImGui::SameLine();

        ImGui::Text("Search");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200 * Shell::DpiScale);

        ImGui::InputText("##Search", _search.data(), _search.capacity());
    }

    void TextureEditor::OnImport(const PigEntry& entry) {
        try {
            static constexpr COMDLG_FILTERSPEC filter[] = { { L"256 Color Bitmap", L"*.BMP" }, };
            if (auto file = OpenFileDialog(filter, "Import custom texture")) {
                Resources::CustomTextures.ImportBmp(*file, _useTransparency, entry, Game::Level.IsDescent1(), _whiteAsTransparent);
                std::array ids{ _selection };
                Render::Materials->LoadMaterialsAsync(ids, true);
                UpdateTextureList();
                Editor::History.SnapshotLevel("Import Texture"); // doesn't actually snapshot anything, but marks level as dirty
            }
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }

    void TextureEditor::UpdateTextureList() {
        _visibleTextures.clear();

        auto levelTextures = Render::GetLevelSegmentTextures(Game::Level);

        for (int i = 1; i < Resources::GetTextureCount(); i++) {
            auto& bmp = Resources::GetBitmap((TexID)i);
            auto type = ClassifyTexture(Game::Level.IsDescent1(), bmp.Info);

            if ((_showModified && bmp.Info.Custom) ||
                (_showInUse && levelTextures.contains(bmp.Info.ID))) {
                // show if modified or in use
            }
            else {
                if (!_showRobots && type == TextureType::Robot)
                    continue;

                if (!_showPowerups && type == TextureType::Powerup)
                    continue;

                if (!_showMisc && type == TextureType::Misc)
                    continue;

                if (!_showLevel && type == TextureType::Level)
                    continue;
            }

            _visibleTextures.push_back((TexID)i);
        }

        if (!Seq::contains(_visibleTextures, _selection))
            _selection = _visibleTextures.empty() ? TexID::None : _visibleTextures.front();
    }
}
