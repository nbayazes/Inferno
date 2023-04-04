#pragma once
#include "WindowBase.h"

namespace Inferno::Editor {
    class MaterialEditor final : public WindowBase {
        TexID _selection = TexID{ 1 };
        List<TexID> _visibleTextures;
        List<char> _search;
        MaterialInfo _copy = {};
        List<MaterialInfo> _backup;

    public:
        MaterialEditor() : WindowBase("Material Editor", &Settings::Editor.Windows.MaterialEditor) {
            Events::SelectTexture += [this](LevelTexID tmap1, LevelTexID tmap2) {
                if (tmap1 > LevelTexID::None) _selection = Resources::LookupTexID(tmap1);
                if (tmap2 > LevelTexID::None) _selection = Resources::LookupTexID(tmap2);
            };

            Events::SelectSegment += [this] {
                const auto& seg = Game::Level.GetSegment(Editor::Selection.Segment);
                auto [t1, t2] = seg.GetTexturesForSide(Editor::Selection.Side);
                _selection = Resources::LookupTexID(t1);
            };

            Events::LevelLoaded += [this] {
                _backup = Seq::toList(Render::Materials->GetAllMaterialInfo());
            };

            _search.resize(20);
            ranges::fill(_search, 0);
        }

        void OnUpdate() {
            //auto contentMax = ImGui::GetWindowContentRegionMax();
            const float listWidth = 250 * Shell::DpiScale;
            const float topRowHeight = 100 * Shell::DpiScale;
            auto searchstr = String::ToLower(string(_search.data()));

            auto contentMax = ImGui::GetWindowContentRegionMax();
            ImVec2 buttonSize{ 125 * Shell::DpiScale, 0 };

            ImGui::Text("Search");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200 * Shell::DpiScale);
            ImGui::InputText("##Search", _search.data(), _search.capacity());

            ImGui::SameLine(contentMax.x - buttonSize.x);
            ImGui::Button("Save All", { 150 * Shell::DpiScale, 0 });

            ImGui::Dummy({ 0, 4 });


            {
                ImGui::BeginChild("list", { listWidth, contentMax.y - topRowHeight });
                constexpr auto flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;

                if (ImGui::BeginTable("materials", 3, flags)) {
                    ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                    ImGui::TableSetupColumn("##Image", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (int i = 1; i < Resources::GetTextureCount(); i++) {
                        auto id = TexID(i);
                        auto& ti = Resources::GetTextureInfo(id);

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
                        ImGui::Text(std::to_string(i).c_str());

                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text(bmp.Info.Name.c_str());

                        if (ImGui::IsItemVisible()) {
                            std::array ids{ id };
                            Render::Materials->LoadMaterialsAsync(ids);
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::EndChild();
            }

            {
                ImGui::SameLine();
                ImGui::BeginChild("details", { contentMax.x - listWidth - 10, contentMax.y - topRowHeight });

                auto& bmp = Resources::GetBitmap(_selection);
                auto& ti = bmp.Info;
                if (ti.ID > TexID::Invalid) {
                    auto& texture = Render::Materials->Get(_selection);
                    auto& material = Render::Materials->GetMaterialInfo(ti.ID);

                    ImVec2 tileSize{ 128 * Shell::DpiScale, 128 * Shell::DpiScale };
                    {
                        ImGui::BeginChild("preview", tileSize);
                        auto ratio = ti.Width > 0 && ti.Height > 0 ? (float)ti.Width / (float)ti.Height : 1.0f;
                        if (ratio > 1) tileSize.y /= ratio;
                        if (ratio < 1) tileSize.x *= ratio;
                        ImGui::Image((ImTextureID)texture.Handles[0].ptr, tileSize, { 0, 0 }, { 1, 1 });
                        ImGui::EndChild();
                    }

                    ImGui::SameLine();
                    {
                        ImGui::BeginChild("previewdetails", tileSize);
                        ImGui::Text(ti.Name.c_str());
                        auto label = fmt::format("Tex ID: {}", ti.ID);
                        ImGui::Text(label.c_str());
                        ImGui::EndChild();
                    }

                    ImGui::Dummy({ 0, 5 });

                    if (ImGui::Button("Copy", buttonSize)) {
                        _copy = material;
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Paste", buttonSize)) {
                        material = _copy;
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Revert", buttonSize)) {
                        if(Seq::inRange(_backup, (int)ti.ID))
                            material = _backup[(int)ti.ID];
                    }

                    ImGui::Dummy({ 0, 5 });

                    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable;

                    if (ImGui::BeginTable("properties", 2, flags)) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableRowLabel("Roughness");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##Roughness", &material.Roughness, 0.3, 1)) {
                            material.Roughness = std::clamp(material.Roughness, 0.0f, 1.0f);
                            Events::LevelChanged();
                        }

                        ImGui::TableRowLabel("Metalness");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##Metalness", &material.Metalness, 0, 1)) {
                            material.Metalness = std::clamp(material.Metalness, 0.0f, 1.0f);
                            Events::LevelChanged();
                        }

                        ImGui::TableRowLabel("Normal Strength");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##Normal", &material.NormalStrength, -2, 2)) {
                            Events::LevelChanged();
                        }

                        ImGui::TableRowLabel("Specular Strength");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##SpecularStrength", &material.SpecularStrength, 0, 2)) {
                            material.SpecularStrength = std::max(material.SpecularStrength, 0.0f);
                            Events::LevelChanged();
                        }

                        ImGui::TableRowLabel("Emissive Strength");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##EmissiveStrength", &material.EmissiveStrength, 0, 10)) {
                            material.EmissiveStrength = std::max(material.EmissiveStrength, 0.0f);
                            Events::LevelChanged();
                        }

                        ImGui::EndTable();
                    }
                }

                ImGui::EndChild();
            }
        }

        //void UpdateTextureList() {
        //    
        //}
    };
}
