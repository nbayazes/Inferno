#pragma once
#include "Game.Object.h"
#include "Graphics/CameraContext.h"
#include "Types.h"

namespace Inferno::Render {
    struct RenderCommand;

    enum class RenderQueueType {
        None,
        Opaque,
        Transparent
    };

    struct EffectBase {
        SegID Segment = SegID::None;
        Vector3 Position, PrevPosition;
        float Duration = 0; // How long the effect lasts
        double CreationTime = 0; // Game time when created
        RenderQueueType Queue = RenderQueueType::Transparent; // Which queue to render to
        float FadeTime = 0; // Fade time at the end of the effect's life
        float StartDelay = 0; // How long to wait in seconds before starting the effect
        ObjRef Parent;
        Inferno::SubmodelRef ParentSubmodel;
        bool FadeOnParentDeath = false; // Detaches from the parent when it dies and uses FadeTime
        RenderFlag Flags = RenderFlag::None;

        // Called once per frame
        void Update(float dt, EffectID id);

        // Called per game tick
        void FixedUpdate(float dt, EffectID);

        bool UpdatePositionFromParent();

        float GetRemainingTime() const;
        float GetElapsedTime() const;

        virtual void OnUpdate(float /*dt*/, EffectID) {}
        virtual void OnFixedUpdate(float /*dt*/, EffectID) {}

        virtual void Draw(GraphicsContext&) {}

        virtual void DepthPrepass(GraphicsContext&) {
            ASSERT(Queue == RenderQueueType::Transparent); // must provide a depth prepass if not transparent
        }

        virtual void DrawFog(GraphicsContext&) {}

        virtual void OnExpire() {}
        virtual void OnInit() {}

        bool ShouldDraw() const;

        EffectBase() = default;
        virtual ~EffectBase() = default;
        EffectBase(const EffectBase&) = default;
        EffectBase(EffectBase&&) = default;
        EffectBase& operator=(const EffectBase&) = default;
        EffectBase& operator=(EffectBase&&) = default;
    };

    struct DecalInstance final : EffectBase {
        Decal Info;
        Vector3 Normal, Tangent, Bitangent;
        SideID Side;
    };

    inline List<Ptr<EffectBase>> VisualEffects;

    void DrawDecals(GraphicsContext& ctx, float dt);

    span<DecalInstance> GetAdditiveDecals();
    span<DecalInstance> GetDecals();

    // Gets a visual effect
    EffectBase* GetEffect(EffectID effect);
    EffectID AddEffect(Ptr<EffectBase> e);

    void ResetEffects();
    void DetachEffects(EffectBase& effect);
    void UpdateEffect(float dt, EffectID id);

    // Either call this or individual effects using UpdateEffect()
    void UpdateAllEffects(float dt);
    void EndUpdateEffects();
}
