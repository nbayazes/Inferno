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

        bool Overlaps(const Bounds2D& bounds) const {
            return Min.x < bounds.Max.x && Max.x > bounds.Min.x &&
                Max.y > bounds.Min.y && Min.y < bounds.Max.y;
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
        void Update(Level& level, span<LevelMesh> levelMeshes, span<LevelMesh> wallMeshes, bool drawObjects);
        span<RenderCommand> Opaque() { return _opaqueQueue; }
        span<RenderCommand> Transparent() { return _transparentQueue; }
        span<RenderCommand> Distortion() { return _distortionQueue; }
        span<RoomID> GetVisibleRooms() { return _roomQueue; }
    private:
        void QueueEditorObject(Object& obj, float lerp);
        void QueueRoomObjects(Level& level, const Room& room);
        void CheckRoomVisibility(Level& level, Room& room, const Bounds2D& srcBounds, int depth);
        void TraverseLevelRooms(RoomID startRoomId, Level& level,span<LevelMesh> wallMeshes);
    };
}
