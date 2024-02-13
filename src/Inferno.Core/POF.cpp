#include "pch.h"
#include <spdlog/spdlog.h>
#include "Polymodel.h"
#include "Robot.h"

namespace Inferno {
    Model ReadPof(span<byte> pof, const Palette* palette) {
        StreamReader r(pof);
        Model model{};

        if (r.ReadInt32() != MakeFourCC("PSPO")) {
            SPDLOG_ERROR("Not a POF file");
            return model;
        }

        auto version = r.ReadInt16();

        constexpr auto COMPATIBLE_VERSION = 6;
        constexpr auto OBJFILE_VERSION = 8;

        if (version < COMPATIBLE_VERSION || version > OBJFILE_VERSION) {
            SPDLOG_ERROR("Incompatible POF version {}", version);
            return model;
        }

        while (!r.EndOfStream()) {
            auto id = r.ReadInt32();
            auto len = r.ReadInt32();
            auto chunkStart = r.Position();
            if (len <= 0) throw Exception("bad chunk length");

            switch (id) {
                case MakeFourCC("OHDR"): // Object header
                {
                    auto submodels = r.ReadInt32Checked(1000, "bad submodel count");
                    model.Submodels.resize(submodels);
                    model.Radius = r.ReadFix();
                    model.MinBounds = r.ReadVector();
                    model.MaxBounds = r.ReadVector();
                    break;
                }

                case MakeFourCC("SOBJ"): // Subobject chunk
                {
                    auto index = r.ReadInt16();
                    if (index > model.Submodels.size())
                        throw Exception("too many submodels");

                    auto& sm = model.Submodels[index];
                    sm.Parent = (ubyte)r.ReadInt16();

                    sm.Normal = r.ReadVector();
                    sm.Point = r.ReadVector();
                    sm.Offset = r.ReadVector();
                    sm.Offset.z *= -1;

                    sm.Radius = r.ReadFix();
                    sm.Pointer = r.ReadInt32();
                    break;
                }

                case MakeFourCC("GUNS"): // Gun chunk
                {
                    model.Guns.resize(r.ReadInt32Checked(MAX_GUNS, "bad number of guns"));

                    for (int i = 0; i < model.Guns.size(); i++) {
                        auto gunid = r.ReadInt16();
                        auto& gun = model.Guns[gunid];
                        gun.Submodel = (uint8)r.ReadInt16();
                        if (gun.Submodel == 0xff) 
                            throw Exception("Invalid gun submodel");

                        gun.Point = r.ReadVector();
                        gun.Point.z *= -1;

                        if (version >= 7)
                            gun.Normal = r.ReadVector();
                    }
                    break;
                }

                case MakeFourCC("ANIM"): // Animation chunk
                {
                    auto frames = r.ReadInt16();
                    if (frames != N_ANIM_STATES)
                        throw Exception("bad number of animation frames");

                    model.Animation.resize(N_ANIM_STATES);
                    for (auto& anim : model.Animation)
                        anim.resize(model.Submodels.size());

                    for (size_t m = 0; m < model.Submodels.size(); m++) {
                        for (size_t f = 0; f < frames; f++) {
                            model.Animation[f][m] = r.ReadAngleVec();
                            model.Animation[f][m].z *= -1;
                            std::swap(model.Animation[f][m].y, model.Animation[f][m].z);
                        }
                    }

                    break;
                }

                case MakeFourCC("TXTR"): // Texture file list
                {
                    auto count = r.ReadInt16();
                    for (size_t i = 0; i < count; i++)
                        model.Textures.push_back(r.ReadCString(128));

                    break;
                }

                case MakeFourCC("IDTA"): // Raw interpreter data
                {
                    List<byte> data(len);
                    r.ReadBytes(data);

                    DecodeInterpreterData(model, data, palette);
                    break;
                }

                default:
                    fmt::print("unknown chunk id {}\n", id);
                    break;
            }

            r.Seek(chunkStart + len); // seek to next chunk (prevents read errors due to individual chunks)
        }

        model.DataSize = (uint)pof.size();
        model.TextureCount = (uint8)model.Textures.size();
        model.Radius = model.Submodels[0].Radius;
        return model;
    }
}
