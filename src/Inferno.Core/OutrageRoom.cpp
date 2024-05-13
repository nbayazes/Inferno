#include "pch.h"

#include "OutrageRoom.h"
#include "Face.h"
#include "Level.h"
#include "OutrageTable.h"
#include "Streams.h"

namespace Inferno {
    namespace {
        constexpr int ROOM_NEW_HEADER_CHUNK = 5;
        constexpr int ROOMFILE_VERSION = 4;

        constexpr int ROOM_VERTEX_CHUNK = 1;
        constexpr int ROOM_FACES_CHUNK = 2;
        constexpr int ROOM_END_CHUNK = 3;
        constexpr int ROOM_TEXTURE_CHUNK = 4;
    }

    // D3 ORF face
    struct RoomFace {
        Vector3 Normal;
        List<short> Vertices; // Indices into the vertex array
        List<Vector2> UVs;
        int16 Texture = 0; // Index into room texture chunk
    };

    void LoadRoom(StreamReader& reader) {
        bool done = false;
        List<Vector3> vertices;
        List<RoomFace> faces;
        List<string> textures;
        int version = -1;

        while (!done) {
            auto command = reader.ReadInt32();
            auto len = reader.ReadInt32();

            switch (command) {
                case ROOM_NEW_HEADER_CHUNK:
                {
                    version = reader.ReadInt32();
                    auto numVerts = reader.ReadInt32();
                    auto numFaces = reader.ReadInt32();
                    vertices.resize(numVerts);
                    faces.resize(numFaces);
                    break;
                }

                case ROOM_VERTEX_CHUNK:
                {
                    for (auto& vert : vertices) {
                        vert.x = reader.ReadFloat();
                        vert.y = reader.ReadFloat();
                        vert.z = reader.ReadFloat();
                    }
                    break;
                }

                case ROOM_TEXTURE_CHUNK:
                {
                    auto count = reader.ReadInt32();
                    for (int i = 0; i < count; i++)
                        textures.push_back(reader.ReadCString(64));
                    break;
                }

                case ROOM_FACES_CHUNK:
                {
                    reader.ReadByte(); // light mult
                    auto nverts = reader.ReadInt32();

                    RoomFace face;
                    face.Normal.x = reader.ReadFloat();
                    face.Normal.y = reader.ReadFloat();
                    face.Normal.z = reader.ReadFloat();
                    face.Vertices.resize(nverts);
                    face.UVs.resize(nverts);

                    reader.ReadInt16(); // tex index

                    for (int i = 0; i < nverts; i++) {
                        face.Vertices[i] = reader.ReadInt16();
                        face.UVs[i].x = reader.ReadFloat();
                        face.UVs[i].y = reader.ReadFloat();
                        reader.ReadFloat();
                        reader.ReadFloat();
                        reader.ReadFloat();
                        reader.ReadFloat();
                        if (version >= 1) {
                            reader.ReadFloat(); // alpha
                        }
                    }

                    return; // Return due to bug with reading ending chunk
                }

                case ROOM_END_CHUNK:
                    done = true;
                    break;

                default:
                    // skip the ones we don't know
                    for (int i = 0; i < len; i++)
                        reader.ReadByte();
                    break;
            }
        }
    }

    void SaveRoom(StreamWriter& writer, const List<Vector3>& vertices, const List<RoomFace>& faces, const Outrage::GameTable& table, span<LevelTexID> textures) {
        // Write header
        writer.Write(ROOM_NEW_HEADER_CHUNK);
        auto headsize = (int)writer.Position();
        writer.Write(-1); // header length

        writer.Write(ROOMFILE_VERSION);
        writer.Write((int)vertices.size());
        writer.Write((int)faces.size());

        auto pos = (int)writer.Position();
        writer.Seek(headsize);
        writer.Write(pos - headsize - 4);
        writer.Seek(pos);

        {
            // write vertex info
            writer.Write(ROOM_VERTEX_CHUNK);
            auto vertsize = (int)writer.Position();
            writer.Write(-1); // placeholder

            for (auto& vert : vertices) {
                writer.WriteFloat(vert.x);
                writer.WriteFloat(vert.y);
                writer.WriteFloat(vert.z);
            }

            pos = (int)writer.Position();
            writer.Seek(vertsize);
            writer.Write<int>(pos - vertsize - 4);
            writer.Seek(pos);
        }

        {
            // write texture info
            writer.Write(ROOM_TEXTURE_CHUNK);
            auto texsize = (int)writer.Position();
            writer.Write(-1); // placeholder

            //texCount++;
            assert(textures.size() > 0);
            auto texCount = (int)textures.size();

            //auto maxIndex = 0;
            writer.Write(texCount); // number of textures

            for (auto& texture : textures) {
                if (auto entry = Seq::tryItem(table.Textures, (int)texture - 3000)) {
                    writer.WriteCString(entry->Name, 64);
                } else {
                    writer.WriteCString("Rainbow Texture", 64);
                }
            }

            pos = (int)writer.Position();
            writer.Seek(texsize);
            writer.Write(pos - texsize - 4);
            writer.Seek(pos);
        }

        writer.Write(ROOM_FACES_CHUNK);
        auto facesize = (int)writer.Position();
        writer.Write(-1); // placeholder

        for (auto& face : faces) {
            writer.Write((sbyte)4); // Light multiplier?
            writer.Write((int32)face.Vertices.size());

            writer.WriteFloat(face.Normal.x);
            writer.WriteFloat(face.Normal.y);
            writer.WriteFloat(face.Normal.z);
            writer.Write(face.Texture); // Texture index

            for (int t = 0; t < face.Vertices.size(); t++) {
                writer.Write(face.Vertices[t]);
                writer.WriteFloat(face.UVs[t].x);
                writer.WriteFloat(face.UVs[t].y);
                writer.WriteFloat(0.0f); // dummy data
                writer.WriteFloat(0.0f); // dummy data
                writer.WriteFloat(0.0f); // dummy data
                writer.WriteFloat(0.0f); // dummy data
                writer.WriteFloat(1.0f); // alpha
            }
        }

        pos = (int)writer.Position();
        writer.Seek(facesize);
        writer.Write(pos - facesize - 4);
        writer.Seek(pos);

        writer.Write(ROOM_END_CHUNK);
        writer.Write(4);
    }

    void WriteSegmentsToOrf(Level& level, span<SegID> segs, const filesystem::path& path, const Outrage::GameTable& table) {
        List<Vector3> vertices;
        List<RoomFace> faces;
        short vertexIndex = 0;

        List<LevelTexID> textures;

        for (auto& segid : segs) {
            auto& seg = level.GetSegment(segid);
            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);
                if (seg.SideHasConnection(sid) && Seq::contains(segs, seg.Connections[(int)sid]))
                    continue; // skip side if it is open and is inside the selection

                auto face = Face::FromSide(level, seg, sid);

                auto indices = side.GetRenderIndices();

                if (face.Side.TMap != LevelTexID::Unset && !Seq::contains(textures, face.Side.TMap))
                    textures.push_back(face.Side.TMap);

                auto texture = int16(Seq::indexOf(textures, face.Side.TMap).value_or(0));

                if (side.Normals[0].Dot(side.Normals[1]) > 0.99999f) {
                    // planar face
                    RoomFace roomFace{};
                    roomFace.Vertices.push_back(vertexIndex++);
                    roomFace.Vertices.push_back(vertexIndex++);
                    roomFace.Vertices.push_back(vertexIndex++);
                    roomFace.Vertices.push_back(vertexIndex++);
                    roomFace.UVs.push_back(side.UVs[0]);
                    roomFace.UVs.push_back(side.UVs[1]);
                    roomFace.UVs.push_back(side.UVs[2]);
                    roomFace.UVs.push_back(side.UVs[3]);
                    vertices.push_back(face[0]);
                    vertices.push_back(face[1]);
                    vertices.push_back(face[2]);
                    vertices.push_back(face[3]);
                    roomFace.Normal = side.AverageNormal;
                    roomFace.Texture = texture;
                    faces.push_back(roomFace);
                }
                else {
                    for (int i = 0; i < 2; i++) {
                        RoomFace roomFace{};
                        roomFace.Vertices.push_back(vertexIndex++);
                        roomFace.Vertices.push_back(vertexIndex++);
                        roomFace.Vertices.push_back(vertexIndex++);

                        roomFace.UVs.push_back(side.UVs[indices[0 + i * 3]]);
                        roomFace.UVs.push_back(side.UVs[indices[1 + i * 3]]);
                        roomFace.UVs.push_back(side.UVs[indices[2 + i * 3]]);

                        vertices.push_back(face[indices[0 + i * 3]]);
                        vertices.push_back(face[indices[1 + i * 3]]);
                        vertices.push_back(face[indices[2 + i * 3]]);

                        roomFace.Normal = side.Normals[i];
                        roomFace.Texture = texture;
                        faces.push_back(roomFace);
                    }
                }
            }
        }

        auto deleteVertex = [&](short index) {
            //Remap vertices in faces
            for (short f = 0; f < faces.size(); f++) {
                auto& face = faces[f];

                for (short v = 0; v < face.Vertices.size(); v++) {
                    if (face.Vertices[v] == index)
                        throw Exception("Deleting a vertex still in use!");
                    else if (face.Vertices[v] > index)
                        face.Vertices[v]--;
                }
            }

            Seq::removeAt(vertices, index);
        };

        uint removed = 0;
        // Remove duplicate vertices
        for (short i = 0; i < vertices.size(); i++) {
            for (short j = 0; j < i; j++) {
                if (Vector3::Distance(vertices[i], vertices[j]) < 0.1f) {
                    //Replace the higher-numbered point with the lower-numbered in all the faces in this room
                    auto fp = faces.begin();
                    for (int f = 0; f < faces.size(); f++, fp++) {
                        auto& face = faces[f];

                        for (int v = 0; v < face.Vertices.size(); v++)
                            if (face.Vertices[v] == i)
                                face.Vertices[v] = j;
                    }

                    deleteVertex(i);
                    i--; //back up, since the point we're checking is now gone
                    removed++;
                    break; //don't keep checking for duplicates
                }
            }
        }

        {
            std::ofstream file(path, std::ios::binary);
            StreamWriter writer(file, false);

            if (textures.empty()) textures.push_back(LevelTexID(3000));
            SaveRoom(writer, vertices, faces, table, textures);
        }

        //{
        //    StreamReader reader(path);
        //    LoadRoom(reader);
        //}
    }
}
