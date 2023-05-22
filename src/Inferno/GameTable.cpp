#include "pch.h"
#include <fstream>

#include "HamFile.h"
#include "Yaml.h"
#include "Graphics/Render.Particles.h"

namespace Inferno {
    void ReadArray(ryml::NodeRef node, span<float> values) {
        if (!node.valid() || node.is_seed()) return;

        if (node.has_children()) {
            // Array of values
            int i = 0;

            for (const auto& child : node.children()) {
                if (i >= values.size()) break;
                Yaml::ReadValue(child, values[i++]);
            }
        }
        else if (node.has_val()) {
            // Single value
            float value;
            Yaml::ReadValue(node, value);
            for (auto& v : values)
                v = value;
        }
    }

    template <class T>
    void ReadRange(ryml::NodeRef node, NumericRange<T>& values) {
        if (!node.valid() || node.is_seed()) return;

        if (node.has_children()) {
            // Array of values
            int i = 0;
            T children[2] = {};

            for (const auto& child : node.children()) {
                if (i >= 2) break;
                Yaml::ReadValue(child, children[i++]);
            }

            if (i == 1) values = { children[0], children[0] };
            if (i == 2) values = { children[0], children[1] };
        }
        else if (node.has_val()) {
            // Single value
            T value{};
            Yaml::ReadValue(node, value);
            values = { value, value };
        }
    }

    void ReadWeaponInfo(ryml::NodeRef node, HamFile& ham, int& id) {
        Yaml::ReadValue(node["id"], id);
        if (!Seq::inRange(ham.Weapons, id)) return;

        auto& weapon = ham.Weapons[id];
#define READ_PROP(name) Yaml::ReadValue(node[#name], weapon.##name)
        READ_PROP(Mass);
        READ_PROP(AmmoUsage);
        READ_PROP(EnergyUsage);
        READ_PROP(ModelSizeRatio);
        READ_PROP(WallHitSound);
        READ_PROP(WallHitVClip);
        READ_PROP(FireDelay);
        READ_PROP(Lifetime);
#undef READ_PROP

        ReadArray(node["Damage"], weapon.Damage);
        ReadArray(node["Speed"], weapon.Speed);

#define READ_PROP_EXT(name) Yaml::ReadValue(node[#name], weapon.Extended.##name)
        READ_PROP_EXT(Name);
        READ_PROP_EXT(Behavior);
        READ_PROP_EXT(Glow);
        READ_PROP_EXT(Model);
        READ_PROP_EXT(ModelScale);
        READ_PROP_EXT(Size);
        READ_PROP_EXT(Chargable);

        READ_PROP_EXT(Decal);
        READ_PROP_EXT(DecalRadius);

        READ_PROP_EXT(ExplosionSize);
        READ_PROP_EXT(ExplosionSound);
        READ_PROP_EXT(ExplosionTexture);
        READ_PROP_EXT(ExplosionTime);

        READ_PROP_EXT(RotationalVelocity);
        READ_PROP_EXT(Bounces);
        READ_PROP_EXT(Sticky);

        READ_PROP_EXT(LightRadius);
        READ_PROP_EXT(LightColor);
        Yaml::ReadValue(node["LightMode"], (int&)weapon.Extended.LightMode);
        READ_PROP_EXT(ExplosionColor);
        READ_PROP_EXT(InheritParentVelocity);
#undef READ_PROP_EXT
    }

    void ReadPowerupInfo(ryml::NodeRef node, HamFile& ham, int& id) {
        Yaml::ReadValue(node["id"], id);
        if (!Seq::inRange(ham.Powerups, id)) return;

        auto& powerup = ham.Powerups[id];
        Yaml::ReadValue(node["LightRadius"], powerup.LightRadius);
        Yaml::ReadValue(node["LightColor"], powerup.LightColor);
        Yaml::ReadValue(node["LightMode"], (int&)powerup.LightMode);
        Yaml::ReadValue(node["Glow"], powerup.Glow);
    }

    Option<string> ReadEffectName(ryml::NodeRef node) {
        string name;
        Yaml::ReadValue(node["Name"], name);
        if (name.empty()) {
            SPDLOG_WARN("Found effect with no name!");
            return {};
        }

        return name;
    }

    void ReadBeamInfo(ryml::NodeRef node, Dictionary<string, Render::BeamInfo>& beams) {
        Render::BeamInfo info{};

#define READ_PROP(name) Yaml::ReadValue(node[#name], info.##name)
        ReadRange(node["Radius"], info.Radius);
        ReadRange(node["Width"], info.Width);
        READ_PROP(Color);
        READ_PROP(Texture);
        READ_PROP(Frequency);
        READ_PROP(Amplitude);
#undef READ_PROP

        bool fadeEnd = false, randomEnd = false, fadeStart = false;
        Yaml::ReadValue(node["FadeEnd"], fadeEnd);
        Yaml::ReadValue(node["FadeStart"], fadeStart);
        Yaml::ReadValue(node["RandomEnd"], randomEnd);
        SetFlag(info.Flags, Render::BeamFlag::FadeEnd, fadeEnd);
        SetFlag(info.Flags, Render::BeamFlag::FadeStart, fadeStart);
        SetFlag(info.Flags, Render::BeamFlag::RandomEnd, randomEnd);

        if (auto name = ReadEffectName(node))
            beams[*name] = info;
    }

    void ReadSparkInfo(ryml::NodeRef node, Dictionary<string, Render::SparkEmitter>& sparks) {
        Render::SparkEmitter info;

#define READ_PROP(name) Yaml::ReadValue(node[#name], info.##name)
        READ_PROP(Color);
        READ_PROP(Restitution);
        READ_PROP(Texture);
        READ_PROP(Width);
        READ_PROP(FadeTime);
        READ_PROP(Drag);
        READ_PROP(VelocitySmear);
        READ_PROP(Duration);
        ReadRange(node["SparkDuration"], info.SparkDuration);
        ReadRange(node["Velocity"], info.Velocity);
        ReadRange(node["Count"], info.Count);
#undef READ_PROP

        if (auto name = ReadEffectName(node))
            sparks[*name] = info;
    }

    void LoadGameTable(filesystem::path path, HamFile& ham) {
        try {
            std::ifstream file(path);
            if (!file) {
                SPDLOG_ERROR(L"Unable to open game table `{}`", path.c_str());
                return;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(buffer.str()));
            ryml::NodeRef root = doc.rootref();

            if (!root.is_map()) {
                SPDLOG_WARN(L"Game table `{}` is empty", path.c_str());
                return;
            }

            auto weapons = root["Weapons"];
            if (!weapons.is_seed()) {
                for (const auto& weapon : weapons.children()) {
                    int id = -1;
                    try {
                        ReadWeaponInfo(weapon, ham, id);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading weapon {}\n{}", id, e.what());
                    }
                }
            }

            auto powerups = root["Powerups"];
            if (!powerups.is_seed()) {
                for (const auto& powerup : powerups.children()) {
                    int id = -1;
                    try {
                        ReadPowerupInfo(powerup, ham, id);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading powerup {}\n{}", id, e.what());
                    }
                }
            }

            auto effects = root["Effects"];
            auto beamNode = effects["Beams"];
            if (!beamNode.is_seed()) {
                for (const auto& beam : beamNode.children()) {
                    try {
                        ReadBeamInfo(beam, Render::DefaultEffects.Beams);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading beam info", e.what());
                    }
                }
            }

            auto sparkNode = effects["Sparks"];
            if (!sparkNode.is_seed()) {
                for (const auto& beam : sparkNode.children()) {
                    try {
                        ReadSparkInfo(beam, Render::DefaultEffects.Sparks);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading spark info", e.what());
                    }
                }
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading game table:\n{}", e.what());
        }
    }
}
