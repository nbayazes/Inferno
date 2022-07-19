#pragma once

#include "Types.h"
#include "Streams.h"

namespace Inferno::Outrage {
    enum class TextureFlag {
        Volatile = 1,
        Water = (1 << 1),
        Metal = (1 << 2), // Editor sorting
        Marble = (1 << 3), // Editor sorting
        Plastic = (1 << 4), // Editor sorting
        Forcefield = (1 << 5),
        Animated = (1 << 6),
        Destroyable = (1 << 7),
        Effect = (1 << 8),
        HudCockpit = (1 << 9),
        Mine = (1 << 10),
        Terrain = (1 << 11),
        Object = (1 << 12),
        Texture64 = (1 << 13),
        Tmap2 = (1 << 14),
        Texture_32 = (1 << 15),
        FlyThru = (1 << 16),
        PassThru = (1 << 17),
        PingPong = (1 << 18),
        Light = (1 << 19), // Full bright
        Breakable = (1 << 20),
        Saturate = (1 << 21), // Additive?
        Alpha = (1 << 22), // Use the alpha value in the tablefile
        Dontuse = (1 << 23), // Not intended for levels? Hidden in texture browser?
        Procedural = (1 << 24),
        WaterProcedural = (1 << 25),
        ForceLightmap = (1 << 26),
        SaturateLightmap = (1 << 27),
        Texture256 = (1 << 28),
        Lava = (1 << 29),
        Rubble = (1 << 30),
        SmoothSpecular = (1 << 31)
    };

    enum class ProceduralType : uint8 {
        None,
        LineLightning,
        SphereLightning,
        Straight,
        RisingEmbers,
        RandomEmbers,
        Spinners,
        Roamers,
        Fountain,
        Cone,
        FallRight,
        FallLeft
    };

    enum class WaterProceduralType : uint8 {
        None,
        HeightBlob,
        SineBlob,
        RandomRaindrops,
        RandomBlobdrops
    };

    struct TextureInfo {
        string Name; // Entry in tablefile
        string FileName; // File name in hog or on disk
        Color Color;
        Vector2 Slide;
        float Speed; // Total time of animation
        float Reflectivity; // For radiosity calcs 
        TextureFlag Flags;
        int8 Corona;
        int Damage;

        //struct {
        //    int Palette[255]{};
        //    int8 Heat, Light, Thickness, EvalTime, OscTime, OscValue;
        //    short Elements;
        //    int8 Type, Frequency, Speed, Size, X1, Y1, X2, Y2;
        //} Procedural;

        string Sound;

        constexpr bool Saturate() const { return bool(Flags & TextureFlag::Saturate); }
        constexpr bool Alpha() const { return bool(Flags & TextureFlag::Alpha); }
        constexpr bool Animated() const { return bool(Flags & TextureFlag::Animated); }
        constexpr bool Procedural() const { return bool(Flags & TextureFlag::Procedural); }
    };

    struct GameTable {
        enum {
            TABLE_FILE_BASE = 0,
            TABLE_FILE_MISSION = 1,
            TABLE_FILE_MODULE = 2
        } Type{};
        
        string Name;

        List<TextureInfo> Textures;
        static GameTable Read(StreamReader&);
    };
}