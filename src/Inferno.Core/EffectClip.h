#pragma once

#include "Types.h"
#include "Utility.h"

namespace Inferno {
    enum class VClipFlag : uint32 { None, Rod = 1 }; // Rod is a hostage?? Axis aligned billboard?

    // Video clip (power ups)
    struct VClip {
        float PlayTime{}; // total time (in seconds) of clip
        int32 NumFrames = 0;
        float FrameTime = 1; // time (in seconds) of each frame
        VClipFlag Flags{};
        SoundID Sound = SoundID::None;
        Array<TexID, 30> Frames{};
        float LightValue{};

        // Returns the active frames
        span<const TexID> GetFrames() const { return span<const TexID>(Frames.begin(), NumFrames); }
    };

    enum class EClipFlag : int32 { Critical = 1, OneShot = 2, Stopped = 4 };
    //DEFINE_ENUM_FLAG_OPERATORS(EClipFlag);

    // Effect clip. Assigns a vclip to a segment side.
    struct EffectClip {
        VClip VClip;    // embedded vclip
        float TimeLeft{};   // for sequencing
        int FrameCount{}; // for sequencing
        LevelTexID ChangingWallTexture{}; //Which element of Textures array to replace.
        short ChangingObjectTexture{}; //Which element of ObjBitmapPtrs array to replace.
        EClipFlag Flags{};
        // is this eclip or vclip
        int CritClip{};  //use this clip instead of above one when mine critical
        LevelTexID DestroyedTexture{}; //use this bitmap when monitor destroyed
        VClipID DestroyedVClip{};  //what vclip to play when exploding
        EClipID DestroyedEClip{};  //what eclip to play when exploding
        float ExplosionSize{};  //3d size of explosion
        SoundID Sound{}; //what sound this makes
        int Segment{}, Side{}; //what seg & side, for one-shot clips. Probably unused

        TexID GetFrame(double elapsedTime) const {
            auto frame = (int)std::floor(elapsedTime / VClip.FrameTime) % VClip.NumFrames;
            assert(frame < VClip.NumFrames);
            return VClip.Frames[frame];
        };
    };

    enum class WallClipFlag : int16 {
        Explodes = 1,  // door explodes when opening (hostage door)
        Blastable = 2, // this is a blastable wall
        TMap1 = 4,     // this uses primary tmap, not tmap2
        Hidden = 8
    };

    //DEFINE_ENUM_FLAG_OPERATORS(WallClipFlag);

    // Wall animation clip (doors)
    struct WallClip {
        float PlayTime{};
        int16 NumFrames{};
        Array<LevelTexID, 50> Frames{};
        SoundID OpenSound{}, CloseSound{};
        WallClipFlag Flags{};
        string Filename;
        // Uses tmap1, otherwise tmap2
        bool UsesTMap1() const { return (bool)((WallClipFlag)((int16)Flags & (int16)WallClipFlag::TMap1)); }
    };

}