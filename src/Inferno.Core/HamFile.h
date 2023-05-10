#pragma once
#include "EffectClip.h"
#include "Weapon.h"
#include "Robot.h"
#include "Utility.h"
#include "Streams.h"
#include "Sound.h"
#include "Pig.h"

namespace Inferno {
    enum class TextureFlag : ubyte {
        Volatile = BIT(0), // Explodes when shot
        Water = BIT(1),
        ForceField = BIT(2),
        GoalBlue = BIT(3),
        GoalRed = BIT(4),
        GoalHoard = BIT(5)
    };

    //DEFINE_ENUM_FLAG_OPERATORS(TextureFlag);

    // The properties that a texture can have
    struct LevelTexture {
        TextureFlag Flags{};
        float Lighting{};
        float Damage{}; //how much damage being against this does (for lava)
        EClipID EffectClip = EClipID::None; //the EffectClip that changes this, or -1
        LevelTexID DestroyedTexture = LevelTexID::None; //bitmap to show when destroyed, or -1
        Vector2 Slide; // Siding UV rate of texture per second
        LevelTexID ID = LevelTexID::None;;
        TexID TexID = TexID::None;
        string D1FileName;

        bool HasFlag(TextureFlag flag) const { return bool(Flags & flag); }
    };

    struct PlayerShip {
        ModelID Model;
        VClipID ExplosionVClip{};
        float Mass, Drag;
        float MaxThrust, ReverseThrust, Brakes;
        float Wiggle;
        float MaxRotationalThrust;
        Array<Vector3, 8> GunPoints{};
    };

    struct Reactor {
        static constexpr auto GunCount = 8;
        ModelID Model{};
        int Guns{};
        Array<Vector3, GunCount> GunPoints;
        Array<Vector3, GunCount> GunDirs;
    };

    struct Powerup {
        VClipID VClip;
        SoundID HitSound; // sound when picked up
        float Size;
        float Light; // Original light (radius?)
        Color LightColor;
        float LightRadius;
    };

    // Stores Texture, Sound, and Animation metadata
    struct HamFile {
        List<LevelTexID> LevelTexIdx; // Maps global texture ids to level (geometry) texture ids. Reverse map of AllTexIdx. Defaults to 255.
        List<TexID> AllTexIdx; // Maps level texture ids to global texture ids (len = 910)
        List<LevelTexture> TexInfo; // Level texture info. Must match length of AllTexIdx.
        List<uint8> Sounds; // Maps SoundID to entry in S11/S22 file
        List<uint8> AltSounds; // Low memory sounds, unneeded
        List<VClip> VClips; // Particles, explosions
        List<EffectClip> Effects; // Animated wall textures
        List<DoorClip> DoorClips;
        List<RobotInfo> Robots;
        List<JointPos> RobotJoints;
        List<Weapon> Weapons;
        List<Powerup> Powerups;
        List<Model> Models;
        List<ModelID> DyingModels; // Corresponds to index in Model
        List<ModelID> DeadModels; // Corresponds to index in Model
        List<TexID> Gauges;
        List<TexID> HiResGauges;
        List<TexID> ObjectBitmaps;
        List<uint16> ObjectBitmapPointers; // Indexes into ObjectBitmaps

        PlayerShip PlayerShip{};
        List<TexID> Cockpits;

        int FirstMultiplayerBitmap = -1;
        ModelID MarkerModel = ModelID::None;
        List<Reactor> Reactors;

        ModelID ExitModel = ModelID::None; // For D1 exits
        ModelID DestroyedExitModel = ModelID::None; // For D1 exits

        HamFile() = default;
        ~HamFile() = default;
        HamFile(const HamFile&) = delete;
        HamFile(HamFile&&) = default;
        HamFile& operator=(const HamFile&) = delete;
        HamFile& operator=(HamFile&&) = default;
    };

    HamFile ReadHam(StreamReader&);
    // Read a vertigo ham data and append it
    void AppendVHam(StreamReader&, HamFile&);
    void ReadHXM(StreamReader&, HamFile&);
    VClip ReadVClip(StreamReader&);
    EffectClip ReadEffect(StreamReader&);
    JointPos ReadRobotJoint(StreamReader&);

    std::tuple<HamFile, PigFile, SoundFile> ReadDescent1GameData(StreamReader&, Palette& palette);
}