#include "pch.h"
#include <fstream>
#include "VisualEffects.h"
#include "HamFile.h"
#include "logging.h"
#include "Yaml.h"

namespace Inferno {
    template <class T>
    bool ReadArray(ryml::NodeRef node, span<T> values) {
        if (!node.valid() || node.is_seed()) return false;

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
            T value{};
            Yaml::ReadValue(node, value);
            for (auto& v : values)
                v = value;
        }
        return true;
    }

    //void ReadVectorArray(ryml::NodeRef node, span<Vector3> values) {
    //    if (!node.valid() || node.is_seed()) return;

    //    if (node.has_children()) {
    //        // Array of values
    //        int i = 0;

    //        for (const auto& child : node.children()) {
    //            if (i >= values.size()) break;
    //            Yaml::ReadValue(child, values[i++]);
    //        }
    //    }
    //    else if (node.has_val()) {
    //        // Single value
    //        Vector3 value;
    //        Yaml::ReadValue(node, value);
    //        for (auto& v : values)
    //            v = value;
    //    }
    //}

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
        Yaml::ReadValue(node["RenderType"], (int&)weapon.RenderType);
        READ_PROP(Thrust);

        READ_PROP(Drag);
        READ_PROP(Mass);
        READ_PROP(AmmoUsage);
        READ_PROP(EnergyUsage);
        READ_PROP(ModelSizeRatio);
        READ_PROP(WallHitSound);
        READ_PROP(WallHitVClip);
        READ_PROP(FireDelay);
        READ_PROP(Lifetime);
        READ_PROP(FireCount);
        READ_PROP(SpeedVariance);
        READ_PROP(PlayerDamageScale);
        READ_PROP(Bounce);
        READ_PROP(BlobSize);
        Yaml::ReadValue(node["BlobBitmap"], (int&)weapon.BlobBitmap);

        READ_PROP(ImpactSize);
        READ_PROP(SplashRadius);
        READ_PROP(TrailSize);
        READ_PROP(Spawn);
        READ_PROP(SpawnCount);

        READ_PROP(FlashSize);
        Yaml::ReadValue(node["FlashVClip"], (int&)weapon.FlashVClip);
        Yaml::ReadValue(node["FlashSound"], (int&)weapon.FlashSound);

        READ_PROP(FlashStrength);
#undef READ_PROP
        Yaml::ReadValue(node["Model"], (int&)weapon.Model);

        ReadArray<float>(node["Damage"], weapon.Damage);
        ReadArray<float>(node["Speed"], weapon.Speed);

#define READ_PROP_EXT(name) Yaml::ReadValue(node[#name], weapon.Extended.##name)
        READ_PROP_EXT(FlashColor);
        READ_PROP_EXT(Name);
        READ_PROP_EXT(Behavior);
        READ_PROP_EXT(Glow);
        READ_PROP_EXT(ModelName);
        READ_PROP_EXT(ModelScale);
        READ_PROP_EXT(Size);
        READ_PROP_EXT(Chargable);
        READ_PROP_EXT(Spread);

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
        READ_PROP_EXT(SoundRadius);
        READ_PROP_EXT(StunMult);
        READ_PROP_EXT(PointCollideWalls);
        READ_PROP_EXT(Recoil);
        ReadArray<float>(node["InitialSpeed"], weapon.Extended.InitialSpeed);

        Yaml::ReadValue(node["LightMode"], (int&)weapon.Extended.LightMode);
        READ_PROP_EXT(LightFadeTime);
        READ_PROP_EXT(ExplosionColor);
        READ_PROP_EXT(InheritParentVelocity);
        READ_PROP_EXT(Sparks);
        READ_PROP_EXT(DeathSparks);

        READ_PROP_EXT(HomingFov);
        READ_PROP_EXT(HomingDistance);
        READ_PROP_EXT(DirectDamage);
        READ_PROP_EXT(UseThrust);

        if (weapon.Extended.HomingFov > 0)
            weapon.Extended.HomingFov = ConvertFov(weapon.Extended.HomingFov);

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

    void ReadBeamInfo(ryml::NodeRef node, Dictionary<string, BeamInfo>& beams) {
        BeamInfo info{};

#define READ_PROP(name) Yaml::ReadValue(node[#name], info.##name)
        ReadRange(node["Radius"], info.Radius);
        ReadRange(node["Width"], info.Width);
        READ_PROP(Color);
        READ_PROP(Texture);
        READ_PROP(Frequency);
        READ_PROP(StrikeTime);
        READ_PROP(Amplitude);
        READ_PROP(Duration);
        READ_PROP(Scale);
        READ_PROP(FadeInOutTime);
#undef READ_PROP

        bool fadeEnd = false, randomEnd = false, fadeStart = false;
        bool randomObjStart = false, randomObjEnd = false;
        Yaml::ReadValue(node["FadeEnd"], fadeEnd);
        Yaml::ReadValue(node["FadeStart"], fadeStart);
        Yaml::ReadValue(node["RandomEnd"], randomEnd);
        Yaml::ReadValue(node["RandomObjStart"], randomObjStart);
        Yaml::ReadValue(node["RandomObjEnd"], randomObjEnd);
        SetFlag(info.Flags, BeamFlag::FadeEnd, fadeEnd);
        SetFlag(info.Flags, BeamFlag::FadeStart, fadeStart);
        SetFlag(info.Flags, BeamFlag::RandomEnd, randomEnd);
        SetFlag(info.Flags, BeamFlag::RandomObjStart, randomObjStart);
        SetFlag(info.Flags, BeamFlag::RandomObjEnd, randomObjEnd);

        if (auto name = ReadEffectName(node))
            beams[*name] = info;
    }

    void ReadSparkInfo(ryml::NodeRef node, Dictionary<string, SparkEmitterInfo>& sparks) {
        SparkEmitterInfo info;

#define READ_PROP(name) Yaml::ReadValue(node[#name], info.##name)
        READ_PROP(Color);
        READ_PROP(Restitution);
        READ_PROP(Texture);
        READ_PROP(Width);
        READ_PROP(FadeTime);
        READ_PROP(Drag);
        READ_PROP(VelocitySmear);
        READ_PROP(SpawnRadius);
        READ_PROP(UseWorldGravity);
        READ_PROP(UsePointGravity);
        READ_PROP(PointGravityStrength);
        READ_PROP(PointGravityVelocity);
        READ_PROP(PointGravityOffset);
        READ_PROP(Offset);
        READ_PROP(FadeSize);
        READ_PROP(Relative);
        READ_PROP(Physics);
        ReadRange(node["Duration"], info.Duration);
        ReadRange(node["Interval"], info.Interval);
        ReadRange(node["Velocity"], info.Velocity);
        ReadRange(node["Count"], info.Count);
#undef READ_PROP

        if (auto name = ReadEffectName(node))
            sparks[*name] = info;
    }

    void ReadExplosions(ryml::NodeRef node, Dictionary<string, ExplosionEffectInfo>& explosions) {
        ExplosionEffectInfo info;
        Yaml::ReadValue(node["Instances"], info.Instances);
        Yaml::ReadValue(node["FadeTime"], info.FadeTime);
        Yaml::ReadValue(node["UseParentVertices"], info.UseParentVertices);
        ReadRange(node["Radius"], info.Radius);
        ReadRange(node["SoundPitch"], info.SoundPitch);
        ReadRange(node["Delay"], info.Delay);
        Yaml::ReadValue(node["Clip"], (int&)info.Clip);
        Yaml::ReadValue(node["Sound"], (int&)info.Sound);
        Yaml::ReadValue(node["SoundRadius"], info.SoundRadius);
        Yaml::ReadValue(node["Volume"], info.Volume);
        Yaml::ReadValue(node["Variance"], info.Variance);
        Yaml::ReadValue(node["Color"], info.Color);
        Yaml::ReadValue(node["LightColor"], info.LightColor);

        if (auto name = ReadEffectName(node))
            explosions[*name] = info;
    }

    void ReadTracers(ryml::NodeRef node, Dictionary<string, TracerInfo>& tracers) {
        TracerInfo info;
        Yaml::ReadValue(node["Length"], info.Length);
        Yaml::ReadValue(node["Width"], info.Width);
        Yaml::ReadValue(node["Texture"], info.Texture);
        Yaml::ReadValue(node["BlobTexture"], info.BlobTexture);
        Yaml::ReadValue(node["Color"], info.Color);
        Yaml::ReadValue(node["FadeSpeed"], info.FadeTime);
        Yaml::ReadValue(node["Duration"], info.Duration);

        if (auto name = ReadEffectName(node))
            tracers[*name] = info;
    }

    void ReadRobotInfo(ryml::NodeRef node, HamFile& ham, int& id) {
        Yaml::ReadValue(node["id"], id);
        if (!Seq::inRange(ham.Robots, id)) return;

        auto& robot = ham.Robots[id];
        ReadArray<Vector3>(node["GunPoints"], robot.GunPoints);
        ReadArray<ubyte>(node["GunSubmodels"], robot.GunSubmodels);

#define READ_PROP(name) Yaml::ReadValue(node[#name], robot.##name)
        READ_PROP(Model);
        READ_PROP(ExplosionClip1);
        READ_PROP(ExplosionClip2);
        READ_PROP(WeaponType);
        READ_PROP(WeaponType2);
        READ_PROP(Guns);

        // todo: contains data
        READ_PROP(ContainsChance);

        READ_PROP(Kamikaze);
        READ_PROP(Score);
        READ_PROP(ExplosionStrength);
        READ_PROP(EnergyDrain);
        READ_PROP(Lighting);
        READ_PROP(HitPoints);
        READ_PROP(Mass);
        READ_PROP(Drag);
        READ_PROP(Radius);

        READ_PROP(Cloaking);
        READ_PROP(Attack);

        READ_PROP(ExplosionSound1);
        READ_PROP(ExplosionSound2);
        READ_PROP(SeeSound);
        READ_PROP(AttackSound);
        READ_PROP(ClawSound);
        READ_PROP(TauntSound);
        READ_PROP(DeathRollSound);

        READ_PROP(IsThief);
        READ_PROP(Pursues);
        READ_PROP(LightCast);
        READ_PROP(DeathRoll);
        READ_PROP(Flags);
        READ_PROP(Glow);
        READ_PROP(Behavior);
        READ_PROP(Aim);
        READ_PROP(Multishot);
        READ_PROP(TeleportInterval);
        READ_PROP(AlertRadius);
        READ_PROP(AlertAwareness);
        READ_PROP(Script);
        READ_PROP(FleeThreshold);
        READ_PROP(ChaseChance);
        READ_PROP(SuppressChance);
        READ_PROP(Curiosity);
        READ_PROP(OpenKeyDoors);
        READ_PROP(AngerBehavior);
        READ_PROP(AimAngle);
        READ_PROP(GetBehind);
        READ_PROP(BurstDelay);
#undef READ_PROP

        Array<float, 5> fov{}, fireDelay{}, fireDelay2{}, turnTime{}, speed{}, circleDistance{}, meleeDamage{};
        Array<int16, 5> shots{}, evasion{};

        bool hasFov = ReadArray<float>(node["FOV"], fov);
        bool hasFireDelay = ReadArray<float>(node["FireDelay"], fireDelay);
        bool hasFireDelay2 = ReadArray<float>(node["FireDelay2"], fireDelay2);
        bool hasTurnTime = ReadArray<float>(node["TurnTime"], turnTime);
        bool hasSpeed = ReadArray<float>(node["Speed"], speed);
        bool hasCircleDist = ReadArray<float>(node["CircleDistance"], circleDistance);
        bool hasMeleeDamage = ReadArray<float>(node["MeleeDamage"], meleeDamage);
        bool hasShots = ReadArray<int16>(node["Shots"], shots);
        bool hasEvasion = ReadArray<int16>(node["Evasion"], evasion);

        for (int i = 0; i < 5; i++) {
            auto& diff = robot.Difficulty[i];
            if (hasCircleDist) diff.CircleDistance = circleDistance[i];
            if (hasFireDelay) diff.FireDelay = fireDelay[i];
            if (hasFireDelay2) diff.FireDelay2 = fireDelay2[i];
            if (hasEvasion) diff.EvadeSpeed = (uint8)evasion[i];
            if (hasShots) diff.ShotCount = (uint8)shots[i];
            if (hasSpeed) diff.Speed = speed[i];
            if (hasTurnTime) diff.TurnTime = turnTime[i];
            if (hasFov) diff.FieldOfView = ConvertFov(fov[i]);
            if (hasMeleeDamage) diff.MeleeDamage = meleeDamage[i];
        }

        if (auto gatedRobots = node["GatedRobots"]; !gatedRobots.is_seed()) {
            for (const auto& gatedRobot : gatedRobots.children()) {
                int robotId = -1;
                Yaml::ReadValue(gatedRobot, robotId);
                if (robotId != -1)
                    robot.GatedRobots.push_back((int8)robotId);
            }
        }
    }

    void ReadEffectClip(ryml::NodeRef node, HamFile& ham, int& id) {
        Yaml::ReadValue(node["id"], id);
        if (!Seq::inRange(ham.Effects, id)) return;

        auto& effect = ham.Effects[id];

#define READ_PROP(name) Yaml::ReadValue(node[#name], effect.##name)
        READ_PROP(DestroyedTexture);
        READ_PROP(DestroyedEClip);
#undef READ_PROP
    }

    void LoadGameTable(const string& data, HamFile& ham) {
        try {
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(data));
            ryml::NodeRef root = doc.rootref();

            if (!root.is_map()) {
                SPDLOG_WARN(L"Game table is empty");
                return;
            }

            EffectLibrary = {}; // Reset effect library

            if (auto weapons = root["Weapons"]; !weapons.is_seed()) {
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

            if (auto robots = root["Robots"]; !robots.is_seed()) {
                for (const auto& robot : robots.children()) {
                    int id = -1;
                    try {
                        ReadRobotInfo(robot, ham, id);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading robot {}\n{}", id, e.what());
                    }
                }
            }

            if (auto node = root["EffectClips"]; !node.is_seed()) {
                for (const auto& child : node.children()) {
                    int id = -1;
                    try {
                        ReadEffectClip(child, ham, id);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading effect clip {}\n{}", id, e.what());
                    }
                }
            }

            if (auto powerups = root["Powerups"]; !powerups.is_seed()) {
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

            if (auto beams = effects["Beams"]; !beams.is_seed()) {
                for (const auto& beam : beams.children()) {
                    try {
                        ReadBeamInfo(beam, EffectLibrary.Beams);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading beam info", e.what());
                    }
                }
                SPDLOG_INFO("Loaded {} beams", EffectLibrary.Beams.size());
            }


            if (auto sparks = effects["Sparks"]; !sparks.is_seed()) {
                for (const auto& beam : sparks.children()) {
                    try {
                        ReadSparkInfo(beam, EffectLibrary.Sparks);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading spark info", e.what());
                    }
                }
                SPDLOG_INFO("Loaded {} sparks", EffectLibrary.Sparks.size());
            }

            if (auto explosions = effects["Explosions"]; !explosions.is_seed()) {
                for (const auto& beam : explosions.children()) {
                    try {
                        ReadExplosions(beam, EffectLibrary.Explosions);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading explosion info", e.what());
                    }
                }
                SPDLOG_INFO("Loaded {} explosions", EffectLibrary.Explosions.size());
            }

            if (auto tracers = effects["Tracers"]; !tracers.is_seed()) {
                for (const auto& beam : tracers.children()) {
                    try {
                        ReadTracers(beam, EffectLibrary.Tracers);
                    }
                    catch (const std::exception& e) {
                        SPDLOG_WARN("Error reading tracer info", e.what());
                    }
                }
                SPDLOG_INFO("Loaded {} tracers", EffectLibrary.Tracers.size());
            }

        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading game table:\n{}", e.what());
        }
    }
}
