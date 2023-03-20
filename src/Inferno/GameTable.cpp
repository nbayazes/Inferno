#include "pch.h"
#include <fstream>

#include "HamFile.h"
#include "Yaml.h"

namespace Inferno {
    void ReadWeaponInfo(ryml::NodeRef node, HamFile& ham) {
        int id;
        Yaml::ReadValue(node["id"], id);
        if (!Seq::inRange(ham.Weapons, id)) return;

        auto& weapon = ham.Weapons[id];
#define ReadProp(name) Yaml::ReadValue(node[#name], weapon.##name)
        ReadProp(Mass);
        ReadProp(AmmoUsage);
        ReadProp(EnergyUsage);
        ReadProp(ModelSizeRatio);
        ReadProp(WallHitSound);
        ReadProp(WallHitVClip);
        ReadProp(FireDelay);

        //Yaml::ReadValue(node["Mass"], weapon.Mass);
        //Yaml::ReadValue(node["AmmoUsage"], weapon.AmmoUsage);

        //Yaml::ReadValue(node["ModelSizeRatio"], weapon.ModelSizeRatio);
        //Yaml::ReadValue(node["EnergyUsage"], weapon.EnergyUsage);

        auto damage = node["Damage"];
        if (!damage.is_seed()) {
            int i = 0;
            for (const auto& d : damage.children()) {
                if (i > weapon.Damage.size()) break;
                Yaml::ReadValue(d, weapon.Damage[i++]);
            }
        }

#define ReadExtendedProp(name) Yaml::ReadValue(node[#name], weapon.Extended.##name)
        ReadExtendedProp(Name);
        ReadExtendedProp(Behavior);
        ReadExtendedProp(Glow);
        ReadExtendedProp(Model);
        ReadExtendedProp(ModelScale);
        ReadExtendedProp(Size);
        ReadExtendedProp(Chargable);
        //ReadExtendedProp(MaxCharge);

        ReadExtendedProp(Decal);
        ReadExtendedProp(DecalRadius);

        ReadExtendedProp(ExplosionSize);
        ReadExtendedProp(ExplosionSound);
        ReadExtendedProp(ExplosionTexture);
        ReadExtendedProp(ExplosionTime);

        ReadExtendedProp(RotationalVelocity);
        ReadExtendedProp(Bounces);

        //Yaml::ReadValue(node["Behavior"], weapon.Extended.Behavior);
        //Yaml::ReadValue(node["Glow"], weapon.Extended.Glow);
        //Yaml::ReadValue(node["Model"], weapon.Extended.Model);
        //Yaml::ReadValue(node["ModelScale"], weapon.Extended.ModelScale);
        //Yaml::ReadValue(node["Size"], weapon.Extended.Size);

        //Yaml::ReadValue(node["ScorchTexture"], weapon.Extended.ScorchTexture);
        //Yaml::ReadValue(node["ScorchRadius"], weapon.Extended.ScorchRadius);



        //Yaml::ReadValue(node["ExplosionSize"], weapon.Extended.ExplosionSize);
        //Yaml::ReadValue(node["ExplosionSound"], weapon.Extended.ExplosionSound);
        //Yaml::ReadValue(node["ExplosionTexture"], weapon.Extended.ExplosionTexture);
        //Yaml::ReadValue(node["ExplosionTime"], weapon.Extended.ExplosionTime);

        //Yaml::ReadValue(node["RotationalVelocity"], weapon.Extended.RotationalVelocity);
#undef ReadExtendedProp
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
                    try {
                        ReadWeaponInfo(weapon, ham);
                    }
                    catch (...) {
                        SPDLOG_WARN("Error reading weapon");
                    }
                    //filesystem::path dataPath;
                    //ReadValue(c, dataPath);
                    //if (!dataPath.empty()) Settings::Inferno.DataPaths.push_back(dataPath);
                }
            }

        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading game table:\n{}", e.what());
        }
    }
}
