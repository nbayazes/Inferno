#include "pch.h"
#include "OutrageModel.h"

namespace Inferno {
    string ReadModelString(StreamReader& r) {
        int mlen = r.ReadInt32();
        return r.ReadString(mlen);
    }

    OutrageModel OutrageModel::Read(StreamReader& r) {
        // can also load data from oof, but let's assume POFs
        auto fileId = r.ReadInt32();
        if (fileId != 'OPSP')
            throw Exception("Not a model file");

        OutrageModel pm{};
        pm.Version = r.ReadInt32();

        if (pm.Version < 18)
            pm.Version *= 100; // fix old version

        if (pm.Version < MIN_OBJFILE_VERSION || pm.Version > OBJFILE_VERSION)
            throw Exception("Bad version");

        pm.MajorVersion = pm.Version / 100;

        if (pm.MajorVersion >= 21)
            pm.Flags = PolymodelFlags(pm.Flags | PMF_LIGHTMAP_RES);

        bool timed = false;
        if (pm.MajorVersion >= 22) {
            timed = true;
            pm.Flags = PolymodelFlags(pm.Flags | PMF_TIMED);
        }

        while (!r.EndOfFile()) {
            auto id = r.ReadInt32();
            auto len = r.ReadInt32();

            if (id == 0 && len == 0)
                break; // not sure why, but running out of data before EOF

            switch (id) {
                case MakeFourCC("OHDR"): // POF file header
                {
                    auto submodels = r.ReadInt32();
                    assert(submodels < 100);
                    pm.Submodels.reserve(submodels);
                    pm.Radius = r.ReadFloat();
                    pm.Min = r.ReadVector3();
                    pm.Max = r.ReadVector3();

                    // Skip details
                    int detail = r.ReadInt32();
                    for (int i = 0; i < detail; i++) {
                        r.ReadInt32();
                    }
                    break;
                }

                case MakeFourCC("SOBJ"): // Subobject header
                {
                    auto& sm = pm.Submodels.emplace_back();

                    /*auto n = */r.ReadInt32();
                    sm.Min = { 90000, 90000, 90000 };
                    sm.Max = { -90000, -90000, -90000 };
                    sm.Parent = r.ReadInt32();
                    sm.Normal = r.ReadVector3();

                    /*auto d =*/ r.ReadFloat();
                    sm.Point = r.ReadVector3();
                    sm.Offset = r.ReadVector3();
                    sm.Radius = r.ReadFloat();

                    sm.TreeOffset = r.ReadInt32();
                    sm.DataOffset = r.ReadInt32();

                    if (pm.Version > 1805)
                        sm.GeometricCenter = r.ReadVector3();

                    sm.Name = ReadModelString(r);
                    sm.Props = ReadModelString(r);

                    sm.MovementType = r.ReadInt32();
                    sm.MovementAxis = r.ReadInt32();

                    // skip freespace chunks
                    auto chunks = r.ReadInt32();
                    for (int i = 0; i < chunks; i++)
                        r.ReadInt32();

                    auto verts = r.ReadInt32();
                    constexpr auto MAX_POLYGON_VECS = 2500;
                    assert(verts < MAX_POLYGON_VECS);

                    sm.Vertices.resize(verts);

                    for (auto& vert : sm.Vertices)
                        vert.Position = r.ReadVector3();

                    for (auto& vert : sm.Vertices)
                        vert.Normal = r.ReadVector3();

                    if (pm.MajorVersion >= 23) {
                        for (auto& vert : sm.Vertices) {
                            vert.Alpha = r.ReadFloat();
                            if (vert.Alpha < 0.99f)
                                pm.Flags = PolymodelFlags(pm.Flags | PMF_ALPHA);
                        }
                    }

                    auto faces = r.ReadInt32();
                    assert(faces < 20000); // Sanity check
                    sm.Faces.resize(faces);

                    for (auto& face : sm.Faces) {
                        face.Normal = r.ReadVector3();
                        auto nverts = r.ReadInt32();
                        assert(nverts < 100);
                        face.Vertices.resize(nverts);

                        bool textured = r.ReadInt32();
                        if (textured)
                            face.TexNum = (short)r.ReadInt32();
                        else
                            face.Color = r.ReadRGB();

                        for (auto& v : face.Vertices) {
                            v.Index = (short)r.ReadInt32();
                            v.U = r.ReadFloat();
                            v.V = r.ReadFloat();
                        }

                        // Lightmap stuff we don't care about
                        if (pm.MajorVersion >= 21) {
                            /*auto xdiff =*/ r.ReadFloat();
                            /*auto ydiff =*/ r.ReadFloat();
                        }
                    }

                    break;
                }
                default:
                    r.SeekForward(len);
                    break;

                    //case MakeFourCC("GPNT"): // gun points

                    //    break;

                    //case MakeFourCC("IDTA"): // Interpreter data

                    //    break;
                    //case MakeFourCC("TXTR"): // Texture filename list

                    //    break;
                    //case MakeFourCC("PINF"): // POF file information, like command line, etc

                    //    break;
                    //case MakeFourCC("GRID"): // Grid information

                    //    break;

                    //case MakeFourCC("RANI"): // rotational animation data

                    //    break;
                    //case MakeFourCC("PANI"): // positional animation data

                    //    break;
                    //case MakeFourCC("ANIM"): // animation data

                    //    break;

                    //case MakeFourCC("WBAT"): // weapon batteries
                    //    break;

                    //case MakeFourCC("GRND"): // ground plane info
                    //    break;

                    //case MakeFourCC("ATCH"): // attach points
                    //    break;

                    //case MakeFourCC("NATH"): // attach vecs
                    //    break;
            }
        }

        // todo: compute object min/max
        // todo: animations

        return pm;
    }
}