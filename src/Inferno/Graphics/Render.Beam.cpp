#include "pch.h"

#include "Render.Beam.h"
#include "Game.h"
#include "Intersect.h"
#include "MaterialLibrary.h"
#include "Render.h"
#include "Render.Particles.h"

// Beam fractal code based on xash3d-fwgs gl_beams.c

namespace Inferno::Render {
    // gets a random point at a given radius, intersecting the level
    Vector3 GetRandomPoint(const Vector3& pos, SegID seg, float radius) {
        //Vector3 end;
        LevelHit hit;
        auto dir = RandomVector(1);
        dir.Normalize();

        RayQuery query{ .MaxDistance = radius, .Start = seg };
        if (Game::Intersect.RayLevel({ pos, dir }, query, hit))
            return hit.Point;
        else
            return pos + dir * radius;
    }

    //struct Beam {
    //    SegID Segment = SegID::None;
    //    List<ObjectVertex> Mesh{};
    //    float NextUpdate = 0;
    //    BeamInfo Info;
    //};

    void BeamInstance::InitRandomPoints(const Object* object) {
        if (HasFlag(Info.Flags, BeamFlag::RandomObjStart)) {
            if (object)
                ParentSubmodel = GetRandomPointOnObject(*object);
        }

        if (HasFlag(Info.Flags, BeamFlag::RandomObjEnd)) {
            if (object)
                EndSubmodel = GetRandomPointOnObject(*object);
        }
        else if (HasFlag(Info.Flags, BeamFlag::RandomEnd)) {
            End = GetRandomPoint(Start, Segment, Info.Radius.GetRandom());
        }
    }

    Vector2 SinCos(float x) {
        return { sin(x), cos(x) };
    }

    // Fractal noise generator, power of 2 wavelength
    void FractalNoise(span<float> noise) {
        if (noise.size() < 2) return;
        int div2 = (int)noise.size() >> 1;

        // noise is normalized to +/- scale
        noise[div2] = (noise.front() + noise.back()) * 0.5f + noise.size() * RandomN11() * 0.125f;

        if (div2 > 1) {
            FractalNoise(noise.subspan(0, div2 + 1));
            FractalNoise(noise.subspan(div2));
        }
    }

    void SineNoise(span<float> noise) {
        float freq = 0;
        float step = DirectX::XM_PI / (float)noise.size();

        for (auto& n : noise) {
            n = sin(freq);
            freq += step;
        }
    }

    Vector3 GetBeamPerpendicular(const Vector3 delta, const Camera& camera) {
        Vector3 dir;
        delta.Normalize(dir);
        auto perp = camera.GetForward().Cross(dir);
        perp.Normalize();
        return perp;
    }

    void BeamInstance::Draw(GraphicsContext& ctx) {
        if (StartDelay > 0) {
            StartDelay -= Game::FrameTime;
            return;
        }

        auto startObj = Game::GetObject(Parent);
        auto endObj = Game::GetObject(EndObj);

        if (!Parent.IsNull() && !HasFlag(Info.Flags, BeamFlag::RandomObjStart)) {
            if (startObj) {
                if (ParentSubmodel.id > -1) {
                    auto offset = GetSubmodelOffset(*startObj, ParentSubmodel);
                    Start = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
                }
                else {
                    Start = startObj->GetPosition(Game::LerpAmount);
                }
            }
        }

        float dissolveFade = 1;

        if (HasFlag(Info.Flags, BeamFlag::RandomObjStart) && startObj) {
            auto offset = GetSubmodelOffset(*startObj, ParentSubmodel);
            Start = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
            if (startObj->IsPhasing())
                dissolveFade = 1 - startObj->Effects.GetPhasePercent();
        }

        if (HasFlag(Info.Flags, BeamFlag::RandomObjEnd) && startObj) {
            // note that this effect uses the start object for begin and end
            auto offset = GetSubmodelOffset(*startObj, EndSubmodel);
            End = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
        }
        else if (endObj) {
            End = endObj->GetPosition(Game::LerpAmount);
        }

        if (Info.HasRandomEndpoints() && Game::Time > NextStrikeTime) {
            InitRandomPoints(startObj); // Relies on Start being updated
            NextStrikeTime = Game::Time + Info.StrikeTime;
        }

        Time += Game::FrameTime;
        auto& noise = Noise;
        auto delta = End - Start;
        auto length = delta.Length();
        if (length < 1) return; // don't draw really short beams

        // DrawSegs()
        auto scale = Info.Amplitude;

        int segments = (int)(length / (Width * 0.5 * 1.414)) + 1;
        segments = std::clamp(segments, 2, 64);
        auto div = 1.0f / (segments - 1);

        auto vLast = std::fmodf((float)Time * Info.ScrollSpeed, 1);
        if (HasFlag(Info.Flags, BeamFlag::SineNoise)) {
            if (segments < 16) {
                segments = 16;
                div = 1.0f / (segments - 1);
            }
            scale *= 100;
            length = segments * 0.1f;
        }
        else {
            scale *= length * 2;
        }

        noise.resize(segments);

        if (Info.Amplitude > 0 && Game::Time > NextUpdate) {
            if (HasFlag(Info.Flags, BeamFlag::SineNoise))
                SineNoise(noise);
            else
                FractalNoise(noise);

            NextUpdate = Game::Time + Info.Frequency;
            OffsetU = Random();
        }

        struct BeamSeg {
            Vector3 pos;
            float texcoord;
            Inferno::Color color;
        };

        BeamSeg curSeg{};
        auto vStep = length / 20 * div * Info.Scale;

        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();
        effect.Shader->SetConstants(cmdList, { Width / 2, 0.2f, TextureFilterMode::Smooth });
        effect.Shader->SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(cmdList, Heaps->States.AnisotropicWrap());

        auto& material = Render::Materials->Get(Info.Texture);
        effect.Shader->SetDiffuse(cmdList, material.Handle());
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(cmdList);

        Vector3 prevNormal;
        Vector3 prevUp;

        auto tangent = GetBeamNormal(Start, End, ctx.Camera);

        float fade = 1;
        if (Info.FadeInOutTime > 0) {
            // option to fade each strike instead of the overall emitter. Tends to look weird.
            //const float elapsed = Info.HasRandomEndpoints() ? Info.StrikeTime - (NextStrikeTime - Game::Time) : GetElapsedTime();
            //const float duration = Info.HasRandomEndpoints() ? Info.StrikeTime : Duration;
            const float elapsed = GetElapsedTime();
            const float duration = Duration;

            if (elapsed < Info.FadeInOutTime) {
                fade = 1 - (Info.FadeInOutTime - elapsed) / Info.FadeInOutTime;
            }
            else if (elapsed > duration - Info.FadeInOutTime) {
                fade = (duration - elapsed) / Info.FadeInOutTime;
            }
        }

        fade *= dissolveFade;

        for (int i = 0; i < segments; i++) {
            BeamSeg nextSeg{ .color = Info.Color };
            auto fraction = i * div;

            nextSeg.pos = Start + delta * fraction;

            if (Info.Amplitude != 0) {
                //auto factor = Runtime.Noise[noiseIndex >> 16] * Amplitude;
                auto factor = noise[i] * Info.Amplitude;

                if (HasFlag(Info.Flags, BeamFlag::SineNoise)) {
                    // rotate the noise along the perpendicluar axis a bit to keep the bolt from looking diagonal
                    auto c = SinCos(fraction * DirectX::XM_PI * length + (float)Time);
                    nextSeg.pos += ctx.Camera.Up * factor * c.x;
                    nextSeg.pos += ctx.Camera.GetRight() * factor * c.y;
                }
                else {
                    //nextSeg.pos += perp1 * factor;
                    nextSeg.pos += tangent * factor;
                }
            }

            nextSeg.texcoord = OffsetU + vLast;
            float brightness = HasFlag(Info.Flags, BeamFlag::FadeStart) ? 0.0f : 1.0f;
            if (HasFlag(Info.Flags, BeamFlag::FadeStart) && HasFlag(Info.Flags, BeamFlag::FadeEnd)) {
                if (fraction < 0.5f)
                    brightness = 2.0f * fraction;
                else
                    brightness = 2.0f * (1.0f - fraction);
            }
            else if (HasFlag(Info.Flags, BeamFlag::FadeStart)) {
                brightness = fraction;
            }
            else if (HasFlag(Info.Flags, BeamFlag::FadeEnd)) {
                brightness = 1 - fraction;
            }

            brightness = std::clamp(brightness, 0.0f, 1.0f);
            nextSeg.color *= brightness;

            if (i > 0) {
                Vector3 avgNormal;
                auto normal = GetBeamNormal(curSeg.pos, nextSeg.pos, ctx.Camera);

                if (i > 1) {
                    // Average with previous normal
                    avgNormal = (normal + prevNormal) * 0.5f;
                    avgNormal.Normalize();
                }
                else {
                    avgNormal = normal;
                }

                prevNormal = normal;

                // draw rectangular segment
                auto start = curSeg.pos;
                auto end = nextSeg.pos;
                auto up = avgNormal * Width * 0.5f;
                if (i == 1) prevUp = up;

                ObjectVertex v0{ start + prevUp, { 0, curSeg.texcoord }, curSeg.color * fade };
                ObjectVertex v1{ start - prevUp, { 1, curSeg.texcoord }, curSeg.color * fade };
                ObjectVertex v2{ end - up, { 1, nextSeg.texcoord }, nextSeg.color * fade };
                ObjectVertex v3{ end + up, { 0, nextSeg.texcoord }, nextSeg.color * fade };

                g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
                prevUp = up;
            }

            curSeg = nextSeg;
            vLast += vStep; // next segment tex V coord
        }

        g_SpriteBatch->End();
    }
}
