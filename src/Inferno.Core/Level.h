#pragma once

#include "Types.h"
#include "Utility.h"
#include "Object.h"
#include "EffectClip.h"
#include "Streams.h"

namespace Inferno {
    struct Matcen {
        uint32 Robots{};
        uint32 Robots2{}; // Additional D2 robot flag
        SegID Segment = SegID::None; // Segment this is attached to
        int16 Producer{}; // runtime fuelcen link??
        int32 HitPoints{}; // runtime
        int32 Interval{}; // runtime
    };

    enum class SideSplitType : uint8 {
        Quad = 1, // render side as quadrilateral
        Tri02 = 2, // render side as two triangles, triangulated along edge from 0 to 2
        Tri13 = 3, // render side as two triangles, triangulated along edge from 1 to 3
    };

    struct LightDeltaIndex {
        Tag Tag;
        uint8 Count = 0;
        int16 Index = -1;
    };

    using SideLighting = Array<Color, 4>;

    struct LightDelta {
        Tag Tag;
        SideLighting Color{};
    };

    enum class OverlayRotation : uint16 { Rotate0, Rotate90, Rotate180, Rotate270 };

    struct SegmentSide {
        SideSplitType Type = SideSplitType::Quad;
        WallID Wall = WallID::None;
        Array<Vector3, 2> Normals;
        Array<Vector3, 2> Centers;
        Vector3 AverageNormal;
        Vector3 Center;

        LevelTexID TMap, TMap2{};
        OverlayRotation OverlayRotation = OverlayRotation::Rotate0;
        Array<Vector2, 4> UVs = { Vector2(0, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0) };
        Array<Color, 4> Light = { Color(1,1,1), Color(1,1,1), Color(1,1,1), Color(1,1,1) };
        Array<bool, 4> LockLight = { false, false, false, false }; // Locks light values from being updated from the light algorithm

        Option<Color> LightOverride; // Editor defined override for amount of light emitted
        Option<float> LightRadiusOverride; // Editor defined override for light radius
        Option<float> LightPlaneOverride; // Editor defined override for light plane tolerance

        bool HasOverlay() const { return TMap2 > LevelTexID::Unset; }
        bool HasWall() const { return Wall > WallID::None; }

        int GetNumFaces() const {
            switch (Type) {
                default:
                case SideSplitType::Quad:
                    return 1;
                case SideSplitType::Tri02:
                case SideSplitType::Tri13:
                    return 2;
            }
        }

        const Array<uint16, 6> GetRenderIndices() const {
            static const Array<uint16, 6> tri02 = { 0u, 1u, 2u, 0u, 2u, 3u };
            static const Array<uint16, 6> tri13 = { 0u, 1u, 3u, 3u, 1u, 2u };
            return Type == SideSplitType::Tri13 ? tri13 : tri02;
        }

        const Vector3& NormalForEdge(int edge) {
            if (Type == SideSplitType::Tri02)
                return (edge == 0 || edge == 1) ? Normals[0] : Normals[1];
            else
                return (edge == 0 || edge == 3) ? Normals[0] : Normals[1];
        }

        const Vector3& CenterForEdge(int edge) {
            if (Type == SideSplitType::Tri02)
                return (edge == 0 || edge == 1) ? Centers[0] : Centers[1];
            else
                return (edge == 0 || edge == 3) ? Centers[0] : Centers[1];
        }
    };

    constexpr uint16 MAX_SIDES = 6;
    constexpr uint16 MAX_VERTICES = 8;

    // Segment point ids for a segment side
    static constexpr Array<Array<int16, 4>, MAX_SIDES> SideIndices{ {
            {{ 7, 6, 2, 3 }}, // left
            {{ 0, 4, 7, 3 }}, // top
            {{ 0, 1, 5, 4 }}, // right
            {{ 2, 6, 5, 1 }}, // bottom
            {{ 4, 5, 6, 7 }}, // back
            {{ 3, 2, 1, 0 }}, // front
    } };

    //// Indices for drawing triangles on each side directly
    //const Array<Array<int16, 4>, MAX_SIDES> IndicesOfSide{ {
    //    {{ 6, 7, 1, 0 }}, // left
    //    {{ 4, 7, 3, 0 }}, // top
    //    {{ 4, 3, 5, 2 }}, // right
    //    {{ 2, 1, 5, 6 }}, // bottom
    //    {{ 5, 4, 6, 7 }}, // back
    //    {{ 1, 0, 2, 3 }}, // front
    //} };

    // Lookup for the edges of each side. Uses the same order / winding as the vertex lookup.
    constexpr Array<Array<int16, 4>, MAX_SIDES> EdgesOfSide{ {
        {{ 4, 9, 0, 8  }}, // right
        {{ 11, 7, 8, 3 }}, // top
        {{ 2, 10, 6, 11 }}, // left
        {{ 9, 5, 10, 1 }}, // bottom
        {{ 6, 5, 4, 7 }}, // back
        {{ 0, 1, 2, 3 }}, // front
    } };

    constexpr Array<Array<int16, 2>, 12> VertsOfEdge{ {
        {{ 0, 1 }}, // 0 // front
        {{ 1, 2 }}, // 1
        {{ 2, 3 }}, // 2
        {{ 3, 0 }}, // 3
        {{ 6, 7 }}, // 6
        {{ 5, 6 }}, // 5
        {{ 4, 5 }}, // 4 // back
        {{ 7, 4 }}, // 7
        {{ 0, 7 }}, // 8
        {{ 1, 6 }}, // 9
        {{ 2, 5 }}, // 10
        {{ 3, 4 }}  // 11
    } };

    enum class SegmentType : uint8 {
        None = 0,
        Energy = 1,
        Repair = 2,
        Reactor = 3,
        Matcen = 4,
        GoalBlue = 5,
        GoalRed = 6,
        Count
    };

    constexpr const char* SegmentTypeLabels[] = {
        "None", "Energy", "Repair", "Reactor", "Matcen", "Blue Goal", "Red Goal"
    };

    struct Level;

    struct Segment {
        Array<SegID, MAX_SIDES> Connections = { SegID::None, SegID::None, SegID::None, SegID::None, SegID::None, SegID::None };
        Array<SegmentSide, MAX_SIDES> Sides{};
        Array<PointID, MAX_VERTICES> Indices{}; // index into the global vertex buffer
        SegmentType Type = SegmentType::None; // What type of center this is
        MatcenID Matcen = MatcenID::None; // Which center segment is associated
        ubyte StationIndex{};
        ubyte Value{}; // related to fuel center numbers, unused
        ubyte S2Flags{}; // ambient sound flag

        ObjID Objects = ObjID::None; // pointer to objects in this segment.
        // If bit n (1 << n) is set, then side #n in segment has had light subtracted from original (editor-computed) value.
        uint8 LightSubtracted;
        //uint8 SlideTextures;
        Color VolumeLight = { 1, 1, 1 };
        bool LockVolumeLight; // Locks volume light from being updated
        Vector3 Center;

        constexpr SegID GetConnection(SideID side) const { return Connections[(int)side]; }
        SegID& GetConnection(SideID side) { return Connections[(int)side]; }

        const SegmentSide& GetSide(SideID side) const {
            assert(side != SideID::None);
            return Sides[(int)side];
        }

        constexpr SegmentSide& GetSide(SideID side) {
            assert(side != SideID::None);
            return Sides[(int)side];
        }

        bool SideHasConnection(SideID side) const {
            return (int)Connections[(int)side] >= 0;
        }

        bool SideIsWall(SideID side) const {
            return Sides[(int)side].Wall != WallID::None;
        }

        bool LightIsSubtracted(SideID side) const {
            return LightSubtracted & (1 << (int)side);
        }

        bool SideContainsPoint(SideID side, PointID point) const {
            for (auto& si : Inferno::SideIndices[(int)side]) {
                if (point == Indices[si]) return true;
            } 

            return false;
        }

        // Vertex indices for a side in the vertex buffer
        Array<PointID, 4> GetVertexIndices(SideID side) const {
            Array<PointID, 4> indices{};
            auto& sideVerts = Inferno::SideIndices[(int)side];
            for (int i = 0; i < indices.size(); i++)
                indices[i] = Indices[sideVerts[i]];

            return indices;
        }

        Array<PointID*, 4> GetVertexIndicesRef(SideID side) {
            auto& sideVerts = Inferno::SideIndices[(int)side];
            return {
                &Indices[sideVerts[0]],
                &Indices[sideVerts[1]],
                &Indices[sideVerts[2]],
                &Indices[sideVerts[3]]
            };
        }

        PointID GetVertexIndex(SideID side, int16 point) {
            auto& indices = Inferno::SideIndices[(int)side];
            return Indices[indices[point % 4]];
        }

        Tuple<LevelTexID, LevelTexID> GetTexturesForSide(SideID side) const {
            auto& s = Sides[(int)side];
            return { s.TMap, s.TMap2 };
        }

        void UpdateNormals(Level&);
        void UpdateCenter(const Level&);
        float GetEstimatedVolume(Level&);
        bool IsZeroVolume(Level&);

        // Returns the vertices of a segment
        Array<const Vector3*, 8> GetVertices(const Level&) const;

        Array<Vector3, 8> CopyVertices(const Level&);
    };

    constexpr int16 MAX_TRIGGERS = 100;
    constexpr int16 MAX_WALLS_PER_LINK = 10;
    constexpr int16 MAX_TRIGGER_TARGETS = 10;

    enum class TriggerType : uint8 {
        OpenDoor = 0,
        CloseDoor = 1,
        Matcen = 2,
        Exit = 3,
        SecretExit = 4,
        IllusionOff = 5,
        IllusionOn = 6,
        UnlockDoor = 7,
        LockDoor = 8,
        OpenWall = 9,       // Wall Closed -> Open
        CloseWall = 10,     // Wall Open -> Closed
        IllusoryWall = 11,  // Makes a wall illusory (fly-through)
        LightOff = 12,
        LightOn = 13,
        NumTriggerTypes
    };

    // Trigger flags for Descent 1
    enum class TriggerFlagD1 : uint16 {
        None,
        OpenDoor = BIT(0),      // Control Trigger
        ShieldDamage = BIT(1),  // Shield Damage Trigger. Not properly implemented
        EnergyDrain = BIT(2),   // Energy Drain Trigger. Not properly implemented
        Exit = BIT(3),          // End of level Trigger
        On = BIT(4),            // Whether Trigger is active. Not properly implemented
        OneShot = BIT(5),       // If Trigger can only be triggered once. Not properly implemented
        Matcen = BIT(6),        // Trigger for materialization centers
        IllusionOff = BIT(7),   // Switch Illusion OFF trigger
        SecretExit = BIT(8),    // Exit to secret level
        IllusionOn = BIT(9),    // Switch Illusion ON trigger
    };

    enum class TriggerFlag : uint8 {
        None,
        NoMessage = BIT(0),
        OneShot = BIT(1),
        Disabled = BIT(2)
    };

    struct Trigger {
        TriggerType Type = TriggerType::OpenDoor; // D2 type
        union {
            TriggerFlag Flags; // D2 flags
            TriggerFlagD1 FlagsD1{}; // D1 flags
        };
        int32 Value = 0; // used for shield and energy drain triggers in D1
        int32 Time = -1; // reduced every frame by passed time until 0
        //int8 linkNum = 0; // unused
        //int8 targetCount = 0;
        ResizeArray<Tag, MAX_TRIGGER_TARGETS> Targets{};

        bool HasFlag(TriggerFlagD1 flag) const { return (uint16)FlagsD1 & (uint16)flag; }
        void SetFlag(TriggerFlagD1 flag) {
            FlagsD1 = TriggerFlagD1((uint16)FlagsD1 | (uint16)flag);
        }
    };

    enum class WallFlag : uint8 {
        None,
        Blasted = BIT(0), // Converts a blastable wall to an illusionary wall
        DoorOpened = BIT(1), // Door only
        DoorLocked = BIT(3), // Door only
        DoorAuto = BIT(4), // Door only
        IllusionOff = BIT(5), // Illusionary wall off state
        Switch = BIT(6), // Unused, maybe Exploding state
        BuddyProof = BIT(7)
    };

    enum class WallState : uint8 {
        Closed = 0,
        DoorOpening = 1,
        DoorWaiting = 2,
        DoorClosing = 3,
        DoorOpen = 4,
        Cloaking = 5,
        Decloaking = 6
    };

    enum class WallKey : uint8 {
        None = BIT(0),
        Blue = BIT(1),
        Red = BIT(2),
        Gold = BIT(3),
    };

    enum class WallType : uint8 {
        None = 0,
        Destroyable = 1, // Hostage and guidebot doors
        Door = 2,      // Solid wall. Opens when triggered.
        Illusion = 3,  // Wall with no collision
        FlyThroughTrigger = 4, // Fly-through invisible trigger
        Closed = 5,    // Solid wall. Fades in or out when triggered.
        WallTrigger = 6,   // For shootable triggers on a segment side
        Cloaked = 7,   // Solid, transparent wall that fades in or out when triggered. Similar to Closed but untextured.
    };

    struct Wall {
        Tag Tag;
        WallType Type = WallType::None;
        float HitPoints = 0; // For destroyable walls
        uint16 ExplodeTimeElapsed = 0;
        WallID LinkedWall = WallID::None; // only used at runtime for doors, should be saved as none from editor.
        WallFlag Flags = WallFlag::None;
        WallState State = WallState::Closed;
        TriggerID Trigger = TriggerID::None; // Trigger for this wall
        WClipID Clip = WClipID::None; // Animation to play for a door
        WallKey Keys = WallKey::None; // Required keys to open a door
        TriggerID ControllingTrigger = TriggerID::None; // Which trigger causes something to happen here. Should be saved as none from editor.
        sbyte cloak_value = 0; // Fade percentage if this wall is cloaked

        Option<bool> BlocksLight; // Editor override

        bool IsValid() const {
            return Tag.Segment != SegID::None;
        }

        bool HasFlag(WallFlag flag) const {
            return (uint8)Flags & (uint8)flag;
        }

        void SetFlag(WallFlag flag) {
            Flags = WallFlag((uint8)Flags | (uint8)flag);
        }

        static constexpr auto CloakStep = 1.0f / 31.0f;

        constexpr float CloakValue() const { return float(cloak_value % 32) * CloakStep; }
        constexpr void CloakValue(float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            cloak_value = sbyte(value / CloakStep);
        }
    };

    // Light generated by a level face
    struct DynamicLightInfo {
        Vector3 Position;
        Vector3 Normal;
        Color Color;
        float Distance;
        //SegID Segment;
    };

    struct FlickeringLight {
        Tag Tag;
        uint32 Mask = 0; // Flickering pattern. Each bit is on/off state.
        float Timer = 0; // Runtime timer for this light. Incremented each frame. Set to max value to disable.
        float Delay = 0; // Delay between each 'tick' of the mask in milliseconds

        void ShiftLeft() { Mask = std::rotl(Mask, 1); }
        void ShiftRight() { Mask = std::rotr(Mask, 1); }

        struct Defaults {
            static constexpr uint32 Strobe4 = 0b10000000'10000000'10000000'10000000;
            static constexpr uint32 Strobe8 = 0b10001000'10001000'10001000'10001000;
            static constexpr uint32 Flicker = 0b11111110'00000011'11000100'11011110;
            static constexpr uint32 On = 0b11111111'11111111'11111111'11111111;
        };
    };

    struct GameDataHeader {
        int32 Offset = -1; // Byte offset into the file
        int32 Count = 0; // The number of elements
        int32 ElementSize = 0; // The size of one element. Used for validation.
    };

    struct LevelFileInfo {
        static constexpr uint16 Signature = 0x6705;
        uint16 GameVersion;
        int32 Size;
        string FileName; // Unused
        int32 LevelNumber; // Unused
        int32 PlayerOffset, PlayerSize = 0;
        GameDataHeader Objects, Walls, Doors, Triggers, Links, ReactorTriggers,
            Matcen, DeltaLightIndices, DeltaLights;
    };

    struct LevelLimits {
        constexpr LevelLimits(int version) :
            Segments(version == 1 ? 800 : 900),
            Vertices(version == 1 ? 2808 : 3608),
            Walls(version == 1 ? 175 : 255),
            FlickeringLights(version >= 1 ? 100 : 0) {}

        int Objects = 350;
        int Segments; // Note that source ports allow thousands of segments
        int Matcens = 20;
        int Vertices;
        int Walls;
        int WallSwitches = 50;
        int WallLinks = 100;
        int FuelCenters = 70;
        int Reactor = 1;
        int Keys = 3;
        int Players = 8;
        int Coop = 3;
        int Triggers = 100;
        int FlickeringLights = 0;
    };

    constexpr auto MaxDynamicLights = 500;
    constexpr uint8 MaxDeltasPerLight = 255;
    constexpr auto MaxLightDeltas = 32000; // Rebirth limit. Original D2: 10000

    struct Level {
        string Palette = "groupa.256";
        SegID SecretExitReturn = SegID(0);
        Matrix SecretReturnOrientation;

        List<Vector3> Vertices;
        List<Segment> Segments;
        List<string> Pofs;
        List<Object> Objects;
        List<Wall> Walls;
        List<Trigger> Triggers;
        List<Matcen> Matcens;
        List<FlickeringLight> FlickeringLights; // Vertigo flickering lights

        // Reactor stuff
        int BaseReactorCountdown = 30;
        int ReactorStrength = -1;
        ResizeArray<Tag, MAX_TRIGGER_TARGETS> ReactorTriggers{};

        string Name; // Name displayed on automap

        static constexpr int MaxNameLength = 35; // +1 for null terminator

        int32 StaticLights = 0;
        int32 DynamicLights = 0;

        List<LightDeltaIndex> LightDeltaIndices;
        List<LightDelta> LightDeltas;

        // 22 to 25: Descent 1
        // 26 to 29: Descent 2
        // >32: D2X-XL, unsupported
        int16 GameVersion{};

        // 1: Descent 1
        // 2 to 7: Descent 2
        // 8: Vertigo Enhanced
        // >8: D2X-XL, unsupported
        int Version{};
        LevelLimits Limits = { 1 };

#pragma region EditorProperties
        string FileName; // Name in hog

        // Name of the level on the filesystem. Empty means it is in a hog or unsaved.
        filesystem::path Path;
#pragma endregion

        bool IsDescent1() const { return Version == 1; }
        // Includes vertigo and non-vertigo
        bool IsDescent2() const { return Version > 1 && Version <= 8; }
        // D2 level not enhanced
        bool IsDescent2NoVertigo() const { return Version > 1 && Version <= 7; }
        // D2 level vertigo enhanced
        bool IsVertigo() const { return Version == 8; }

        bool HasSecretExit() const;

        Vector3* TryGetVertex(PointID id) {
            if (!Seq::inRange(Vertices, id)) return nullptr;
            return &Vertices[id];
        }

        Matcen* TryGetMatcen(MatcenID id) {
            if (id == MatcenID::None || (int)id >= Matcens.size())
                return nullptr;

            return &Matcens[(int)id];
        }

        bool VertexIsValid(PointID id) const {
            return Seq::inRange(Vertices, id);
        }

        bool IsValidTriggerTarget(Tag tag) {
            if (!SegmentExists(tag)) return false;

            auto [seg, side] = GetSegmentAndSide(tag);

            if (seg.Type == SegmentType::Matcen) return true;

            auto wall = TryGetWall(tag);
            if (!wall) return false;

            if (seg.GetConnection(tag.Side) == SegID::None)
                return false; // targeting a solid wall makes no sense

            // could test specific wall types, but that would be annoying
            return true;
        }

        int GetSegmentCount(SegmentType type) {
            int count = 0;
            for (auto& seg : Segments)
                if (seg.Type == type) count++;

            return count;
        }

        constexpr const Wall& GetWall(WallID id) const { return Walls[(int)id]; }
        constexpr Wall& GetWall(WallID id) { return Walls[(int)id]; }

        constexpr Wall* TryGetWall(Tag tag) {
            if (tag.Segment == SegID::None) return nullptr;

            for (auto& wall : Walls) {
                if (wall.Tag == tag)
                    return &wall;
            }

            return nullptr;
        }

        constexpr Wall* TryGetWall(TriggerID trigger) {
            if (trigger == TriggerID::None) return nullptr;

            for (auto& wall : Walls) {
                if (wall.Trigger == trigger)
                    return &wall;
            }

            return nullptr;
        }

        constexpr WallID TryGetWallID(Tag tag) const {
            if (!tag) return WallID::None;

            for (int id = 0; id < Walls.size(); id++) {
                auto& wall = Walls[id];
                if (wall.Tag == tag)
                    return (WallID)id;
            }

            return WallID::None;
        }

        const Wall* TryGetWall(WallID id) const {
            if (id == WallID::None || (int)id >= Walls.size())
                return nullptr;

            // Check for invalid walls
            if (Walls[(int)id].Tag.Segment == SegID::None) return nullptr;
            return &Walls[(int)id];
        }

        Wall* TryGetWall(WallID id) { return (Wall*)std::as_const(*this).TryGetWall(id); }

        // Tries to get the side connecting the two segments
        Option<SideID> TryGetConnectedSide(SegID baseId, SegID otherId) const {
            if (!SegmentExists(otherId)) return {};
            auto& other = GetSegment(otherId);

            for (auto& side : SideIDs) {
                if (other.GetConnection(side) == baseId)
                    return { side };
            }

            return {};
        }

        // Gets the connected side of the other segment
        Tag GetConnectedSide(Tag src) const {
            if (!SegmentExists(src)) return {};

            auto& seg = GetSegment(src);
            auto otherId = seg.GetConnection(src.Side);
            if (auto other = TryGetSegment(otherId)) {
                for (auto& side : SideIDs) {
                    if (other->GetConnection(side) == src.Segment)
                        return { otherId, side };
                }
            }

            return {};
        }

        // Gets the wall connected to the other side of a wall (if present)
        WallID GetConnectedWall(WallID wallId) {
            auto wall = TryGetWall(wallId);
            if (!wall) return WallID::None;
            auto other = GetConnectedSide(wall->Tag);
            return TryGetWallID(other);
        }

        // Gets the wall connected to the other side of a wall (if present)
        WallID GetConnectedWall(Tag tag) {
            auto other = GetConnectedSide(tag);
            return TryGetWallID(other);
        }

        bool SegmentExists(SegID id) const {
            return Seq::inRange(Segments, (int)id);
        }

        bool SegmentExists(Tag tag) const { return SegmentExists(tag.Segment); }

        Segment* TryGetSegment(SegID id) {
            if (!Seq::inRange(Segments, (int)id)) return nullptr;
            return &Segments[(int)id];
        }

        const Segment* TryGetSegment(SegID id) const { 
            if (!Seq::inRange(Segments, (int)id)) return nullptr;
            return &Segments[(int)id];
        }

        Segment* TryGetSegment(Tag tag) { return TryGetSegment(tag.Segment); }
        const Segment* TryGetSegment(Tag tag) const { return TryGetSegment(tag.Segment); }

        Segment& GetSegment(SegID id) { return Segments[(int)id]; }
        const Segment& GetSegment(SegID id) const { return Segments[(int)id]; }

        Segment& GetSegment(Tag tag) { return Segments[(int)tag.Segment]; }
        const Segment& GetSegment(Tag tag) const { return Segments[(int)tag.Segment]; }

        SegmentSide& GetSide(Tag tag) {
            return Segments[(int)tag.Segment].Sides[(int)tag.Side];
        };

        SegmentSide* TryGetSide(Tag tag) {
            auto seg = TryGetSegment(tag);
            if (!seg) return nullptr;
            return &seg->GetSide(tag.Side);
        };

        Tuple<Segment&, SegmentSide&> GetSegmentAndSide(Tag tag) {
            auto& seg = Segments[(int)tag.Segment];
            auto& side = seg.Sides[(int)tag.Side];
            return { seg, side };
        }

        bool HasConnection(Tag tag) const {
            if (auto seg = TryGetSegment(tag))
                return seg->SideHasConnection(tag.Side);

            return false;
        }

        bool TryAddConnection(Tag srcId, Tag destId) {
            if (!SegmentExists(srcId) || !SegmentExists(destId)) return false;

            auto& src = GetSegment(srcId.Segment);
            auto& dest = GetSegment(destId.Segment);

            if (src.SideHasConnection(srcId.Side) || dest.SideHasConnection(destId.Side))
                return false;

            src.Connections[(int)srcId.Side] = destId.Segment;
            dest.Connections[(int)destId.Side] = srcId.Segment;
            return true;
        }

        const Object& GetObject(ObjID id) const { return Objects[(int)id]; }
        Object& GetObject(ObjID id) { return Objects[(int)id]; }
        Object* TryGetObject(ObjID id) {
            if ((int)id < 0 || (int)id >= Objects.size()) return nullptr;
            return &Objects[(int)id];
        }

        TriggerID GetTriggerID(WallID wid) const {
            auto wall = TryGetWall(wid);
            if (!wall) return TriggerID::None;
            return wall->Trigger;
        }

        Trigger& GetTrigger(TriggerID id) { return Triggers[(int)id]; }
        Trigger* TryGetTrigger(TriggerID id) {
            if ((int)id < 0 || (int)id >= Triggers.size()) return nullptr;
            return &Triggers[(int)id];
        }

        Trigger* TryGetTrigger(WallID wid) {
            auto wall = TryGetWall(wid);
            if (!wall) return nullptr;
            return TryGetTrigger(wall->Trigger);
        }

        // Returns segments that contain a given vertex
        List<SegID> SegmentsByVertex(uint i);

        Array<Vector3, 4> VerticesForSide(Tag tag) {
            Array<Vector3, 4> verts{};

            if (auto seg = TryGetSegment(tag.Segment)) {
                auto indices = seg->GetVertexIndices(tag.Side);
                for (int i = 0; i < 4; i++)
                    verts[i] = Vertices[indices[i]];
            }

            return verts;
        }

        Option<PointID> IndexForSide(PointTag tag) {
            if (auto seg = TryGetSegment(tag.Segment)) {
                auto indices = seg->GetVertexIndices(tag.Side);
                return indices[tag.Point % 4];
            }

            return {};
        }

        Vector3* VertexForSide(PointTag tag) {
            if (auto seg = TryGetSegment(tag.Segment)) {
                auto indices = seg->GetVertexIndices(tag.Side);
                return &Vertices[indices[tag.Point % 4]];
            }

            return nullptr;
        }

        LightDeltaIndex* GetLightDeltaIndex(Tag light) {
            for (auto& index : LightDeltaIndices)
                if (index.Tag == light) return &index;

            return nullptr;
        }

        FlickeringLight* GetFlickeringLight(Tag light) {
            for (auto& index : FlickeringLights)
                if (index.Tag == light) return &index;

            return nullptr;
        }

        void UpdateAllGeometricProps() {
            for (auto& seg : Segments) {
                seg.UpdateNormals(*this);
                seg.UpdateCenter(*this);
            }
        }

        bool CanAddMatcen() { return Matcens.size() < Limits.Matcens; }

        size_t Serialize(StreamWriter& writer);
        static Level Deserialize(span<ubyte>);
    };
}