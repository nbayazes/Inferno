#pragma once

#include "Segment.h"
#include "Types.h"
#include "Utility.h"

namespace Inferno {
    struct NavigationNode {
        Vector3 Position;
        SegID Segment = SegID::None; // None indicates node is not directly associated with a segment
        Tag Tag; // Tag this node is associated with. Could be none for intermediate nodes.
        List<int> Connections;
        //int PortalID = -1; // For the edge nodes
        //RoomID Room;
    };

    // A room is a group of segments divided by walls
    struct Room {
        List<SegID> Segments;
        List<Portal> Portals; // Which tags of this room have connections to other rooms
        List<RoomID> NearbyRooms; // Rooms potentially visible from this one
        List<SegID> VisibleSegments; // Segments potentially visible from this room
        List<EffectID> Effects; // Effects visible in this room
        List<int> WallMeshes; // Index for which wall meshes to render

        //Sound::Reverb Reverb = Sound::Reverb::Generic;
        Color Fog;
        float FogDepth = -1;
        SegmentType Type = SegmentType::None;

        int Meshes{}; // Meshes for each material
        //float EstimatedSize;
        //bool ContainsLava{}, ContainsWater{}; // Ambient noise
        SoundID AmbientSound = SoundID::None;
        DirectX::BoundingOrientedBox Bounds;
        Vector3 Center;

        List<List<float>> PortalDistances; // List of distances from portal A to ABCD
        List<NavigationNode> NavNodes;

        bool Contains(SegID id) {
            return Seq::contains(Segments, id);
        }

        void AddPortal(Portal portal) {
            for (auto& p : Portals) {
                if (p.Tag == portal.Tag) return;
            }

            Portals.push_back(portal);
        }

        void AddSegment(SegID seg) {
            if (!Seq::contains(Segments, seg))
                Segments.push_back(seg);
        }

        Portal* GetPortal(Tag tag) {
            for (auto& portal : Portals) {
                if (portal.Tag == tag) return &portal;
            }

            return nullptr;
        }

        int GetPortalIndex(Tag tag) const {
            for (int i = 0; i < Portals.size(); i++) {
                if (Portals[i].Tag == tag) return i;
            }

            return -1;
        }

        bool IsPortal(Tag tag) const {
            for (auto& portal : Portals) {
                if (portal.Tag == tag) return true;
            }

            return false;
        }

        int FindClosestNode(const Vector3& position) const {
            float closest = FLT_MAX;
            int index = 0;
            for (int i = 0; i < NavNodes.size(); i++) {
                auto dist = Vector3::DistanceSquared(NavNodes[i].Position, position);
                if (dist < closest) {
                    closest = dist;
                    index = i;
                }
            }

            return index;
        }

        DirectX::BoundingBox GetBounds(Level& level) const;
    };
}
