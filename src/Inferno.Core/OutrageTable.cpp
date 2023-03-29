#include "pch.h"
#include "OutrageTable.h"

namespace Inferno::Outrage {
    constexpr auto PAGENAME_LEN = 35;

    enum PageType {
        PAGETYPE_TEXTURE = 1,
        PAGETYPE_DOOR = 5,
        PAGETYPE_SOUND = 7,
        PAGETYPE_GENERIC = 10,
    };

    constexpr int MAX_STRING_LEN = 256;
    constexpr int MAX_MODULENAME_LEN = 32;
    constexpr int MAX_DESCRIPTION_LEN = 1024;

    SoundInfo ReadSoundPage(StreamReader& r) {
        constexpr int KNOWN_VERSION = 1;
        auto version = r.ReadInt16();
        if (version > KNOWN_VERSION)
            throw Exception("Unsupported texture info version");

        SoundInfo si{};
        si.Name = r.ReadCString(PAGENAME_LEN);
        si.FileName = r.ReadCString(PAGENAME_LEN);
        si.Flags = r.ReadInt32();
        si.LoopStart = r.ReadInt32();
        si.LoopEnd = r.ReadInt32();
        si.OuterConeVolume = r.ReadFloat();
        si.InnerConeAngle = r.ReadInt32();
        si.OuterConeAngle = r.ReadInt32();
        si.MaxDistance = r.ReadFloat();
        si.MinDistance = r.ReadFloat();
        si.ImportVolume = r.ReadFloat();
        return si;
    }

    TextureInfo ReadTexturePage(StreamReader& r) {
        constexpr int KNOWN_VERSION = 7;
        auto version = r.ReadInt16();
        if (version > KNOWN_VERSION)
            throw Exception("Unsupported texture info version");

        TextureInfo tex{};
        tex.Name = r.ReadCString(MAX_STRING_LEN);
        tex.FileName = r.ReadCString(MAX_STRING_LEN);
        r.ReadCString(MAX_STRING_LEN);
        tex.Color.x = r.ReadFloat();
        tex.Color.y = r.ReadFloat();
        tex.Color.z = r.ReadFloat();
        tex.Color.w = r.ReadFloat();

        tex.Speed = r.ReadFloat();
        tex.Slide.x = r.ReadFloat();
        tex.Slide.y = r.ReadFloat();
        tex.Reflectivity = r.ReadFloat();

        tex.Corona = r.ReadByte();
        tex.Damage = r.ReadInt32();

        tex.Flags = (TextureFlag)r.ReadInt32();

        if (tex.Procedural()) {
            for (int i = 0; i < 255; i++)
                /*tex.Procedural.Palette[i] = */r.ReadInt16();
                
            r.ReadByte(); // heat
            r.ReadByte(); // light
            r.ReadByte(); // thickness
            r.ReadFloat(); // eval time
            if (version >= 6) {
                r.ReadFloat(); // osc time
                r.ReadByte(); // osc value
            }
            int n = r.ReadInt16(); // elements
            for (int i = 0; i < n; i++) {
                r.ReadByte(); // type
                r.ReadByte(); // frequency
                r.ReadByte(); // speed
                r.ReadByte(); // size
                r.ReadByte(); // x1
                r.ReadByte(); // y1
                r.ReadByte(); // x2
                r.ReadByte(); // y2
            }
        }

        if (version >= 5) {
            if (version < 7)
                r.ReadInt16();
            else
                tex.Sound = r.ReadCString(MAX_STRING_LEN);
            r.ReadFloat();
        }
        return tex;
    }

    PhysicsInfo ReadPhysicsInfo(StreamReader& r) {
        PhysicsInfo phys;
        phys.Mass = r.ReadFloat();
        phys.Drag = r.ReadFloat();
        phys.FullThrust = r.ReadFloat();
        phys.Flags = r.ReadInt32();
        phys.RotDrag = r.ReadFloat();
        phys.FullRotThrust = r.ReadFloat();
        phys.NumBounces = r.ReadInt32();
        phys.Velocity.z = r.ReadFloat();
        phys.RotVel = r.ReadVector3();
        phys.WiggleAmplitude = r.ReadFloat();
        phys.WigglesPerSec = r.ReadFloat();
        phys.CoeffRestitution = r.ReadFloat();
        phys.HitDieDot = r.ReadFloat();
        phys.MaxTurnrollRate = r.ReadFloat();
        phys.TurnrollRatio = r.ReadFloat();
        return phys;
    }

    LightInfo ReadLightInfo(StreamReader& r) {
        LightInfo light;
        light.LightDistance = r.ReadFloat();
        light.Color1 = Color(r.ReadVector3());
        light.TimeInterval = r.ReadFloat();
        light.FlickerDistance = r.ReadFloat();
        light.DirectionalDot = r.ReadFloat();
        light.Color2 = Color(r.ReadVector3());
        light.Flags = r.ReadInt32();
        light.TimeBits = r.ReadInt32();
        light.Angle = r.ReadByte();
        light.LightingRenderType = r.ReadByte();
        return light;
    }

    AIInfo ReadAIInfo(StreamReader& r, int version, GenericFlag genFlags) {
        AIInfo ai;
        ai.Flags = (AIFlag)r.ReadInt32();
        ai.AIClass = r.ReadByte();
        ai.AIType = r.ReadByte();
        ai.MovementType = r.ReadByte();
        ai.MovementSubtype = r.ReadByte();
        ai.FOV = r.ReadFloat();
        ai.MaxVelocity = r.ReadFloat();
        ai.MaxDeltaVelocity = r.ReadFloat();
        ai.MaxTurnRate = r.ReadFloat();
        ai.NotifyFlags = (AINotifyFlag)r.ReadInt32() | AINotifyFlag::AlwaysOn;
        ai.MaxDeltaTurnRate = r.ReadFloat();
        ai.CircleDistance = r.ReadFloat();
        ai.AttackVelPercent = r.ReadFloat();
        ai.DodgePercent = r.ReadFloat();
        ai.DodgeVelPercent = r.ReadFloat();
        ai.FleeVelPercent = r.ReadFloat();
        ai.MeleeDamage[0] = r.ReadFloat();
        ai.MeleeDamage[1] = r.ReadFloat();
        ai.MeleeLatency[0] = r.ReadFloat();
        ai.MeleeLatency[1] = r.ReadFloat();
        ai.Curiousity = r.ReadFloat();
        ai.NightVision = r.ReadFloat();
        ai.FogVision = r.ReadFloat();
        ai.LeadAccuracy = r.ReadFloat();
        ai.LeadVarience = r.ReadFloat();
        ai.FireSpread = r.ReadFloat();
        ai.FightTeam = r.ReadFloat();
        ai.FightSame = r.ReadFloat();
        ai.Agression = r.ReadFloat();
        ai.Hearing = r.ReadFloat();
        ai.Frustration = r.ReadFloat();
        ai.Roaming = r.ReadFloat();
        ai.LifePreservation = r.ReadFloat();
        if (version < 16) {
            if ((bool)(genFlags & GenericFlag::UsesPhysics) && ai.MaxVelocity > 0) {
                ai.Flags |= AIFlag::AutoAvoidFriends;
                ai.AvoidFriendsDistance = ai.CircleDistance * 0.1f;
                if (ai.AvoidFriendsDistance > 4.0f)
                    ai.AvoidFriendsDistance = 4.0f;
            } else
                ai.AvoidFriendsDistance = 4.0f;
        } else
            ai.AvoidFriendsDistance = r.ReadFloat();
        if (version < 17) {
            ai.BiasedFlightImportance = 0.5f;
            ai.BiasedFlightMin = 10.0f;
            ai.BiasedFlightMax = 50.0f;
        } else {
            ai.BiasedFlightImportance = r.ReadFloat();
            ai.BiasedFlightMin = r.ReadFloat();
            ai.BiasedFlightMax = r.ReadFloat();
        }
        return ai;
    }

    AnimInfo ReadAnimInfo(StreamReader& r, int version) {
        AnimInfo anim;
        for (int i = 0; i < NUM_MOVEMENT_CLASSES; i++)
            for (int j = 0; j < NUM_ANIMS_PER_CLASS; j++) {
                auto& elem = anim.Classes[i].Elems[j];
                if (version < 20) {
                    elem.From = r.ReadByte();
                    elem.To = r.ReadByte();
                } else {
                    elem.From = r.ReadInt16();
                    elem.To = r.ReadInt16();
                }
                elem.Speed = r.ReadFloat();
            }
        return anim;
    }

    DeathInfo ReadDeathInfo(StreamReader& r) {
        DeathInfo dt;
        dt.Flags = r.ReadInt32();
        dt.DelayMin = r.ReadFloat();
        dt.DelayMax = r.ReadFloat();
        dt.Probabilities = r.ReadByte();
        return dt;
    }

    WeaponBatteryInfo ReadWeaponBatteryInfo(StreamReader& r, int version) {
        WeaponBatteryInfo wb;

        wb.EnergyUsage = r.ReadFloat();
        wb.AmmoUsage = r.ReadFloat();
        for (int i = 0; i < MAX_WB_GUNPOINTS; i++)
            wb.GPWeaponIndex[i] = r.ReadInt16();

        for (int i = 0; i < MAX_WB_FIRING_MASKS; i++) {
            wb.GPFireMasks[i] = r.ReadByte();
            wb.GPFireWait[i] = r.ReadFloat();
            wb.AnimTime[i] = r.ReadFloat();
            wb.AnimStartFrame[i] = r.ReadFloat();
            wb.AnimFireFrame[i] = r.ReadFloat();
            wb.AnimEndFrame[i] = r.ReadFloat();
        }
        wb.NumMasks = r.ReadByte();
        wb.AimingGPIndex = r.ReadInt16();
        wb.AimingFlags = r.ReadByte();
        wb.Aiming3DDot = r.ReadFloat();
        wb.Aiming3DDist = r.ReadFloat();
        wb.AimingXZDot = r.ReadFloat();
        wb.Flags = version < 2 ? r.ReadByte() : r.ReadInt16();
        wb.GPQuadFireMask = r.ReadByte();
        return wb;
    }

    GenericInfo ReadGenericPage(StreamReader& r) {
        constexpr int KNOWN_VERSION = 27;
        auto version = r.ReadInt16();
        if (version > KNOWN_VERSION)
            throw Exception("Unsupported generic info version");

        GenericInfo gen{};
        gen.Type = (ObjectType)r.ReadByte();
        gen.Name = r.ReadCString(PAGENAME_LEN);
        gen.ModelName = r.ReadCString(PAGENAME_LEN);
        gen.MedModelName = r.ReadCString(PAGENAME_LEN);
        gen.LoModelName = r.ReadCString(PAGENAME_LEN);
        gen.ImpactSize = r.ReadFloat();
        gen.ImpactTime = r.ReadFloat();
        gen.Damage = r.ReadFloat();
        gen.Score = version < 24 ? r.ReadByte() : r.ReadInt16();

        if (gen.Type == ObjectType::Powerup) {
            if (version < 25)
                gen.AmmoCount = 0;
            else
                gen.AmmoCount = r.ReadInt16();
        } else
            gen.AmmoCount = 0;

        r.ReadCString(MAX_STRING_LEN); // old script name
        if (version >= 18)
            gen.ModuleName = r.ReadCString(MAX_MODULENAME_LEN);
        if (version >= 19)
            gen.ScriptNameOverride = r.ReadCString(PAGENAME_LEN);
        if (r.ReadByte())
            gen.Description = r.ReadCString(MAX_DESCRIPTION_LEN);

        gen.IconName = r.ReadCString(PAGENAME_LEN);
        gen.MedLodDistance = r.ReadFloat();
        gen.LoLodDistance = r.ReadFloat();

        gen.Physics = ReadPhysicsInfo(r);
        gen.Size = r.ReadFloat();
        gen.Light = ReadLightInfo(r);

        gen.HitPoints = r.ReadInt32();
        gen.Flags = (GenericFlag)r.ReadInt32();
        gen.AI = ReadAIInfo(r, version, gen.Flags);

        for (int i = 0; i < MAX_DSPEW_TYPES; i++) {
            gen.DSpewFlags = r.ReadByte();
            gen.DSpewPercent[i] = r.ReadFloat();
            gen.DSpewNumber[i] = r.ReadInt16();
            gen.DSpewGenericNames[i] = r.ReadCString(PAGENAME_LEN);
        }

        gen.Anim = ReadAnimInfo(r, version);

        for (int i = 0; i < MAX_WBS_PER_OBJ; i++)
            gen.WeaponBatteries[i] = ReadWeaponBatteryInfo(r, version);

        for (int i = 0; i < MAX_WBS_PER_OBJ; i++)
            for (int j = 0; j < MAX_WB_GUNPOINTS; j++)
                gen.WBWeaponNames[i][j] = r.ReadCString(PAGENAME_LEN);

        for (int i = 0; i < MAX_OBJ_SOUNDS; i++)
            gen.SoundNames[i] = r.ReadCString(PAGENAME_LEN);

        if (version < 26)
            r.ReadCString(PAGENAME_LEN); // unused sound

        for (int i = 0; i < MAX_AI_SOUNDS; i++)
            gen.AISoundNames[i] = r.ReadCString(PAGENAME_LEN);

        for (int i = 0; i < MAX_WBS_PER_OBJ; i++)
            for (int j = 0; j < MAX_WB_FIRING_MASKS; j++)
                gen.WBSoundNames[i][j] = r.ReadCString(PAGENAME_LEN);

        for (int i = 0; i < NUM_MOVEMENT_CLASSES; i++)
            for (int j = 0; j < NUM_ANIMS_PER_CLASS; j++)
                gen.AnimSoundNames[i][j] = r.ReadCString(PAGENAME_LEN);

        gen.RespawnScalar = version >= 21 ? r.ReadFloat() : 1.0f;

        if (version >= 22) {
            int n = r.ReadInt16();
            for (int i = 0; i < n; i++)
                gen.DeathTypes.push_back(ReadDeathInfo(r));
        }

        if (version < 20 &&
            (gen.Type == ObjectType::Robot || gen.Type == ObjectType::Building) &&
            gen.HasFlag(GenericFlag::ControlAI) && gen.HasFlag(GenericFlag::Destroyable))
            gen.Score = gen.HitPoints * 3;

        return gen;
    }

    GameTable GameTable::Read(StreamReader& r) {
        GameTable table{};

        while (!r.EndOfStream()) {
            auto pageType = r.ReadByte();
            auto pageStart = r.Position();
            auto len = r.ReadInt32();
            if (len <= 0) throw Exception("bad page length");

            switch (pageType) {
                case PAGETYPE_TEXTURE:
                    table.Textures.push_back(ReadTexturePage(r));
                    break;

                case PAGETYPE_SOUND:
                    table.Sounds.push_back(ReadSoundPage(r));
                    break;

                case PAGETYPE_GENERIC:
                    table.Generics.push_back(ReadGenericPage(r));
                    break;
            }

            //auto readbytes = r.Position() - pageStart;
            r.Seek(pageStart + len); // seek to next chunk (prevents read errors due to individual chunks)
        }

        return table;
    }
}
