#include "pch.h"
#include <fstream>

#include "HamFile.h"
#include "Yaml.h"
#include "Graphics/Render.Particles.h"

namespace Inferno {
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

        auto damage = node["Damage"];
        if (!damage.is_seed()) {
            int i = 0;
            for (const auto& d : damage.children()) {
                if (i > weapon.Damage.size()) break;
                Yaml::ReadValue(d, weapon.Damage[i++]);
            }
        }

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
    }

    void ReadRange(ryml::NodeRef node, NumericRange<float>& values) {
        if (!node.valid() || node.is_seed()) return;

        if (node.has_children()) {
            // Array of values
            int i = 0;
            float children[2] = {};

            for (const auto& child : node.children()) {
                if (i >= 2) break;
                Yaml::ReadValue(child, children[i++]);
            }

            if (i == 1) values = { children[0], children[0] };
            if (i == 2) values = { children[0], children[1] };
        }
        else if (node.has_val()) {
            // Single value
            float value;
            Yaml::ReadValue(node, value);
            values = { value, value };
        }
    }

    void ReadBeamInfo(ryml::NodeRef node, Dictionary<string, Render::BeamInfo>& beams) {
        string name;
        Yaml::ReadValue(node["Name"], name);
        if (name.empty()) {
            SPDLOG_WARN("Found beam entry with missing name!");
            return;
        }

        Render::BeamInfo beam{};
#define READ_PROP(name) Yaml::ReadValue(node[#name], beam.##name)
        ReadRange(node["Radius"], beam.Radius);
        ReadRange(node["Width"], beam.Width);
        READ_PROP(Color); // range
        READ_PROP(Texture);
        READ_PROP(Frequency);
        READ_PROP(Amplitude);

        bool fadeEnd = false, randomEnd = false, fadeStart = false;
        Yaml::ReadValue(node["FadeEnd"], fadeEnd);
        Yaml::ReadValue(node["FadeStart"], fadeStart);
        Yaml::ReadValue(node["RandomEnd"], randomEnd);
        if (fadeEnd) SetFlag(beam.Flags, Render::BeamFlag::FadeEnd);
        if (fadeStart) SetFlag(beam.Flags, Render::BeamFlag::FadeStart);
        if (randomEnd) SetFlag(beam.Flags, Render::BeamFlag::RandomEnd);
#undef READ_PROP

        beams[name] = beam;
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
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading game table:\n{}", e.what());
        }
    }
}
