#pragma once

#include "Types.h"
#include "Streams.h"

namespace Inferno::Outrage {
    enum TextureFlag {
        TF_VOLATILE = 1,
        TF_WATER = (1 << 1),
        TF_METAL = (1 << 2),
        TF_MARBLE = (1 << 3),
        TF_PLASTIC = (1 << 4),
        TF_FORCEFIELD = (1 << 5),
        TF_ANIMATED = (1 << 6),
        TF_DESTROYABLE = (1 << 7),
        TF_EFFECT = (1 << 8),
        TF_HUD_COCKPIT = (1 << 9),
        TF_MINE = (1 << 10),
        TF_TERRAIN = (1 << 11),
        TF_OBJECT = (1 << 12),
        TF_TEXTURE_64 = (1 << 13),
        TF_TMAP2 = (1 << 14),
        TF_TEXTURE_32 = (1 << 15),
        TF_FLY_THRU = (1 << 16),
        TF_PASS_THRU = (1 << 17),
        TF_PING_PONG = (1 << 18),
        TF_LIGHT = (1 << 19),
        TF_BREAKABLE = (1 << 20),
        TF_SATURATE = (1 << 21),
        TF_ALPHA = (1 << 22),
        TF_DONTUSE = (1 << 23),
        TF_PROCEDURAL = (1 << 24),
        TF_WATER_PROCEDURAL = (1 << 25),
        TF_FORCE_LIGHTMAP = (1 << 26),
        TF_SATURATE_LIGHTMAP = (1 << 27),
        TF_TEXTURE_256 = (1 << 28),
        TF_LAVA = (1 << 29),
        TF_RUBBLE = (1 << 30),
        TF_SMOOTH_SPECULAR = (1 << 31)
    };

    struct TextureInfo {
        string Name; // Entry in tablefile
        string FileName; // File name in hog or on disk
        Color Color;
        Vector2 Slide;
        float Speed; // Time per frame of animation?
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

        bool IsAnimated() const { return Flags & TF_ANIMATED; }
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