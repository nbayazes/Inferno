#include "pch.h"
#include "HamFile.h"
#include "Pig.h"
#include "Streams.h"
#include "Utility.h"
#include "Sound.h"

namespace Inferno {
    LevelTexture ReadTextureInfo(StreamReader& r) {
        LevelTexture t{};
        t.Flags = (TextureFlag)r.ReadByte();
        r.ReadByte(); // padding
        r.ReadByte();
        r.ReadByte();
        t.Lighting = r.ReadFix();
        t.Damage = r.ReadFix();
        t.EffectClip = (EClipID)r.ReadInt16();
        t.DestroyedTexture = (LevelTexID)r.ReadInt16();
        auto slideU = FixToFloat(r.ReadInt16());
        auto slideV = FixToFloat(r.ReadInt16());
        t.Slide = Vector2{ slideU, slideV };
        return t;
    }

    LevelTexture ReadLevelTextureD1(StreamReader& r) {
        LevelTexture t{};
        t.D1FileName = r.ReadString(13);
        t.Flags = (TextureFlag)r.ReadByte();
        t.Lighting = r.ReadFix();
        t.Damage = r.ReadFix();
        t.EffectClip = (EClipID)r.ReadInt32();
        return t;
    }

    VClip ReadVClip(StreamReader& r) {
        VClip vc{};
        vc.PlayTime = r.ReadFix();
        vc.NumFrames = r.ReadInt32();
        vc.FrameTime = r.ReadFix();
        vc.Flags = (VClipFlag)r.ReadInt32();
        vc.Sound = (SoundID)r.ReadInt16();
        for (auto& id : vc.Frames)
            id = (TexID)r.ReadInt16();
        vc.LightValue = r.ReadFix();
        return vc;
    }

    EffectClip ReadEffect(StreamReader& r) {
        EffectClip ec{};
        ec.VClip = ReadVClip(r);
        ec.TimeLeft = r.ReadFix();
        ec.FrameCount = r.ReadInt32();
        ec.ChangingWallTexture = (LevelTexID)r.ReadInt16();
        ec.ChangingObjectTexture = r.ReadInt16();
        ec.Flags = (EClipFlag)r.ReadInt32();
        ec.CritClip = (EClipID)r.ReadInt32();
        ec.DestroyedTexture = (LevelTexID)r.ReadInt32();
        ec.DestroyedVClip = (VClipID)r.ReadInt32();
        ec.DestroyedEClip = (EClipID)r.ReadInt32();
        ec.ExplosionSize = r.ReadFix();
        ec.Sound = (SoundID)r.ReadInt32();
        ec.OneShotTag = { (SegID)r.ReadInt32(), (SideID)r.ReadInt32() };
        return ec;
    }

    DoorClip ReadDoorClip(StreamReader& r) {
        DoorClip wc{};
        wc.PlayTime = r.ReadFix();
        wc.NumFrames = r.ReadInt16();
        for (auto& f : wc.Frames)
            f = (LevelTexID)r.ReadInt16();
        wc.OpenSound = (SoundID)r.ReadInt16();
        wc.CloseSound = (SoundID)r.ReadInt16();
        wc.Flags = (DoorClipFlag)r.ReadInt16();
        wc.Filename = r.ReadString(13);
        r.ReadByte(); // padding
        return wc;
    }


    RobotInfo ReadRobotD1(StreamReader& r) {
        RobotInfo ri{};

        ri.Model = (ModelID)r.ReadInt32();
        ri.Guns = (uint8)r.ReadInt32();

        for (auto& gp : ri.GunPoints) {
            gp = r.ReadVector();
            gp.z *= -1; // flip lh/rh
        }

        for (auto& gs : ri.GunSubmodels)
            gs = r.ReadByte();

        ri.ExplosionClip1 = (VClipID)r.ReadInt16();
        ri.ExplosionSound1 = (SoundID)r.ReadInt16();

        ri.ExplosionClip2 = (VClipID)r.ReadInt16();
        ri.ExplosionSound2 = (SoundID)r.ReadInt16();

        ri.WeaponType = (WeaponID)r.ReadInt16();

        ri.Contains.ID = r.ReadByte();
        ri.Contains.Count = r.ReadByte();
        ri.ContainsChance = r.ReadByte();
        ri.Contains.Type = (ObjectType)r.ReadByte();

        ri.Score = (short)r.ReadInt32();

        ri.Lighting = r.ReadFix();
        ri.HitPoints = r.ReadFix();

        ri.Mass = r.ReadFix();
        ri.Drag = r.ReadFix();

        for (auto& d : ri.Difficulty) d.FieldOfView = r.ReadFix();
        for (auto& d : ri.Difficulty) d.FireDelay = r.ReadFix();
        for (auto& d : ri.Difficulty) d.TurnTime = r.ReadFix();

        for (int s = 0; s < 5; s++) r.ReadInt32(); // Unused firepower value
        for (int s = 0; s < 5; s++) r.ReadInt32(); // Unused shield value

        for (auto& d : ri.Difficulty) d.Speed = r.ReadFix();
        for (auto& d : ri.Difficulty) d.CircleDistance = r.ReadFix();
        for (auto& d : ri.Difficulty) d.ShotCount = r.ReadByte();
        for (auto& d : ri.Difficulty) d.EvadeSpeed = r.ReadByte();

        ri.Cloaking = (CloakType)r.ReadByte();
        ri.Attack = (AttackType)r.ReadByte();

        ri.IsBoss = r.ReadByte();

        ri.SeeSound = (SoundID)r.ReadByte();
        ri.AttackSound = (SoundID)r.ReadByte();
        ri.ClawSound = (SoundID)r.ReadByte();

        for (auto& joint : ri.Joints) {
            for (auto& k : joint) {
                k.Count = r.ReadInt16();
                k.Offset = r.ReadInt16();
            }
        }

        auto check = r.ReadInt32();
        if (ri.Score != 0 && check != 0xabcd) // the trailing records are zeroed out, only verify data on real records
            throw Exception("Robot info read error");

        return ri;
    }

    RobotInfo ReadRobotInfo(StreamReader& r) {
        RobotInfo ri{};

        ri.Model = (ModelID)r.ReadInt32();
        for (auto& gp : ri.GunPoints)
            gp = r.ReadVector();

        for (auto& gs : ri.GunSubmodels)
            gs = r.ReadByte();

        ri.ExplosionClip1 = (VClipID)r.ReadInt16();
        ri.ExplosionSound1 = (SoundID)r.ReadInt16();

        ri.ExplosionClip2 = (VClipID)r.ReadInt16();
        ri.ExplosionSound2 = (SoundID)r.ReadInt16();

        ri.WeaponType = (WeaponID)r.ReadByte();
        ri.WeaponType2 = (WeaponID)r.ReadByte();
        ri.Guns = r.ReadByte();

        ri.Contains.ID = r.ReadByte();
        ri.Contains.Count = r.ReadByte();
        ri.ContainsChance = r.ReadByte();
        ri.Contains.Type = (ObjectType)r.ReadByte();

        ri.Kamikaze = r.ReadByte();

        ri.Score = r.ReadInt16();
        ri.Badass = r.ReadByte();
        ri.EnergyDrain = r.ReadByte();

        ri.Lighting = r.ReadFix();
        ri.HitPoints = r.ReadFix();

        ri.Mass = r.ReadFix();
        ri.Drag = r.ReadFix();

        for (auto& d : ri.Difficulty) d.FieldOfView = r.ReadFix();
        for (auto& d : ri.Difficulty) d.FireDelay = r.ReadFix();
        for (auto& d : ri.Difficulty) d.FireDelay2 = r.ReadFix();
        for (auto& d : ri.Difficulty) d.TurnTime = r.ReadFix();
        for (auto& d : ri.Difficulty) d.Speed = r.ReadFix();
        for (auto& d : ri.Difficulty) d.CircleDistance = r.ReadFix();
        for (auto& d : ri.Difficulty) d.ShotCount = r.ReadByte();
        for (auto& d : ri.Difficulty) d.EvadeSpeed = r.ReadByte();

        ri.Cloaking = (CloakType)r.ReadByte();
        ri.Attack = (AttackType)r.ReadByte();

        ri.SeeSound = (SoundID)r.ReadByte();
        ri.AttackSound = (SoundID)r.ReadByte();
        ri.ClawSound = (SoundID)r.ReadByte();
        ri.TauntSound = (SoundID)r.ReadByte();

        ri.IsBoss = r.ReadByte();
        ri.IsCompanion = r.ReadByte();
        ri.smart_blobs = r.ReadByte();
        ri.energy_blobs = r.ReadByte();

        ri.IsThief = r.ReadByte();
        ri.Pursues = r.ReadByte();
        ri.LightCast = r.ReadByte();
        ri.DeathRoll = r.ReadByte();

        ri.Flags = r.ReadByte();
        r.ReadByte(); // padding
        r.ReadByte();
        r.ReadByte();

        ri.DeathrollSound = (SoundID)r.ReadByte();
        ri.Glow = r.ReadByte();
        ri.Behavior = r.ReadByte();
        ri.Aim = r.ReadByte();

        for (auto& gunState : ri.Joints) {
            for (auto& state : gunState) {
                state.Count = r.ReadInt16();
                state.Offset = r.ReadInt16();
            }
        }

        if (r.ReadInt32() != 0xabcd)
            throw Exception("Robot info read error");

        return ri;
    }

    JointPos ReadRobotJoint(StreamReader& r) {
        JointPos j{};
        j.ID = r.ReadInt16();
        auto angles =  r.ReadAngleVec();
        j.Angle = Vector3(-angles.x, angles.z, angles.y);
        //std::swap(j.Angle.y, j.Angle.z); // Match create matrix from angles function
        return j;
    }

    Weapon ReadWeapon(StreamReader& r) {
        Weapon w{};
        w.RenderType = (WeaponRenderType)r.ReadByte();
        w.Piercing = r.ReadByte();
        w.Model = (ModelID)r.ReadInt16();
        w.ModelInner = (ModelID)r.ReadInt16();

        w.FlashVClip = (VClipID)r.ReadByte();
        w.RobotHitVClip = (VClipID)r.ReadByte();
        w.FlashSound = (SoundID)r.ReadInt16();

        w.WallHitVClip = (VClipID)r.ReadByte();
        w.FireCount = r.ReadByte();
        w.RobotHitSound = (SoundID)r.ReadInt16();

        w.AmmoUsage = r.ReadByte();
        w.WeaponVClip = (VClipID)r.ReadByte();
        w.WallHitSound = (SoundID)r.ReadInt16();

        w.IsDestroyable = r.ReadByte();
        w.IsMatter = r.ReadByte();
        w.Bounce = r.ReadByte();
        w.IsHoming = r.ReadByte();

        w.SpeedVariance = r.ReadByte() / 128.0f;
        w.Flags = (WeaponFlag)r.ReadByte();
        w.FlashStrength = r.ReadByte();
        w.TrailSize = r.ReadByte();

        w.Spawn = (WeaponID)r.ReadByte();

        w.EnergyUsage = r.ReadFix();
        w.FireDelay = r.ReadFix();

        w.PlayerDamageScale = r.ReadFix();

        w.BlobBitmap = (TexID)r.ReadInt16();
        w.BlobSize = r.ReadFix();

        w.FlashSize = r.ReadFix();
        w.ImpactSize = r.ReadFix();

        for (auto& s : w.Damage) s = r.ReadFix();
        for (auto& s : w.Speed) s = r.ReadFix();

        w.Mass = r.ReadFix();
        w.Drag = r.ReadFix();
        w.Thrust = r.ReadFix();
        w.ModelSizeRatio = r.ReadFix();
        w.Light = r.ReadFix();
        w.Lifetime = r.ReadFix();
        w.SplashRadius = r.ReadFix();
        w.Icon = (TexID)r.ReadInt16();
        w.HiresIcon = (TexID)r.ReadInt16();
        return w;
    }

    Powerup ReadPowerup(StreamReader& r) {
        Powerup p{};
        p.VClip = (VClipID)r.ReadInt32();
        p.HitSound = (SoundID)r.ReadInt32();
        p.Size = r.ReadFix();
        p.Light = r.ReadFix();
        return p;
    }

    Model ReadModelInfo(StreamReader& r) {
        Model model{};
        auto submodelCount = r.ReadInt32();
        model.Submodels.resize(submodelCount);
        model.DataSize = r.ReadInt32();
        r.ReadInt32(); // model data offset

        Array<Submodel, MAX_SUBMODELS> submodels;
        for (auto& s : submodels) s.Pointer = r.ReadInt32();
        for (auto& s : submodels) {
            s.Offset = r.ReadVector();
            s.Offset.z *= -1; // flip lh/rh
        }
        for (auto& s : submodels) s.Normal = r.ReadVector();
        for (auto& s : submodels) s.Point = r.ReadVector();
        for (auto& s : submodels) s.Radius = r.ReadFix();
        for (auto& s : submodels) s.Parent = r.ReadByte();
        for (auto& s : submodels) s.Min = r.ReadVector();
        for (auto& s : submodels) s.Max = r.ReadVector();
        span range{ model.Submodels };
        std::copy_n(submodels.begin(), submodelCount, range.begin());

        model.MinBounds = r.ReadVector();
        model.MaxBounds = r.ReadVector();
        model.Radius = r.ReadFix();
        model.TextureCount = r.ReadByte();
        model.FirstTexture = r.ReadInt16();
        model.SimplerModel = r.ReadByte();
        return model;
    }

    void ReadModelData(const StreamReader& r, Model& m, const Palette* palette = nullptr) {
        List<ubyte> data;
        data.resize(m.DataSize);
        r.ReadBytes(data.data(), data.size());
        ReadPolymodel(m, data, palette);
    }

    PlayerShip ReadPlayerShip(StreamReader& r) {
        PlayerShip ship{};
        ship.Model = (ModelID)r.ReadInt32();
        ship.ExplosionVClip = (VClipID)r.ReadInt32();
        ship.Mass = r.ReadFix();
        ship.Drag = r.ReadFix();
        ship.MaxThrust = r.ReadFix();
        ship.ReverseThrust = r.ReadFix();
        ship.Brakes = r.ReadFix();
        ship.Wiggle = r.ReadFix();
        ship.MaxRotationalThrust = r.ReadFix();
        for (auto& g : ship.GunPoints) {
            g = r.ReadVector();
            g.z *= -1; // flip lh/rh
        }
        return ship;
    }

    Reactor ReadReactor(StreamReader& r) {
        Reactor reactor{};
        reactor.Model = (ModelID)r.ReadInt32();
        reactor.Guns = r.ReadInt32();
        for (auto& g : reactor.GunPoints) {
            g = r.ReadVector();
            g.z *= -1; // flip lh/rh
        }
        for (auto& g : reactor.GunDirs) {
            g = r.ReadVector();
            g.z *= -1; // flip lh/rh
        }
        return reactor;
    }

    void UpdateTexInfo(HamFile& ham) {
        auto& levelTexIdx = ham.LevelTexIdx;
        auto maxIndex = *ranges::max_element(ham.AllTexIdx);
        if ((int)maxIndex > 10000) throw Exception("Index out of range in texture indices");
        levelTexIdx.resize((size_t)maxIndex + 1);
        ranges::fill(levelTexIdx, LevelTexID(255));

        for (auto i = 0; i < ham.TexInfo.size(); i++) {
            ham.TexInfo[i].ID = LevelTexID(i);
            ham.TexInfo[i].TexID = ham.AllTexIdx[i];
            levelTexIdx.at((int)ham.AllTexIdx[i]) = (LevelTexID)i;
        }
    }

    HamFile ReadHam(StreamReader& reader) {
        HamFile ham;

        const auto id = (uint)reader.ReadInt32();
        if (id != MakeFourCC("HAM!")) throw Exception("invalid ham");

        const auto version = reader.ReadInt32();
        if (version < 3)
            /*int soundOffset = */reader.ReadInt32();
        auto textureCount = reader.ReadInt32();

        auto& allTexIdx = ham.AllTexIdx;
        allTexIdx.resize(textureCount);
        for (auto& i : allTexIdx) i = (TexID)reader.ReadInt16();

        ham.TexInfo.resize(textureCount);
        for (auto& t : ham.TexInfo) t = ReadTextureInfo(reader);

        UpdateTexInfo(ham);

        {
            auto soundCount = reader.ReadInt32();
            ham.Sounds.resize(soundCount);
            for (auto& s : ham.Sounds) s = reader.ReadByte();

            ham.AltSounds.resize(soundCount);
            for (auto& s : ham.AltSounds) s = reader.ReadByte();
        }

        ham.VClips.resize(reader.ReadInt32());
        for (auto& vc : ham.VClips) vc = ReadVClip(reader);
        ham.Effects.resize(reader.ReadInt32());
        for (auto& e : ham.Effects) e = ReadEffect(reader);

        ham.DoorClips.resize(reader.ReadInt32());
        for (auto& wc : ham.DoorClips) wc = ReadDoorClip(reader);

        ham.Robots.resize(reader.ReadInt32());
        for (auto& wc : ham.Robots) wc = ReadRobotInfo(reader);

        ham.RobotJoints.resize(reader.ReadInt32());
        for (auto& j : ham.RobotJoints) j = ReadRobotJoint(reader);

        ham.Weapons.resize(reader.ReadInt32());
        for (auto& w : ham.Weapons) w = ReadWeapon(reader);

        ham.Powerups.resize(reader.ReadInt32());
        for (auto& p : ham.Powerups) p = ReadPowerup(reader);

        {
            auto modelCount = reader.ReadInt32();
            ham.Models.resize(modelCount);
            for (auto& m : ham.Models) m = ReadModelInfo(reader);
            for (auto& m : ham.Models) ReadModelData(reader, m);

            ham.DyingModels.resize(modelCount);
            for (auto& m : ham.DyingModels) m = (ModelID)reader.ReadInt32();

            ham.DeadModels.resize(modelCount);
            for (auto& m : ham.DeadModels) m = (ModelID)reader.ReadInt32();
        }

        {
            auto gaugeCount = reader.ReadInt32();

            ham.Gauges.resize(gaugeCount);
            for (auto& g : ham.Gauges) g = (TexID)reader.ReadInt16();

            ham.HiResGauges.resize(gaugeCount);
            for (auto& g : ham.HiResGauges) g = (TexID)reader.ReadInt16();
        }

        {
            auto objBitmapCount = reader.ReadInt32();

            ham.ObjectBitmaps.resize(objBitmapCount);
            for (auto& b : ham.ObjectBitmaps) b = (TexID)reader.ReadInt16();

            ham.ObjectBitmapPointers.resize(objBitmapCount);
            for (auto& p : ham.ObjectBitmapPointers) p = reader.ReadUInt16();
        }

        ham.PlayerShip = ReadPlayerShip(reader);

        ham.Cockpits.resize(reader.ReadInt32());
        for (auto& c : ham.Cockpits) c = (TexID)reader.ReadUInt16();

        ham.FirstMultiplayerBitmap = reader.ReadInt32();

        ham.Reactors.resize(reader.ReadInt32());
        for (auto& r : ham.Reactors) r = ReadReactor(reader);

        ham.MarkerModel = (ModelID)reader.ReadInt32();
        return ham;
    }

    void AppendVHam(StreamReader& reader, HamFile& ham) {
        auto id = reader.ReadInt32();
        if (id != 'XHAM')
            throw Exception("Vertigo XHAM is invalid");

        /*auto version = */reader.ReadInt32();

        auto weaponTypes = reader.ReadElementCount();
        for (size_t i = 0; i < weaponTypes; i++)
            ham.Weapons.push_back(ReadWeapon(reader));

        auto robotTypes = reader.ReadElementCount();
        for (size_t i = 0; i < robotTypes; i++)
            ham.Robots.push_back(ReadRobotInfo(reader));

        auto robotJoints = reader.ReadElementCount();
        for (size_t i = 0; i < robotJoints; i++)
            ham.RobotJoints.push_back(ReadRobotJoint(reader));

        List<Model> models(reader.ReadElementCount());
        for (auto& model : models) model = ReadModelInfo(reader);
        for (auto& model : models) ReadModelData(reader, model);
        for (auto& model : models) ham.Models.push_back(model);

        Seq::iter(models, [&](auto) { ham.DyingModels.push_back((ModelID)reader.ReadInt32()); });
        Seq::iter(models, [&](auto) { ham.DeadModels.push_back((ModelID)reader.ReadInt32()); });

        auto bitmaps = reader.ReadElementCount();
        for (size_t i = 422; i < 422 + bitmaps; i++)
            ham.ObjectBitmaps[i] = (TexID)reader.ReadInt16();

        auto bitmapPointers = reader.ReadElementCount();
        for (size_t i = 502; i < 502 + bitmapPointers; i++)
            ham.ObjectBitmapPointers[i] = reader.ReadInt16();
    }

    void CheckRange(auto&& xs, auto index, const char* message) {
        if (index < 0 || index >= xs.size())
            throw Exception(message);
    }

    // Updates a HAM using data from a HXM
    void ReadHXM(StreamReader& reader, HamFile& ham) {
        // Should have been HXM! but the original source typo'd it as HMX!
        if (reader.ReadInt32() != MakeFourCC("HMX!"))
            throw Exception("HXM header is wrong");

        if (reader.ReadInt32() < 1)
            throw Exception("HXM version is wrong");

        auto robots = reader.ReadElementCount();
        for (int i = 0; i < robots; i++) {
            auto hamIdx = reader.ReadInt32();
            CheckRange(ham.Robots, hamIdx, "Robot index is out of range: " + hamIdx);

            // replace existing robot. mark as custom?
            ham.Robots[hamIdx] = ReadRobotInfo(reader);
        }

        auto joints = reader.ReadElementCount();
        for (int i = 0; i < joints; i++) {
            auto idx = reader.ReadInt32();
            CheckRange(ham.RobotJoints, idx, "HXM robot joint index out of range");
            ham.RobotJoints[idx] = ReadRobotJoint(reader);
        }

        auto models = reader.ReadElementCount();
        for (int i = 0; i < models; i++) {
            auto index = reader.ReadInt32();
            CheckRange(ham.Models, index, "HXM model data index out of range");
            ham.Models[index] = ReadModelInfo(reader);
            ReadModelData(reader, ham.Models[index]);

            ham.DyingModels[index] = (ModelID)reader.ReadInt32();
            ham.DeadModels[index] = (ModelID)reader.ReadInt32();
        }

        auto tindices = reader.ReadElementCount();
        for (int i = 0; i < tindices; i++) {
            auto idx = reader.ReadInt32();
            CheckRange(ham.ObjectBitmaps, idx, "HXM model object bitmap index out of range");
            ham.ObjectBitmaps[idx] = (TexID)reader.ReadUInt16();
        }

        auto mtindices = reader.ReadElementCount();
        for (int i = 0; i < mtindices; i++) {
            auto idx = reader.ReadInt32();
            CheckRange(ham.ObjectBitmapPointers, idx, "HXM model object bitmap pointer index out of range");
            ham.ObjectBitmapPointers[idx] = reader.ReadUInt16();
        }
    }

    DoorClip ReadDoorClipD1(StreamReader& r) {
        DoorClip w{};
        w.PlayTime = r.ReadFix();
        w.NumFrames = r.ReadInt16();
        for (int f = 0; f < 20; f++)
            w.Frames[f] = (LevelTexID)r.ReadInt16();

        w.OpenSound = (SoundID)r.ReadInt16();
        w.CloseSound = (SoundID)r.ReadInt16();
        w.Flags = (DoorClipFlag)r.ReadInt16();
        w.Filename = r.ReadString(13);
        r.ReadByte(); // padding
        return w;
    }

    Weapon ReadWeaponD1(StreamReader& r) {
        Weapon w{};
        w.RenderType = (WeaponRenderType)r.ReadByte();
        w.Model = (ModelID)r.ReadByte();
        w.ModelInner = (ModelID)r.ReadByte();
        w.Piercing = r.ReadByte();

        w.FlashVClip = (VClipID)r.ReadByte();
        w.FlashSound = (SoundID)r.ReadInt16();

        w.RobotHitVClip = (VClipID)r.ReadByte();
        w.RobotHitSound = (SoundID)r.ReadInt16();

        w.WallHitVClip = (VClipID)r.ReadByte();
        w.WallHitSound = (SoundID)r.ReadInt16();

        w.FireCount = r.ReadByte();
        w.AmmoUsage = r.ReadByte();
        w.WeaponVClip = (VClipID)r.ReadByte();

        w.IsDestroyable = r.ReadByte();
        w.IsMatter = r.ReadByte();
        w.Bounce = r.ReadByte();
        w.IsHoming = r.ReadByte();

        r.SeekForward(3); // padding

        w.EnergyUsage = r.ReadFix();
        w.FireDelay = r.ReadFix();

        w.PlayerDamageScale = 1;

        w.BlobBitmap = (TexID)r.ReadInt16();
        w.BlobSize = r.ReadFix();

        w.FlashSize = r.ReadFix();
        w.ImpactSize = r.ReadFix();

        for (auto& s : w.Damage) s = r.ReadFix();
        for (auto& s : w.Speed) s = r.ReadFix();

        w.Mass = r.ReadFix();
        w.Drag = r.ReadFix();
        w.Thrust = r.ReadFix();
        w.ModelSizeRatio = r.ReadFix();
        w.Light = r.ReadFix();
        w.Lifetime = r.ReadFix();
        w.SplashRadius = r.ReadFix();

        w.Icon = (TexID)r.ReadInt16();
        w.HiresIcon = w.Icon;

        return w;
    }

    std::tuple<HamFile, PigFile, SoundFile> ReadDescent1GameData(StreamReader& reader, const Palette& palette) {
        HamFile ham;
        auto dataOffset = reader.ReadInt32();

        // D1 pigs have no signature so guess based on the data offset.
        if (dataOffset <= 1800)
            throw Exception("Cannot read this PIG file");

        ham.AllTexIdx.resize(800);
        ham.TexInfo.resize(800);
        ham.Sounds.resize(250);
        ham.VClips.resize(70);
        ham.Effects.resize(60);
        ham.DoorClips.resize(30);
        ham.Robots.resize(30);
        ham.RobotJoints.resize(600);
        ham.Weapons.resize(30);
        ham.Gauges.resize(80); // 85 for mac
        ham.ObjectBitmaps.resize(210);
        ham.ObjectBitmapPointers.resize(210);
        ham.Cockpits.resize(4);
        ham.Powerups.resize(29);
        ham.Reactors.resize(1);

        /*auto numTextures =*/ reader.ReadElementCount();
        for (auto& t : ham.AllTexIdx) t = (TexID)reader.ReadInt16();
        for (auto& t : ham.TexInfo) t = ReadLevelTextureD1(reader);

        UpdateTexInfo(ham);

        reader.ReadBytes(ham.Sounds.data(), 250);
        reader.SeekForward(250); // skip low memory alt sounds
        /*auto vclips =*/ reader.ReadInt32(); // invalid vclip count

        for (auto& c : ham.VClips) c = ReadVClip(reader);

        /*auto numEClips = */reader.ReadElementCount();
        for (auto& c : ham.Effects) c = ReadEffect(reader);

        reader.ReadElementCount();
        for (auto& c : ham.DoorClips) c = ReadDoorClipD1(reader);

        reader.ReadElementCount();
        for (auto& r : ham.Robots) r = ReadRobotD1(reader);

        reader.ReadElementCount();
        for (auto& j : ham.RobotJoints) j = ReadRobotJoint(reader);

        reader.ReadElementCount();
        for (auto& w : ham.Weapons) w = ReadWeaponD1(reader);

        reader.ReadElementCount();
        for (auto& w : ham.Powerups) w = ReadPowerup(reader);

        auto numModels = reader.ReadElementCount();
        ham.Models.resize(numModels);
        for (auto& m : ham.Models) m = ReadModelInfo(reader);
        for (auto& m : ham.Models) ReadModelData(reader, m, &palette);

        for (auto& w : ham.Gauges) w = (TexID)reader.ReadInt16();

        ham.DyingModels.resize(85);
        ham.DeadModels.resize(85);

        for (int i = 0; i < 85; i++)
            ham.DyingModels[i] = (ModelID)reader.ReadInt32();

        for (int i = 0; i < 85; i++)
            ham.DeadModels[i] = (ModelID)reader.ReadInt32();

        for (auto& o : ham.ObjectBitmaps) o = (TexID)reader.ReadInt16();
        for (auto& o : ham.ObjectBitmapPointers) o = reader.ReadInt16();

        ham.PlayerShip = ReadPlayerShip(reader);

        /*auto numCockpits = */reader.ReadElementCount();
        for (int i = 0; i < 4; i++)
            ham.Cockpits[i] = (TexID)reader.ReadInt16();

        // why is this read again?
        reader.ReadBytes(ham.Sounds.data(), 250);
        reader.SeekForward(250); // skip low memory alt sounds

        /*auto numObjects =*/ reader.ReadInt32();

        enum EditorObjectType : ubyte {
            Unknown,
            Robot,
            Hostage,
            Powerup,
            ControlCenter,
            Player,
            Clutter,
            Exit
        };

        struct EditorObject {
            EditorObjectType type;
            sbyte id;
            float strength;
        };

        List<EditorObject> objectTypes;
        objectTypes.resize(100);
        for (auto& o : objectTypes) o.type = (EditorObjectType)reader.ReadByte();
        for (auto& o : objectTypes) o.id = reader.ReadByte();
        for (auto& o : objectTypes) o.strength = reader.ReadFix();

        ham.FirstMultiplayerBitmap = reader.ReadInt32();
        auto& reactor = ham.Reactors[0];
        reactor.Guns = reader.ReadInt32();
        ASSERT(reactor.Guns == 4);
        reactor.Model = ModelID(39); // Hard code the model because it's missing from the ham file
        for (int i = 0; i < 4; i++) reactor.GunPoints[i] = reader.ReadVector();
        for (int i = 0; i < 4; i++) reactor.GunDirs[i] = reader.ReadVector();

        ham.ExitModel = (ModelID)reader.ReadInt32();
        ham.DestroyedExitModel = (ModelID)reader.ReadInt32();

        // texture translation table for low memory mode. skip it
        for (int i = 0; i < 1800; i++)
            reader.ReadInt16();

        reader.Seek(dataOffset);

        auto numBitmaps = reader.ReadElementCount();
        auto numSounds = reader.ReadElementCount();

        PigFile pig;
        pig.Entries.resize(numBitmaps + 1);

        // Skip entry 1 as it is meant to be an invalid / error texture
        for (int i = 1; i < pig.Entries.size(); i++)
            pig.Entries[i] = ReadD1BitmapHeader(reader, (TexID)i);

        SoundFile sounds;
        sounds.Sounds.resize(numSounds);
        sounds.Frequency = 11025;

        for (auto& sound : sounds.Sounds) {
            sound.Name = reader.ReadString(8);
            sound.Length = reader.ReadInt32();
            sound.DataLength = reader.ReadInt32();
            sound.Offset = reader.ReadInt32();
        }

        sounds.DataStart = pig.DataStart = reader.Position();
        return std::make_tuple(std::move(ham), std::move(pig), std::move(sounds));
    }
}