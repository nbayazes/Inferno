#pragma once
#include <queue>
#include "LevelMesh.h"
#include "Mesh.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Render {
    enum class RenderCommandType {
        LevelMesh, Object, Particle, Emitter, Debris, Beam
    };

    struct RenderCommand {
        float Depth; // Scene depth for sorting
        RenderCommandType Type;
        union Data {
            struct Object* Object;
            Inferno::LevelMesh* LevelMesh;
            ParticleEmitter* Emitter;
            Particle* Particle;
            struct Debris* Debris;
            BeamInfo* Beam;
        } Data{};

        RenderCommand(Object* obj, float depth)
            : Depth(depth), Type(RenderCommandType::Object) {
            Data.Object = obj;
        }

        RenderCommand(LevelMesh* mesh, float depth)
            : Depth(depth), Type(RenderCommandType::LevelMesh) {
            Data.LevelMesh = mesh;
        }

        RenderCommand(Particle* particle, float depth)
            : Depth(depth), Type(RenderCommandType::Particle) {
            Data.Particle = particle;
        }

        RenderCommand(ParticleEmitter* emitter, float depth)
            : Depth(depth), Type(RenderCommandType::Emitter) {
            Data.Emitter = emitter;
        }

        RenderCommand(Debris* debris, float depth)
            : Depth(depth), Type(RenderCommandType::Debris) {
            Data.Debris = debris;
        }

        RenderCommand(BeamInfo* beam, float depth)
            : Depth(depth), Type(RenderCommandType::Beam) {
            Data.Beam = beam;
        }
    };

    class RenderQueue {
        List<RenderCommand> _opaqueQueue;
        List<RenderCommand> _transparentQueue;
        MeshBuffer* _meshBuffer;
        Set<SegID> _visited;
        std::queue<SegID> _search;
    public:
        RenderQueue(MeshBuffer* meshBuffer) : _meshBuffer(meshBuffer) {}

        void Update(Level& level, span<LevelMesh> levelMeshes, span<LevelMesh> wallMeshes);
        span<RenderCommand> Opaque() { return _opaqueQueue; }
        span<RenderCommand> Transparent() { return _transparentQueue; }
    private:
        void QueueEditorObject(Object& obj, float lerp);
        void TraverseLevel(SegID startId, Level& level, span<LevelMesh> wallMeshes);
    };
}
