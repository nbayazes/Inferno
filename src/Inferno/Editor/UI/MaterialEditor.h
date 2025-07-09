#pragma once
#include "Editor/Editor.IO.h"
#include "Editor/Events.h"
#include "Hog.IO.h"
#include "Procedural.h"
#include "Resources.h"
#include "WindowBase.h"

namespace Inferno::Editor {
    /*
     * Materials are defined in one of four tables. D1, D2, the mission
     * (shared for all levels in the mission), and level specific.
     *
     * Materials are merged together before being uploaded to the GPU in the order:
     * D1 -> D2 (for D2 levels) -> Mission -> Level
     *
     * When the edit source changes, a copy of the original table is made and can be used
     * to discard changes. Need to show a warning that this will cause data loss.
     * Need to track if there are any changes in a table.
     *
     * To preview changes, the table must be re-merged in the graphics layer.
     */

    class MaterialEditor final : public WindowBase {
        TexID _selection = TexID{ 1 };
        List<TexID> _visibleTextures;
        List<char> _search;
        MaterialInfo _copy = {}; // for copy/paste
        MaterialTable _backupTable; // copy of the individual table - for example "mission"
        MaterialTable* _table = nullptr;

        bool _enableLoading = true;
        bool _onlyShowDefined = false;

        // Whether to modify the materials for the base game (D1 or D2) or specific to the mission (Requires a hog)
        TableSource _source = TableSource::Descent1;
        bool _modified = false;

    public:
        static string SourceToString(TableSource source) {
            switch (source) {
                case TableSource::Undefined: return "Undefined";
                case TableSource::Descent1: return "Descent 1";
                case TableSource::Descent2: return "Descent 2";
                case TableSource::Mission: return "Mission";
                case TableSource::Level: return "Level";
                case TableSource::Descent3: return "Descent 3";
                default: return "None";
            }
        }

        static MaterialTable* GetMaterialTableForSource(TableSource source) {
            switch (source) {
                case TableSource::Descent1: return &Descent1Materials;
                case TableSource::Descent2: return &Descent2Materials;
                case TableSource::Mission: return &MissionMaterials;
                case TableSource::Level: return &LevelMaterials;
                //case TableSource::Descent3: return "Descent 3";
                default: return nullptr;
            }
        }

        MaterialEditor() : WindowBase("Material Editor", &Settings::Editor.Windows.MaterialEditor) {
            Events::SelectTexture += [this](LevelTexID tmap1, LevelTexID tmap2) {
                if (tmap1 > LevelTexID::None) _selection = Resources::LookupTexID(tmap1);
                if (tmap2 > LevelTexID::None) _selection = Resources::LookupTexID(tmap2);
                _source = Resources::GetMaterial(_selection).Source;
            };

            Events::SelectSegment += [this] {
                if (auto seg = Game::Level.TryGetSegment(Editor::Selection.Segment)) {
                    auto [t1, t2] = seg->GetTexturesForSide(Editor::Selection.Side);
                    _selection = Resources::LookupTexID(t1);
                }
            };

            Events::LevelLoaded += [this] {
                if (_source == TableSource::Mission && !Game::Mission)
                    _source = TableSource::Level;

                if (Game::Level.IsDescent1() && _source == TableSource::Descent2)
                    _source = TableSource::Descent1;
                else if (Game::Level.IsDescent2() && _source == TableSource::Descent1)
                    _source = TableSource::Descent2;

                _table = GetMaterialTableForSource(_source);
                if (_table)
                    _backupTable = *_table;

                _modified = false;
            };

            _search.resize(20);
            ranges::fill(_search, 0);
        }

        void OnSave() {
            try {
                if (Game::Level.IsShareware || Game::DemoMode) {
                    // Disable saving materials for shareware levels. It causes all non-shareware textures to be lost.
                    ShowErrorMessage("Cannot save materials for shareware level.");
                    return;
                }

                // todo: this should delete the material file if there are no entries left

                switch (_source) {
                    case TableSource::Descent1: {
                        SPDLOG_INFO("Saving materials to {}", D1_MATERIAL_FILE.string());
                        std::ofstream stream(D1_MATERIAL_FILE);
                        Descent1Materials.Save(stream);
                        break;
                    }
                    case TableSource::Descent2: {
                        SPDLOG_INFO("Saving materials to {}", D2_MATERIAL_FILE.string());
                        std::ofstream stream(D2_MATERIAL_FILE);
                        Descent2Materials.Save(stream);
                        break;
                    }
                    case TableSource::Descent3:
                        // not implemented
                        break;
                    case TableSource::Mission:
                        // save into hog
                        if (Game::Mission) {
                            auto data = SerializeMaterialInfo(MissionMaterials);
                            HogWriter::AddOrUpdate(Game::Mission->Path, MATERIAL_TABLE_FILE, data);
                        }
                        break;
                    case TableSource::Level:
                        if (Game::Mission) {
                            // save level specific table into hog
                            auto data = SerializeMaterialInfo(LevelMaterials);
                            auto fileName = String::NameWithoutExtension(Game::Level.FileName) + MATERIAL_TABLE_EXTENSION;
                            HogWriter::AddOrUpdate(Game::Mission->Path, fileName, data);
                        }
                        else {
                            // save level specific table next to level
                            filesystem::path path = Game::Level.Path;
                            path.replace_extension(MATERIAL_TABLE_EXTENSION);
                            std::ofstream stream(path);
                            LevelMaterials.Save(stream);
                        }
                        break;
                }

                if (_table) {
                    _backupTable = *_table;

                    // Clear modified flag after saving
                    for (auto& material : _table->Data()) {
                        material.Modified = false;
                    }
                }

                _modified = false;
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }

            // Reload data tables after saving
            Resources::LoadDataTables(Game::Level);
        }

        void OnUpdate() override {
            const float listWidth = 250 * Shell::DpiScale;
            const float topRowHeight = 100 * Shell::DpiScale;

            auto contentMax = ImGui::GetWindowContentRegionMax();

            // header
            {
                ImGui::Text("Search");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200 * Shell::DpiScale);
                ImGui::InputText("##Search", _search.data(), _search.capacity());

                //ImGui::SameLine();
                //ImGui::Checkbox("Enable loading", &_enableLoading);
                //ImGui::SameLine(contentMax.x - 350);

                ImGui::SameLine();
                ImGui::Text("Table");
                ImGui::SameLine();

                //ImGui::Text("");
                {
                    ImGui::SetNextItemWidth(200 * Shell::DpiScale);

                    auto label = SourceToString(_source);

                    if (ImGui::BeginCombo("##segs", label.c_str())) {
                        for (int i = 1; i <= (int)TableSource::Level; i++) {
                            auto source = (TableSource)i;
                            const bool isSelected = _source == source;
                            auto itemLabel = SourceToString((TableSource)i);

                            if (Game::Level.IsDescent1() && source == TableSource::Descent2)
                                continue; // Don't show D2 when a D1 level is loaded

                            // Don't show 12 when a D2 level is loaded
                            // Though preferably D1 textures that are shared should be modifiable.
                            if (Game::Level.IsDescent2() && source == TableSource::Descent1)
                                continue;

                            if (!Game::Mission && source == TableSource::Mission)
                                continue; // No mission (hog) file to save to

                            if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                if ((TableSource)i != _source) {
                                    bool switchTables = true;

                                    if (_modified) {
                                        auto resp = ShowYesNoCancelMessage("Do you want to save the current table?", "Unsaved Changes");
                                        if (resp) {
                                            // yes or no
                                            if (*resp) OnSave(); // yes, save
                                        }
                                        else {
                                            switchTables = false; // cancel
                                        }
                                    }

                                    if (switchTables) {
                                        _source = (TableSource)i;

                                        // Reload data tables after switching
                                        Resources::LoadDataTables(Game::Level);

                                        // switch loaded table, make new copy
                                        _table = GetMaterialTableForSource(_source);
                                        if (_table) _backupTable = *_table;
                                        _modified = false;
                                        Events::MaterialsChanged();
                                    }
                                }
                            }

                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }
                }

                ImGui::SameLine();
                // only shows materials defined in the selected source.
                // for example picking mission will only show textures defined by the mission materials.
                ImGui::Checkbox("Only defined", &_onlyShowDefined);

                ImGui::SameLine(contentMax.x - 150);

                auto modified = _modified;
                ImGui::BeginDisabled(!modified);
                if (modified) {
                    ImGui::PushStyleColor(ImGuiCol_Button, { 0.5f, 1, 0.5f, .75f });
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.5f, 1, 0.5f, 1 });
                    ImGui::PushStyleColor(ImGuiCol_Text, { 0, 0, 0, 1 });
                }

                if (ImGui::Button("Save Materials", { 150 * Shell::DpiScale, 0 }))
                    OnSave();

                if (modified) ImGui::PopStyleColor(3);
                ImGui::EndDisabled();

                ImGui::Dummy({ 0, 4 });
            }

            MaterialList(listWidth, contentMax.y, topRowHeight);

            MaterialEdit(listWidth, contentMax, topRowHeight);
        }

    private:
        void MaterialList(float width, float height, float topRowHeight) {
            auto searchstr = String::ToLower(string(_search.data()));

            ImGui::BeginChild("list", { width, height - topRowHeight });
            constexpr auto flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
            uint counter = 0;

            if (ImGui::BeginTable("materials", 3, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                ImGui::TableSetupColumn("##Image", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto tableRect = ImGui::GetCurrentWindow()->ClipRect;

                for (int i = 1; i < Resources::GetTextureCount(); i++) {
                    auto id = TexID(i);
                    auto& ti = Resources::GetTextureInfo(id);

                    if (_onlyShowDefined && _table) {
                        if (!_table->Find(ti.Name)) continue;
                    }

                    if (!searchstr.empty()) {
                        if (!String::Contains(String::ToLower(ti.Name), searchstr))
                            continue;
                    }

                    // Filter non-zero animation frames. The material is shared between frames.
                    if (String::Contains(ti.Name, "#") && !String::Contains(ti.Name, "#0"))
                        continue;

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
                    ImRect rowRect = { cursor, { cursor.x + tileSize.x, cursor.y + tileSize.y } };

                    ImGui::TableNextColumn();
                    ImGui::PushID((int)id);
                    constexpr auto selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                    if (ImGui::Selectable("", selected, selectable_flags, ImVec2(0, rowHeight))) {
                        _selection = id;
                    }

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        ApplyTexture(ti.ID);

                    ImGui::GetIO().MouseDown;
                    ImGui::PopID();
                    //bool itemInView = ImGui::GetCurrentWindow()->ClipRect.Contains(ImGui::GetCursorScreenPos());

                    if (material && tableRect.Overlaps(rowRect)) {
                        ImGui::SameLine();
                        ImGui::Image((ImTextureID)material.Pointer(), tileSize, { 0, 0 }, { 1, 1 });
                        counter++;
                    }

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(std::to_string(i).c_str());

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(bmp.Info.Name.c_str());

                    if (ImGui::IsItemVisible() && !Game::IsLoading && _enableLoading) {
                        std::array ids{ id };
                        Render::Materials->LoadMaterialsAsync(ids);
                    }
                }
                ImGui::EndTable();
            }

            ImGui::EndChild();
        }

        //static MaterialInfo* TryFindMaterial(MaterialTable& table, string_view name) {
        //    return Seq::find(table, [name](const MaterialInfo& mat) {
        //        return mat.Name == name;
        //    });
        //}

        void MaterialEdit(float listWidth, const ImVec2& contentMax, float topRowHeight) {
            ImGui::SameLine();

            if (!_table) {
                ImGui::Text("No data table");
                return;
            }

            ImVec2 buttonSize{ 125 * Shell::DpiScale, 0 };

            ImGui::BeginChild("details", { contentMax.x - listWidth - 10, contentMax.y - topRowHeight });

            auto& bmp = Resources::GetBitmap(_selection);
            auto& ti = bmp.Info;
            if (ti.ID > TexID::Invalid) {
                auto& texture = Render::Materials->Get(_selection);

                MaterialInfo defaultMaterial = {};
                auto existing = _table->Find(ti.Name);
                auto merged = Resources::TryGetMaterial(ti.ID);

                if (!existing && merged) {
                    existing = merged;
                }

                if (!existing)
                    existing = &defaultMaterial; // use placeholder until modified

                auto& material = *existing;

                ImVec2 tileSize{ 128 * Shell::DpiScale, 128 * Shell::DpiScale };
                {
                    ImGui::BeginChild("preview", tileSize);
                    auto ratio = ti.Width > 0 && ti.Height > 0 ? (float)ti.Width / (float)ti.Height : 1.0f;
                    if (ratio > 1) tileSize.y /= ratio;
                    if (ratio < 1) tileSize.x *= ratio;
                    ImGui::Image((ImTextureID)texture.Pointer(), tileSize, { 0, 0 }, { 1, 1 });
                    ImGui::EndChild();
                }

                ImGui::SameLine();
                {
                    ImGui::BeginChild("previewdetails", { 0, 128 * Shell::DpiScale });
                    ImGui::Text("%s", ti.Name.c_str());

                    bool discard = false;

                    if (material.Source != TableSource::Undefined && material.Source != TableSource::Descent1) {
                        // Discarding a material removes it from the current table
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Discard")) {
                            _table->Erase(ti.Name);
                            _modified = _table->IsModified(_backupTable);
                            Resources::MergeMaterials(Game::Level);
                            Events::MaterialsChanged();
                            discard = true;
                        }
                    }

                    string label;
                    if (auto ltid = Resources::LookupLevelTexID(ti.ID); (int)ltid != 255)
                        label = fmt::format("Tex ID: {}  Level ID: {}", (int)ti.ID, (int)ltid);
                    else
                        label = fmt::format("Tex ID: {}", (int)ti.ID);

                    ImGui::Text(label.c_str());

                    // material.Source
                    if (!discard && merged && merged->Source != _source && merged->Source != TableSource::Undefined) {
                        ImGui::PushStyleColor(ImGuiCol_Text, { 0, 1, 0, 1 });
                        ImGui::Text("Source: %s", SourceToString(merged->Source).c_str());
                        ImGui::PopStyleColor();
                    }
                    else {
                        ImGui::Text(" ");
                    }

                    //ImGui::Text("Source table: %s", SourceToString(material.Source).c_str());

                    ImGui::Dummy({ 0, 5 * Shell::DpiScale });

                    if (ImGui::Button("Apply texture", buttonSize))
                        ApplyTexture(ti.ID);

                    ImGui::SameLine();
                    if (ImGui::Button("Select overlay", buttonSize))
                        ToggleSelection();
                    ImGui::EndChild();
                }

                //ImGui::SameLine();
                //ImGui::SetNextItemWidth(200);

                //if (ImGui::Combo("##Source", (int*)&_editSource, "Descent 1\0Descent 2\0Mission\0Level")) {
                //    // if changed from default, make a copy of the entry
                //}

                //if (source != originalSource) {
                //    ImGui::SameLine();
                //    ImGui::Button("Reset");
                //    // delete entry from mission or d2, depending on the game selection
                //}

                ImGui::Dummy({ 0, 5 });
                ImGui::Separator();
                ImGui::Dummy({ 0, 5 });

                auto onMaterialChanged = [&] {
                    if (ti.Name.empty()) return;

                    if (material.Source != _source || !_table->Find(ti.Name)) {
                        // source material wasn't defined in this table, insert it
                        SPDLOG_INFO("Adding new material to table `{}`", SourceToString(_source));
                        material = _table->AddOrUpdate(material, ti.Name);
                        material.Source = _source;
                        Resources::MergeMaterials(Game::Level);
                    }

                    material.Modified = true;
                    material.Source = _source;

                    // Update the indexed material so views respond properly.
                    // Indexed materials are uploaded to the GPU.
                    if (auto indexedMaterial = Resources::TryGetMaterial(ti.ID)) {
                        *indexedMaterial = material;
                    }

                    if (ti.Animated)
                        Resources::ExpandAnimatedFrames(ti.ID);

                    _modified = _table->IsModified(_backupTable);

                    Events::MaterialsChanged();
                };

                if (ImGui::Button("Copy", buttonSize)) {
                    _copy = material;
                }

                ImGui::SameLine();
                if (ImGui::Button("Paste", buttonSize)) {
                    material = _copy;
                    onMaterialChanged();
                }

                ImGui::SameLine();
                if (ImGui::Button("Revert", buttonSize)) {
                    if (auto backup = _backupTable.Find(ti.Name)) {
                        material = *backup;

                        // Revert indexed material so views update
                        if (auto indexed = Resources::TryGetMaterial(ti.ID)) {
                            *indexed = *backup;
                        }

                        // Changes to a texture can be reverted after discarding.
                        // If this happens, add the material back to the table.
                        _table->AddOrUpdate(material, ti.Name);
                    }
                    else {
                        // Didn't exist in the backup table, so revert to the indexed table
                        if (auto indexed = Resources::TryGetMaterial(ti.ID)) {
                            material = *indexed;
                        }
                    }

                    _modified = _table->IsModified(_backupTable);

                    if (ti.Animated)
                        Resources::ExpandAnimatedFrames(ti.ID);

                    Events::MaterialsChanged();
                }

                ImGui::Dummy({ 0, 5 });

                constexpr ImGuiTableFlags flags = 0;

                if (ImGui::BeginTable("properties", 2, flags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableRowLabel("Roughness");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##Roughness", &material.Roughness, 0.2f, 1)) {
                        material.Roughness = std::clamp(material.Roughness, 0.0f, 1.0f);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Metalness");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##Metalness", &material.Metalness, 0, 1)) {
                        material.Metalness = std::clamp(material.Metalness, 0.0f, 1.0f);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Normal Strength");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##Normal", &material.NormalStrength, -1, 1)) {
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Specular Strength");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##SpecularStrength", &material.SpecularStrength, 0, 2)) {
                        material.SpecularStrength = std::max(material.SpecularStrength, 0.0f);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Emissive Strength");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##EmissiveStrength", &material.EmissiveStrength, 0, 10)) {
                        material.EmissiveStrength = std::max(material.EmissiveStrength, 0.0f);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Specular Color");
                    ImGui::SetNextItemWidth(-1);

                    if (ImGui::ColorEdit3("##Specular Color", &material.SpecularColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
                        //material.SpecularColor.x
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Envmap Strength");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##EnvPct", &material.SpecularColor.w, 0, 1)) {
                        material.SpecularColor.w = std::max(material.SpecularColor.w, 0.0f);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Light Received");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##LightReceived", &material.LightReceived, 0, 1)) {
                        material.LightReceived = std::max(material.LightReceived, 0.0f);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Additive");
                    ImGui::SetNextItemWidth(-1);
                    auto additive = HasFlag(material.Flags, MaterialFlags::Additive);
                    if (ImGui::Checkbox("##Additive", &additive)) {
                        SetFlag(material.Flags, MaterialFlags::Additive, additive);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Wrap U");
                    ImGui::SetNextItemWidth(-1);
                    auto wrapu = HasFlag(material.Flags, MaterialFlags::WrapU);
                    if (ImGui::Checkbox("##wrapu", &wrapu)) {
                        SetFlag(material.Flags, MaterialFlags::WrapU, wrapu);
                        onMaterialChanged();
                    }

                    ImGui::TableRowLabel("Wrap V");
                    ImGui::SetNextItemWidth(-1);
                    auto wrapv = HasFlag(material.Flags, MaterialFlags::WrapV);
                    if (ImGui::Checkbox("##wrapv", &wrapv)) {
                        SetFlag(material.Flags, MaterialFlags::WrapV, wrapv);
                        onMaterialChanged();
                    }

                    ImGui::EndTable();
                }

                if (auto proc = GetProcedural(ti.ID)) {
                    auto& info = proc->Info.Procedural; // directly modify the procedural info

                    ImGui::SeparatorText("Procedural");
                    if (ImGui::BeginTable("procedural", 2, flags)) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableRowLabel("FPS");
                        auto fps = info.EvalTime > 0 ? int(std::round(1 / info.EvalTime)) : 30;
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderInt("##fps", &fps, 1, 90)) {
                            fps = std::clamp(fps, 1, 90);
                            info.EvalTime = 1 / (float)fps;
                        }

                        if (info.IsWater) {
                            ImGui::TableRowLabel("Thickness");
                            ImGui::SetNextItemWidth(-1);
                            int thickness = info.Thickness;
                            if (ImGui::SliderInt("##Thickness", &thickness, 0, 31)) {
                                info.Thickness = (uint8)std::clamp(thickness, 0, 31);
                            }

                            ImGui::TableRowLabel("Light");
                            ImGui::SetNextItemWidth(-1);
                            int light = info.Light;
                            if (ImGui::SliderInt("##Light", &light, 0, 31)) {
                                info.Light = (uint8)std::clamp(light, 0, 31);
                            }

                            ImGui::TableRowLabel("Oscillate time");
                            ImGui::SetNextItemWidth(-1);
                            ImGui::SliderFloat("##osctime", &info.OscillateTime, 0, 25);

                            ImGui::TableRowLabel("Oscillate value");
                            ImGui::SetNextItemWidth(-1);
                            int oscval = info.OscillateValue;
                            if (ImGui::SliderInt("##oscval", &oscval, 0, 31)) {
                                info.OscillateValue = (uint8)std::clamp(oscval, 0, 31);
                            }
                        }
                        else {
                            ImGui::TableRowLabel("Heat");
                            ImGui::SetNextItemWidth(-1);
                            int heat = info.Heat;
                            if (ImGui::SliderInt("##Heat", &heat, 0, 255)) {
                                info.Heat = (uint8)std::clamp(heat, 0, 255);
                            }
                        }

                        ImGui::EndTable();
                    }
                }
            }

            ImGui::EndChild();
        }

        static void ApplyTexture(TexID id) {
            auto tid = Resources::LookupLevelTexID(id);
            auto& info = Resources::GetTextureInfo(id);
            if (Resources::IsLevelTexture(Game::Level.IsDescent1(), id)) {
                if (info.Transparent)
                    Events::SelectTexture(LevelTexID::None, tid); // overlay
                else
                    Events::SelectTexture(tid, LevelTexID::None);
            }
        }

        void ToggleSelection() {
            if (auto seg = Game::Level.TryGetSegment(Editor::Selection.Segment)) {
                auto [t1, t2] = seg->GetTexturesForSide(Editor::Selection.Side);
                auto tid1 = Resources::LookupTexID(t1);
                auto tid2 = Resources::LookupTexID(t2);

                _selection = _selection != tid2 ? tid2 : tid1;
            }
        }
    };
}
