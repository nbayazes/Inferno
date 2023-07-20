#pragma once
#include "Procedural.h"
#include "WindowBase.h"

namespace Inferno::Editor {
    class MaterialEditor final : public WindowBase {
        TexID _selection = TexID{ 1 };
        List<TexID> _visibleTextures;
        List<char> _search;
        MaterialInfo _copy = {};
        List<MaterialInfo> _backup;
        bool _enableLoading = true;

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

        void OnSave() {
            try {
                SPDLOG_INFO("Saving materials");
                // todo: save to hog toggle?
                auto name = Resources::GetMaterialFileName(Game::Level);
                std::ofstream stream(name);
                auto materials = Render::Materials->GetAllMaterialInfo();
                SaveMaterialTable(stream, materials);
                _backup = Seq::toList(materials);
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }
        }

        void OnUpdate() override {
            const float listWidth = 250 * Shell::DpiScale;
            const float topRowHeight = 100 * Shell::DpiScale;
            auto searchstr = String::ToLower(string(_search.data()));

            auto contentMax = ImGui::GetWindowContentRegionMax();
            ImVec2 buttonSize{ 125 * Shell::DpiScale, 0 };

            ImGui::Text("Search");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200 * Shell::DpiScale);
            ImGui::InputText("##Search", _search.data(), _search.capacity());

            //ImGui::SameLine();
            //ImGui::Checkbox("Enable loading", &_enableLoading);

            ImGui::SameLine(contentMax.x - 150);
            if (ImGui::Button("Save All", { 150 * Shell::DpiScale, 0 }))
                OnSave();

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

                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            ApplyTexture(ti.ID);

                        ImGui::GetIO().MouseDown;
                        ImGui::PopID();

                        if (material) {
                            ImGui::SameLine();
                            ImGui::Image((ImTextureID)material.Pointer(), tileSize, { 0, 0 }, { 1, 1 });
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
                        ImGui::Image((ImTextureID)texture.Pointer(), tileSize, { 0, 0 }, { 1, 1 });
                        ImGui::EndChild();
                    }

                    ImGui::SameLine();
                    {
                        ImGui::BeginChild("previewdetails", tileSize);
                        ImGui::Text(ti.Name.c_str());
                        auto label = fmt::format("Tex ID: {}", ti.ID);
                        ImGui::Text(label.c_str());
                        if (ImGui::Button("Apply texture", buttonSize))
                            ApplyTexture(ti.ID);

                        ImGui::EndChild();
                    }

                    ImGui::Dummy({ 0, 5 });
                    ImGui::Separator();
                    ImGui::Dummy({ 0, 5 });

                    if (ImGui::Button("Copy", buttonSize)) {
                        _copy = material;
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Paste", buttonSize)) {
                        material = _copy;
                        Events::LevelChanged();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Revert", buttonSize)) {
                        if (Seq::inRange(_backup, (int)ti.ID))
                            material = _backup[(int)ti.ID];
                        Events::LevelChanged();
                    }

                    ImGui::Dummy({ 0, 5 });

                    constexpr ImGuiTableFlags flags = 0;

                    if (ImGui::BeginTable("properties", 2, flags)) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                        auto onMaterialChanged = [&] {
                            material.ID = (int)ti.ID;
                            Events::LevelChanged();
                        };

                        ImGui::TableRowLabel("Roughness");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##Roughness", &material.Roughness, 0.3, 1)) {
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
                        if (ImGui::SliderFloat("##Normal", &material.NormalStrength, -2, 2)) {
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

                        ImGui::TableRowLabel("Light Received");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##LightReceived", &material.LightReceived, 0, 1)) {
                            material.LightReceived = std::max(material.LightReceived, 0.0f);
                            onMaterialChanged();
                        }

                        ImGui::TableRowLabel("Additive");
                        ImGui::SetNextItemWidth(-1);
                        auto additive = (bool)material.Additive;
                        if (ImGui::Checkbox("##Additive", &additive)) {
                            material.Additive = additive;
                            Events::LevelChanged(); // Rebuild meshes due to blend mode changing
                        }

                        ImGui::EndTable();
                    }

                    if (auto proc = GetProcedural(ti.ID)) {
                        auto& info = proc->Info.Procedural;
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
        }

    private:
        void ApplyTexture(TexID id) const {
            auto tid = Resources::LookupLevelTexID(id);
            auto& info = Resources::GetTextureInfo(id);
            if (Resources::IsLevelTexture(id)) {
                if (info.Transparent)
                    Events::SelectTexture(LevelTexID::None, tid); // overlay
                else
                    Events::SelectTexture(tid, LevelTexID::None);
            }
        }
    };
}
