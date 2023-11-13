#pragma once

#include "Types.h"
#include "Utility.h"
#include "Object.h"
#include "Streams.h"
#include "Wall.h"
#include "DataPool.h"
#include "Room.h"
#include "Segment.h"

namespace Inferno {
    struct Matcen {
        uint32 Robots{};
        uint32 Robots2{}; // Additional D2 robot flag
        SegID Segment = SegID::None; // Segment this is attached to
        int16 Producer{}; // Index to matcen runtime data
        int32 HitPoints{}; // Unused but present in file data
        int32 Interval{}; // Unused but present in file data

        int8 Energy{}; // Number of times it can be activated
        bool Active{};
        int8 RobotCount{}; // Number of robots to create per activation
        float Timer{};
        float Delay{}; // Randomized delay to the next robot
        bool CreateRobotState = false;
        List<SegID> TriggerPath; // Path to the trigger that last activated this matcen. Used for robot pathing.
        EffectID Light = EffectID::None; // Permanent light to indicate the matcen has activations remaining

        // Returns the robot ids from merging the two flags
        List<int8> GetEnabledRobots() const;
    };

    struct LightDeltaIndex {
        Tag Tag; // Which light source?
        uint8 Count = 0; // Number of affected sides
        int16 Index = -1;
    };

    using SideLighting = Array<Color, 4>;

    struct LightDelta {
        Tag Tag; // Which side to affect?
        SideLighting Color{};
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
        static constexpr uint16 SIGNATURE = 0x6705;
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

    constexpr auto MAX_DYNAMIC_LIGHTS = 500;
    constexpr uint8 MAX_DELTAS_PER_LIGHT = 255;
    constexpr auto MAX_LIGHT_DELTAS = 32000; // Rebirth limit. Original D2: 10000
    constexpr int DEFAULT_REACTOR_COUNTDOWN = 30;

    struct Level {
        string Palette = "groupa.256";
        SegID SecretExitReturn = SegID(0);
        Matrix3x3 SecretReturnOrientation;

        List<Vector3> Vertices;
        List<Segment> Segments;
        List<string> Pofs;
        List<Object> Objects;
        List<Wall> Walls;
        List<Trigger> Triggers;
        List<Matcen> Matcens;
        List<FlickeringLight> FlickeringLights; // Vertigo flickering lights
        List<Room> Rooms;

        // Reactor stuff
        int BaseReactorCountdown = DEFAULT_REACTOR_COUNTDOWN;
        int ReactorStrength = -1;
        ResizeArray<Tag, MAX_TRIGGER_TARGETS> ReactorTriggers{};

        string Name; // Name displayed on automap

        static constexpr int MAX_NAME_LENGTH = 35; // +1 for null terminator

        int32 StaticLights = 0;
        int32 DynamicLights = 0;

        List<LightDeltaIndex> LightDeltaIndices; // Index into LightDeltas
        List<LightDelta> LightDeltas; // For breakable or flickering lights

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

        DataPool<ActiveDoor> ActiveDoors{ &ActiveDoor::IsAlive, 20 };


#pragma region EditorProperties
        string FileName; // Name in hog

        // Name of the level on the filesystem. Empty means it is in a hog or unsaved.
        filesystem::path Path;

        Vector3 CameraPosition;
        Vector3 CameraTarget;
        Vector3 CameraUp;
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

        int GetSegmentCount(SegmentType type) const {
            int count = 0;
            for (auto& seg : Segments)
                if (seg.Type == type) count++;

            return count;
        }

        constexpr const Wall& GetWall(WallID id) const { return Walls[(int)id]; }
        constexpr Wall& GetWall(WallID id) { return Walls[(int)id]; }

        constexpr Wall* TryGetWall(Tag tag) {
            if (!tag)
                return nullptr;

            if (auto seg = TryGetSegment(tag)) {
                auto id = (int)seg->GetSide(tag.Side).Wall;
                if (Seq::inRange(Walls, id))
                    return &Walls[id];
            }

            return nullptr;
        }

        constexpr const Wall* TryGetWall(Tag tag) const {
            if (!tag)
                return nullptr;

            if (auto seg = TryGetSegment(tag)) {
                auto id = (int)seg->GetSide(tag.Side).Wall;
                if (Seq::inRange(Walls, id))
                    return &Walls[id];
            }

            return nullptr;
        }

        constexpr WallID TryGetWallID(Tag tag) const {
            if (!tag) return WallID::None;
            if (auto seg = TryGetSegment(tag))
                return seg->GetSide(tag.Side).Wall;

            return WallID::None;
        }

        constexpr Tuple<Wall*, Wall*> TryGetWalls(Tag tag) {
            if (!tag)
                return { nullptr, nullptr };

            auto wall = TryGetWall(tag);
            auto cwall = TryGetWall(GetConnectedWall(tag));
            return { wall, cwall };
        }

        constexpr WallID GetWallID(Tag tag) const {
            if (!tag) return WallID::None;
            if (auto seg = TryGetSegment(tag))
                return seg->GetSide(tag.Side).Wall;

            return WallID::None;
        }

        constexpr Wall* TryGetWall(TriggerID trigger) {
            if (trigger == TriggerID::None) return nullptr;

            for (auto& wall : Walls) {
                if (wall.Trigger == trigger)
                    return &wall;
            }

            return nullptr;
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
        SideID GetConnectedSide(SegID src, SegID dst) const {
            if (!SegmentExists(src) || !SegmentExists(dst))
                return SideID::None;

            auto& other = GetSegment(dst);

            for (auto& side : SideIDs) {
                if (other.GetConnection(side) == src)
                    return side;
            }

            return SideID::None;
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

        Wall* GetConnectedWall(const Wall& wall) {
            auto other = GetConnectedSide(wall.Tag);
            return TryGetWall(other);
        }

        // Gets the wall connected to the other side of a wall (if present)
        WallID GetConnectedWall(WallID wallId) {
            auto wall = TryGetWall(wallId);
            if (!wall) return WallID::None;
            auto other = GetConnectedSide(wall->Tag);
            return TryGetWallID(other);
        }

        // Gets the wall connected to the other side of a wall (if present)
        WallID GetConnectedWall(Tag tag) const {
            auto other = GetConnectedSide(tag);
            return TryGetWallID(other);
        }

        // Gets the wall connected to the other side of a wall (if present)
        Wall* TryGetConnectedWall(Tag tag) {
            auto id = GetConnectedWall(tag);
            return TryGetWall(id);
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

        // Unchecked access
        SegmentSide& GetSide(Tag tag) {
            return Segments[(int)tag.Segment].Sides[(int)tag.Side];
        }

        const SegmentSide& GetSide(Tag tag) const {
            return Segments[(int)tag.Segment].Sides[(int)tag.Side];
        }

        SegmentSide* TryGetSide(Tag tag) {
            if (tag.Side == SideID::None) return nullptr;
            auto seg = TryGetSegment(tag);
            if (!seg) return nullptr;
            return &seg->GetSide(tag.Side);
        };

        SegmentSide* TryGetConnectedSide(Tag tag) {
            return TryGetSide(GetConnectedSide(tag));
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

        Object* TryGetObject(ObjID id) {
            if (!Seq::inRange(Objects, (int)id)) return nullptr;
            return &Objects[(int)id];
        }

        const Object* TryGetObject(ObjID id) const {
            if (!Seq::inRange(Objects, (int)id)) return nullptr;
            return &Objects[(int)id];
        }

        Object* TryGetObject(ObjRef ref) {
            auto obj = TryGetObject(ref.Id);
            if (!obj || obj->Signature != ref.Signature) return nullptr;
            return obj;
        }

        const Object* TryGetObject(ObjRef ref) const {
            auto obj = TryGetObject(ref.Id);
            if (!obj || obj->Signature != ref.Signature) return nullptr;
            return obj;
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

        Trigger* TryGetTrigger(Tag tag) {
            if (auto wall = TryGetWall(tag))
                return TryGetTrigger(wall->Trigger);

            return nullptr;
        }

        // Returns segments that contain a given vertex
        List<SegID> SegmentsByVertex(uint i) const;

        Array<Vector3, 4> VerticesForSide(Tag tag) const {
            Array<Vector3, 4> verts{};

            if (auto seg = TryGetSegment(tag.Segment)) {
                auto indices = seg->GetVertexIndices(tag.Side);
                for (int i = 0; i < 4; i++)
                    verts[i] = Vertices[indices[i]];
            }

            return verts;
        }

        Option<PointID> IndexForSide(PointTag tag) const {
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
                seg.UpdateGeometricProps(*this);
            }
        }

        bool CanAddMatcen() const { return Matcens.size() < Limits.Matcens; }

        RoomID GetRoomID(const Object& obj) {
            if (auto seg = TryGetSegment(obj.Segment))
                return seg->Room;
            return RoomID::None;
        }

        RoomID GetRoomID(SegID seg) {
            for (int i = 0; i < Rooms.size(); i++) {
                if (Seq::contains(Rooms[i].Segments, seg)) return RoomID(i);
            }

            return RoomID::None;
        }

        Room* GetRoom(const Object& obj) {
            auto id = GetRoomID(obj);
            if (id == RoomID::None) return nullptr;
            return &Rooms[(int)id];
        }

        Room* GetRoom(RoomID id) {
            if (!Seq::inRange(Rooms, (int)id)) return nullptr;
            return &Rooms[(int)id];
        }

        Room* GetRoom(SegID id) {
            auto roomId = GetRoomID(id);
            if (roomId == RoomID::None) return nullptr;
            return &Rooms[(int)roomId];
        }



        Portal* GetPortal(Tag tag) {
            if (auto room = GetRoom(tag.Segment)) {
                return room->GetPortal(tag);
            }

            return nullptr;
        }

        //Room* GetConnectedRoom(Tag tag) {
        //    if (auto room = GetRoom(tag.Segment)) {
        //        if (auto portal = room->GetPortal(tag)) {
        //            return GetRoom(portal->RoomLink);
        //        }
        //    }

        //    return nullptr;
        //}

        size_t Serialize(StreamWriter& writer);
        static Level Deserialize(span<ubyte>);
    };
}
