#include "pch.h"
#include "Level.h"
#include "Streams.h"

namespace Inferno {
    class LevelWriter {
    public:
        size_t Write(StreamWriter& writer, const Level& level) {
            writer.Write(MakeFourCC("LVLP"));
            writer.Write(level.Version);

            auto offsets = (int32)writer.Position();

            writer.Write((int32)0); // mine data offset
            writer.Write((int32)0); // game data offset

            if (level.Version >= 8) {
                // Dummy vertigo data
                writer.Write(0);
                writer.Write((int16)0);
                writer.Write((ubyte)0);
            }

            if (level.Version < 5)
                writer.Write(0); // hostage text pointer

            WriteVersionSpecificLevelInfo(writer, level);

            auto mineDataOffset = (int32)writer.Position();
            WriteMineData(writer, level);

            auto gameDataOffset = (int32)writer.Position();
            auto size = WriteGameData(writer, level); // end of file

            auto hostageTextOffset = (int32)writer.Position();

            // Go back and write offsets
            writer.Seek(offsets);
            writer.Write(mineDataOffset);
            writer.Write(gameDataOffset);

            if (level.Version >= 8)
                writer.SeekForward(7); // Skip Vertigo data

            if (level.Version < 5)
                writer.Write(hostageTextOffset);

            return size;
        }

    private:
        void WriteVersionSpecificLevelInfo(StreamWriter& writer, const Level& level) {
            if (level.Version >= 2)
                writer.WriteNewlineTerminatedString(level.Palette, 13);

            if (level.Version >= 3)
                writer.Write((int32)level.BaseReactorCountdown);

            if (level.Version >= 4)
                writer.Write(level.ReactorStrength > 0 ? (int32)level.ReactorStrength : int32(-1));

            if (level.Version >= 7) {
                writer.Write((int32)level.FlickeringLights.size());
                for (auto& light : level.FlickeringLights) {
                    writer.Write(light.Tag.Segment);
                    writer.Write(light.Tag.Side);
                    writer.Write(light.Mask);
                    writer.WriteFix(light.Timer);
                    writer.WriteFix(light.Delay);
                }
            }

            if (level.Version >= 6) {
                writer.Write((int32)level.SecretExitReturn);
                // Secret return matrix is serialized in a different order from every other matrix in the RDL/RL2 format
                writer.WriteVector(level.SecretReturnOrientation.Right());
                writer.WriteVector(level.SecretReturnOrientation.Forward());
                writer.WriteVector(level.SecretReturnOrientation.Up());
            }
        }

        void WriteDynamicLights(StreamWriter& writer, const Level& level, LevelFileInfo& info) {
            info.DeltaLightIndices.Count = (int32)level.LightDeltaIndices.size();
            info.DeltaLightIndices.Offset = (int32)writer.Position();
            info.DeltaLightIndices.ElementSize = 6;

            for (auto& index : level.LightDeltaIndices) {
                assert(index.Index != -1);
                writer.Write((int16)index.Tag.Segment);
                writer.Write((uint8)index.Tag.Side);
                writer.Write((uint8)index.Count);
                writer.Write((int16)index.Index);
            }

            AssertDataSize(writer, info.DeltaLightIndices);

            info.DeltaLights.Count = (int32)level.LightDeltas.size();
            info.DeltaLights.Offset = (int32)writer.Position();
            info.DeltaLights.ElementSize = 8;

            for (auto& delta : level.LightDeltas) {
                writer.Write((int16)delta.Tag.Segment);
                writer.Write((ubyte)delta.Tag.Side);
                writer.Write((ubyte)0); // dummy - probably used for dword alignment

                for (int i = 0; i < 4; i++) {
                    auto l = FloatToFix(Desaturate(delta.Color[i]));
                    writer.Write((ubyte)(l / 2048));
                }
            }

            AssertDataSize(writer, info.DeltaLights);
        }

        ubyte GetSegmentBitMask(const Level& level, const Segment& segment) {
            ubyte mask = 0;

            auto HasSpecialData = [&] {
                if (level.Version > 5) return true; // Special light is always in special data
                if (segment.Type != SegmentType::None) return true;
                return false;
            };

            for (auto i = 0; i < MAX_SIDES; i++) {
                if (segment.Connections[i] != SegID::None || segment.Connections[i] == SegID::Exit)
                    mask |= (ubyte)(1 << i);

                if (HasSpecialData())
                    mask |= (1 << MAX_SIDES);
            }

            return mask;
        }

        bool SegmentIsFuelcen(const Segment& segment) {
            switch (segment.Type) {
                case SegmentType::Energy:
                case SegmentType::Repair:
                case SegmentType::Reactor:
                case SegmentType::Matcen:
                    return true;
                default:
                    return false;
            }
        }

        void WriteSegmentSpecialData(StreamWriter& writer, const Level& level, const Segment& segment) {
            writer.Write((ubyte)segment.Type);
            //ubyte matcenIndex = segment.Matcen == nullptr ? (ubyte)0xFF : level.Matcens(segment.Matcen);
            writer.Write((ubyte)segment.Matcen);

            if (level.Version > 5)
                writer.Write((ubyte)segment.Value);
            else
                writer.Write((int16)segment.Value);

            if (level.Version > 5) {
                writer.Write((ubyte)segment.S2Flags);
                Color desaturate;
                segment.VolumeLight.AdjustSaturation(0, desaturate);
                writer.WriteFix(desaturate.x * 12);
            }
        }

        void WriteSegmentVertices(StreamWriter& writer, const Segment& segment) {
            writer.Write(segment.Indices);
        }

        void WriteSegmentConnections(StreamWriter& writer, const Segment& segment) {
            for (auto& connection : segment.Connections) {
                if (connection != SegID::None)
                    writer.Write(connection);
            }
        }

        void WriteWalls(StreamWriter& writer, const Segment& segment) {
            ubyte mask = 0;
            for (short i = 0; i < MAX_SIDES; i++) {
                if (segment.Sides[i].Wall != WallID::None)
                    mask |= 1 << i;
            }

            writer.Write(mask);

            for (auto& side : segment.Sides) {
                if (side.Wall == WallID::None) continue;
                assert(side.Wall < WallID::Max);
                writer.Write((ubyte)side.Wall);
            }
        }

        void WriteSegmentTextures(StreamWriter& writer, const Segment& seg) {
            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);
                auto conn = seg.GetConnection(sid);
                if ((conn == SegID::None && conn != SegID::Exit) ||
                    side.Wall != WallID::None) {

                    // Writing None causes the file to be corrupted
                    auto tmap = (int16)(side.TMap == LevelTexID::None ? LevelTexID::Unset : side.TMap);
                    auto tmap2 = (int16)(side.TMap2 == LevelTexID::None ? LevelTexID::Unset : side.TMap2);

                    if (tmap2 != 0) tmap |= 0x8000;
                    writer.Write(tmap);

                    if (tmap2 != 0) {
                        tmap2 |= (uint16)side.OverlayRotation << 14;
                        writer.Write(tmap2);
                    }

                    for (int i = 0; i < 4; i++) {
                        auto u = FloatToFix(side.UVs[i].x);
                        auto v = FloatToFix(side.UVs[i].y);
                        auto l = FloatToFix(Desaturate(side.Light[i]));
                        writer.Write((int16)(u >> 5));
                        writer.Write((int16)(v >> 5));
                        writer.Write((int16)(l >> 1));
                    }
                }
            }
        }

        void WriteMineData(StreamWriter& writer, const Level& level) {
            writer.Write((ubyte)0); // Compiled mine version
            writer.Write((int16)level.Vertices.size());
            writer.Write((int16)level.Segments.size());

            for (auto& vertex : level.Vertices)
                writer.WriteVector(vertex);

            for (auto& segment : level.Segments) {
                auto bitMask = GetSegmentBitMask(level, segment);
                bool hasSpecialData = bitMask & (1 << MAX_SIDES);
                writer.Write(bitMask);

                if (level.Version == 5) {
                    if (hasSpecialData)
                        WriteSegmentSpecialData(writer, level, segment);

                    WriteSegmentVertices(writer, segment);
                    WriteSegmentConnections(writer, segment);
                }
                else {
                    WriteSegmentConnections(writer, segment);
                    WriteSegmentVertices(writer, segment);

                    if (level.Version <= 1 && hasSpecialData)
                        WriteSegmentSpecialData(writer, level, segment);
                }

                if (level.Version <= 5) {
                    auto l = Desaturate(segment.VolumeLight);
                    writer.Write((ushort)(FloatToFix(l * 12) >> 4));
                }
                WriteWalls(writer, segment);
                WriteSegmentTextures(writer, segment);
            }

            if (level.Version > 5) {
                for (auto& segment : level.Segments)
                    WriteSegmentSpecialData(writer, level, segment);
            }

        }

        void WriteLevelFileInfo(StreamWriter& writer, const LevelFileInfo& info) {
            writer.Write(info.Signature);
            writer.Write(info.GameVersion);
            writer.Write(info.Size);
            writer.WriteString(info.FileName, 15);
            writer.Write(info.LevelNumber);
            writer.Write(info.PlayerOffset);
            writer.Write(info.PlayerSize);
            writer.Write(info.Objects);
            writer.Write(info.Walls);
            writer.Write(info.Doors);
            writer.Write(info.Triggers);
            writer.Write(info.Links);
            writer.Write(info.ReactorTriggers);
            writer.Write(info.Matcen);

            if (info.GameVersion >= 29) {
                writer.Write(info.DeltaLightIndices);
                writer.Write(info.DeltaLights);
            }
        }

        void WriteObject(StreamWriter& writer, const Level& level, const Object& obj) {
            if (obj.Type == ObjectType::SecretExitReturn) return;
            writer.Write(obj.Type);
            writer.Write(obj.ID); // subtype
            writer.Write(obj.Control.Type);
            writer.Write(obj.Movement.Type);
            writer.Write(obj.Render.Type);
            writer.Write((sbyte)obj.Flags);
            writer.Write(obj.Segment);
            writer.WriteVector(obj.Position());
            writer.WriteMatrix(obj.Transform);
            writer.WriteFix(obj.Radius);
            writer.WriteFix(obj.Shields);
            writer.WriteVector(obj.last_pos);
            writer.Write(obj.Contains);

            switch (obj.Movement.Type) {
                case MovementType::Physics:
                {
                    auto& physics = obj.Movement.Physics;
                    writer.WriteVector(physics.Velocity);
                    writer.WriteVector(physics.Thrust);

                    writer.WriteFix(physics.Mass);
                    writer.WriteFix(physics.Drag);
                    writer.WriteFix(physics.Brakes);

                    writer.WriteVector(physics.AngularVelocity);
                    writer.WriteVector(physics.RotThrust);
                    writer.WriteAngle(physics.TurnRoll);

                    writer.Write((int16)physics.Flags);
                    break;
                }

                case MovementType::Spinning:
                    writer.WriteVector(obj.Movement.SpinRate);
                    break;
            }

            switch (obj.Control.Type) {
                case ControlType::AI:
                {
                    auto& ai = obj.Control.AI;
                    writer.Write(ai.Behavior);

                    for (auto& flag : ai.Flags)
                        writer.Write(flag);

                    writer.Write(ai.HideSegment);
                    writer.Write(ai.HideIndex);
                    writer.Write(ai.PathLength);
                    writer.Write(ai.CurrentPathIndex);

                    if (level.GameVersion <= 25)
                        writer.Write((int32)0); // These are supposed to be the path start and end for robots with the "FollowPath" AI behavior in Descent 1, but these fields are unused

                    break;
                }
                case ControlType::Explosion:
                    writer.WriteFix(obj.Control.Explosion.SpawnTime);
                    writer.WriteFix(obj.Control.Explosion.DeleteTime);
                    writer.Write(obj.Control.Explosion.DeleteObject);
                    break;
                case ControlType::Powerup:
                    if (level.GameVersion >= 25)
                        writer.Write(obj.Control.Powerup.Count);
                    break;
                case ControlType::Light:
                    writer.WriteFix(obj.Control.Light.Intensity);
                    break;
                case ControlType::Weapon:
                    writer.Write((int16)obj.Control.Weapon.ParentType);
                    writer.Write((int16)obj.Control.Weapon.Parent);
                    writer.Write((int32)obj.Control.Weapon.ParentSig);
                    break;
                case ControlType::None:
                case ControlType::Flying:
                case ControlType::Debris:
                case ControlType::Slew: //the player is generally saved as slew
                case ControlType::Reactor:
                    break;
                default:
                    throw Exception("Unknown control type");
            };

            switch (obj.Render.Type) {
                case RenderType::None:
                case RenderType::Laser:
                    break;
                case RenderType::Morph:
                case RenderType::Polyobj:
                {
                    auto& model = obj.Render.Model;
                    writer.Write(model.ID);

                    for (auto& angle : model.Angles)
                        writer.WriteAngles(angle);

                    writer.Write((int32)model.subobj_flags);
                    writer.Write((int32)model.TextureOverride);
                    break;
                }
                case RenderType::WeaponVClip:
                case RenderType::Hostage:
                case RenderType::Powerup:
                case RenderType::Fireball:
                    writer.Write(obj.Render.VClip.ID);
                    writer.WriteFix(obj.Render.VClip.FrameTime);
                    writer.Write(obj.Render.VClip.Frame);
                    break;
                default:
                    throw Exception("Unknown render type");
            }
        }

        void WriteTriggerTargets(StreamWriter& writer, const std::array<Tag, MAX_TRIGGER_TARGETS>& targets) {
            for (auto& target : targets)
                writer.Write((int16)target.Segment);

            for (auto& target : targets)
                writer.Write((int16)target.Side);
        }

        void WriteTrigger(StreamWriter& writer, const Level& level, const Trigger& trigger) {
            if (level.Version > 1) {
                // Descent 2
                writer.Write((sbyte)trigger.Type);
                writer.Write((sbyte)trigger.Flags);
                writer.Write((sbyte)trigger.Targets.Count());
                writer.Write((sbyte)0);
                writer.Write((int32)trigger.Value);
                writer.Write((int32)trigger.Time);
            }
            else {
                // Descent 1
                // Note that the sizes are different between D1 and D2
                writer.Write((sbyte)trigger.Type);
                writer.Write((int16)trigger.FlagsD1);
                writer.Write((int32)trigger.Value);
                writer.Write((int32)trigger.Time);
                writer.Write((sbyte)0);
                writer.Write((int16)trigger.Targets.Count());
            }

            WriteTriggerTargets(writer, trigger.Targets.data());
        }

#ifdef _DEBUG
        void AssertDataSize(const StreamWriter& writer, const GameDataHeader& data) {
            if (data.Offset == -1) return;
            //auto written = writer.Position() - data.Offset;
            //auto expected = data.ElementSize * data.Count;
            assert(writer.Position() - data.Offset == data.ElementSize * data.Count);
        };
#else
        void AssertDataSize(const StreamWriter&, const GameDataHeader&) {};
#endif

        void WritePofData(StreamWriter& writer, const Level& level) {
            int pofCount = 0;
            //string pofFile;

            if (level.IsDescent1()) {
                writer.Write((int16)25); // does not match the actual model count
                pofCount = 78;
                //pofFile = "pofs1.dat";
            }
            else {
                writer.Write((int16)166);
                pofCount = 166;
                //pofFile = "pofs2.dat";
            }

            //std::ifstream file(pofFile, std::ios::binary);
            //if (!file) throw Exception("pofs data file does not exist");

            //auto size = filesystem::file_size(pofFile);
            //List<ubyte> buffer(size);
            //if (!file.read((char*)buffer.data(), size))
            //    throw Exception("Error reading pof data");

            //writer.WriteBytes(buffer);
            // fill POF table with garbage so non-robot polymodels (reactors) load properly
            for (int i = 0; i < pofCount; i++)
                writer.WriteString("inferno.pof", 13);
        }

        size_t WriteGameData(StreamWriter& writer, const Level& level) {
            auto offset = writer.Position();

            LevelFileInfo info{};
            info.GameVersion = level.GameVersion;
            info.Doors.ElementSize = 16;

            WriteLevelFileInfo(writer, info);
            info.Size = (int32)(writer.Position() - offset);

            if (info.GameVersion >= 14) {
                if (info.GameVersion >= 31)
                    writer.WriteNewlineTerminatedString(level.Name, Level::MaxNameLength + 1);
                else
                    writer.WriteCString(level.Name, Level::MaxNameLength + 1);
            }

            WritePofData(writer, level);


            // Player info (empty)
            info.PlayerOffset = (int32)writer.Position();

            // Objects
            info.Objects.Offset = (int32)writer.Position();
            info.Objects.Count = (int32)level.Objects.size();
            info.Objects.ElementSize = 264;
            if (level.HasSecretExit() && level.IsDescent2())
                info.Objects.Count--;

            for (auto& obj : level.Objects)
                WriteObject(writer, level, obj);

            //AssertDataSize(writer, info.Objects); // object size varies based on type

            // Walls
            info.Walls.Offset = (int32)(level.Walls.size() > 0 ? writer.Position() : -1);
            info.Walls.Count = (int32)level.Walls.size();
            info.Walls.ElementSize = 24;

            // Wall triggers are written before object triggers, so we have to filter
            for (auto& wall : level.Walls) {
                writer.Write((int32)wall.Tag.Segment);
                writer.Write((int32)wall.Tag.Side);
                writer.WriteFix(wall.HitPoints);
                writer.Write((int32)wall.LinkedWall);
                writer.Write((sbyte)wall.Type);
                writer.Write((sbyte)wall.Flags);
                writer.Write((sbyte)wall.State);
                writer.Write((sbyte)wall.Trigger);
                writer.Write((sbyte)wall.Clip);
                writer.Write((sbyte)wall.Keys);
                writer.Write((sbyte)wall.ControllingTrigger);
                writer.Write((sbyte)wall.cloak_value);
            }

            AssertDataSize(writer, info.Walls);

            // Triggers
            info.Triggers.Offset = (int32)(level.Triggers.size() > 0 ? writer.Position() : -1);
            info.Triggers.Count = (int32)level.Triggers.size();
            info.Triggers.ElementSize = level.IsDescent1() ? 54 : 52;

            for (auto& trigger : level.Triggers)
                WriteTrigger(writer, level, trigger);

            AssertDataSize(writer, info.Triggers);

            // Reactor triggers
            info.ReactorTriggers.Offset = (int32)writer.Position();
            info.ReactorTriggers.Count = 1;
            info.ReactorTriggers.ElementSize = 42; // Is actually total size

            writer.Write((int16)level.ReactorTriggers.Count());
            WriteTriggerTargets(writer, level.ReactorTriggers.data());
            AssertDataSize(writer, info.ReactorTriggers);

            // Matcens
            info.Matcen.Offset = (int32)writer.Position();
            info.Matcen.Count = (int32)level.Matcens.size();
            info.Matcen.ElementSize = level.GameVersion > 25 ? 20 : 16;

            for (auto& m : level.Matcens) {
                writer.Write(m.Robots);
                if (level.GameVersion > 25)
                    writer.Write(m.Robots2);

                writer.Write(m.HitPoints);
                writer.Write(m.Interval);
                writer.Write(m.Segment);
                writer.Write(m.Producer);
            }

            AssertDataSize(writer, info.Matcen);

            if (level.GameVersion >= 29)
                WriteDynamicLights(writer, level, info);

            auto size = writer.Position();

            writer.Seek(offset);
            WriteLevelFileInfo(writer, info);

            return size;
        }
    };

    // Writes level data to a stream. Returns the number of bytes written.
    size_t WriteLevel(const Level& level, StreamWriter& writer) {
        LevelWriter levelWriter;
        return levelWriter.Write(writer, level);
    }

    size_t Level::Serialize(StreamWriter& writer) {
        LevelWriter levelWriter;
        GameVersion = IsDescent1() ? 25 : 32; // Always use the latest version
        return levelWriter.Write(writer, *this);
    }
}