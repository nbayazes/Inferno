#pragma once

#include "Types.h"
#include "Utility.h"

namespace Inferno {
    enum class VClipFlag : uint32 { None, Rod = 1 }; // Rod is a hostage?? Axis aligned billboard?

    //VCLIP_PLAYER_HIT 1
    //VCLIP_MORPHING_ROBOT 10
    //VCLIP_PLAYER_APPEARANCE	61
    //VCLIP_POWERUP_DISAPPEARANCE	62
    //VCLIP_VOLATILE_WALL_HIT	5

    //VCLIP_MAXNUM 70
    //VCLIP_MAX_FRAMES 30

    // Video clip (power ups or animated walls)
    struct VClip {
        float PlayTime{}; // total time (in seconds) of clip
        int32 NumFrames = 0; // Valid frames in Frames
        float FrameTime = 1; // time (in seconds) of each frame
        VClipFlag Flags{};
        SoundID Sound = SoundID::None;
        Array<TexID, 30> Frames{};
        float LightValue{};

        // Returns the active frames
        span<const TexID> GetFrames() const { 
            return span<const TexID>(Frames.begin(), std::max(NumFrames, 0)); 
        }

        // Returns the frame for the vclip based on elapsed time
        TexID GetFrame(double elapsed) const {
            auto frame = (int)std::floor(elapsed / FrameTime) % NumFrames;
            return Frames[frame];
        };
    };

    enum class EClipFlag : int32 { Critical = 1, OneShot = 2, Stopped = 4 };
    //DEFINE_ENUM_FLAG_OPERATORS(EClipFlag);

    // Effect clip. Assigns a vclip to a segment side.
    struct EffectClip {
        VClip VClip;    // embedded vclip
        float TimeLeft{};   // for sequencing
        int FrameCount{}; // for sequencing
        LevelTexID ChangingWallTexture = LevelTexID::None; //Which element of Textures array to replace.
        short ChangingObjectTexture{}; //Which element of ObjBitmapPtrs array to replace.
        EClipFlag Flags{};
        int CritClip{};  //use this clip instead of above one when mine critical
        LevelTexID DestroyedTexture = LevelTexID::None; //use this bitmap when monitor destroyed
        VClipID DestroyedVClip = VClipID::None;  //what vclip to play when exploding
        EClipID DestroyedEClip = EClipID::None;  //what eclip to play when exploding
        float ExplosionSize{};  //3d size of explosion
        SoundID Sound = SoundID::None; //what sound this makes
        Tag OneShotTag; //what seg & side, for one-shot clips. Probably unused
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

        span<const LevelTexID> GetFrames() const {
            return span<const LevelTexID>(Frames.begin(), NumFrames);
        }

        bool HasFlag(WallClipFlag flag) const { return bool(Flags & flag); }

        // Uses tmap1, otherwise tmap2
        bool UsesTMap1() const { return (bool)((WallClipFlag)((int16)Flags & (int16)WallClipFlag::TMap1)); }
    };

}