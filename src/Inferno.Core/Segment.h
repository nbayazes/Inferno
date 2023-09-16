#pragma once

#include "Types.h"

namespace Inferno {
    enum class SideSplitType : uint8 {
        Quad = 1, // render side as quadrilateral
        Tri02 = 2, // render side as two triangles, triangulated along edge from 0 to 2
        Tri13 = 3, // render side as two triangles, triangulated along edge from 1 to 3
    };

    enum class OverlayRotation : uint16 { Rotate0, Rotate90, Rotate180, Rotate270 };

    constexpr float GetOverlayRotationAngle(OverlayRotation rotation) {
        switch (rotation) {
            case OverlayRotation::Rotate0: default: return 0.0f;
            case OverlayRotation::Rotate90: return DirectX::XM_PIDIV2;
            case OverlayRotation::Rotate180: return DirectX::XM_PI;
            case OverlayRotation::Rotate270: return DirectX::XM_PI * 1.5f;
        }
    }

    struct SegmentSide {
        SideSplitType Type = SideSplitType::Quad;
        WallID Wall = WallID::None;
        Array<Vector3, 2> Normals;
        Array<Vector3, 2> Tangents;
        Array<Vector3, 2> Centers;
        Vector3 AverageNormal;
        Vector3 Center;

        LevelTexID TMap, TMap2{};
        OverlayRotation OverlayRotation = OverlayRotation::Rotate0;
        Array<Vector2, 4> UVs = { Vector2(0, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0) };
        Array<Color, 4> Light = { Color(1, 1, 1), Color(1, 1, 1), Color(1, 1, 1), Color(1, 1, 1) };
        Array<bool, 4> LockLight = { false, false, false, false }; // Locks light values from being updated from the light algorithm

        Option<Color> LightOverride; // Editor defined override for amount of light emitted
        Option<float> LightRadiusOverride; // Editor defined override for light radius
        Option<float> LightPlaneOverride; // Editor defined override for light plane tolerance
        Option<float> DynamicMultiplierOverride; // Multiplier used for flickering or breaking lights
        bool EnableOcclusion = true; // Editor defined override for light occlusion

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

        const std::array<uint16, 6>& GetRenderIndices() const {
            static constexpr std::array<uint16, 6> TRI02 = { 0u, 1u, 2u, 0u, 2u, 3u };
            static constexpr std::array<uint16, 6> TRI13 = { 0u, 1u, 3u, 3u, 1u, 2u };
            return Type == SideSplitType::Tri13 ? TRI13 : TRI02;
        }

        const Vector3& NormalForEdge(int edge) const {
            if (Type == SideSplitType::Tri02)
                return (edge == 0 || edge == 1) ? Normals[0] : Normals[1];
            else
                return (edge == 0 || edge == 3) ? Normals[0] : Normals[1];
        }

        const Vector3& CenterForEdge(int edge) const {
            if (Type == SideSplitType::Tri02)
                return (edge == 0 || edge == 1) ? Centers[0] : Centers[1];
            else
                return (edge == 0 || edge == 3) ? Centers[0] : Centers[1];
        }
    };

    constexpr uint16 MAX_SIDES = 6;
    constexpr uint16 MAX_VERTICES = 8;

    // Segment point ids for a segment side
    inline constexpr Array<Array<int16, 4>, MAX_SIDES> SIDE_INDICES{
        {
            { { 7, 6, 2, 3 } }, // left
            { { 0, 4, 7, 3 } }, // top
            { { 0, 1, 5, 4 } }, // right
            { { 2, 6, 5, 1 } }, // bottom
            { { 4, 5, 6, 7 } }, // back
            { { 3, 2, 1, 0 } }, // front
        }
    };

    // Lookup for the edges of each side. Uses the same order / winding as the vertex lookup.
    inline constexpr Array<Array<int16, 4>, MAX_SIDES> EDGES_OF_SIDE{
        {
            { { 4, 9, 0, 8 } }, // right
            { { 11, 7, 8, 3 } }, // top
            { { 2, 10, 6, 11 } }, // left
            { { 9, 5, 10, 1 } }, // bottom
            { { 6, 5, 4, 7 } }, // back
            { { 0, 1, 2, 3 } }, // front
        }
    };

    inline constexpr Array<Array<int16, 2>, 12> VERTS_OF_EDGE{
        {
            { { 0, 1 } }, // 0 // front
            { { 1, 2 } }, // 1
            { { 2, 3 } }, // 2
            { { 3, 0 } }, // 3
            { { 6, 7 } }, // 6
            { { 5, 6 } }, // 5
            { { 4, 5 } }, // 4 // back
            { { 7, 4 } }, // 7
            { { 0, 7 } }, // 8
            { { 1, 6 } }, // 9
            { { 2, 5 } }, // 10
            { { 3, 4 } } // 11
        }
    };

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

    struct Level;

    enum class SoundFlag : ubyte {
        AmbientWater = 0x01,
        AmbientLava = 0x02
    };

    struct Segment {
        Array<SegID, MAX_SIDES> Connections = { SegID::None, SegID::None, SegID::None, SegID::None, SegID::None, SegID::None };
        Array<SegmentSide, MAX_SIDES> Sides{};
        Array<PointID, MAX_VERTICES> Indices{}; // index into the global vertex buffer
        SegmentType Type = SegmentType::None; // What type of center this is
        MatcenID Matcen = MatcenID::None; // Which center segment is associated
        ubyte StationIndex{};
        ubyte Value{}; // related to fuel center numbers, unused
        SoundFlag AmbientSound{};

        List<ObjID> Objects;
        List<EffectID> Effects;

        // If bit n (1 << n) is set, then side #n in segment has had light subtracted from original (editor-computed) value.
        uint8 LightSubtracted;
        //uint8 SlideTextures;
        Color VolumeLight = { 1, 1, 1 };
        bool LockVolumeLight; // Locks volume light from being updated
        Vector3 Center;
        RoomID Room = RoomID::None; // Room this segment belongs to

        constexpr SegID GetConnection(SideID side) const { return Connections[(int)side]; }
        SegID& GetConnection(SideID side) { return Connections[(int)side]; }

        void RemoveObject(ObjID id);
        void AddObject(ObjID id);

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

        bool SideIsSolid(SideID side, const Level& level) const;

        bool LightIsSubtracted(SideID side) const {
            return LightSubtracted & (1 << (int)side);
        }

        bool SideContainsPoint(SideID side, PointID point) const {
            for (auto& si : SIDE_INDICES[(int)side]) {
                if (point == Indices[si]) return true;
            }

            return false;
        }

        // Vertex indices for a side in the vertex buffer
        Array<PointID, 4> GetVertexIndices(SideID side) const {
            Array<PointID, 4> indices{};
            auto& sideVerts = SIDE_INDICES[(int)side];
            for (int i = 0; i < indices.size(); i++)
                indices[i] = Indices[sideVerts[i]];

            return indices;
        }

        Array<PointID*, 4> GetVertexIndicesRef(SideID side) {
            auto& sideVerts = SIDE_INDICES[(int)side];
            return {
                &Indices[sideVerts[0]],
                &Indices[sideVerts[1]],
                &Indices[sideVerts[2]],
                &Indices[sideVerts[3]]
            };
        }

        PointID GetVertexIndex(SideID side, uint16 point) const {
            auto& indices = SIDE_INDICES[(int)side];
            return Indices[indices[point % 4]];
        }

        Tuple<LevelTexID, LevelTexID> GetTexturesForSide(SideID side) const {
            auto& s = Sides[(int)side];
            return { s.TMap, s.TMap2 };
        }

        // Returns the longest distance between two sides
        float GetLongestSide() const {
            auto d0 = Vector3::Distance(Sides[0].Center, Sides[2].Center);
            auto d1 = Vector3::Distance(Sides[1].Center, Sides[3].Center);
            auto d2 = Vector3::Distance(Sides[4].Center, Sides[5].Center);
            return std::max({ d0, d1, d2 });
        }

        // Updates the normals and centers
        void UpdateGeometricProps(const Level&);
        float GetEstimatedVolume() const;
        bool IsZeroVolume(Level&);

        // Returns the vertices of the segment
        Array<const Vector3*, 8> GetVertices(const Level&) const;

        Array<Vector3, 8> CopyVertices(const Level&) const;
    };

    inline SideID GetAdjacentSide(SideID side, int edge) {
        auto s = (int)side;
        if (s > 5 || s < 0) return SideID::None;

        // For each side, returns the adjacent side for each edge
        static constexpr Array<Array<SideID, 4>, 6> AdjacentFaceTable = {
            Array<SideID, 4>{ SideID(4), SideID(3), SideID(5), SideID(1) }, // Side 0
            Array<SideID, 4>{ SideID(2), SideID(4), SideID(0), SideID(5) }, // Side 1
            Array<SideID, 4>{ SideID(5), SideID(3), SideID(4), SideID(1) }, // Side 2
            Array<SideID, 4>{ SideID(0), SideID(4), SideID(2), SideID(5) }, // Side 3
            Array<SideID, 4>{ SideID(2), SideID(3), SideID(0), SideID(1) }, // Side 4
            Array<SideID, 4>{ SideID(0), SideID(3), SideID(2), SideID(1) }, // Side 5
        };

        return AdjacentFaceTable[s][edge % 4];
    }
}