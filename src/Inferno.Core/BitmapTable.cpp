#include "pch.h"
#include "BitmapTable.h"
#include <spdlog/spdlog.h>
#include "EffectClip.h"
#include "HamFile.h"
#include "Robot.h"
#include "Streams.h"
#include "Types.h"

/*
 * A bitmap table stores metadata for objects and images. Alternative format of a HAM.
 * It is used by the D1 demo and original editor.
 */
namespace Inferno {
    enum class TableChunk {
        None = -1,
        Cockpit = 0,
        Object = 1,
        Textures = 2,
        Unused = 3,
        VClip = 4,
        Effects = 5,
        EClip = 6,
        Weapon = 7,
        Demo = 8,
        RobotAI = 9,
        Sound = 10,
        Ship = 11,
        WallAnims = 12,
        WClip = 13,
        Robot = 14,
        Powerup = 15,
        Gauges = 20
    };

    string ReadLine(StreamReader& reader) {
        constexpr int MAX_LINE_LEN = 600;
        auto line = reader.ReadStringToNewline(MAX_LINE_LEN);
        DecodeText(span((ubyte*)line.data(), line.length()));
        
        if (auto index = line.find(';'); index > -1)
            line = line.substr(0, index);

        return line;
    }

    ModelID FindModelID(const HamFile& ham, string_view name) {
        auto index = Seq::findIndex(ham.Models, [&name](const Model& model) {
            return model.FileName == name;
        });

        return index ? ModelID(*index) : ModelID::None;
    }

    const Model* FindModel(const HamFile& ham, string_view name) {
        return Seq::find(ham.Models, [&name](const Model& model) {
            return model.FileName == name;
        });
    }

    void RobotSetAngles(RobotInfo& robot, const Model& model, HamFile& ham) {
        int guns[MAX_SUBMODELS]{}; //which gun each submodel is part of
        robot.Guns = (uint8)model.Guns.size();

        for (int m = 0; m < model.Submodels.size(); m++)
            guns[m] = robot.Guns; //assume part of body...

        guns[0] = -1; //body never animates, at least for now

        for (int g = 0; g < model.Guns.size(); g++) {
            int m = model.Guns[g].Submodel;

            // Recursively search submodels
            while (m != 0) {
                guns[m] = g; //...unless we find it in a gun
                m = model.Submodels[m].Parent;
            }
        }

        for (int g = 0; g < robot.Guns + 1; g++) {
            for (int state = 0; state < N_ANIM_STATES; state++) {
                robot.Joints[g][state].Count = 0;
                robot.Joints[g][state].Offset = (short)ham.RobotJoints.size();

                for (short m = 0; m < model.Submodels.size(); m++) {
                    if (guns[m] == g) {
                        auto& joints = ham.RobotJoints.emplace_back();
                        joints.ID = m;
                        joints.Angle = model.Animation[state][m];
                        robot.Joints[g][state].Count++;
                    }
                }
            }
        }
    }

    void ReadBitmapTable(span<byte> data, const PigFile& pig, HamFile& ham, const SoundFile& sounds) {
        StreamReader reader(data);

        ham.DyingModels.resize(ham.Models.size());
        ranges::fill(ham.DyingModels, ModelID::None);

        auto chunkType = TableChunk::None;

        struct ModelInfo {
            string name;
            List<string> textures;
        };

        List<ModelInfo> models;
        ham.AllTexIdx.resize(pig.Entries.size());
        uint totalTextures = 0;
        List<string> allocatedTextures;
        bool redhulk = false;

        //auto allocateLevelTexture = [&ham, &allocatedTextures](const string& name) {
        //    if (auto index = Seq::indexOf(allocatedTextures, name)) {
        //        return Tuple{ &ham.LevelTextures[*index], LevelTexID(*index) };
        //    }

        //    auto id = LevelTexID(ham.LevelTextures.size());
        //    auto levelTexture = &ham.LevelTextures.emplace_back();
        //    allocatedTextures.push_back(name);
        //    return Tuple{ levelTexture, id };
        //};

        while (!reader.EndOfStream()) {
            auto line = ReadLine(reader);
            bool skip = line.starts_with('@');
            //auto superx = line.find("superx=") > -1;
            auto tokens = String::Split(line, ' ', true);
            //fmt::println("{}", line);
            if (skip) tokens[0] = tokens[0].substr(1);
            if (tokens[0].starts_with('!')) continue; // Skip editor annotations

            auto findTokenIndex = [&tokens](string_view name) {
                for (int i = 1; i < tokens.size(); i++) {
                    if (tokens[i].starts_with(name)) return i;
                }

                return -1;
            };

            auto readLineValue = []<typename T>(span<string> tokens, string_view name, T& dest) {
                // Skip first token because it is the identifier or name
                for (int i = 1; i < tokens.size(); i++) {
                    if (!tokens[i].starts_with(name)) continue;
                    auto tokenValue = string_view(tokens[i]).substr(name.length() + 1);
                    auto begin = tokenValue.data();
                    auto end = tokenValue.data() + tokenValue.length();

                    if constexpr (std::is_same_v<std::string, T>) {
                        dest = tokenValue;
                        return true;
                    }
                    else if constexpr (std::is_enum_v<T>) {
                        if (std::from_chars(begin, end, (std::underlying_type_t<T>&)dest).ec == std::errc())
                            return true;
                    }
                    else if (std::from_chars(begin, end, dest).ec == std::errc()) {
                        return true;
                    }
                }

                return false;
            };

            auto readTokenValue = [&tokens, &readLineValue]<typename T>(string_view name, T& dest) {
                return readLineValue(tokens, name, dest);
            };

            // Reads a token in the format `value=1 2 3 4 5`
            auto readTokenArray = [&tokens]<typename T>(string_view name, Array<T, 5>& dest) {
                uint count = 0;

                for (uint i = 1; i < tokens.size(); i++) {
                    auto& token = tokens[i];
                    if (count >= dest.size()) break;

                    if (count == 0) {
                        if (!token.starts_with(name)) continue;
                        auto tokenValue = string_view(token).substr(name.length() + 1);
                        auto begin = tokenValue.data();
                        auto end = tokenValue.data() + tokenValue.length();

                        std::from_chars(begin, end, dest[0]);
                    }
                    else {
                        std::from_chars(token.data(), token.data() + token.length(), dest[count]);
                    }

                    count++;
                }
            };

            auto readTextures = [&tokens](size_t start, ModelInfo& model) {
                bool foundTextures = false;
                for (size_t i = start; i < tokens.size(); i++) {
                    auto& token = tokens[i];

                    if (token.ends_with(".bbm") || token.starts_with("%")) {
                        foundTextures = true;
                        model.textures.push_back(token);
                    }
                    else if (foundTextures) {
                        break; // Already started reading textures, stop once something else shows up
                    }
                }
            };

            // Reads an array of consecutive values
            auto readArray = [&tokens]<typename T>(uint startIndex, Array<T, 5>& dest) {
                uint read = 0;
                for (uint i = startIndex; i < tokens.size() && read < dest.size(); i++) {
                    auto& token = tokens[i];
                    std::from_chars(token.data(), token.data() + token.length(), dest[read++]);
                }

                return read;
            };

            auto maybeChunkType = [&tokens, &skip] {
                if (tokens.empty()) return TableChunk::None;

                switch (String::Hash(tokens[0])) {
                    case String::Hash("$ROBOT"): return TableChunk::Robot;
                    case String::Hash("$ROBOT_AI"): return TableChunk::RobotAI;
                    case String::Hash("$OBJECT"): return TableChunk::Object;
                    case String::Hash("$PLAYER_SHIP"): return TableChunk::Ship;
                    case String::Hash("$POWERUP"): return TableChunk::Powerup;
                    case String::Hash("$POWERUP_UNUSED"):
                        skip = true;
                        return TableChunk::Powerup;
                    case String::Hash("$SOUND"): return TableChunk::Sound;
                    case String::Hash("$COCKPIT"): return TableChunk::Cockpit;
                    case String::Hash("$GAUGES"): return TableChunk::Gauges;
                    case String::Hash("$WEAPON"): return TableChunk::Weapon;
                    case String::Hash("$DOOR_ANIMS"):
                    case String::Hash("$WALL_ANIMS"):
                        return TableChunk::WallAnims;
                    case String::Hash("$TEXTURES"): return TableChunk::Textures;
                    case String::Hash("$VCLIP"): return TableChunk::VClip;
                    case String::Hash("$ECLIP"): return TableChunk::EClip;
                    case String::Hash("$WCLIP"): return TableChunk::WClip;
                    case String::Hash("$EFFECTS"): return TableChunk::Effects;
                    default: return TableChunk::None;
                }
            }();

            if (maybeChunkType != TableChunk::None) {
                chunkType = maybeChunkType;
                if (chunkType == TableChunk::Cockpit ||
                    chunkType == TableChunk::Gauges ||
                    chunkType == TableChunk::Textures ||
                    chunkType == TableChunk::Effects) {
                    //chunkType = TableChunk::None; // otherwise effect clips don't read
                    continue; // Skip lines that are 'headers'
                }
            }

            switch (chunkType) {
                case TableChunk::Robot:
                {
                    auto& robot = ham.Robots.emplace_back();
                    if (skip) continue;

                    readTokenValue("score_value", robot.Score);
                    readTokenValue("mass", robot.Mass);
                    readTokenValue("drag", robot.Drag);
                    readTokenValue("exp1_vclip", robot.ExplosionClip1);
                    readTokenValue("exp1_sound", robot.ExplosionSound1);
                    readTokenValue("exp2_vclip", robot.ExplosionClip2);
                    readTokenValue("exp2_sound", robot.ExplosionSound2);
                    readTokenValue("lighting", robot.Lighting);
                    readTokenValue("weapon_type", robot.WeaponType);
                    readTokenValue("strength", robot.HitPoints);
                    readTokenValue("contains_id", robot.Contains.ID);
                    readTokenValue("contains_count", robot.Contains.Count);
                    readTokenValue("contains_prob", robot.ContainsChance);
                    readTokenValue("see_sound", robot.SeeSound);
                    readTokenValue("attack_sound", robot.AttackSound);
                    readTokenValue("boss", (int&)robot.IsBoss);
                    readTokenValue("attack_type", robot.Attack);
                    readTokenValue("cloak_type", robot.Cloaking);

                    auto& modelInfo = models.emplace_back();
                    modelInfo.name = tokens[1];

                    // Workaround for red and brown hulks sharing the same model
                    if (modelInfo.name == HULK_MODEL_NAME) {
                        if (redhulk) modelInfo.name = RED_HULK_MODEL_NAME;
                        redhulk = true;
                    }

                    robot.Model = FindModelID(ham, modelInfo.name);

                    for (size_t i = 3; i < tokens.size(); i++) {
                        auto& token = tokens[i];
                        if (token.starts_with("simple_model"))
                            break; // don't care about simple models, stop after reaching it

                        if (token.ends_with(".bbm") || token.starts_with('%'))
                            modelInfo.textures.push_back(token);
                    }

                    if (auto model = FindModel(ham, modelInfo.name)) {
                        robot.Guns = (uint8)model->Guns.size();
                        for (size_t i = 0; i < model->Guns.size(); i++) {
                            robot.GunPoints[i] = model->Guns[i].Point;
                            robot.GunSubmodels[i] = model->Guns[i].Submodel;
                        }
                    }

                    break;
                }

                case TableChunk::RobotAI:
                {
                    auto index = std::stoi(tokens[1]);
                    if (!Seq::inRange(ham.Robots, index) || skip) continue;
                    auto& robot = ham.Robots[index];
                    Array<float, 5> fov{}, fireDelay{}, turnTime{}, speed{}, circleDist{};
                    Array<uint8, 5> shots{}, evade{};

                    uint offset = 2;
                    offset += readArray(offset, fov);
                    offset += readArray(offset, fireDelay);
                    offset += readArray(offset, shots);
                    offset += readArray(offset, turnTime);
                    offset += 10; // skip damage and shield
                    offset += readArray(offset, speed);
                    offset += readArray(offset, circleDist);
                    offset += readArray(offset, evade);

                    for (int i = 0; i < 5; i++) {
                        robot.Difficulty[i].FieldOfView = ConvertFov(fov[i]);
                        robot.Difficulty[i].FireDelay = fireDelay[i];
                        robot.Difficulty[i].ShotCount = shots[i];
                        robot.Difficulty[i].TurnTime = turnTime[i];
                        robot.Difficulty[i].Speed = speed[i];
                        robot.Difficulty[i].CircleDistance = circleDist[i];
                        robot.Difficulty[i].EvadeSpeed = evade[i];
                    }

                    break;
                }

                case TableChunk::Sound:
                {
                    ASSERT(tokens.size() > 2);
                    auto id = std::stoi(tokens[1]);
                    ham.Sounds.resize(id + 1);
                    if (!skip) {
                        if (auto index = sounds.Find(tokens[2]))
                            ham.Sounds[id] = uint8(*index);
                    }

                    break;
                }

                case TableChunk::Cockpit:
                    ham.Cockpits.push_back(pig.Find(tokens[0]));
                    break;

                case TableChunk::Textures:
                {
                    auto texId = pig.Find(tokens[0]);
                    ham.AllTexIdx[totalTextures] = texId;
                    auto ltid = LevelTexID(totalTextures++);
                    //auto ltid = ham.AllTexIdx.size();
                    //ham.AllTexIdx.push_back(texId);
                    //if (skip) continue;

                    //ham.LevelTextures.resize((int)ltid + 1);
                    auto& levelTexture = ham.LevelTextures.emplace_back();

                    if (texId != TexID::None) {
                        readTokenValue("lighting", levelTexture.Lighting);
                        levelTexture.D1FileName = tokens[0];
                        levelTexture.TexID = texId;
                        levelTexture.ID = ltid;
                    }

                    break;
                }

                case TableChunk::VClip:
                {
                    int clipNum = -1;
                    readTokenValue("clip_num", clipNum);
                    if (clipNum == -1) continue;
                    ham.VClips.resize(clipNum + 1);
                    auto& clip = ham.VClips[clipNum];

                    readTokenValue("time", clip.PlayTime);
                    readTokenValue("sound_num", clip.Sound);

                    auto bmLine = ReadLine(reader);
                    auto frames = pig.FindAnimation(bmLine, (uint)clip.Frames.size());
                    clip.NumFrames = (int16)frames.size();

                    for (int i = 0; i < frames.size(); i++) {
                        clip.Frames[i] = frames[i];
                        pig.Entries[(int)frames[i]].Frame;
                    }

                    clip.FrameTime = clip.PlayTime / clip.NumFrames;

                    int rod = 0;
                    if (readTokenValue("rod_flag", rod) && rod)
                        clip.Flags |= VClipFlag::AxisAligned;

                    break;
                }

                case TableChunk::EClip:
                {
                    // clip_num=5 time=0.50 abm_flag=1 crit_clip=48 dest_bm=blown06.bbm
                    // dest_vclip=3 dest_size=20 dest_eclip=40
                    int clipNum = -1;
                    readTokenValue("clip_num", clipNum);
                    if (clipNum == -1) continue;
                    ham.Effects.resize(clipNum + 1);
                    auto& clip = ham.Effects[clipNum];
                    auto bmLine = ReadLine(reader);

                    int objClip = 0;
                    readTokenValue("obj_eclip", objClip);

                    auto id = LevelTexID::None;
                    LevelTexture* levelTexture = nullptr;

                    if (!objClip) {
                        auto lineTokens = String::Split(bmLine, ' ');
                        id = LevelTexID(ham.LevelTextures.size());
                        levelTexture = &ham.LevelTextures.emplace_back();
                        levelTexture->D1FileName = lineTokens[0];

                        if (String::Contains(bmLine, "volatile"))
                            SetFlag(levelTexture->Flags, TextureFlag::Volatile);

                        readLineValue(lineTokens, "damage", levelTexture->Damage);
                    }

                    SPDLOG_INFO("{} {}", String::Split(bmLine, ' ')[0], (int)id);
                    //    continue; // Don't allocate object clips

                    if (!skip) {
                        readTokenValue("time", clip.VClip.PlayTime);
                        readTokenValue("crit_clip", clip.CritClip);
                        readTokenValue("dest_vclip", clip.DestroyedVClip);
                        readTokenValue("dest_size", clip.ExplosionSize);
                        readTokenValue("dest_eclip", clip.DestroyedEClip);
                        readTokenValue("sound_num", clip.Sound);

                        if (levelTexture)
                            levelTexture->EffectClip = EClipID(clipNum);

                        auto frames = pig.FindAnimation(bmLine, (uint)clip.VClip.Frames.size());
                        clip.VClip.NumFrames = (int)frames.size();

                        for (int i = 0; i < frames.size(); i++) {
                            clip.VClip.Frames[i] = frames[i];
                            pig.Entries[(int)frames[i]].Frame;
                            if (i == 0 && levelTexture) {
                                levelTexture->ID = id;
                                levelTexture->TexID = frames[i];
                                ham.AllTexIdx[(int)id] = frames[i];
                            }
                        }

                        clip.VClip.FrameTime = clip.VClip.PlayTime / frames.size();
                    }

                    // Reserve space for the destroyed texture
                    string destroyedBitmap;
                    if (readTokenValue("dest_bm", destroyedBitmap)) {
                        if (!Seq::contains(allocatedTextures, destroyedBitmap)) {
                            //if (!skip)
                            auto ltid = LevelTexID(ham.LevelTextures.size());
                            clip.DestroyedTexture = ltid;
                            levelTexture->DestroyedTexture = ltid;

                            SPDLOG_INFO("tid: {} destroyed tid: {}", id, (int)clip.DestroyedTexture);
                            auto& texture = ham.LevelTextures.emplace_back();
                            texture.ID = ltid;
                            texture.TexID = pig.Find(destroyedBitmap);
                            ham.AllTexIdx[(int)ltid] = texture.TexID;
                            allocatedTextures.push_back(destroyedBitmap);
                        }
                    }

                    break;
                }

                case TableChunk::WClip:
                {
                    // Doors

                    // $WCLIP clip_num=6 time=1 abm_flag=1 tmap1_flag=1 open_sound=140 close_sound=141
                    // vlighting=0 blastable=1 explodes=1
                    int clipNum = -1;
                    readTokenValue("clip_num", clipNum);
                    if (clipNum == -1) continue;
                    ham.DoorClips.resize(clipNum + 1);
                    auto& clip = ham.DoorClips[clipNum];
                    auto bmLine = ReadLine(reader);

                    // id 372 should point at 1034

                    if (skip) {
                        // add placeholder
                        ham.LevelTextures.emplace_back();
                        //SPDLOG_INFO("SKIP: {}", line);
                        continue;
                    }

                    readTokenValue("time", clip.PlayTime);

                    int tmap1Flag = 0;
                    if (readTokenValue("tmap1_flag", tmap1Flag) && tmap1Flag)
                        SetFlag(clip.Flags, DoorClipFlag::TMap1);

                    int blastable = 0;
                    if (readTokenValue("blastable", blastable) && blastable)
                        SetFlag(clip.Flags, DoorClipFlag::Blastable);

                    int explodes = 0;
                    if (readTokenValue("explodes", explodes) && explodes)
                        SetFlag(clip.Flags, DoorClipFlag::Explodes);

                    readTokenValue("open_sound", clip.OpenSound);
                    readTokenValue("close_sound", clip.CloseSound);

                    auto frames = pig.FindAnimation(bmLine, (uint)clip.Frames.size());
                    clip.NumFrames = (int16)frames.size();

                    for (int i = 0; i < frames.size(); i++) {
                        auto id = LevelTexID(ham.LevelTextures.size());
                        clip.Frames[i] = id;

                        auto& levelTexture = ham.LevelTextures.emplace_back();
                        //SPDLOG_INFO("{}#{} {}", String::Split(bmLine, ' ')[0], i, (int)id);
                        levelTexture.ID = id;
                        levelTexture.TexID = frames[i];
                        ham.AllTexIdx[(int)id] = frames[i];
                    }

                    break;
                }

                case TableChunk::Weapon:
                {
                    // $WEAPON picture=gauge06.bbm weapon_pof=laser1-1.pof weapon_pof_inner=laser1-2.pof
                    // lw_ratio=9.8 simple_model=laser11s.pof simple_model=laser12s.pof mass=0.5
                    // drag=0.0 blob_size=0.0 strength=10 10 10 10 10 flash_vclip=11
                    // flash_size=1.0 flash_sound=13 robot_hit_vclip=59 wall_hit_vclip=1
                    // robot_hit_sound=11 wall_hit_sound=28 impact_size=3.0 speed=120 120 120 120 120
                    // lighted=0 lightcast=.75 energy_usage=0.5 ammo_usage=0.0 fire_wait=0.25 fire_count=1 lifetime=10.0

                    auto& weapon = ham.Weapons.emplace_back();

                    if (!skip) {
                        string picture;
                        if (readTokenValue("picture", picture))
                            weapon.Icon = pig.Find(picture);

                        string pof;
                        if (readTokenValue("weapon_pof", pof)) {
                            weapon.Model = FindModelID(ham, pof);
                            weapon.RenderType = WeaponRenderType::Model;

                            if (!Seq::exists(models, [&pof](const auto& model) { return model.name == pof; })) {
                                auto& modelInfo = models.emplace_back();
                                modelInfo.name = pof;
                                for (size_t i = 1; i < tokens.size(); i++) {
                                    auto& token = tokens[i];
                                    if (token.ends_with(".bbm") || token.starts_with('%'))
                                        modelInfo.textures.push_back(token);
                                }
                            }
                        }

                        string pofInner;
                        if (readTokenValue("weapon_pof_inner", pofInner))
                            weapon.ModelInner = FindModelID(ham, pofInner);

                        if (readTokenValue("weapon_vclip", weapon.WeaponVClip)) {
                            weapon.RenderType = WeaponRenderType::VClip;
                        }

                        readTokenValue("mass", weapon.Mass);
                        readTokenValue("drag", weapon.Drag);
                        readTokenValue("blob_size", weapon.BlobSize);

                        string blobBmp;
                        if (readTokenValue("blob_bmp", blobBmp)) {
                            weapon.RenderType = weapon.RenderType = WeaponRenderType::Blob;
                            weapon.BlobBitmap = pig.Find(blobBmp);
                        }

                        readTokenValue("flash_vclip", weapon.FlashVClip);
                        readTokenValue("flash_size", weapon.FlashSize);
                        readTokenValue("flash_sound", weapon.FlashSound);
                        readTokenValue("robot_hit_vclip", weapon.RobotHitVClip);
                        readTokenValue("wall_hit_vclip", weapon.WallHitVClip);
                        readTokenValue("robot_hit_sound", weapon.RobotHitSound);
                        readTokenValue("wall_hit_sound", weapon.WallHitSound);
                        readTokenValue("impact_size", weapon.ImpactSize);
                        readTokenValue("energy_usage", weapon.EnergyUsage);
                        readTokenValue("ammo_usage", weapon.AmmoUsage);
                        readTokenValue("fire_wait", weapon.FireDelay);
                        readTokenValue("fire_count", weapon.FireCount);
                        readTokenValue("lifetime", weapon.Lifetime);
                        readTokenValue("homing", (int&)weapon.IsHoming);
                        readTokenValue("damage_radius", weapon.SplashRadius);

                        readTokenArray("strength", weapon.Damage);
                        readTokenArray("speed", weapon.Speed);
                    }
                    break;
                }

                case TableChunk::Powerup:
                {
                    // $POWERUP name="Vulcan" vclip_num=37 hit_sound=83 size=4.0
                    auto& powerup = ham.Powerups.emplace_back();
                    if (skip) continue;

                    powerup.Size = 3;
                    powerup.Light = 1 / 3.0f;
                    readTokenValue("vclip_num", powerup.VClip);
                    readTokenValue("hit_sound", powerup.HitSound);
                    readTokenValue("size", powerup.Size);
                    break;
                }

                case TableChunk::Object:
                {
                    string type;
                    readTokenValue("type", type);

                    if (type == "controlcen") {
                        // $OBJECT reactor.pof type=controlcen exp_vclip=3 exp_sound=33 lighting=0.6
                        // %50 %51 %32 rmap03.bbm %52
                        // dead_pof=reactor2.pof strength=200.1 rmap03.bbm rbot056.bbm rbot057.bbm
                        auto& reactor = ham.Reactors.emplace_back();
                        reactor.Model = FindModelID(ham, tokens[1]);

                        auto& reactorModel = models.emplace_back();
                        auto& destroyedReactorModel = models.emplace_back();
                        reactorModel.name = tokens[1];
                        readTextures(2, reactorModel);

                        if (auto destIndex = findTokenIndex("dead_pof"); destIndex != -1) {
                            readTokenValue("dead_pof", destroyedReactorModel.name);
                            readTextures(destIndex, destroyedReactorModel);
                        }

                        if (string deadModel; readTokenValue("dead_pof", deadModel))
                            ham.DeadModels[(int)reactor.Model] = FindModelID(ham, deadModel);

                        // Copy gunpoints
                        if (auto pof = FindModel(ham, tokens[1])) {
                            for (size_t i = 0; i < pof->Guns.size(); i++) {
                                reactor.GunPoints[i] = pof->Guns[i].Point;
                                reactor.GunDirs[i] = pof->Guns[i].Normal;
                                reactor.GunPoints[i].z *= -1;
                                reactor.Guns++;
                            }
                        }
                    }
                    else if (type == "exit") {
                        // $OBJECT exit01.pof type=exit steel1.bbm rbot061.bbm rbot062.bbm
                        // dead_pof=exit01d.pof steel1.bbm rbot061.bbm rbot063.bbm
                        ham.ExitModel = FindModelID(ham, tokens[1]);
                        auto& exit = models.emplace_back();
                        exit.name = tokens[1];
                        readTextures(2, exit);

                        if (string deadModel; readTokenValue("dead_pof", deadModel)) {
                            ham.DestroyedExitModel = FindModelID(ham, deadModel);

                            auto destIndex = findTokenIndex("dead_pof");
                            auto& destroyedExit = models.emplace_back();
                            destroyedExit.name = deadModel;
                            readTextures(destIndex, destroyedExit);
                        }
                    }

                    break;
                }

                case TableChunk::Ship:
                {
                    // $PLAYER_SHIP mass=4.0 drag=0.033 max_thrust=7.8 wiggle=0.5 max_rotthrust=0.14
                    // model=pship1.pof glow04.bbm ship1-1.bbm ship1-2.bbm ship1-3.bbm ship1-4.bbm ship1-5.bbm
                    // dying_pof=pship1b.pof expl_vclip_num=58
                    // simple_model=pship1s.pof glow04.bbm ship1-1.bbm ship1-2.bbm ship1-3.bbm ship1-4.bbm ship1-5.bbm multi_textures ship2-4.bbm ship2-5.bbm ship3-4.bbm ship3-5.bbm ship4-4.bbm ship4-5.bbm ship5-4.bbm ship5-5.bbm ship6-4.bbm ship6-5.bbm ship7-4.bbm ship7-5.bbm ship8-4.bbm ship8-5.bbm
                    auto& ship = ham.PlayerShip;
                    readTokenValue("mass", ship.Mass);
                    readTokenValue("drag", ship.Drag);
                    readTokenValue("max_thrust", ship.MaxThrust);
                    readTokenValue("wiggle", ship.Wiggle);
                    readTokenValue("max_rotthrust", ship.MaxRotationalThrust);
                    readTokenValue("expl_vclip_num", ship.ExplosionVClip);

                    auto& shipModel = models.emplace_back();

                    if (string model; readTokenValue("model", model)) {
                        ship.Model = FindModelID(ham, model);
                        shipModel.name = model;
                        readTextures(2, shipModel);

                        // Copy gunpoints
                        if (auto pof = FindModel(ham, model)) {
                            for (size_t i = 0; i < pof->Guns.size(); i++)
                                ship.GunPoints[i] = pof->Guns[i].Point;
                        }
                    }

                    if (string model; readTokenValue("dying_pof", model)) {
                        auto& deadModel = models.emplace_back();
                        deadModel.name = model;
                        deadModel.textures = shipModel.textures;
                        ham.DyingModels[(int)ship.Model] = FindModelID(ham, model);
                    }

                    break;
                }

                case TableChunk::Gauges:
                {
                    int abm = tokens[0].ends_with(".abm");

                    auto frames = pig.FindAnimation(tokens[0], 30);
                    if (abm && !frames.empty()) {
                        for (auto& frame : frames)
                            ham.Gauges.push_back(frame);
                    }
                    else {
                        ham.Gauges.push_back(pig.Find(tokens[0]));
                    }
                    break;
                }
            }
        }

        for (auto& door : ham.DoorClips) {
            // translate the texids to level texids
            for (size_t i = 0; i < door.NumFrames; i++) {
                if (auto tid = Seq::tryItem(ham.LevelTexIdx, (int)door.Frames[i]))
                    door.Frames[i] = *tid;
            }
        }

        // for each model assign a FirstTexture which indexes ObjectBitmapPointers
        // then add the texids to ObjectBitmaps at that location
        for (auto& modelInfo : models) {
            auto offset = ham.ObjectBitmapPointers.size();

            for (auto& bitmap : modelInfo.textures) {
                bool isEclip = bitmap.starts_with('%');

                if (isEclip) {
                    auto i = std::stoi(bitmap.substr(1));
                    // eclip reference
                    if (auto eclip = Seq::tryItem(ham.Effects, i)) {
                        ham.ObjectBitmapPointers.push_back((uint16)ham.ObjectBitmaps.size());
                        ham.ObjectBitmaps.push_back(eclip->VClip.Frames[0]);
                    }
                }
                else if (auto tid = pig.Find(bitmap); tid != TexID::None) {
                    ham.ObjectBitmapPointers.push_back((uint16)ham.ObjectBitmaps.size());
                    ham.ObjectBitmaps.push_back(tid);
                }
            }

            auto id = FindModelID(ham, modelInfo.name);
            if (id == ModelID::None) continue;
            auto& model = ham.Models[(int)id];
            model.TextureCount = (ubyte)modelInfo.textures.size();
            model.FirstTexture = (ubyte)offset;
        }

        ham.LevelTexIdx.resize(pig.Entries.size());
        ranges::fill(ham.LevelTexIdx, LevelTexID(255));

        for (auto i = 0; i < ham.LevelTextures.size(); i++) {
            ham.LevelTextures[i].ID = LevelTexID(i);
            if (ham.AllTexIdx[i] > TexID::Invalid) {
                ham.LevelTextures[i].TexID = ham.AllTexIdx[i];
                ham.LevelTexIdx.at((int)ham.AllTexIdx[i]) = (LevelTexID)i;
            }
        }

        for (size_t i = 0; i < ham.Robots.size(); i++) {
            auto& robot = ham.Robots[i];
            if (auto model = Seq::tryItem(ham.Models, (int)robot.Model)) {
                RobotSetAngles(robot, *model, ham);
            }
        }

        ham.Weapons.resize(30);
        ham.Weapons[29] = ham.Weapons[19]; // Copy the player smart missile blob to the regular robot smart missile blob location
    }
}
