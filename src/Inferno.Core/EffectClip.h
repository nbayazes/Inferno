#pragma once

#include "Types.h"
#include "Utility.h"

namespace Inferno {
    enum class VClipFlag : uint32 { None, AxisAligned = 1 };

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
            return span(Frames.begin(), std::max(NumFrames, 0));
        }

        // Returns the frame for the vclip based on elapsed time
        TexID GetFrame(double elapsed) const {
            if (NumFrames == 0) return TexID::None;
            auto frame = (int)std::floor(std::abs(elapsed) / (double)FrameTime) % NumFrames;
            return Frames[frame];
        }

        // Returns a non-looped frame for the vclip
        TexID GetFrameClamped(double elapsed) const {
            if (NumFrames == 0) return TexID::None;
            auto frame = (int)std::floor(std::abs(elapsed) / (double)FrameTime);
            if (frame > NumFrames) frame = NumFrames - 1;
            return Frames[frame];
        }
    };

    enum class EClipFlag : int32 { None = 0, Critical = 1, OneShot = 2, Stopped = 4 };
    //DEFINE_ENUM_FLAG_OPERATORS(EClipFlag);

    // Effect clip. Assigns a vclip to a segment side.
    struct EffectClip {
        VClip VClip{}; // embedded vclip for this effect
        LevelTexID ChangingWallTexture = LevelTexID::None; // Which element of Textures array to replace. Unused?
        short ChangingObjectTexture{}; // Which element of ObjBitmapPtrs array to replace.
        EClipFlag Flags{};
        EClipID CritClip{};  // swap to this animation when mine is critical
        LevelTexID DestroyedTexture = LevelTexID::None; // swap to this texture when destroyed after playing the eclip if present
        EClipID DestroyedEClip = EClipID::None;  // swap to this animation when destroyed
        VClipID DestroyedVClip = VClipID::None;  // vclip to play when exploding
        float ExplosionSize{};  // Radius for vclip
        SoundID Sound = SoundID::None; // Ambient sound

        // the follow are a hack for animating a breaking clip on a wall
        float TimeLeft{};
        int FrameCount{};
        Tag OneShotTag;
    };

    enum class DoorClipFlag : int16 {
        Explodes = 1,  // door explodes when opening (hostage door)
        Blastable = 2, // this is a blastable wall
        TMap1 = 4,     // this uses primary tmap, not tmap2
        Hidden = 8
    };

    //DEFINE_ENUM_FLAG_OPERATORS(DoorClipFlag);

    // Wall animation clip (doors)
    struct DoorClip {
        float PlayTime{};
        int16 NumFrames{};
        Array<LevelTexID, 50> Frames{};
        SoundID OpenSound{}, CloseSound{};
        DoorClipFlag Flags{};
        string Filename;

        DoorClip() {
            Frames.fill(LevelTexID::None);
        }

        span<const LevelTexID> GetFrames() const {
            if (!Seq::inRange(Frames, NumFrames)) return {};
            return span(Frames.begin(), NumFrames);
        }

        bool HasFlag(DoorClipFlag flag) const { return bool(Flags & flag); }
    };
}