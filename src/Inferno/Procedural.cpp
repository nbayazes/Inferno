#include "pch.h"
#include "Procedural.h"
#include "Graphics/GpuResources.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"

// Descent 3 procedural texture generation
//
// Most of this code is credited to the efforts of ISB

namespace Inferno {
    ubyte WaterProcTableLo[16384];
    ushort WaterProcTableHi[16384];

    void InitWaterTables() {
        for (int i = 0; i < 64; i++) {
            float intensity1 = i * 0.01587302f;
            float intensity2 = intensity1 * 2;
            if (intensity2 > 1.0)
                intensity2 = 1.0;

            intensity1 = (intensity1 - .5f) * 2;
            if (intensity1 < 0)
                intensity1 = 0;

            for (int j = 0; j < 32; j++) {
                auto channel = ushort(j * intensity2 + intensity1 * 31.0f);
                if (channel > 31)
                    channel = 31;

                for (int k = 0; k < 4; k++) {
                    WaterProcTableHi[((i * 64) + j) * 4 + k] = ((channel | 65504u) << 10u);
                }
            }

            for (int j = 0; j < 32; j++) {
                auto channel = ubyte(j * intensity2 + intensity1 * 31.0f);
                if (channel > 31)
                    channel = 31;

                for (int k = 0; k < 8; k++) {
                    WaterProcTableLo[(i * 256) + j + (32 * k)] = channel;
                }
            }

            for (int j = 0; j < 8; j++) {
                auto channel = ushort(j * intensity2 + intensity1 * 7.0f);
                if (channel > 7)
                    channel = 7;

                for (int k = 0; k < 32; k++) {
                    WaterProcTableLo[(i * 256) + (j * 32) + k] |= channel << 5;
                }
            }

            for (int j = 0; j < 4; j++) {
                auto channel = ushort((j * 8) * intensity2 + intensity1 * 24.0f);
                if (channel > 24)
                    channel = 24;

                for (int k = 0; k < 32; k++) {
                    WaterProcTableHi[(i * 256) + j + (k * 4)] |= channel << 5;
                }
            }
        }
    }

    int Floor(double value) {
        return (int)std::floor(value);
    }

    int Floor(float value) {
        return (int)std::floor(value);
    }

    constexpr int16 RGB32ToBGR16(int r, int g, int b) {
        return int16((b >> 3) + ((g >> 3) << 5) + ((r >> 3) << 10));
    }

    // Converts BGRA5551 to RGBA8888
    constexpr int BGRA16ToRGB32(uint src) {
        auto r = (uint8)(((src >> 10) & 31) * 255.0f / 31);
        auto g = (uint8)(((src >> 5) & 31) * 255.0f / 31);
        auto b = (uint8)((src & 31) * 255.0f / 31);
        auto a = src >> 15 ? 0 : 255;
        return r | g << 8 | b << 16 | a << 24;
    }

    class ProceduralTexture {
        int64 _lcg = 1;
        int _numParticles = 0;

        List<int> _freeParticles;
        List<ubyte> _fireBuffer[2]{};
        List<int16> _waterBuffer[2]{};

        List<uint32> _pixels;
        List<uint32> _palette;

        wstring _name;

        struct Particle {
            union {
                int8 Type; // Determine type based on flags in TextureInfo
                Outrage::WaterProceduralType WaterType;
                Outrage::FireProceduralType FireType;
            };

            int X, Y;
            int VelX, VelY;
            int8 Speed;
            ubyte Color;
            int8 Lifetime;
            int Num;
            // Next and Previous dynamic element
            int Prev = -1, Next = -1;

            void ApplyVelocity() {
                X += VelX;
                Y += VelY;
            }
        };

        List<Particle> _particles;
        int _dynamicProcElements = -1;
        Outrage::TextureInfo _info;

        using Element = Outrage::ProceduralInfo::Element;
        double NextTime = 0;
        int _index = 0;
        int FrameCount = 0;
        int TotalSize; // Resolution * Resolution

        int _resMask;
        TexID BaseTexture;
        EClipID EClip = EClipID::None;

    public:
        int Resolution; // width-height

        std::atomic<bool> PendingCopy = false;

        Outrage::TextureInfo& GetTextureInfo() { return _info; }
        Texture2D Texture;
        DescriptorHandle Handle;

        ProceduralTexture(const Outrage::TextureInfo& info, TexID baseTexture = TexID::None) : _info(info) {
            _name = Convert::ToWideString(_info.Name);
            Resolution = info.GetSize();
            _resMask = Resolution - 1;
            TotalSize = Resolution * Resolution;
            BaseTexture = baseTexture;
            EClip = Resources::GetEffectClipID(baseTexture);

            if (HasFlag(info.Flags, Outrage::TextureFlag::WaterProcedural)) {
                _waterBuffer[0].resize(TotalSize);
                _waterBuffer[1].resize(TotalSize);
            }
            else {
                _fireBuffer[0].resize(TotalSize);
                _fireBuffer[1].resize(TotalSize);

                constexpr int MAX_PARTICLES = 8000;
                _freeParticles.resize(MAX_PARTICLES);
                _particles.resize(MAX_PARTICLES);

                for (int i = 0; i < MAX_PARTICLES; i++) {
                    _freeParticles[i] = i;
                    _particles[i].Num = i;
                }

                _palette.resize(std::size(info.Procedural.Palette));

                for (int i = 0; i < _palette.size(); i++) {
                    // Encode the BGRA5551 palette to RGBA8888
                    // BGRA5551, max value of 31 -> 255 
                    auto srcColor = info.Procedural.Palette[i] % 32768U;
                    _palette[i] = BGRA16ToRGB32(srcColor);
                }
            }

            _pixels.resize(TotalSize);

            Texture.SetDesc(Resolution, Resolution);
            Texture.CreateOnDefaultHeap(Convert::ToWideString(_info.Name));
            Handle = Render::Heaps->Procedurals.GetHandle(0);
            //Texture.CreateShaderResourceView(Handle.GetCpuHandle());
            Render::Device->CreateShaderResourceView(Texture.Get(), Texture.GetSrvDesc(), Handle.GetCpuHandle());
        }

        //void SetBaseTexture(span<ubyte> data, int width, int height) {
        //    _baseTexture.resize(TotalSize);
        //    assert(width == height); // only works with square textures
        //    auto scale = Resolution / width;

        //    if(width != Resolution || height != Resolution) {
        //        // Resize source to match procedural
        //        for (int y = 0; y < Resolution; y++) {
        //            for (int x = 0; x < Resolution; x++) {
        //                // Nearest
        //                _baseTexture[y * Resolution + x] = data[(y / scale) * Resolution + x / scale];
        //            }
        //        }
        //    }
        //}

        bool CopyToTexture(ID3D12GraphicsCommandList* cmdList) {
            if (PendingCopy) {
                Texture.UploadData(cmdList, _pixels.data());
                PendingCopy = false;
                return true;
            }

            return false;
        }

        void Update() {
            if (NextTime > Render::ElapsedTime) return;

            if (_info.IsWaterProcedural())
                EvalulateWaterProcedural();
            else
                EvalulateFireProcedural();

            FrameCount++;
            NextTime = Render::ElapsedTime + _info.Procedural.EvalTime;
            NextTime = Render::ElapsedTime + 1 / 30.0f;
            _index = 1 - _index; // swap buffers
        }

    private:
        void EvalulateFireProcedural() {
            using namespace Outrage;

            HeatDecay();

            for (auto& elem : _info.Procedural.Elements) {
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

            if (!PendingCopy) {
                for (auto i = 0; i < TotalSize; i++)
                    _pixels[i] = _palette[_fireBuffer[1 - _index][i]]; // Note the use of dest buffer

                PendingCopy = true; // New data to copy to the GPU
            }
        }

        void EvalulateWaterProcedural() {
            //proc_struct* proc = GameTextures[texnum].procedural;
            //if (proc->memory_type != PROC_MEMORY_TYPE_WATER)
            //    AllocateMemoryForWaterProcedural(texnum);
            for (auto& elem : _info.Procedural.Elements) {
                switch (elem.WaterType) {
                    case Outrage::WaterProceduralType::HeightBlob:
                        AddWaterHeightBlob(elem);
                        break;
                    case Outrage::WaterProceduralType::SineBlob:
                        AddWaterSineBlob(elem);
                        break;
                    case Outrage::WaterProceduralType::RandomRaindrops:
                        AddWaterRaindrops(elem);
                        break;
                    case Outrage::WaterProceduralType::RandomBlobdrops:
                        //AddWaterBlobdrops(elem);
                        break;
                }
            }

            //if (EasterEgg) {
            //    if (Easter_egg_handle != -1) {
            //        ushort* srcData = bm_data(Easter_egg_handle, 0);
            //        short* heightData = (short*)proc->proc1;
            //        int width = bm_w(Easter_egg_handle, 0);
            //        int height = bm_w(Easter_egg_handle, 0); //oops, they get bm_w twice..

            //        if (width <= RESOLUTION && height <= RESOLUTION && width > 0 && height > 0) {
            //            int offset = (RESOLUTION / 2 - height / 2) * RESOLUTION + (RESOLUTION / 2 - width / 2);
            //            for (int y = 0; y < height; y++) {
            //                for (int x = 0; x < width; x++) {
            //                    if (*srcData & 128)
            //                        heightData[offset + x] += 200;

            //                    srcData++;
            //                }
            //                offset += RESOLUTION;
            //            }
            //        }
            //    }
            //    EasterEgg = false;
            //}

            UpdateWater();

            if (_info.Procedural.Light > 0)
                DrawWaterWithLight(_info.Procedural.Light - 1);
            else
                DrawWaterNoLight();
            //DrawWaterNoLight();
            PendingCopy = true;
        }


        int Rand(int min, int max) const {
            assert(max > min);
            auto range = max - min + 1;
            auto value = Floor(Random() * range);
            assert(value + min <= max);
            return value + min;
        }

        int ProceduralRand() {
            // Linear congruential generator
            _lcg = _lcg * 214013 + 2531011;
            return (_lcg >> 16) & 32767;
        }

        int GetDynamicElement() {
            if (_numParticles + 1 >= _particles.size()) return -1;
            //assert(_numParticles < 2000);

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

            const auto mask = Resolution - 1;

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
                int ptr = curY * Resolution;

                for (int i = 0; i < yLen; i++) {
                    error += xLen;
                    _fireBuffer[_index][ptr + curX] = color;
                    curY = (curY + xDir) & 127;
                    ptr = curY * Resolution;

                    if (yLen <= error) {
                        curX = (curX + yDir) % Resolution;
                        error -= yLen;
                    }
                }
            }
            else {
                curY = curY & mask;
                curX = curX & mask;
                int error = 0;
                int ptr = curY * Resolution;

                for (int i = 0; i < xLen; i++) {
                    error += yLen;
                    _fireBuffer[_index][ptr + (curX & mask)] = color;
                    curX = (curX & mask) + yDir;
                    if (xLen <= error) {
                        curY = (curY + xDir) & mask;
                        ptr = curY * Resolution;
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
                int numSegments = Floor(boltLength / 8);

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
            auto ang = (double)ProceduralRand() / 32768 * 6.2831854;

            auto x2 = Floor(std::cos(ang) * (size / 2)) + elem.X1;
            auto y2 = Floor(std::sin(ang) * (size / 2)) + elem.Y1;

            LineLightning(elem.X1, elem.Y1, x2, y2, 254, elem);
        }


        bool ParticleIsAlive(Particle& elem) {
            if (--elem.Lifetime <= 0) {
                UnlinkElement(elem.Num);
                return false;
            }

            if (--elem.Color <= 0) {
                UnlinkElement(elem.Num);
                return false;
            }

            return true;
        }

        void UpdateBufferColorDynamic(const Particle& elem) {
            //ProcDestData[((elem->x1 >> 16) & RESMASK) + ((elem->y1 >> 16) & RESMASK) * RESOLUTION] = elem->color;
            auto x = (elem.X >> 16) & _resMask;
            auto y = ((elem.Y >> 16) & _resMask) * Resolution;
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
            speed = Floor(speed + speed + 1);

            elem.VelX = Floor((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.VelY = Floor((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.ApplyVelocity();
        }

        // Emits an arc of particles. It doesn't seem like it's supposed to be a full circle?
        void Spinners(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto num = GetDynamicElement();
            if (num != -1) {
                //auto iVar3 = FrameCount + elem.Num * 60;

                auto iVar3 = (uint)(FrameCount + num * 60);
                auto iVar2 = (uint)Floor(elem.Speed * 0.00392156862745098 * 5.0 + 1.0);

                LinkElement(num);
                auto& particle = _particles[num];
                particle.Type = elem.Type;

                auto ang0 = (iVar3 * iVar2 & 2147483711U) << 10;
                auto ang1 = (iVar3 * iVar2 & 63U) << 10;
                auto ang = ((iVar3 * iVar2 & 63U) << 10) / 65536.0 * 6.2831854;
                auto size = elem.Size * -65536.0;
                particle.VelX = Floor(std::cos(ang) * 65536);
                particle.VelY = Floor(std::sin(ang) * 65536);

                particle.X = Floor(elem.X1 - (size * particle.VelX)) * 65536;
                particle.Y = Floor(elem.Y1 - (size * particle.VelY)) * 65536;
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
            speed = Floor(speed + speed + 1);

            elem.VelX = Floor((ProceduralRand() % 3 - 1) * speed * 65536);
            elem.VelY = Floor((ProceduralRand() % 3 - 1) * speed * 65536);
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
                    particle.VelX = Floor(((ProceduralRand() % 100) - 50) * 0.005 * 65536);
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
                    particle.VelX = Floor(((ProceduralRand() % 100) - 50) * 0.0125 * 65536);
                    if (ProceduralRand() % 10 == 0) {
                        particle.VelY = Floor(-(ProceduralRand() % 100) * 0.003333333333333334 * 65536);
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
                elem.VelX += Floor(Rand(0, 100) * .0005 * -65536);
            if (elem.VelY < 131072)
                elem.VelY += Floor(Rand(0, 100) * .001 * 65536);

            elem.ApplyVelocity();
        }

        // Decays the contents of ProceduralBuffer based on the current "heat" level. Higher heat causes slower decay. 
        void HeatDecay() {
            auto decay = (int8)(Floor((255 - _info.Procedural.Heat) / 8.0f) + 1);

            for (auto& pixel : _fireBuffer[_index]) {
                if (pixel - decay < 0)
                    pixel = 0;
                else
                    pixel -= decay;
            }
        }

        bool ShouldDrawElement(const Element& elem) const {
            return elem.Frequency == 0 || FrameCount % elem.Frequency == 0;
        }

        struct BlobBounds {
            int minX, minY;
            int maxX, maxY;
            float sizeSq;
        };

        BlobBounds GetBlobBounds(const Element& elem) const {
            const float sizeSq = (float)elem.Size * (float)elem.Size;
            int minX = -elem.Size;
            int minY = -elem.Size;
            if (elem.X1 + minX < 1)
                minX = 1 - elem.X1;
            if (elem.Y1 + minY < 1)
                minY = 1 - elem.Y1;

            auto maxX = (int)elem.Size;
            auto maxY = (int)elem.Size;

            if (elem.X1 + maxX > Resolution - 1)
                maxX = Resolution - elem.X1 - 1;
            if (elem.Y1 + maxY > Resolution - 1)
                maxY = Resolution - elem.Y1 - 1;

            return { minX, minY, maxX, maxY, sizeSq };
        }

        void AddWaterHeightBlob(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto blob = GetBlobBounds(elem);

            for (int y = blob.minY; y < blob.maxY; y++) {
                auto yOffset = (y + elem.Y1) * Resolution;
                for (int x = blob.minX; x < blob.maxX; x++) {
                    auto offset = yOffset + x + elem.X1;
                    if (x * x + y * y < blob.sizeSq)
                        _waterBuffer[_index][offset] += elem.Speed;
                }
            }
        }

        void AddWaterSineBlob(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto blob = GetBlobBounds(elem);

            for (int y = blob.minY; y < blob.maxY; y++) {
                auto yOffset = (y + elem.Y1) * Resolution;
                for (int x = blob.minX; x < blob.maxX; x++) {
                    auto offset = yOffset + x + elem.X1;
                    auto radSq = (float)x * x + (float)y * y;
                    if (radSq < blob.sizeSq) {
                        auto cosine = Floor(cos(Floor(sqrt(radSq * ((1024.0 / elem.Size) * (1024.0 / elem.Size)))) / 65536.0 * 2 * 3.141592654) * elem.Speed);
                        //TBH no clue what this is about
                        if (cosine < 0)
                            cosine += 7;
                        _waterBuffer[_index][offset] += uint8(cosine >> 3);
                    }
                }
            }
        }

        void AddWaterRaindrops(const Element& elem) {
            if (!ShouldDrawElement(elem)) return;

            Element drop = {
                .WaterType = Outrage::WaterProceduralType::HeightBlob,
                .Speed = (int8)std::max(0, elem.Speed * Rand(-5, 5)),
                .Frequency = 0,
                .Size = (uint8)Rand(1, 4),
                .X1 = uint8(Rand(-elem.Size, elem.Size) + elem.X1),
                .Y1 = uint8(Rand(-elem.Size, elem.Size) + elem.Y1),
            };

            AddWaterHeightBlob(drop);
        }

        void AddWaterBlobdrops(const Element& elem) {
            if (!ShouldDrawElement(elem)) return;

            Element drop = {
                .WaterType = Outrage::WaterProceduralType::HeightBlob,
                .Speed = (int8)std::max(0, elem.Speed * Rand(-25, 25)),
                .Frequency = 0,
                .Size = (uint8)Rand(4, 10),
                .X1 = uint8(Rand(-elem.Size, elem.Size) + elem.X1),
                .Y1 = uint8(Rand(-elem.Size, elem.Size) + elem.Y1),
            };

            AddWaterHeightBlob(drop);
        }

        void UpdateWater() {
            int factor = _info.Procedural.Thickness;
            if (_info.Procedural.OscillateTime > 0) {
                int thickness = _info.Procedural.Thickness;
                int oscValue = _info.Procedural.OscillateValue;
                if (thickness < oscValue) {
                    oscValue = thickness;
                    thickness = oscValue;
                }

                int delta = thickness - oscValue;
                if (delta > 0) {
                    int time = (int)(Render::ElapsedTime / _info.Procedural.OscillateTime / delta) % (delta * 2);
                    if (time < delta)
                        time %= delta;
                    else
                        time = (delta - time % delta) - 1;

                    factor = time + oscValue;
                }
            }

            auto& src = _waterBuffer[_index];
            auto& dest = _waterBuffer[1 - _index];

            factor &= 31;

            // Handle dampening within the center of the heightmaps
            for (int y = 1; y < Resolution - 1; y++) {
                for (int x = 1; x < Resolution - 1; x++) {
                    auto offset = y * Resolution + x;
                    auto sum = ((src[offset + Resolution] + src[offset - 1] + src[offset + 1] + src[offset - Resolution]) >> 1) - dest[offset];
                    dest[offset] = int16(sum - (sum >> factor));
                }
            }

            // Handle dampening on the edges of the heightmaps
            for (int y = 0; y < Resolution; y++) {
                int belowoffset, aboveoffset;

                if (y == 0) {
                    aboveoffset = -(Resolution - 1) * Resolution;
                    belowoffset = Resolution;
                }
                else if (y == Resolution - 1) {
                    aboveoffset = Resolution;
                    belowoffset = -(Resolution - 1) * Resolution;;
                }
                else {
                    belowoffset = aboveoffset = Resolution;
                }

                for (int x = 0; x < Resolution; x++) {
                    //only dampen if actually on an edge
                    if (y == 0 || y == Resolution - 1 || x == 0 || x == Resolution - 1) {
                        int leftoffset, rightoffset;
                        if (x == 0) {
                            leftoffset = -(Resolution - 1);
                            rightoffset = 1;
                        }
                        else if (x == Resolution - 1) {
                            leftoffset = 1;
                            rightoffset = -(Resolution - 1);
                        }
                        else {
                            leftoffset = rightoffset = 1;
                        }

                        int offset = y * Resolution + x;
                        int sum = ((src[offset - leftoffset] + src[offset + rightoffset] + src[offset - aboveoffset] + src[offset + belowoffset]) >> 1) - dest[offset];

                        dest[offset] = int16(sum - (sum >> factor));
                    }
                }
            }
        }

        const PigBitmap& GetBitmap() const {
            return Resources::GetBitmap(BaseTexture);
            //return EClip == EClipID::None
            //    ? Resources::GetBitmap(BaseTexture)
            //    : Resources::GetBitmap(Resources::GetEffectClip(EClip).VClip.GetFrame(Render::ElapsedTime));
        }

        void DrawWaterNoLight() {
            auto& texture = GetBitmap();
            auto xScale = (float)texture.Info.Width / Resolution;
            auto yScale = (float)texture.Info.Height / Resolution;
            //assert(baseTexture.Info.Width == baseTexture.Info.Height); // Only supports square textures
            // todo: effect clips

            auto& heights = _waterBuffer[_index];

            for (int y = 0; y < Resolution; y++) {
                for (int x = 0; x < Resolution; x++) {
                    int offset = y * Resolution + x;
                    int height = heights[offset];

                    int xHeight;
                    if (x == Resolution - 1)
                        xHeight = heights[offset - Resolution + 1];
                    else
                        xHeight = heights[offset + 1];


                    int yHeight;
                    if (y == Resolution - 1)
                        yHeight = heights[offset - ((Resolution - 1) * Resolution)];
                    else
                        yHeight = heights[offset + Resolution];

                    xHeight = std::max(0, height - xHeight);
                    yHeight = std::max(0, height - yHeight);

                    int xShift = int((xHeight >> 3) + x * xScale) % texture.Info.Width;
                    int yShift = int((yHeight >> 3) + y * yScale) % texture.Info.Width;

                    int destOffset = y * Resolution + x;
                    //int srcOffset = botshift * Resolution + rightshift;
                    int srcOffset = yShift * (int)texture.Info.Width + xShift;

                    _pixels[destOffset] = texture.Data[srcOffset].ToRgba8888();
                }
            }
        }

        void DrawWaterWithLight(int lightFactor) {
            //proc_struct* proc = GameTextures[texnum].procedural;
            //ushort* destData = bm_data(proc->procedural_bitmap, 0);
            //ushort* srcData = bm_data(GameTextures[texnum].bm_handle, 0);
            //short* heightData = (short*)proc->proc1;
            auto& heights = _waterBuffer[_index];
            int lightshift = lightFactor & 31;

            auto& texture = GetBitmap();
            auto xScale = (float)texture.Info.Width / Resolution;
            auto yScale = (float)texture.Info.Height / Resolution;
            auto srcResmaskX = texture.Info.Width - 1;
            auto srcResmaskY = texture.Info.Height - 1;

            for (int y = 0; y < Resolution; y++) {
                int topoffset, botoffset;
                if (y == (Resolution - 1)) {
                    botoffset = _resMask * Resolution;
                    topoffset = Resolution;
                }
                else if (y == 0) {
                    botoffset = -Resolution;
                    topoffset = -_resMask * Resolution;
                }
                else {
                    topoffset = Resolution;
                    botoffset = -Resolution;
                }

                for (int x = 0; x < Resolution; x++) {
                    int offset = y * Resolution + x;

                    int horizheight;
                    if (x == Resolution - 1)
                        horizheight = heights[offset - 1] - heights[offset - Resolution + 1];
                    else if (x == 0)
                        horizheight = heights[offset + Resolution - 1] - heights[offset + 1];
                    else
                        horizheight = heights[offset - 1] - heights[offset + 1];

                    int vertheight = heights[offset - topoffset] - heights[offset - botoffset];

                    int lightval = 32 - (horizheight >> lightshift);
                    if (lightval > 63)
                        lightval = 63;
                    if (lightval < 0)
                        lightval = 0;

                    //int srcOffset = 
                    //    ((y + (vertheight >> 3)) & RESMASK) * RESOLUTION + 
                    //    ((x + (horizheight >> 3)) & RESMASK);

                    //int xOffset = int(((x + (horizheight >> 3)) & _resMask) * xScale);
                    //int yOffset = int(((y + (vertheight >> 3)) & _resMask) * Resolution * yScale);
                    //int xOffset = int(int(x * xScale) + (horizheight >> 3) & srcResmaskX);
                    //int yOffset = int((int(y * yScale) + (vertheight >> 3) & srcResmaskY) * Resolution * yScale);
                    //int xOffset = (x + (horizheight >> 3)) >> 1;
                    //int yOffset = (y + (vertheight >> 3)) >> 1;

                    int xShift = int((horizheight >> 3) + x * xScale) % texture.Info.Width;
                    int yShift = int((vertheight >> 3) + y * yScale) % texture.Info.Width;

                    int srcOffset = (yShift & srcResmaskY) * texture.Info.Width + (xShift & srcResmaskX);
                    //int srcOffset = yShift * (int)texture.Info.Width + xShift;

                    //int srcOffset = (int(yOffset * yScale) & srcResmaskY) * texture.Info.Width + (int(xOffset * xScale) & srcResmaskX);
                    auto& c = texture.Data[srcOffset];
                    //uint32 srcPixel = texture.Data[srcOffset].ToR8G8B8A8();
                    int destOffset = y * Resolution + x;
                    //auto srcPixel = uint16(c.r | c.g << 5 | c.b << 10 | 255);
                    // int16((r >> 3) + ((g >> 3) << 5) + ((b >> 3) << 10));
                    auto srcPixel = RGB32ToBGR16(c.r, c.g, c.b);
                    auto dest16 = WaterProcTableLo[(srcPixel & 255) + lightval * 256] + WaterProcTableHi[((srcPixel >> 8) & 127) + lightval * 256];
                    _pixels[destOffset] = BGRA16ToRGB32(dest16);
                    _pixels[destOffset] |= 0xFF000000;
                    //_pixels[destOffset] = c.ToR8G8B8A8();
                }
            }
        }

        void CopyBaseTexture() {
            auto& texture = Resources::GetBitmap(BaseTexture);
            auto srcResmaskX = texture.Info.Width - 1;
            auto srcResmaskY = texture.Info.Height - 1;
            auto scale = 0.5f;

            for (int y = 0; y < Resolution; y++) {
                for (int x = 0; x < Resolution; x++) {
                    //int xOffset = x >> 1;
                    //int yOffset = y >> 1;
                    //int srcOffset = x + y;
                    int srcOffset = (int(y * scale) & srcResmaskY) * texture.Info.Width + (int(x * scale) & srcResmaskX);
                    int destOffset = y * Resolution + x;
                    _pixels[destOffset] = texture.Data[srcOffset].ToRgba8888();
                }
            }
        }

        // Blends the contents of the active fire buffer and writes to the output buffer
        void BlendFireBuffer() {
            auto& src = _fireBuffer[_index];
            auto& dest = _fireBuffer[1 - _index];

            for (auto y = 0; y < Resolution; y++) {
                auto yptr = y * Resolution;
                auto up = yptr + Resolution;
                if (y == Resolution - 1)
                    up = 0; // wrap top edge

                auto down = yptr - Resolution;
                if (y == 0)
                    down = TotalSize - Resolution;

                for (auto x = 0; x < Resolution; x++) {
                    auto ptr = yptr + x;
                    auto right = ptr + 1;
                    auto left = ptr - 1;

                    if (x == Resolution - 1)
                        right = yptr;
                    if (x == 0)
                        left = yptr + Resolution - 1;

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

    Dictionary<string, Ptr<ProceduralTexture>> Procedurals;

    class CommandList {
        ComPtr<ID3D12GraphicsCommandList> _cmdList;
        ComPtr<ID3D12CommandAllocator> _allocator;
        ComPtr<ID3D12CommandQueue> _queue;
        ComPtr<ID3D12Fence> _fence;

        int _fenceValue = 1;
        HANDLE _fenceEvent{};

    public:
        CommandList(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const wstring& name) {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.Type = type;
            ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_queue)));
            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
            ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
            ThrowIfFailed(device->CreateCommandList(1, type, _allocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
            ThrowIfFailed(_cmdList->Close());

            ThrowIfFailed(_queue->SetName(name.c_str()));
            ThrowIfFailed(_allocator->SetName(name.c_str()));
            ThrowIfFailed(_cmdList->SetName(name.c_str()));
            ThrowIfFailed(_fence->SetName(name.c_str()));
        }

        void Reset() const {
            ThrowIfFailed(_cmdList->Reset(_allocator.Get(), nullptr));
        }

        ID3D12GraphicsCommandList* Get() const { return _cmdList.Get(); }

        void Execute(bool wait) {
            ThrowIfFailed(_cmdList->Close());
            ID3D12CommandList* ppCommandLists[] = { _cmdList.Get() };
            _queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            if (wait) Wait();
        }

    private:
        void Wait() {
            // Create synchronization objects and wait until assets have been uploaded to the GPU.

            // Create an event handle to use for frame synchronization.
            _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (_fenceEvent == nullptr)
                ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

            // Wait for the command list to execute; we are reusing the same command 
            // list in our main loop but for now, we just want to wait for setup to 
            // complete before continuing.
            //WaitForPreviousFrame();

            const UINT64 fence = _fenceValue;
            ThrowIfFailed(_queue->Signal(_fence.Get(), fence));
            _fenceValue++;

            // Wait until the previous frame is finished.
            if (_fence->GetCompletedValue() < fence) {
                ThrowIfFailed(_fence->SetEventOnCompletion(fence, _fenceEvent));
                WaitForSingleObject(_fenceEvent, INFINITE);
            }
        }
    };

    Ptr<CommandList> UploadQueue, CopyQueue;

    void FreeProceduralTextures() {
        Procedurals.clear();
        UploadQueue.reset();
        CopyQueue.reset();
    }

    void CreateTestProcedural(Outrage::TextureInfo& texture) {
        InitWaterTables();

        if (!Procedurals.contains(texture.Name)) {
            Procedurals[texture.Name] = MakePtr<ProceduralTexture>(texture, TexID(1080));
            auto ltid = Resources::GameData.LevelTexIdx[1080];
            Resources::GameData.TexInfo[(int)ltid].Procedural = true;
        }
    }

    void CopyProceduralToTexture(const string& srcName, TexID destId) {
        destId = TexID(1080);
        auto& material = Render::Materials->Get(destId);
        auto& src = Procedurals[srcName]->Texture;
        if (!src) return;

        // todo: this is only necessary once on level load
        auto destHandle = Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5);
        Render::Device->CopyDescriptorsSimple(1, destHandle, Procedurals[srcName]->Handle.GetCpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void UploadChangedProcedurals() {
        if (!UploadQueue) {
            UploadQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_COPY, L"Procedural upload queue");
            CopyQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_DIRECT, L"Procedural copy queue");
        }

        UploadQueue->Reset();

        int count = 0;
        for (auto& tex : Procedurals | views::values) {
            tex->Update();
            if (tex->CopyToTexture(UploadQueue->Get()))
                count++;
        }

        UploadQueue->Execute(true);
    }
}
