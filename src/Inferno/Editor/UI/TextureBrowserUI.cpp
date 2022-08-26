#include "pch.h"
#include "TextureBrowserUI.h"
#include "../Editor.h"
#include "Graphics/Render.h"
#include "Resources.h"

namespace Inferno::Editor {
    struct TextureFilter {
        short Min, Max;
        FilterGroup Group;
    };

    constexpr FilterGroup ParseFilterGroup(const string& group) {
        switch (String::Hash(group)) {
            case String::Hash("GrayRock"): return FilterGroup::GrayRock;
            case String::Hash("BrownRock"): return FilterGroup::BrownRock;
            case String::Hash("RedRock"): return FilterGroup::RedRock;
            case String::Hash("GreenRock"): return FilterGroup::GreenRock;
            case String::Hash("YellowRock"): return FilterGroup::YellowRock;
            case String::Hash("BlueRock"): return FilterGroup::BlueRock;
            case String::Hash("Ice"): return FilterGroup::Ice;
            case String::Hash("Stones"): return FilterGroup::Stones;
            case String::Hash("Grass"): return FilterGroup::Grass;
            case String::Hash("Sand"): return FilterGroup::Sand;
            case String::Hash("Lava"): return FilterGroup::Lava;
            case String::Hash("Water"): return FilterGroup::Water;
            case String::Hash("Steel"): return FilterGroup::Steel;
            case String::Hash("Concrete"): return FilterGroup::Concrete;
            case String::Hash("Brick"): return FilterGroup::Brick;
            case String::Hash("Tarmac"): return FilterGroup::Tarmac;
            case String::Hash("Wall"): return FilterGroup::Wall;
            case String::Hash("Floor"): return FilterGroup::Floor;
            case String::Hash("Ceiling"): return FilterGroup::Ceiling;
            case String::Hash("Grate"): return FilterGroup::Grate;
            case String::Hash("Fan"): return FilterGroup::Fan;
            case String::Hash("Light"): return FilterGroup::Light;
            case String::Hash("Energy"): return FilterGroup::Energy;
            case String::Hash("Forcefield"): return FilterGroup::Forcefield;
            case String::Hash("Sign"): return FilterGroup::Sign;
            case String::Hash("Switch"): return FilterGroup::Switch;
            case String::Hash("Tech"): return FilterGroup::Tech;
            case String::Hash("Door"): return FilterGroup::Door;
            case String::Hash("Label"): return FilterGroup::Label;
            case String::Hash("Monitor"): return FilterGroup::Monitor;
            case String::Hash("Stripes"): return FilterGroup::Stripes;
            case String::Hash("Moving"): return FilterGroup::Moving;
            default:
                SPDLOG_WARN("Unknown filter group: {}", group);
                return FilterGroup::None;
        };
    }

    List<TextureFilter> ParseFilter(filesystem::path path) {
        List<TextureFilter> filters;

        try {
            std::ifstream file(path);
            if (!file) throw Exception("Unable to read filter file");

            string line;
            while (std::getline(file, line)) {
                // format: "0-0 GrayRock|Concrete"
                auto tokens = String::Split(line, ' ');
                if (tokens.size() != 2) {
                    SPDLOG_WARN("Expected two tokens in texture filter line: {}", line);
                    continue;
                }

                FilterGroup group = FilterGroup::None;
                for (auto& g : String::Split(tokens[1], '|'))
                    group |= ParseFilterGroup(g);

                auto rangeTokens = String::Split(tokens[0], '-');

                filters.push_back(TextureFilter{
                    .Min = (short)std::stoi(rangeTokens[0]),
                    .Max = (short)std::stoi(rangeTokens[1]),
                    .Group = group });
            }
        }
        catch (...) {
            SPDLOG_ERROR(L"Error reading texture filter from `{}`", path.wstring());
        }

        return filters;
    }

    List<TextureFilter> D1Filter, D2Filter;

    Set<LevelTexID> GetInUseBaseTextures(const Level& level) {
        Set<LevelTexID> texIds;

        for (auto& seg : level.Segments) {
            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);
                if (!seg.SideHasConnection(sideId) || seg.SideIsWall(sideId))
                    texIds.insert(side.TMap);

                if (side.HasOverlay())
                    texIds.insert(side.TMap2);
            }
        }

        return texIds;
    }

    List<LevelTexID> FilterLevelTextures(FilterGroup filter, bool showInUse, bool showEverything) {
        Set<LevelTexID> ids;

        if (showEverything) {
            for (int i = 0; i < Resources::GameData.TexInfo.size(); i++) {
                ids.insert(LevelTexID(i));
            }

            return Seq::ofSet(ids);
        }

        for (auto& entry : Game::Level.IsDescent1() ? D1Filter : D2Filter) {
            if (!bool(entry.Group & filter)) continue;

            for (int16 i = entry.Min; i <= entry.Max; i++) {
                LevelTexID id{ i };
                auto& info = Resources::GetTextureInfo(id);
                if (info.Frame == 0) // omit frames of doors
                    ids.insert(id);
            }
        }

        if (showInUse) {
            auto inUse = GetInUseBaseTextures(Game::Level);
            for (auto& x : inUse) ids.insert(x);
        }

        return Seq::ofSet(ids);
    }

    void TextureBrowserUI::UpdateTextureList(FilterGroup filter, bool loadMaterials) {
        //SPDLOG_INFO("Updating texture browser");
        auto ids = FilterLevelTextures(filter, _showInUse, _showEverything);
        auto tids = Seq::map(ids, Resources::LookupLevelTexID);
        if (loadMaterials)
            Render::Materials->LoadMaterialsAsync(tids);

        // Update ids immediately. They will display as loading completes.
        _textureIds.clear();
        Seq::append(_textureIds, ids);
        Seq::insert(Render::Materials->KeepLoaded, tids); // so browser textures don't get discarded after a prune
    }

    TextureBrowserUI::TextureBrowserUI() : WindowBase("Textures", &Settings::Windows.Textures) {
        Events::LevelLoaded += [this] { UpdateTextureList(_filter, true); };
        Events::LevelChanged += [this] { UpdateTextureList(_filter, false); };

        D1Filter = ParseFilter("d1filter.txt");
        D2Filter = ParseFilter("d2filter.txt");
    }

    void TextureBrowserUI::DrawFilter() {
        constexpr int ColumnWidth = 170;
        float contentWidth = ImGui::GetWindowContentRegionMax().x;
        float availableWidth = ImGui::GetWindowPos().x + contentWidth;
        bool twoColumn = availableWidth >= ColumnWidth * 2 - 20; // + padding

        if (ImGui::Checkbox("Show in use textures", &_showInUse))
            UpdateTextureList(_filter, true);

        if (ImGui::Checkbox("Show everything", &_showEverything))
            UpdateTextureList(_filter, true);

        ImGui::HelpMarker("This includes animation frames and textures\nnot in the normal filters");

        auto& s = _state;
        constexpr auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap;

        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        bool isOpen = ImGui::TreeNodeEx("##filters", flags);
        ImGui::SameLine();
        bool allChecked = s.SelectAll();
        if (ImGui::Checkbox("##toggle", &allChecked))
            s.SelectAll(allChecked);
        ImGui::SameLine();
        ImGui::Text("Filters");

        if (isOpen) {
            auto ToggleGroupButtons = [&](const char* label, std::function<void(bool)> fn, std::function<bool(void)> getCheckState) {
                ImGui::PushID(label);
                ImGui::AlignTextToFramePadding();
                bool open = ImGui::TreeNodeEx("##label", flags);
                ImGui::SameLine();
                bool checked = getCheckState();
                if (ImGui::Checkbox("##toggle", &checked))
                    fn(checked);
                ImGui::SameLine();
                ImGui::Text(label);
                ImGui::PopID();
                return open;
            };

            if (ToggleGroupButtons("Rock", [&s](bool b) { s.SelectRock(b); }, [&s] { return s.SelectRock(); })) {
                ImGui::Checkbox("Gray Rock", &s.GrayRock);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Brown Rock", &s.BrownRock);

                ImGui::Checkbox("Green Rock", &s.GreenRock);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Yellow Rock", &s.YellowRock);

                ImGui::Checkbox("Blue Rock", &s.BlueRock);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Red Rock", &s.RedRock);

                ImGui::TreePop();
            }

            if (ToggleGroupButtons("Natural Materials", [&s](bool b) { s.SelectNatural(b); }, [&s] { return s.SelectNatural(); })) {
                ImGui::Checkbox("Ice", &s.Ice);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Stones", &s.Stones);
                ImGui::Checkbox("Grass", &s.Grass);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Sand", &s.Sand);
                ImGui::Checkbox("Lava", &s.Lava);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Water", &s.Water);
                ImGui::TreePop();
            }

            if (ToggleGroupButtons("Structural Materials", [&s](bool b) { s.SelectBuilding(b); }, [&s] { return s.SelectBuilding(); })) {
                ImGui::Checkbox("Steel", &s.Steel);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Concrete", &s.Concrete);
                ImGui::Checkbox("Bricks", &s.Brick);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Tarmac", &s.Tarmac);
                ImGui::Checkbox("Walls", &s.Wall);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Floors", &s.Floor);
                ImGui::Checkbox("Ceilings", &s.Ceiling);
                ImGui::TreePop();
            }

            if (ToggleGroupButtons("Doors, Fans and Grates", [&s](bool b) { s.SelectMisc(b); }, [&s] { return s.SelectMisc(); })) {
                ImGui::Checkbox("Grates", &s.Grate);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Fans", &s.Fan);
                ImGui::Checkbox("Doors", &s.Door);
                ImGui::TreePop();
            }

            if (ToggleGroupButtons("Technical Materials", [&s](bool b) { s.SelectTechnical(b); }, [&s] { return s.SelectTechnical(); })) {
                ImGui::Checkbox("Lights", &s.Light);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Energy", &s.Energy);
                ImGui::Checkbox("Forcefield", &s.ForceField);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Tech", &s.Tech);
                ImGui::Checkbox("Switches", &s.Switches);
                ImGui::TreePop();
            }

            if (ToggleGroupButtons("Signs and Monitors", [&s](bool b) { s.SelectSigns(b); }, [&s] { return s.SelectSigns(); })) {
                ImGui::Checkbox("Labels", &s.Labels);
                if (twoColumn) ImGui::SameLine(ColumnWidth, -1);
                ImGui::Checkbox("Monitors", &s.Monitors);
                ImGui::Checkbox("Stripes", &s.Stripes);
                ImGui::TreePop();
            }

            ImGui::TreePop();
        }
        ImGui::Separator();

        auto newState = s.GetState();
        if (newState != _filter) {
            _filter = newState;
            UpdateTextureList(_filter, true);
        }
    }

    void TextureBrowserUI::OnUpdate() {
        float contentWidth = ImGui::GetWindowContentRegionMax().x;
        float availableWidth = ImGui::GetWindowPos().x + contentWidth;

        DrawFilter();

        LevelTexID tmap1 = LevelTexID::None, tmap2 = LevelTexID::Unset;
        if (auto seg = Game::Level.TryGetSegment(Editor::Selection.Segment)) {
            std::tie(tmap1, tmap2) = seg->GetTexturesForSide(Editor::Selection.Side);
        }

        {
            auto overlayText = tmap2 <= LevelTexID(0) ? "None" : std::to_string((int)tmap2);
            ImGui::Text("Base: %i Overlay: %s", tmap1, overlayText.c_str());
            if (tmap2 > LevelTexID(0)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear"))
                    Events::SelectTexture(LevelTexID::None, LevelTexID::Unset);
            }
            ImGui::Separator();
        }

        // Don't draw any textures when a new level is loading
        if (Game::IsLoading) return;

        ImGui::BeginChild("textures");

        ImGuiStyle& style = ImGui::GetStyle();
        auto count = (uint)_textureIds.size();
        uint i = 0;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2, 2 });

        ImVec2 tileSize{};
        switch (Settings::TexturePreviewSize) {
            case TexturePreviewSize::Small: tileSize = { 48, 48 }; break;
            case TexturePreviewSize::Large: tileSize = { 96, 96 }; break;
            default: tileSize = { 64, 64 };
        }

        tileSize.x *= Shell::DpiScale;
        tileSize.y *= Shell::DpiScale;

        const ImVec4 bg = { 0.1f, 0.1f, 0.1f, 1.0f };
        constexpr int borderThickess = 2;

        for (auto& id : _textureIds) {
            auto& material = Render::Materials->Get(id);
            if (material.ID <= TexID::Invalid) continue; // don't show invalid textures (usually TID 910)

            //ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 2, 2 });
            //ImGui::PushStyleColor(ImGuiCol_BorderShadow, { 1, 0, 0, 1 });

            ImVec4 borderColor =
                id == tmap1 ? ImVec4(1, 1, 1, 0.8f) :
                (id == tmap2 && tmap2 > LevelTexID(0) ? ImVec4(0, 1, 1, 0.8f) : ImVec4(1, 1, 1, 0));

            ImGui::PushStyleColor(ImGuiCol_Button, borderColor);

            ImGui::ImageButton((ImTextureID)material.Handles[0].ptr, tileSize, { 0, 0 }, { 1, 1 }, borderThickess, bg);

            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    tmap1 = id;
                    Events::SelectTexture(tmap1, LevelTexID::None);
                    Events::TextureInfo(id);
                    Render::LoadTextureDynamic(id);
                }
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    tmap2 = id;
                    auto tm1 = LevelTexID::None;
                    if (tmap1 == id) {
                        tm1 = id;
                        tmap2 = LevelTexID::Unset;
                    }

                    Events::SelectTexture(tm1, tmap2);
                    Events::TextureInfo(id);
                    Render::LoadTextureDynamic(id);
                }
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                    Events::TextureInfo(id);
                }
            }

            ImGui::PopStyleColor();
            //ImGui::PopStyleVar();

            float spacing = style.ItemSpacing.x / 2.0f;
            float xLast = ImGui::GetItemRectMax().x;
            float xNext = xLast + spacing + tileSize.x; // Expected position if next button was on same line
            if (i + 1 < count && xNext < availableWidth)
                ImGui::SameLine(0, spacing);

            i++;
        };

        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
}
