#pragma once
#include "Game.Object.h"
#include "Graphics/CommandContext.h"

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
        float Elapsed = 0; // How long the effect has been alive for
        RenderQueueType Queue = RenderQueueType::Transparent; // Which queue to render to
        float FadeTime = 0; // Fade time at the end of the effect's life
        float StartDelay = 0; // How long to wait in seconds before starting the effect
        ObjRef Parent;
        Inferno::SubmodelRef ParentSubmodel;
        bool FadeOnParentDeath = false; // Detaches from the parent when it dies and uses FadeTime

        // Called once per frame
        void Update(float dt, EffectID id);

        // Called per game tick
        void FixedUpdate(float dt, EffectID);

        bool UpdatePositionFromParent();

        virtual void OnUpdate(float /*dt*/, EffectID) {}
        virtual void OnFixedUpdate(float /*dt*/, EffectID) {}

        virtual void Draw(Graphics::GraphicsContext&) {}

        virtual void DepthPrepass(Graphics::GraphicsContext&) {
            ASSERT(Queue == RenderQueueType::Transparent); // must provide a depth prepass if not transparent
        }

        virtual void OnExpire() {}
        virtual void OnInit() {}

        EffectBase() = default;
        virtual ~EffectBase() = default;
        EffectBase(const EffectBase&) = default;
        EffectBase(EffectBase&&) = default;
        EffectBase& operator=(const EffectBase&) = default;
        EffectBase& operator=(EffectBase&&) = default;
    };

}
