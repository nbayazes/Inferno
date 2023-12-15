#pragma once
#include <queue>
#include "LevelMesh.h"
#include "Mesh.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Render {
    enum class RenderCommandType {
        LevelMesh, Object, Effect
    };

    struct Bounds2D {
        Vector2 Min, Max;
        bool CrossesPlane = false;

        // Note: this isn't implemented robustly and order of operations matters
        Bounds2D Intersection(const Bounds2D& bounds) const {
            auto min = Vector2::Max(bounds.Min, Min);
            auto max = Vector2::Min(bounds.Max, Max);
            if (max.x <= min.x || max.y <= min.y)
                return {}; // no intersection

            return { min, max, CrossesPlane };
        }

        constexpr bool Empty() const {
            return Min.x == Max.x || Min.y == Max.y;
        }

        static Bounds2D FromPoints(const Array<Vector3, 4>& points) {
            Vector2 min(FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX);
            bool crossesPlane = false;

            for (auto& p : points) {
                if (p.x < min.x)
                    min.x = p.x;
                if (p.y < min.y)
                    min.y = p.y;
                if (p.x > max.x)
                    max.x = p.x;
                if (p.y > max.y)
                    max.y = p.y;

                if (p.z < 0)
                    crossesPlane = true;
            }

            return { min, max, crossesPlane };
        }
    };

    struct RenderCommand {
        float Depth; // Scene depth for sorting
        RenderCommandType Type;

        union Data {
            struct Object* Object;
            Inferno::LevelMesh* LevelMesh;
            EffectBase* Effect;
        } Data{};

        RenderCommand(Object* obj, float depth)
            : Depth(depth), Type(RenderCommandType::Object) {
            Data.Object = obj;
        }

        RenderCommand(LevelMesh* mesh, float depth)
            : Depth(depth), Type(RenderCommandType::LevelMesh) {
            Data.LevelMesh = mesh;
        }

        RenderCommand(EffectBase* effect, float depth)
            : Depth(depth), Type(RenderCommandType::Effect) {
            Data.Effect = effect;
        }
    };

    class RenderQueue {
        struct SegDepth {
            SegID Seg = SegID::None;
            float Depth = 0;
            bool Culled = false;
        };

        List<RenderCommand> _opaqueQueue;
        List<RenderCommand> _decalQueue;
        List<RenderCommand> _transparentQueue;
        List<RenderCommand> _distortionQueue;
        Set<SegID> _visited;
        std::queue<SegDepth> _search;
        List<RoomID> _roomQueue;

        struct ObjDepth {
            Object* Obj = nullptr;
            float Depth = 0;
            EffectBase* Effect;
        };

        List<ObjDepth> _objects;

    public:
        void Update(Level& level, LevelMeshBuilder& meshBuilder, bool drawObjects);
        span<RenderCommand> Opaque() { return _opaqueQueue; }
        span<RenderCommand> Decal() { return _decalQueue; }
        span<RenderCommand> Transparent() { return _transparentQueue; }
        span<RenderCommand> Distortion() { return _distortionQueue; }
        span<RoomID> GetVisibleRooms() { return _roomQueue; }

    private:
        void QueueEditorObject(Object& obj, float lerp);
        void QueueRoomObjects(Level& level, const Room& room);
        void CheckRoomVisibility(Level& level, Room& room, const Bounds2D& srcBounds, int depth);
        void TraverseLevelRooms(RoomID startRoomId, Level& level, span<LevelMesh> wallMeshes);
    };
}
