#pragma once
#include "OutrageTable.h"
#include "DirectX.h"
#include "Graphics/Render.h"

namespace Inferno {
    void AddProcedural(Outrage::TextureInfo& info, TexID dest);
    void UploadProcedurals();
    int GetProceduralCount();
    void FreeProceduralTextures();

    // Converts BGRA5551 to RGBA8888
    constexpr int BGRA16ToRGB32(uint src) {
        auto r = (uint8)(((src >> 10) & 31) * 255.0f / 31);
        auto g = (uint8)(((src >> 5) & 31) * 255.0f / 31);
        auto b = (uint8)((src & 31) * 255.0f / 31);
        //auto a = src >> 15 ? 0 : 255;
        return r | g << 8 | b << 16 | 255 << 24;
    }

    class ProceduralTextureBase {
        double NextTime = 0;
        wstring _name;
        std::atomic<bool> PendingCopy = false;

    protected:
        int FrameCount = 0;
        using Element = Outrage::ProceduralInfo::Element;
        Outrage::TextureInfo _info;
        List<uint32> _pixels;
        ubyte _index = 0;
        int _resMask;
        uint16 TotalSize; // Resolution * Resolution
        uint16 Resolution; // width-height

    public:
        Texture2D Texture;
        DescriptorHandle Handle;
        TexID BaseTexture; // Texture slot to replace with this procedural effect

        ProceduralTextureBase(const Outrage::TextureInfo& info, TexID baseTexture) {
            BaseTexture = baseTexture;
            _info = info;
            _name = Convert::ToWideString(_info.Name);
            Resolution = info.GetSize();
            _resMask = Resolution - 1;
            TotalSize = Resolution * Resolution;
            _pixels.resize(TotalSize);
            Texture.SetDesc(Resolution, Resolution);
            Texture.CreateOnDefaultHeap(Convert::ToWideString(_info.Name));

            Handle = Render::Heaps->Procedurals.GetHandle(GetProceduralCount());
            Render::Device->CreateShaderResourceView(Texture.Get(), Texture.GetSrvDesc(), Handle.GetCpuHandle());
        }

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

            OnUpdate();
            FrameCount++;
            NextTime = Render::ElapsedTime + _info.Procedural.EvalTime;
            NextTime = Render::ElapsedTime + 1 / 30.0f;
            _index = 1 - _index; // swap buffers
            PendingCopy = true;
        }

        virtual ~ProceduralTextureBase() = default;

        ProceduralTextureBase(const ProceduralTextureBase&) = delete;
        ProceduralTextureBase(ProceduralTextureBase&&) = delete;
        ProceduralTextureBase& operator=(const ProceduralTextureBase&) = delete;
        ProceduralTextureBase& operator=(ProceduralTextureBase&&) = delete;

    protected:
        virtual void OnUpdate() = 0;

        bool ShouldDrawElement(const Element& elem) const {
            return elem.Frequency == 0 || FrameCount % elem.Frequency == 0;
        }

        static int Rand(int min, int max) {
            assert(max > min);
            auto range = max - min + 1;
            auto value = int(Random() * range);
            return value + min;
        }
    };
}
