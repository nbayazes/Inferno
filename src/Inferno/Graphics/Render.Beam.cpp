#include "pch.h"

#include "Game.h"
#include "Intersect.h"
#include "Render.h"
#include "Render.Particles.h"
#include "Render.Beam.h"
#include "Game.Segment.h"
#include "MaterialLibrary.h"

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

    struct Beam {
        SegID Segment = SegID::None;
        List<ObjectVertex> Mesh{};
        float NextUpdate = 0;
        BeamInfo Info;
    };

    void InitRandomBeamPoints(BeamInfo& beam, const Object* object) {
        if (HasFlag(beam.Flags, BeamFlag::RandomObjStart)) {
            if (object)
                beam.ParentSubmodel = GetRandomPointOnObject(*object);
        }

        if (HasFlag(beam.Flags, BeamFlag::RandomObjEnd)) {
            if (object)
                beam.EndSubmodel = GetRandomPointOnObject(*object);
        }
        else if (HasFlag(beam.Flags, BeamFlag::RandomEnd)) {
            beam.End = GetRandomPoint(beam.Start, beam.Segment, beam.Radius.GetRandom());
        }
    }

    void AddBeam(BeamInfo beam, float life, const Vector3& start, const Vector3& end) {
        beam.Start = start;
        beam.End = end;
        beam.Duration = life;
        AddBeam(beam);
    }

    void AddBeam(BeamInfo beam, float life, ObjRef start, const Vector3& end, int startGun) {
        auto obj = Game::Level.TryGetObject(start);

        if (obj) {
            beam.Parent = start;
            if (startGun >= 0) {
                beam.Start = GetGunpointOffset(*obj, (uint8)startGun);
                beam.ParentSubmodel = GetGunpointSubmodelOffset(*obj, (uint8)startGun);
            }
            else {
                beam.Start = obj->Position;
            }
            beam.Segment = obj->Segment;
            beam.End = end;
            beam.Duration = life;
            AddBeam(beam);
        }
    }

    void AddBeam(BeamInfo beam, float duration, ObjRef start, ObjRef end, int startGun) {
        auto obj = Game::Level.TryGetObject(start);

        if (obj) {
            beam.Parent = start;
            if (startGun >= 0) {
                beam.Start = GetGunpointOffset(*obj, (uint8)startGun);
                beam.ParentSubmodel = GetGunpointSubmodelOffset(*obj, (uint8)startGun);
            }
            else {
                beam.Start = obj->Position;
            }
            beam.Segment = obj->Segment;
            beam.EndObj = end;
            beam.Duration = duration;
            AddBeam(beam);
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

    Vector3 GetBeamPerpendicular(const Vector3 delta) {
        Vector3 dir;
        delta.Normalize(dir);
        auto perp = Camera.GetForward().Cross(dir);
        perp.Normalize();
        return perp;
    }

    void BeamInfo::Draw(Graphics::GraphicsContext& ctx) {
        if (StartDelay > 0) {
            StartDelay -= Game::FrameTime;
            return;
        }

        auto startObj = Game::Level.TryGetObject(Parent);
        auto endObj = Game::Level.TryGetObject(EndObj);

        if (!Parent.IsNull() && !HasFlag(Flags, BeamFlag::RandomObjStart)) {
            if (startObj) {
                if (ParentSubmodel.ID > -1) {
                    auto offset = GetSubmodelOffset(*startObj, ParentSubmodel);
                    Start = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
                }
                else {
                    Start = startObj->GetPosition(Game::LerpAmount);
                }
            }
        }

        float dissolveFade = 1;

        if (HasFlag(Flags, BeamFlag::RandomObjStart) && startObj) {
            auto offset = GetSubmodelOffset(*startObj, ParentSubmodel);
            Start = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
            if (startObj->IsPhasing())
                dissolveFade = 1 - startObj->Effects.GetPhasePercent();
        }

        if (HasFlag(Flags, BeamFlag::RandomObjEnd) && startObj) {
            // note that this effect uses the start object for begin and end
            auto offset = GetSubmodelOffset(*startObj, EndSubmodel);
            End = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
        }
        else if (endObj) {
            End = endObj->GetPosition(Game::LerpAmount);
        }

        if (HasRandomEndpoints() && Game::Time > Runtime.NextStrikeTime) {
            InitRandomBeamPoints(*this, startObj); // Relies on Start being updated
            Runtime.NextStrikeTime = Game::Time + StrikeTime;
        }

        Time += Game::FrameTime;
        auto& noise = Runtime.Noise;
        auto delta = End - Start;
        auto length = delta.Length();
        if (length < 1) return; // don't draw really short beams

        // DrawSegs()
        auto scale = Amplitude;

        int segments = (int)(length / (Runtime.Width * 0.5 * 1.414)) + 1;
        segments = std::clamp(segments, 2, 64);
        auto div = 1.0f / (segments - 1);

        auto vLast = std::fmodf(Time * ScrollSpeed, 1);
        if (HasFlag(Flags, BeamFlag::SineNoise)) {
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

        if (Amplitude > 0 && Game::Time > Runtime.NextUpdate) {
            if (HasFlag(Flags, BeamFlag::SineNoise))
                SineNoise(noise);
            else
                FractalNoise(noise);

            Runtime.NextUpdate = Game::Time + Frequency;
            Runtime.OffsetU = Random();
        }

        struct BeamSeg {
            Vector3 pos;
            float texcoord;
            Inferno::Color color;
        };

        BeamSeg curSeg{};
        auto vStep = length / 20 * div * Scale;

        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();
        effect.Shader->SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(cmdList, Render::GetWrappedTextureSampler());

        auto& material = Render::Materials->Get(Texture);
        effect.Shader->SetDiffuse(cmdList, material.Handle());
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(cmdList);

        Vector3 prevNormal;
        Vector3 prevUp;

        auto tangent = GetBeamNormal(Start, End);

        float fade = 1;
        if (FadeInOutTime > 0) {
            auto elapsed = GetElapsedTime();
            if (elapsed < FadeInOutTime) {
                fade = 1 - (FadeInOutTime - elapsed) / FadeInOutTime;
            }
            else if (elapsed > Duration - FadeInOutTime) {
                fade = (Duration - elapsed) / FadeInOutTime;
            }
        }

        fade *= dissolveFade;

        for (int i = 0; i < segments; i++) {
            BeamSeg nextSeg{ .color = Color };
            auto fraction = i * div;

            nextSeg.pos = Start + delta * fraction;

            if (Amplitude != 0) {
                //auto factor = Runtime.Noise[noiseIndex >> 16] * Amplitude;
                auto factor = noise[i] * Amplitude;

                if (HasFlag(Flags, BeamFlag::SineNoise)) {
                    // rotate the noise along the perpendicluar axis a bit to keep the bolt from looking diagonal
                    auto c = SinCos(fraction * DirectX::XM_PI * length + Time);
                    nextSeg.pos += Render::Camera.Up * factor * c.x;
                    nextSeg.pos += Render::Camera.GetRight() * factor * c.y;
                }
                else {
                    //nextSeg.pos += perp1 * factor;
                    nextSeg.pos += tangent * factor;
                }
            }

            nextSeg.texcoord = Runtime.OffsetU + vLast;
            float brightness = HasFlag(Flags, BeamFlag::FadeStart) ? 0.0f : 1.0f;
            if (HasFlag(Flags, BeamFlag::FadeStart) && HasFlag(Flags, BeamFlag::FadeEnd)) {
                if (fraction < 0.5f)
                    brightness = 2.0f * fraction;
                else
                    brightness = 2.0f * (1.0f - fraction);
            }
            else if (HasFlag(Flags, BeamFlag::FadeStart)) {
                brightness = fraction;
            }
            else if (HasFlag(Flags, BeamFlag::FadeEnd)) {
                brightness = 1 - fraction;
            }

            brightness = std::clamp(brightness, 0.0f, 1.0f);
            nextSeg.color *= brightness;

            if (i > 0) {
                Vector3 avgNormal;
                auto normal = GetBeamNormal(curSeg.pos, nextSeg.pos);

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
                auto up = avgNormal * Runtime.Width * 0.5f;
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
