#include "pch.h"
#include "Level.h"
#include "Streams.h"
#include "Utility.h"
#include "Pig.h"
#include "spdlog/spdlog.h"

namespace Inferno {
    void ReadLevelInfo(StreamReader& reader, Level& level) {
        if (level.Version >= 2)
            level.Palette = reader.ReadStringToNewline(13);

        level.BaseReactorCountdown = level.Version >= 3 ? reader.ReadInt32() : 30;
        level.ReactorStrength = level.Version >= 4 ? reader.ReadInt32() : -1;

        if (level.Version >= 7) {
            auto numFlickeringLights = reader.ReadInt32();
            for (int i = 0; i < numFlickeringLights; i++) {
                auto& light = level.FlickeringLights.emplace_back();
                light.Tag = { (SegID)reader.ReadInt16(), (SideID)reader.ReadInt16() };
                light.Mask = reader.ReadUInt32();
                light.Timer = reader.ReadFix();
                light.Delay = reader.ReadFix();
            }
        }

        if (level.Version >= 6) {
            level.SecretExitReturn = (SegID)reader.ReadInt32();
            // Secret return matrix is serialized in a different order from every other matrix in the RDL/RL2 format
            level.SecretReturnOrientation.Right(reader.ReadVector());
            level.SecretReturnOrientation.Forward(reader.ReadVector());
            level.SecretReturnOrientation.Up(reader.ReadVector());
        }
    }

    // Descent 1 and 2 level reader
    class LevelReader {
        StreamReader _reader;
        int16 _gameVersion = 0;
        int _mineDataOffset;
        int _gameDataOffset;
        int _levelVersion;

        GameDataHeader _deltaLights{}, _deltaLightIndices{};

        // This map is filled while reading sides and maps wallId to all
        // sides (Tags) that reference it.
        // Normally this is one to one relation. Now we (may)
        // allow all closed walls without a trigger to be one wall
        // referenced by many sides to save WallID's that are limited by 255.
        // While reading such a file we need to "unpack" shared walls.
        std::unordered_map<WallID, std::vector<Tag>> wallToTag_;

    public:
        LevelReader(span<ubyte> data) : _reader(data) {}

        Level Read(WallsSerialization wallsSerialization) {
            auto sig = (uint)_reader.ReadInt32();
            if (sig != MakeFourCC("LVLP"))
                throw Exception("File is not a level (bad header)");

            _levelVersion = _reader.ReadInt32();
            if (_levelVersion > 8)
                throw Exception("D2X-XL levels are not supported");

            _mineDataOffset = _reader.ReadInt32();
            _gameDataOffset = _reader.ReadInt32();

            if (_mineDataOffset == 0 || _gameDataOffset == 0)
                throw Exception("Level data is missing");

            if (_levelVersion >= 8) {
                // Dummy Vertigo-related data
                _reader.ReadInt32();
                _reader.ReadInt16();
                _reader.ReadByte();
            }

            if (_levelVersion < 5) {
                // Hostage text offset - not used
                _reader.ReadInt32();
            }

            Level level{ _levelVersion, wallsSerialization };

            ReadLevelInfo(_reader, level);
            ReadSegments(level);
            ReadGameData(level);
            ReadDynamicLights(level);

            for (auto& seg : level.Segments) {
                seg.UpdateGeometricProps(level);
            }

            return level;
        }

    private:
        void ReadSegmentData(Segment& seg) {
            auto bitMask = _reader.ReadByte();

            for (uint bit = 0; bit < MAX_SIDES; bit++)
                seg.Connections[bit] = bitMask & (1 << bit) ? SegID(_reader.ReadInt16()) : SegID::None;
        }

        void ReadSegmentVertices(Segment& seg) {
            for (auto& i : seg.Indices)
                i = _reader.ReadInt16();
        }

        void ReadSegmentSpecial(StreamReader& reader, Segment& seg) {
            seg.Type = (SegmentType)reader.ReadByte();
            if (seg.Type >= SegmentType::Count)
                throw Exception("Segment type is invalid");

            seg.Matcen = (MatcenID)reader.ReadByte();
            seg.Value = _levelVersion > 5 ? reader.ReadByte() : (sbyte)reader.ReadInt16();

            if (_levelVersion > 5) {
                seg.S2Flags = reader.ReadByte(); // Ambient sound flag, overwritten at runtime
                // 24 light samples per segment. 12 = 24/2 due to conversion from fix to float
                auto light = reader.ReadFix() / 12;
                seg.VolumeLight = Color(light, light, light);
            }
        }

        void ReadSegmentTextures(Segment& seg) {
            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);

                // Solid face or a wall
                if (seg.GetConnection(sid) == SegID::None ||
                    side.Wall != WallID::None) {

                    auto tmap = _reader.ReadUInt16();
                    side.TMap = LevelTexID(tmap & 0x7fff);
                    if (tmap & 0x8000) {
                        auto tmap2 = _reader.ReadUInt16();
                        side.TMap2 = LevelTexID(tmap2 & 0x3fff);
                        side.OverlayRotation = OverlayRotation(((tmap2 & 0xC000) >> 14) & 3);
                    }

                    for (int i = 0; i < 4; i++) {
                        auto u = fix(_reader.ReadInt16()) << 5;
                        auto v = fix(_reader.ReadInt16()) << 5;
                        auto l = fix(_reader.ReadUInt16()) << 1;
                        side.UVs[i].x = FixToFloat(u);
                        side.UVs[i].y = FixToFloat(v);
                        auto light = FixToFloat(l);
                        side.Light[i] = Color(light, light, light);
                    }
                }
            }
        }

        void ReadSegmentConnections(Segment& seg, ubyte bitMask) {
            for (uint bit = 0; bit < MAX_SIDES; bit++)
                seg.Connections[bit] = bitMask & (1 << bit) ? (SegID)_reader.ReadInt16() : SegID::None;
        }

        void ReadSegmentWalls(Segment& seg, SegID id) {
            auto mask = _reader.ReadByte();

            for (int i = 0; i < MAX_SIDES; i++) {
                auto& side = seg.Sides[i];

                if (mask & (1 << i)) {
                    side.Wall = WallID(_reader.ReadByte());
                    ////see the comment for wallToTag_
                    wallToTag_[side.Wall].emplace_back(id, static_cast<SideID>(i));
                }
            }
        }

        void ReadSegments(Level& level) {
            _reader.Seek(_mineDataOffset);
            //OutputDebugString(std::format(L"Mine data offset: {}\n", _reader.Position()).c_str());

            // Header
            _reader.ReadByte(); // compiled mine version, unused
            const auto vertexCount = _reader.ReadInt16();
            const auto segmentCount = _reader.ReadInt16();

            level.Vertices.resize(vertexCount);
            level.Segments.resize(segmentCount);

            for (auto& v : level.Vertices)
                v = _reader.ReadVector();

            size_t segmentId = 0;
            for (auto& seg : level.Segments) {
                auto bitMask = _reader.ReadByte();
                bool hasSpecialData = bitMask & (1 << MAX_SIDES);

                if (level.Version == 5) {
                    if (hasSpecialData)
                        ReadSegmentSpecial(_reader, seg);

                    ReadSegmentVertices(seg);
                    ReadSegmentConnections(seg, bitMask);
                }
                else {
                    ReadSegmentConnections(seg, bitMask);
                    ReadSegmentVertices(seg);

                    if (level.Version <= 1 && hasSpecialData)
                        ReadSegmentSpecial(_reader, seg);
                }

                if (level.Version <= 5) {
                    auto light = FixToFloat(fix(_reader.ReadUInt16() << 4)) / 2;
                    seg.VolumeLight = Color(light, light, light);
                }

                ReadSegmentWalls(seg, static_cast<SegID>(segmentId));
                ReadSegmentTextures(seg);
                ++segmentId;
            }

            // D2 retail location for segment special data
            if (level.Version > 5) {
                for (auto& seg : level.Segments)
                    ReadSegmentSpecial(_reader, seg);
            }
        }

        Object ReadObject() {
            Object obj{};
            obj.Type = (ObjectType)_reader.ReadByte();
            obj.ID = _reader.ReadByte();
            obj.Control.Type = (ControlType)_reader.ReadByte();
            obj.Movement.Type = (MovementType)_reader.ReadByte();
            obj.Render.Type = (RenderType)_reader.ReadByte();
            obj.Flags = (ObjectFlag)_reader.ReadByte();

            obj.Segment = (SegID)_reader.ReadInt16();
            obj.Position = _reader.ReadVector();
            obj.Rotation = obj.LastRotation = _reader.ReadRotation();
            obj.Radius = _reader.ReadFix();
            obj.Shields = _reader.ReadFix();
            obj.LastPosition = _reader.ReadVector();

            obj.Contains.Type = (ObjectType)_reader.ReadByte();
            obj.Contains.ID = _reader.ReadByte();
            obj.Contains.Count = _reader.ReadByte();

            switch (obj.Movement.Type) {
                case MovementType::Physics:
                {
                    auto& phys = obj.Movement.Physics;
                    phys.Velocity = _reader.ReadVector();
                    phys.Thrust = _reader.ReadVector();

                    phys.Mass = _reader.ReadFix();
                    phys.Drag = _reader.ReadFix();
                    phys.Brakes = _reader.ReadFix();

                    phys.AngularVelocity = _reader.ReadVector();
                    phys.AngularThrust = _reader.ReadVector();

                    phys.TurnRoll = _reader.ReadFixAng();
                    phys.Flags = (PhysicsFlag)_reader.ReadInt16();
                    break;
                }

                case MovementType::Spinning:
                    obj.Movement.SpinRate = _reader.ReadVector();
                    break;

                case MovementType::None:
                    break;

                default:
                    throw Exception("Unknown movement type");
            }

            switch (obj.Control.Type) {
                case ControlType::AI:
                {
                    auto& ai = obj.Control.AI;
                    ai.Behavior = (AIBehavior)_reader.ReadByte();

                    for (auto& i : ai.Flags)
                        i = _reader.ReadByte();

                    ai.HideSegment = (SegID)_reader.ReadInt16();
                    ai.HideIndex = _reader.ReadInt16();
                    ai.PathLength = _reader.ReadInt16();
                    ai.CurrentPathIndex = _reader.ReadInt16();

                    if (_gameVersion <= 25)
                        _reader.ReadInt32(); // These are supposed to be the path start and end for robots with the "FollowPath" AI behavior in Descent 1, but these fields are unused
                    break;
                }

                case ControlType::Explosion:
                {
                    auto& expl = obj.Control.Explosion;
                    expl.SpawnTime = _reader.ReadFix();
                    expl.DeleteTime = _reader.ReadFix();
                    expl.DeleteObject = (ObjID)_reader.ReadInt16();
                    expl.NextAttach = expl.PrevAttach = expl.Parent = ObjID::None;
                    break;
                }

                case ControlType::Weapon:
                {
                    auto& weapon = obj.Control.Weapon;
                    weapon.ParentType = (ObjectType)_reader.ReadInt16();
                    weapon.Parent = (ObjID)_reader.ReadInt16();
                    weapon.ParentSig = (ObjSig)_reader.ReadInt32();
                    break;
                }

                case ControlType::Light:
                    obj.Control.Light.Intensity = _reader.ReadFix();
                    break;

                case ControlType::Powerup:
                {
                    auto& powerup = obj.Control.Powerup;
                    powerup.Count = _reader.ReadInt32();
                    break;
                }

                case ControlType::None:
                case ControlType::Flying:
                case ControlType::Debris:
                case ControlType::Slew: //the player is generally saved as slew
                case ControlType::Reactor:
                    break;

                case ControlType::Morph:
                case ControlType::FlyThrough:
                case ControlType::Repaircen:
                default:
                    throw Exception("Unknown control type");
            }

            switch (obj.Render.Type) {
                case RenderType::None:
                    break;

                case RenderType::Morph:
                case RenderType::Model:
                {
                    auto& model = obj.Render.Model;
                    model.ID = (ModelID)_reader.ReadInt32();

                    for (auto& angles : model.Angles)
                        angles = _reader.ReadAngleVec();

                    model.subobj_flags = _reader.ReadInt32();
                    model.TextureOverride = (LevelTexID)_reader.ReadInt32();
                    break;
                }

                case RenderType::WeaponVClip:
                case RenderType::Hostage:
                case RenderType::Powerup:
                case RenderType::Fireball:
                {
                    auto& vclip = obj.Render.VClip;
                    vclip.ID = (VClipID)_reader.ReadInt32();
                    vclip.FrameTime = _reader.ReadFix();
                    vclip.Frame = _reader.ReadByte();
                    break;
                }

                case RenderType::Laser:
                    break;

                default:
                    throw Exception("Unknown render type");
            }

            return obj;
        }

        void VerifyObject() {}

        Wall ReadWall() {
            Wall w{};
            auto segment = (SegID)_reader.ReadInt32();
            auto side = (SideID)_reader.ReadInt32();
            w.Tag = { segment, side };
            w.HitPoints = _reader.ReadFix();
            w.LinkedWall = WallID(_reader.ReadInt32());
            w.Type = (WallType)_reader.ReadByte();
            w.Flags = (WallFlag)_reader.ReadByte();
            w.State = (WallState)_reader.ReadByte();
            w.Trigger = (TriggerID)_reader.ReadByte();
            w.Clip = (WClipID)_reader.ReadByte();
            w.Keys = (WallKey)_reader.ReadByte();
            w.ControllingTrigger = (TriggerID)_reader.ReadByte();
            w.cloak_value = _reader.ReadByte();
            return w;
        }

        void ReadTriggerTargets(std::array<Tag, MAX_TRIGGER_TARGETS>& targets) {
            for (auto& target : targets)
                target.Segment = (SegID)_reader.ReadInt16();

            for (auto& target : targets)
                target.Side = (SideID)_reader.ReadInt16();
        }

        Trigger ReadTrigger() {
            Trigger trigger = {};

            if (_levelVersion > 1) {
                // Descent 2
                trigger.Type = (TriggerType)_reader.ReadByte();
                trigger.Flags = (TriggerFlag)_reader.ReadByte();
                trigger.Targets.Count(_reader.ReadByte());
                /*trigger.linkNum = */_reader.ReadByte();
                trigger.Value = _reader.ReadInt32();
                trigger.Time = _reader.ReadInt32();
            }
            else {
                // Descent 1
                trigger.Type = (TriggerType)_reader.ReadByte();
                trigger.FlagsD1 = (TriggerFlagD1)_reader.ReadInt16();
                trigger.Value = _reader.ReadInt32();
                trigger.Time = _reader.ReadInt32();
                /*trigger.linkNum = */_reader.ReadByte();
                trigger.Targets.Count(_reader.ReadInt16());
            }

            ReadTriggerTargets(trigger.Targets.data());
            return trigger;
        }

        Matcen ReadMatcen() {
            Matcen m;
            m.Robots = _reader.ReadInt32();
            if (_gameVersion > 25)
                m.Robots2 = _reader.ReadInt32();
            m.HitPoints = _reader.ReadInt32();
            m.Interval = _reader.ReadInt32();
            m.Segment = (SegID)_reader.ReadInt16();
            m.Producer = _reader.ReadInt16();
            return m;
        }

        void ReadDynamicLights(Level& level) {
            if (_deltaLights.Offset != -1) {
                _reader.Seek(_deltaLights.Offset);

                for (int32 i = 0; i < _deltaLights.Count; i++) {
                    auto& delta = level.LightDeltas.emplace_back();
                    delta.Tag.Segment = (SegID)_reader.ReadInt16();
                    delta.Tag.Side = (SideID)_reader.ReadByte();
                    _reader.ReadByte(); // dummy - probably used for dword alignment

                    for (int j = 0; j < 4; j++) {
                        // Vertex deltas scaled by 2048 - see DL_SCALE in segment.h
                        auto light = FixToFloat(fix(_reader.ReadByte() * 2048));
                        delta.Color[j] = Color(light, light, light, 0.0f);
                    }
                }
            }

            if (_deltaLightIndices.Offset != -1) {
                _reader.Seek(_deltaLightIndices.Offset);

                for (int32 i = 0; i < _deltaLightIndices.Count; i++) {
                    auto& index = level.LightDeltaIndices.emplace_back();
                    index.Tag.Segment = (SegID)_reader.ReadInt16();
                    index.Tag.Side = (SideID)_reader.ReadByte();
                    index.Count = _reader.ReadByte();
                    index.Index = _reader.ReadInt16();
                }
            }
        }

        void ReadGameData(Level& level) {
            _reader.Seek(_gameDataOffset);

            auto sig = _reader.ReadInt16();
            if (sig != 0x6705)
                throw Exception("Level game data signature is invalid");

            _gameVersion = _reader.ReadInt16();
            level.GameVersion = _gameVersion;

            if (_gameVersion < 22)
                throw Exception("Level game data version is invalid");

            // Skip parts of the former header
            // size, mineFilename, level number, player offset, player size
            _reader.SeekForward(31);

            auto ReadHeader = [this]() {
                return GameDataHeader{ _reader.ReadInt32(), _reader.ReadInt32(), _reader.ReadInt32() };
            };

            auto objects = ReadHeader();
            auto walls = ReadHeader();
            auto doors = ReadHeader();
            auto triggers = ReadHeader();
            auto links = ReadHeader();
            auto reactorTriggers = ReadHeader();
            auto matcens = ReadHeader();

            //level.Walls.resize(walls.Count);
            level.Triggers.resize(triggers.Count);
            level.Objects.resize(objects.Count);
            level.Matcens.resize(matcens.Count);

            if (_gameVersion >= 29) {
                _deltaLightIndices = ReadHeader();
                _deltaLights = ReadHeader();
            }

            level.Name = _reader.ReadStringToNewline(Level::MaxNameLength + 1);

            // Read object file names (unused)
            //auto pofNames = _reader.ReadInt16();
            //level.Pofs.resize(pofNames);

            //for (auto& pof : level.Pofs)
            //    pof = _reader.ReadString(13);

            // Objects
            _reader.Seek(objects.Offset);

            for (auto& obj : level.Objects) {
                obj = ReadObject();
                VerifyObject(); // TODO: Actually verify the object
            }

            // Walls
            if (walls.Offset != -1) {
                _reader.Seek(walls.Offset);
                for (size_t i=0; i<walls.Count; ++i)
                    level.Walls.Append(ReadWall());
                try { 
                    //see the comment for wallToTag_
                    if (auto n = level.CreateClosed(wallToTag_)) {
                        SPDLOG_INFO(std::string("Found shared walls in the file, ") + std::to_string(n) + " re-created");
                        if (level.Walls.Overfilled())
                            throw Exception("The file contains too many walls, try activating shared closed walls option");
                    }
                }
                catch (Exception const& e) {
                    SPDLOG_ERROR(e.what());
                    throw;
                }
            }

            if (triggers.Offset != -1) {
                _reader.Seek(triggers.Offset);
                for (auto& t : level.Triggers)
                    t = ReadTrigger();
            }

            // temporary code to repair the trigger id bug: 
            // TriggerID::None (255) has been decremented when removing a trigger
            // leading to many walls having a non-none controlling trigger
            // that does not exist. If deletion is repeated many times the non-existing
            // trigger ids could even become existing ids potentially causing
            // unexpected level behavior
            for (auto& w : level.Walls) {
                if ((int)w.ControllingTrigger >= level.Triggers.size())
                    w.ControllingTrigger = TriggerID::None;
            }
            //just in case: check all trigger targets
            size_t counter = 0;
            for (auto& t : level.Triggers) {
                for (auto& tar : t.Targets) {
                    if (auto w = level.TryGetWall(tar))
                        w->ControllingTrigger = (TriggerID)counter;
                }
                ++counter;
            }
            //end of temporary repair


            // Control center triggers
            if (reactorTriggers.Offset != -1) {
                _reader.Seek(reactorTriggers.Offset);
                level.ReactorTriggers.Count(_reader.ReadInt16());
                ReadTriggerTargets(level.ReactorTriggers.data());
            }

            for (auto& m : level.Matcens)
                m = ReadMatcen();
        }
    };

    Level Level::Deserialize(span<ubyte> data, WallsSerialization serialization) {
        LevelReader reader(data);
        return reader.Read(serialization);
    }
}