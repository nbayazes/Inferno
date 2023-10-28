#include "pch.h"
#include "LevelSettings.h"
#include "imgui.h"
#include "Yaml.h"
#include "Resources.h"
#include "Settings.h"

using namespace Yaml;

namespace Inferno {
    // Declared in settings
    void SaveLightSettings(ryml::NodeRef node, const LightSettings& s);
    LightSettings LoadLightSettings(ryml::NodeRef node);

    void SaveSideInfo(ryml::NodeRef node, const Level& level) {
        node |= ryml::SEQ;

        for (int segid = 0; segid < level.Segments.size(); segid++) {
            auto& seg = level.Segments[segid];
            for (auto& sideid : SideIDs) {
                auto& side = seg.GetSide(sideid);
                Tag tag((SegID)segid, sideid);

                bool isLightSource = side.LightOverride.has_value();
                if (!isLightSource) {
                    auto& ti2 = Resources::GetLevelTextureInfo(side.TMap2);
                    if (ti2.Lighting > 0) isLightSource = true;

                    auto& ti = Resources::GetLevelTextureInfo(side.TMap);
                    if (ti.Lighting > 0) isLightSource = true;
                }

                bool hasLockLight = side.LockLight[0] || side.LockLight[1] || side.LockLight[2] || side.LockLight[3];

                // Don't write sides that aren't light sources and don't have vertex overrides
                if (!isLightSource && !hasLockLight) continue;

                // Check that any properties are modified before writing
                if (!side.LightOverride &&
                    !hasLockLight &&
                    side.EnableOcclusion &&
                    !side.LightRadiusOverride &&
                    !side.LightPlaneOverride &&
                    side.LightMode == DynamicLightMode::Constant &&
                    !side.DynamicMultiplierOverride)
                    continue;

                auto child = node.append_child();
                child |= ryml::MAP;
                child["Tag"] << EncodeTag(tag);

                if (side.LightOverride)
                    child["LightColor"] << EncodeColor(*side.LightOverride);

                if (side.LightRadiusOverride)
                    child["LightRadius"] << *side.LightRadiusOverride;

                if (side.LightPlaneOverride)
                    child["LightPlane"] << *side.LightPlaneOverride;

                if (side.LightMode != DynamicLightMode::Constant)
                    child["LightMode"] << (int)side.LightMode;

                if (!side.EnableOcclusion) // Only save when false
                    child["Occlusion"] << side.EnableOcclusion;

                if (hasLockLight)
                    child["LockLight"] << EncodeArray(side.LockLight);

                if (side.DynamicMultiplierOverride)
                    child["DynamicMultiplier"] << *side.DynamicMultiplierOverride;
            }
        }
    }

    void ReadSideInfo(ryml::NodeRef node, Level& level) {
        if (!node.valid() || node.is_seed()) return;

        for (const auto& child : node.children()) {
            Tag tag;
            ReadValue(child["Tag"], tag);

            if (auto side = level.TryGetSide(tag)) {
                if (child.has_child("LightColor")) {
                    Color color;
                    ReadValue(child["LightColor"], color);
                    side->LightOverride = color;
                }

                if (child.has_child("LightRadius")) {
                    float radius{};
                    ReadValue(child["LightRadius"], radius);
                    side->LightRadiusOverride = radius;
                }

                if (child.has_child("LightPlane")) {
                    float value{};
                    ReadValue(child["LightPlane"], value);
                    side->LightPlaneOverride = value;
                }

                if (child.has_child("LightMode")) {
                    int value{};
                    ReadValue(child["LightMode"], value);
                    side->LightMode = (DynamicLightMode)value;
                }

                if (child.has_child("Occlusion"))
                    ReadValue(child["Occlusion"], side->EnableOcclusion);

                if (child.has_child("LockLight"))
                    ReadValue(child["LockLight"], side->LockLight);

                if (child.has_child("DynamicMultiplier")) {
                    float value{};
                    ReadValue(child["DynamicMultiplier"], value);
                    side->DynamicMultiplierOverride = value;
                }
            }
        }
    }

    void SaveSegmentInfo(ryml::NodeRef node, const Level& level) {
        node |= ryml::SEQ;

        for (int segid = 0; segid < level.Segments.size(); segid++) {
            auto& seg = level.Segments[segid];

            if (seg.LockVolumeLight) {
                auto child = node.append_child();
                child |= ryml::MAP;
                child["ID"] << segid;
                child["LockVolumeLight"] << seg.LockVolumeLight;
            }
        }
    }

    void ReadSegmentInfo(ryml::NodeRef node, Level& level) {
        if (!node.valid() || node.is_seed()) return;

        for (const auto& child : node.children()) {
            int id;
            ReadValue(child["ID"], id);

            if (auto seg = level.TryGetSegment(SegID(id))) {
                if (child.has_child("LockVolumeLight"))
                    ReadValue(child["LockVolumeLight"], seg->LockVolumeLight);
            }
        }
    }

    void SaveWallInfo(ryml::NodeRef node, const Level& level) {
        node |= ryml::SEQ;

        for (int id = 0; id < level.Walls.size(); id++) {
            auto& wall = level.Walls[id];
            if (wall.BlocksLight) {
                auto child = node.append_child();
                child |= ryml::MAP;
                child["ID"] << id;
                child["BlocksLight"] << *wall.BlocksLight;
            }
        }
    }

    void ReadWallInfo(ryml::NodeRef node, Level& level) {
        if (!node.valid() || node.is_seed()) return;

        for (const auto& child : node.children()) {
            auto id = WallID::None;
            ReadValue(child["ID"], (int16&)id);

            if (auto wall = level.TryGetWall(id)) {
                bool blocksLight = false;
                ReadValue(child["BlocksLight"], blocksLight);
                wall->BlocksLight = blocksLight;
            }
        }
    }

    constexpr int SEGMENT_LIGHT_VALUES = 1 + 4 * 6;

    List<Color> ParseSegmentLighting(const string& line) {
        List<string> tokens;

        {
            bool inColorToken = false;
            string token;

            for (auto& c : line) {
                if (c == '[') {
                    inColorToken = true;
                    token = "";
                }
                else if (inColorToken) {
                    if (c == ']') {
                        inColorToken = false;
                        tokens.push_back(token);
                    }
                    else {
                        token += c;
                    }
                }
                else {
                    // not in a token
                    if (c == '0') {
                        tokens.push_back(""); // empty element
                        tokens.push_back(""); // empty element
                        tokens.push_back(""); // empty element
                        tokens.push_back(""); // empty element
                    }
                    // do nothing with whitespace
                }
            }
        }

        ASSERT(tokens.size() == SEGMENT_LIGHT_VALUES);

        List<Color> colors;
        for (auto& token : tokens) {
            if (token.empty()) {
                colors.push_back({});
                continue;
            }

            auto channels = Inferno::String::Split(token, ',', true);

            float rgb[3]{};
            for (int i = 0; i < channels.size(); i++)
                ParseFloat(channels[i], rgb[i]);

            colors.push_back({ rgb[0], rgb[1], rgb[2] });
        }

        ASSERT(colors.size() == SEGMENT_LIGHT_VALUES);
        return colors;
    }

    void ReadLevelLighting(ryml::NodeRef node, Level& level) {
        if (!node.valid() || node.is_seed()) return;

        int segid = 0;
        for (const auto& child : node.children()) {
            string line;
            child >> line;

            auto colors = ParseSegmentLighting(line);

            if (colors.size() != SEGMENT_LIGHT_VALUES) {
                SPDLOG_WARN("Unexpected number of color light elements, skipping seg {}", segid);
                continue;
            }

            auto& seg = level.Segments[segid];
            seg.VolumeLight = colors[0];
            for (int i = 0; i < 6; i++) {
                for (auto j = 0; j < 4; j++) {
                    seg.Sides[i].Light[j] = colors[1 + 4 * i + j];
                }
            }

            segid++;
            if (segid >= level.Segments.size())
                break;
        }

        if (segid > 0)
            SPDLOG_INFO("Loaded color lighting for {} segments", segid);
    }

    void SaveLevelLighting(ryml::NodeRef node, const Level& level) {
        // Array of colors. First value is volume light. Followed by six x4 vertex light colors.
        // 0 skips the side
        // [1, 1, 1], 0, [3, 0, 1], [0.11, 0.22, 0.33], ...

        node |= ryml::SEQ;

        auto encodeColor = [](const Color& color) {
            return fmt::format("[{:.3g},{:.3g},{:.3g}]", color.x, color.y, color.z);
        };

        for (auto& seg : level.Segments) {
            string line = encodeColor(seg.VolumeLight);
            line.reserve(256);

            for (auto& sideid : SideIDs) {
                auto& side = seg.GetSide(sideid);

                if (seg.SideHasConnection(sideid) && side.Wall == WallID::None) {
                    // Write 0 for open side with no wall
                    line += ",0";
                }
                else {
                    for (auto& light : side.Light) {
                        line += "," + encodeColor(light);
                    }
                }
            }

            node.append_child() << line;
        }
    }

    void SaveLevelMetadata(const Level& level, std::ostream& stream) {
        try {
            ryml::Tree doc(30, 128);
            doc.rootref() |= ryml::MAP;

            doc["Version"] << 1;
            SaveLightSettings(doc["Lighting"], Settings::Editor.Lighting);
            SaveSegmentInfo(doc["Segments"], level);
            SaveSideInfo(doc["Sides"], level);
            SaveWallInfo(doc["Walls"], level);

            if (level.CameraUp != Vector3::Zero) {
                doc["CameraPosition"] << EncodeVector(level.CameraPosition);
                doc["CameraTarget"] << EncodeVector(level.CameraTarget);
                doc["CameraUp"] << EncodeVector(level.CameraUp);
            }

            SaveLevelLighting(doc["LevelLighting"], level);

            stream << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving level metadata:\n{}", e.what());
        }
    }

    void LoadLevelMetadata(Level& level, const string& data) {
        try {
            SPDLOG_INFO("Loading level metadata");
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(data));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                Settings::Editor.Lighting = LoadLightSettings(root["Lighting"]);
                ReadSegmentInfo(root["Segments"], level);
                ReadSideInfo(root["Sides"], level);
                ReadWallInfo(root["Walls"], level);
                ReadValue(root["CameraPosition"], level.CameraPosition);
                ReadValue(root["CameraTarget"], level.CameraTarget);
                ReadValue(root["CameraUp"], level.CameraUp);
                ReadLevelLighting(root["LevelLighting"], level);
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading level metadata:\n{}", e.what());
        }
    }
}
