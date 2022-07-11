#include "pch.h"
#include <streambuf>
#include "LevelSettings.h"
#include "Yaml.h"

using namespace Yaml;

namespace Inferno {
    void SaveSideInfo(ryml::NodeRef node, const Level& level) {
        node |= ryml::SEQ;

        for (int segid = 0; segid < level.Segments.size(); segid++) {
            auto& seg = level.Segments[segid];
            for (auto& sideid : SideIDs) {
                auto& side = seg.GetSide(sideid);
                Tag tag((SegID)segid, sideid);

                bool hasLockLight = side.LockLight[0] || side.LockLight[1] || side.LockLight[2] || side.LockLight[3];

                if (side.LightOverride || hasLockLight) {
                    auto child = node.append_child();
                    child |= ryml::MAP;
                    child["Tag"] << EncodeTag(tag);

                    if (side.LightOverride)
                        child["LightColor"] << EncodeColor(*side.LightOverride);

                    if (side.LightRadiusOverride)
                        child["LightRadius"] << *side.LightRadiusOverride;

                    if (side.LightPlaneOverride)
                        child["LightPlane"] << *side.LightPlaneOverride;

                    if (hasLockLight)
                        child["LockLight"] << EncodeArray(side.LockLight);
                }
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
                    float plane{};
                    ReadValue(child["LightPlane"], plane);
                    side->LightPlaneOverride = plane;
                }

                if (child.has_child("LockLight"))
                    ReadValue(child["LockLight"], side->LockLight);
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
            WallID id = WallID::None;
            ReadValue(child["ID"], (int16&)id);

            if (auto wall = level.TryGetWall(id)) {
                bool blocksLight = false;
                ReadValue(child["BlocksLight"], blocksLight);
                wall->BlocksLight = blocksLight;
            }
        }
    }

    void LoadLevelMetadata(Level& level, const string& data) {
        try {
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(data));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                Settings::Lighting = Settings::LoadLightSettings(root["Lighting"]);
                ReadSegmentInfo(root["Segments"], level);
                ReadSideInfo(root["Sides"], level);
                ReadWallInfo(root["Walls"], level);
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading level metadata:\n{}", e.what());
        }
    }

    void SaveLevelMetadata(const Level& level, std::ostream& stream) {
        try {
            ryml::Tree doc(30, 128);
            doc.rootref() |= ryml::MAP;

            doc["Version"] << 1;
            Settings::SaveLightSettings(doc["Lighting"]);
            SaveSegmentInfo(doc["Segments"], level);
            SaveSideInfo(doc["Sides"], level);
            SaveWallInfo(doc["Walls"], level);
            stream << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving level metadata:\n{}", e.what());
        }
    }
}