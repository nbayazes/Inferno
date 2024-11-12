#pragma once
#include "Pig.h"
#include "HamFile.h"

namespace Inferno {
    // Saves and loads generated textures for specular and normal maps
    struct TextureMapCache {
        struct Entry {
            TexID Id = TexID::None;
            uint16 Width = 0, Height = 0;
            uint32 DiffuseLength = 0, SpecularLength = 0, NormalLength = 0, MaskLength = 0;
            uint8 Mips = 0;
            List<Palette::Color> Diffuse; //rgb8
            List<ubyte> Specular; // u8
            List<Palette::Color> Normal; //rgb8
            List<ubyte> Mask; // u8, supertransparency mask
        };

        List<Entry> Entries;
        filesystem::path Path; // the source path

        void GenerateTextures(const HamFile& ham, const PigFile& pig, const Palette& palette);

        static TextureMapCache Read(const filesystem::path& path);
        void Write(const filesystem::path& path) const;

        const Entry* GetMaterial(TexID id) const {
            return Seq::tryItem(Entries, (int)id);
        }

        const List<Palette::Color>* GetNormalMap(TexID id) const {
            if (auto entry = Seq::tryItem(Entries, (int)id))
                return &entry->Normal;

            return nullptr;
        }
    };

    void BuildTextureMapCache();
    void ExpandMask(const PigEntry& bmp, List<uint8>& data);
}
