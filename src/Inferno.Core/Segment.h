#pragma once

#include "Types.h"

namespace Inferno {
    enum class SideSplitType : uint8 {
        Quad = 1, // render side as quadrilateral
        Tri02 = 2, // render side as two triangles, triangulated along edge from 0 to 2
        Tri13 = 3, // render side as two triangles, triangulated along edge from 1 to 3
    };

    enum class OverlayRotation : uint16 { Rotate0, Rotate90, Rotate180, Rotate270 };

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
        Array<Color, 4> Light = { Color(1,1,1), Color(1,1,1), Color(1,1,1), Color(1,1,1) };
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

        bool SideIsSolid(SideID side, Level& level) const;

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

        PointID GetVertexIndex(SideID side, uint16 point) {
            auto& indices = Inferno::SideIndices[(int)side];
            return Indices[indices[point % 4]];
        }

        Tuple<LevelTexID, LevelTexID> GetTexturesForSide(SideID side) const {
            auto& s = Sides[(int)side];
            return { s.TMap, s.TMap2 };
        }

        // Updates the normals and centers
        void UpdateGeometricProps(const Level&);
        float GetEstimatedVolume(Level&);
        bool IsZeroVolume(Level&);

        // Returns the vertices of the segment
        Array<const Vector3*, 8> GetVertices(const Level&) const;

        Array<Vector3, 4> GetVertices(const Level&, SideID) const;

        Array<Vector3, 8> CopyVertices(const Level&);
    };

    inline SideID GetAdjacentSide(SideID side, int edge) {
        if (side > SideID(5) && side < SideID(0)) return SideID::None;

        // For each side, returns the adjacent side for each edge
        static constexpr Array<Array<SideID, 4>, 6> AdjacentFaceTable = {
            Array<SideID, 4>{ SideID(4), SideID(3), SideID(5), SideID(1) }, // Side 0
            Array<SideID, 4>{ SideID(2), SideID(4), SideID(0), SideID(5) }, // Side 1
            Array<SideID, 4>{ SideID(5), SideID(3), SideID(4), SideID(1) }, // Side 2
            Array<SideID, 4>{ SideID(0), SideID(4), SideID(2), SideID(5) }, // Side 3
            Array<SideID, 4>{ SideID(2), SideID(3), SideID(0), SideID(1) }, // Side 4
            Array<SideID, 4>{ SideID(0), SideID(3), SideID(2), SideID(1) }, // Side 5
        };

        return AdjacentFaceTable[(int)side][edge % 4];
    }
}