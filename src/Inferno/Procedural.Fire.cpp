#include "pch.h"
#include "Procedural.h"

// Descent 3 procedural fire effects
//
// Most of this code is credited to the efforts of SaladBadger

namespace Inferno {
    struct Particle {
        union {
            int8 Type; // Determine type based on flags in TextureInfo
            Outrage::WaterProceduralType WaterType;
            Outrage::FireProceduralType FireType;
        };

        int X, Y;
        int VelX, VelY;
        int Speed;
        ubyte Color;
        int8 Lifetime;
        // Next and Previous dynamic element
        int Prev = -1, Next = -1;

        void ApplyVelocity() {
            X += VelX;
            Y += VelY;
        }
    };

    class ProceduralFire : public ProceduralTextureBase {
        List<ubyte> _fireBuffer[2]{};
        List<Particle> _particles;
        int _dynamicProcElements = -1;
        List<int> _freeParticles;
        List<uint32> _palette;
        int64 _lcg = 1;
        int _numParticles = 0;

    public:
        ProceduralFire(const Outrage::TextureInfo& info, TexID baseTexture)
            : ProceduralTextureBase(info, baseTexture) {
            _fireBuffer[0].resize(_totalSize);
            _fireBuffer[1].resize(_totalSize);

            constexpr int MAX_PARTICLES = 8000;
            _freeParticles.resize(MAX_PARTICLES);
            _particles.resize(MAX_PARTICLES);

            for (int i = 0; i < MAX_PARTICLES; i++)
                _freeParticles[i] = i;

            _palette.resize(std::size(info.Procedural.Palette));

            for (int i = 0; i < _palette.size(); i++) {
                // Encode the BGRA5551 palette to RGBA8888
                auto srcColor = info.Procedural.Palette[i] % 32768U;
                _palette[i] = BGRA16ToRGB32(srcColor, 255);
            }
        }

    protected:
        void OnUpdate(double /*currentTime*/) override {
            using namespace Outrage;

            HeatDecay();

            for (auto& elem : Info.Procedural.Elements) {
                switch (elem.FireType) {
                    case FireProceduralType::LineLightning:
                        LineLightning(elem.X1, elem.Y1, elem.X2, elem.Y2, 254, elem);
                        break;
                    case FireProceduralType::SphereLightning:
                        SphereLightning(elem);
                        break;
                    case FireProceduralType::Straight:
                        // Straight type was never implemented
                        break;
                    case FireProceduralType::RisingEmbers:
                        RisingEmbers(elem);
                        break;
                    case FireProceduralType::RandomEmbers:
                        RandomEmbers(elem);
                        break;
                    case FireProceduralType::Spinners:
                        Spinners(elem);
                        break;
                    case FireProceduralType::Roamers:
                        Roamers(elem);
                        break;
                    case FireProceduralType::Fountain:
                        Fountain(elem);
                        break;
                    case FireProceduralType::Cone:
                        Cone(elem);
                        break;
                    case FireProceduralType::FallRight:
                        FallRight(elem);
                        break;
                    case FireProceduralType::FallLeft:
                        FallLeft(elem);
                        break;
                }
            }

            //Run particles from dynamic effects.
            auto particleNum = _dynamicProcElements;
            while (particleNum != -1) {
                auto& particle = _particles[particleNum];
                switch (particle.FireType) {
                    case FireProceduralType::RisingEmbers:
                    case FireProceduralType::RandomEmbers:
                        EmbersDynamic(particle);
                        break;
                    case FireProceduralType::Spinners:
                    case FireProceduralType::Fountain:
                    case FireProceduralType::Cone:
                        DefaultDynamic(particle);
                        break;
                    case FireProceduralType::Roamers:
                        RoamersDynamic(particle);
                        break;
                    case FireProceduralType::FallRight:
                        FallRightDynamic(particle);
                        break;
                    case FireProceduralType::FallLeft:
                        FallLeftDynamic(particle);
                        break;
                }

                particleNum = particle.Prev;
            }

            BlendFireBuffer();

            for (auto i = 0; i < _totalSize; i++)
                _pixels[i] = _palette[_fireBuffer[1 - _index][i]]; // Note the use of dest buffer
        }

        int ProceduralRand() {
            // Linear congruential generator
            _lcg = _lcg * 214013 + 2531011;
            return (_lcg >> 16) & 32767;
        }

        int GetDynamicElement() {
            if (_numParticles + 1 >= _particles.size()) return -1;

            auto i = _freeParticles[_numParticles++];
            _particles[i].Next = _particles[i].Prev = -1;
            return i;
        }

        void FreeDynamicElement(int num) {
            _numParticles--;
            assert(_numParticles >= 0);
            _freeParticles[_numParticles] = num;
            _particles[num].Type = 0;
        }

        void LinkElement(int num) {
            _particles[num].Prev = _dynamicProcElements;
            _dynamicProcElements = num;
            _particles[num].Next = -1;
            if (_particles[num].Prev != -1)
                _particles[_particles[num].Prev].Next = num;
        }

        void UnlinkElement(int num) {
            auto& particle = _particles[num];

            if (particle.Next == -1)
                _dynamicProcElements = particle.Prev;
            else
                _particles[particle.Next].Prev = particle.Prev;

            if (particle.Prev != -1)
                _particles[particle.Prev].Next = particle.Next;

            FreeDynamicElement(num);
        }

        void DrawLine(int x1, int y1, int x2, int y2, ubyte color) {
            int xDir = 1;
            int yDir = 1;
            int curX = x1;
            int curY = y1;

            if (x2 < x1) {
                curX = x2;
                x2 = x1;
                curY = y2;
                y2 = y1;
            }

            int xLen = x2 - curX;
            int yLen = y2 - curY;

            const auto mask = _resolution - 1;

            if (xLen < 0) {
                yDir = -1;
                xLen = -xLen;
            }
            if (yLen < 0) {
                xDir = -1;
                yLen = -yLen;
            }

            if (xLen < yLen) {
                curY = curY & mask;
                curX = curX & mask;
                int error = 0;
                int ptr = curY * _resolution;

                for (int i = 0; i < yLen; i++) {
                    error += xLen;
                    _fireBuffer[_index][ptr + curX] = color;
                    curY = (curY + xDir) & 127;
                    ptr = curY * _resolution;

                    if (yLen <= error) {
                        curX = (curX + yDir) % _resolution;
                        error -= yLen;
                    }
                }
            }
            else {
                curY = curY & mask;
                curX = curX & mask;
                int error = 0;
                int ptr = curY * _resolution;

                for (int i = 0; i < xLen; i++) {
                    error += yLen;
                    _fireBuffer[_index][ptr + (curX & mask)] = color;
                    curX = (curX & mask) + yDir;
                    if (xLen <= error) {
                        curY = (curY + xDir) & mask;
                        ptr = curY * _resolution;
                        error -= xLen;
                    }
                }
            }
        }

        void LineLightning(int x1, int y1, int x2, int y2, ubyte color, const Element& elem) {
            auto diffX = float(x2 - x1);
            auto diffY = float(y2 - y1);
            auto boltLength = std::sqrtf(diffX * diffX + diffY * diffY);

            if (boltLength > 1) {
                float posX{}, posY{};
                int numSegments = int(boltLength / 8);

                auto lastX = (float)x1;
                auto lastY = (float)y1;
                auto tempX = (float)x1;
                auto tempY = (float)y1;

                for (auto i = 0; i < numSegments; i++) {
                    tempX += diffX / boltLength * 8;
                    tempY += diffY / boltLength * 8;
                    posX = tempX;
                    posY = tempY;

                    if (i != numSegments - 1) {
                        auto rnd1 = ProceduralRand() % 200;
                        auto rnd2 = ProceduralRand() % 200;
                        posX = tempX + (elem.Speed + 1) * (rnd1 - 100) * .05555555f * (diffX / boltLength);
                        posY = tempY + (elem.Speed + 1) * (rnd2 - 100) * .05555555f * (diffY / boltLength);
                    }

                    DrawLine(int(lastX), int(lastY), int(posX), int(posY), color);
                    lastX = posX;
                    lastY = posY;
                }
            }
        }

        void SphereLightning(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto size = elem.Size * 0.00392156862745098 * 128;
            auto ang = (float)ProceduralRand() / 32768 * DirectX::XM_2PI;

            auto x2 = int(std::cos(ang) * (size / 2)) + elem.X1;
            auto y2 = int(std::sin(ang) * (size / 2)) + elem.Y1;

            LineLightning(elem.X1, elem.Y1, x2, y2, 254, elem);
        }


        bool ParticleIsAlive(Particle& elem) {
            if (--elem.Lifetime <= 0) {
                UnlinkElement(int(&elem - &_particles[0]));
                return false;
            }

            if (--elem.Color <= 0) {
                UnlinkElement(int(&elem - &_particles[0]));
                return false;
            }

            return true;
        }

        void UpdateBufferColorDynamic(const Particle& elem) {
            //ProcDestData[((elem->x1 >> 16) & RESMASK) + ((elem->y1 >> 16) & RESMASK) * RESOLUTION] = elem->color;
            auto x = (elem.X >> 16) & _resMask;
            auto y = ((elem.Y >> 16) & _resMask) * _resolution;
            _fireBuffer[_index][y + x] = elem.Color;
        }

        //Emits particles that move randomly.
        void RisingEmbers(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = ProceduralRand() % 7;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);
                    auto& particle = _particles[num];
                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536;
                    particle.Y = elem.Y1 * 65536;
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.Lifetime = ProceduralRand() % 10 + 15;
                }
            }
        }

        // Emits particles that move randomly but less of them.
        void RandomEmbers(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = (ProceduralRand() % 4) + 1;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);
                    auto& particle = _particles[num];
                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536;
                    particle.Y = elem.Y1 * 65536;
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.Lifetime = ProceduralRand() % 10 + 15;
                }
            }
        }

        void EmbersDynamic(Particle& elem) {
            UpdateBufferColorDynamic(elem);
            if (!ParticleIsAlive(elem)) return;

            auto speed = elem.Speed * 0.00392156862745098;
            speed = int(speed + speed + 1);

            elem.VelX = int((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.VelY = int((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.ApplyVelocity();
        }

        // Emits an arc of particles. It doesn't seem like it's supposed to be a full circle?
        void Spinners(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto num = GetDynamicElement();
            if (num != -1) {
                auto elemNum = int(&elem - &Info.Procedural.Elements[0]);
                int iVar3 = _frameCount + elemNum * 60;
                int iVar2 = int(elem.Speed / 255.0f * 5.0f + 1.0f);
                int size = elem.Size * -65536;

                LinkElement(num);
                auto& particle = _particles[num];
                particle.Type = elem.Type;

                int fixAng = ((iVar3 * iVar2) & 63) << 10;
                float ang = float(fixAng) / 65536.0f * 6.2831854f;

                particle.VelX = int(std::cos(ang) * 65536);
                particle.VelY = int(std::sin(ang) * 65536);

                particle.X = int((elem.X1 - size * particle.VelX) * 65536);
                particle.Y = int((elem.Y1 - size * particle.VelY) * 65536);
                particle.Color = 254;
                particle.Speed = elem.Speed;
                particle.Lifetime = ProceduralRand() % 10 + 15;
            }
        }

        void DefaultDynamic(Particle& elem) {
            UpdateBufferColorDynamic(elem);
            if (!ParticleIsAlive(elem)) return;
            elem.ApplyVelocity();
        }

        // Emits random particles, but the source will roam around. 
        void Roamers(Element& elem) {
            elem.X1 += (int8)Rand(-2, 2);
            elem.Y1 += (int8)Rand(-2, 2);
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = (ProceduralRand() % 4) + 1;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);
                    auto& particle = _particles[num];
                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536;
                    particle.Y = elem.Y1 * 65536;
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.Lifetime = ProceduralRand() % 10 + 15;
                }
            }
        }

        void RoamersDynamic(Particle& elem) {
            UpdateBufferColorDynamic(elem);
            if (!ParticleIsAlive(elem)) return;

            auto speed = elem.Speed * 0.00392156862745098;
            speed = int(speed + speed + 1);

            elem.VelX = int((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.VelY = int((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.ApplyVelocity();
        }

        // Emits a fountain of particles, with some drifting in other directions
        void Fountain(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = (ProceduralRand() % 4) + 1;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);

                    auto& particle = _particles[num];
                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536;
                    particle.Y = elem.Y1 * 65536;
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.VelX = int(((ProceduralRand() % 100) - 50) * 0.005 * 65536);
                    if (ProceduralRand() % 10 == 0) {
                        particle.VelY = int(-(ProceduralRand() % 100) * 0.003333333333333334 * 65536);
                        particle.Lifetime = (ProceduralRand() % 6) + 3;
                    }
                    else {
                        particle.VelY = int((ProceduralRand() % 100) * 0.02 * 65536);
                        particle.Lifetime = (ProceduralRand() % 10) + 15;
                    }
                }
            }
        }

        // More straightforward cone
        void Cone(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = (ProceduralRand() % 4) + 1;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);
                    auto& particle = _particles[num];
                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536;
                    particle.Y = elem.Y1 * 65536;
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.VelX = int(((ProceduralRand() % 100) - 50) * 0.0125 * 65536);
                    if (ProceduralRand() % 10 == 0) {
                        particle.VelY = int(-(ProceduralRand() % 100) * 0.003333333333333334 * 65536);
                        particle.Lifetime = (ProceduralRand() % 6) + 3;
                    }
                    else {
                        particle.VelY = 65536;
                        particle.Lifetime = (ProceduralRand() % 10) + 15;
                    }
                }
            }
        }

        // Emits a stream rightwards that falls with gravity. 
        void FallRight(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = (ProceduralRand() & 1) + 1;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);
                    auto& particle = _particles[num];

                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536 + Rand(-2, 2);
                    particle.Y = elem.Y1 * 65536 + Rand(-2, 2);
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.VelX = 65536;
                    particle.VelY = int(-(ProceduralRand() % 100) * 0.003333333333333334 * 65536);
                    particle.Lifetime = (ProceduralRand() % 15) + 25;
                }
            }
        }

        void FallRightDynamic(Particle& elem) {
            UpdateBufferColorDynamic(elem);
            if (!ParticleIsAlive(elem)) return;

            if (elem.VelX > 0)
                elem.VelX += int(Rand(0, 100) * .0005 * -65536);
            if (elem.VelY < 131072)
                elem.VelY += int(Rand(0, 100) * .001 * 65536);

            elem.ApplyVelocity();
        }

        // Emits a stream leftwards that falls with gravity.
        void FallLeft(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto numParticles = (ProceduralRand() % 4) + 1;

            for (auto i = 0; i < numParticles; i++) {
                auto num = GetDynamicElement();
                if (num != -1) {
                    LinkElement(num);
                    auto& particle = _particles[num];
                    particle.Type = elem.Type;
                    particle.X = elem.X1 * 65536 + Rand(-2, 2);
                    particle.Y = elem.Y1 * 65536 + Rand(-2, 2);
                    particle.Color = 254;
                    particle.Speed = elem.Speed;
                    particle.VelX = -65536;
                    particle.VelY = int(-(ProceduralRand() % 100) * 0.003333333333333334 * 65536);
                    particle.Lifetime = (ProceduralRand() % 15) + 25;
                }
            }
        }

        void FallLeftDynamic(Particle& elem) {
            UpdateBufferColorDynamic(elem);
            if (!ParticleIsAlive(elem)) return;

            if (elem.VelX > 0)
                elem.VelX += int(Rand(0, 100) * .0005 * -65536);
            if (elem.VelY < 131072)
                elem.VelY += int(Rand(0, 100) * .001 * 65536);

            elem.ApplyVelocity();
        }

        // Decays the contents of ProceduralBuffer based on the current "heat" level. Higher heat causes slower decay. 
        void HeatDecay() {
            auto decay = (int8)(int((255 - Info.Procedural.Heat) / 8.0f) + 1);

            for (auto& pixel : _fireBuffer[_index]) {
                if (pixel - decay < 0)
                    pixel = 0;
                else
                    pixel -= decay;
            }
        }

        // Blends the contents of the active fire buffer and writes to the output buffer
        void BlendFireBuffer() {
            auto& src = _fireBuffer[_index];
            auto& dest = _fireBuffer[1 - _index];

            for (auto y = 0; y < _resolution; y++) {
                auto yptr = y * _resolution;
                auto up = yptr + _resolution;
                if (y == _resolution - 1)
                    up = 0; // wrap top edge

                auto down = yptr - _resolution;
                if (y == 0)
                    down = _totalSize - _resolution;

                for (auto x = 0; x < _resolution; x++) {
                    auto ptr = yptr + x;
                    auto right = ptr + 1;
                    auto left = ptr - 1;

                    if (x == _resolution - 1)
                        right = yptr;
                    if (x == 0)
                        left = yptr + _resolution - 1;

                    // legacy 4-tap sampling that creates visible triangles
                    //auto v = buffer[ptr] + buffer[up] + buffer[right] + buffer[left];
                    //writeBuffer[ptr] = ubyte(v >> 2); // divide by 4

                    // 5 tap weighted sampling. Anti-alises lines.
                    auto v = src[ptr] + src[up] * 0.5f + src[down] * 0.5f + src[right] * 0.5f + src[left] * 0.5f;
                    dest[ptr] = uint8(v / 3.0f);

                    up += 1;
                    down += 1;
                }
            }
        }
    };

    Ptr<ProceduralTextureBase> CreateProceduralFire(Outrage::TextureInfo& texture, TexID dest) {
        return MakePtr<ProceduralFire>(texture, dest);
    }
}
