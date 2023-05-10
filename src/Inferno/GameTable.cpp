#include "pch.h"
#include <fstream>

#include "HamFile.h"
#include "Yaml.h"

namespace Inferno {
    void ReadWeaponInfo(ryml::NodeRef node, HamFile& ham, int &id) {
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

        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading game table:\n{}", e.what());
        }
    }
}
