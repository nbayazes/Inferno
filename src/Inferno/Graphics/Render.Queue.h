#pragma once
#include <queue>
#include "LevelMesh.h"
#include "Mesh.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Render {
    enum class RenderCommandType {
        LevelMesh, Object, Effect
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
        List<RenderCommand> _opaqueQueue;
        List<RenderCommand> _transparentQueue;
        Set<SegID> _visited;
        std::queue<SegID> _search;
    public:
        void Update(Level& level, span<LevelMesh> levelMeshes, span<LevelMesh> wallMeshes);
        span<RenderCommand> Opaque() { return _opaqueQueue; }
        span<RenderCommand> Transparent() { return _transparentQueue; }
    private:
        void QueueEditorObject(Object& obj, float lerp);
        void TraverseLevel(SegID startId, Level& level, span<LevelMesh> wallMeshes);
    };
}
