#pragma once
#include <queue>
#include "Game.Visibility.h"
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

        void Expand(const Vector2& point) {
            if (point.x < Min.x) Min.x = point.x;
            if (point.x > Max.x) Max.x = point.x;

            if (point.y < Min.y) Min.y = point.y;
            if (point.y < Min.y) Max.y = point.y;
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

    class RoomStack {
        List<RoomID> _stack;
        int _index = 0;

    public:
        RoomStack(size_t size) : _stack(size) {
            Reset();
        }

        void Reset() {
            _index = 0;
            ranges::fill(_stack, RoomID::None);
        }

        bool Push(RoomID id) {
            //ASSERT(!Contains(id));
            //if (Contains(id)) return false;

            if (_index + 1 >= _stack.size()) {
                SPDLOG_WARN("Reached max portal stack depth");
                return false;
            }

            _stack[_index++] = id;
            return true;
        }

        bool Contains(RoomID id) {
            return Seq::contains(_stack, id);
        }

        void Rewind(RoomID id) {
            ASSERT(Seq::contains(_stack, id));

            //while (_index > 0) {
            //    _index--;

            //    if (_stack[_index] == id)
            //        break;

            //    _stack[_index] = RoomID::None;
            //}
            for (; _index > 0; _index--) {
                if (_stack[_index] == id)
                    break;

                _stack[_index] = RoomID::None;
            }
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
        //List<RenderCommand> _distortionQueue;
        Set<SegID> _visited;
        std::queue<SegDepth> _search;
        List<RoomID> _visibleRooms;
        RoomStack _roomStack = { 50 };

        struct ObjDepth {
            Object* Obj = nullptr;
            float Depth = 0;
            EffectBase* Effect;
        };

        List<ObjDepth> _objects;

        struct SegmentInfo {
            Window window;
            bool visited = false;
            bool processed = false;
            bool queued = false;
        };

        List<SegmentInfo> _segInfo;
        List<SegID> _renderList;
        List<char> _roomList;

    public:
        void Update(Level& level, LevelMeshBuilder& meshBuilder, bool drawObjects, const Camera& camera);
        span<RenderCommand> Opaque() { return _opaqueQueue; }
        span<RenderCommand> Decal() { return _decalQueue; }
        span<RenderCommand> Transparent() { return _transparentQueue; }
        //span<RenderCommand> Distortion() { return _distortionQueue; }
        span<RoomID> GetVisibleRooms() { return _visibleRooms; }

        void TraverseSegments(Level& level, const Camera& camera, span<LevelMesh> wallMeshes, SegID startSeg);

        // When false, uses per-segment lighting.
        // Segment lighting causes more popin, but room lighting causes more bleeding.
        bool UseRoomLighting = true; 
    private:
        void QueueEditorObject(Object& obj, float lerp, const Camera& camera);
        void QueueSegmentObjects(Level& level, const Segment& seg, const Camera& camera);
        //void QueueRoomObjects(Level& level, const Room& room, const Camera& camera);
        //void CheckRoomVisibility(Level& level, const Portal& srcPortal, const Bounds2D& srcBounds, const Camera& camera);
        //void TraverseLevelRooms(RoomID startRoomId, Level& level, span<LevelMesh> wallMeshes, const Camera& camera);
    };

    Option<Array<Vector3, 4>> GetNdc(const ConstFace& face, const Matrix& viewProj);
}
