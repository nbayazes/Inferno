#pragma once
#include "OutrageTable.h"
#include "DirectX.h"
#include "Graphics/Render.h"

namespace Inferno {
    void AddProcedural(Outrage::TextureInfo& info, TexID dest);
    Outrage::ProceduralInfo* GetProceduralInfo(TexID id);
    void CopyProceduralsToMaterial();
    void StartProceduralWorker();
    void StopProceduralWorker();
    void FreeProceduralTextures();

    int GetProceduralCount();
    void EnableProceduralTextures(bool);

    // Converts BGRA5551 to RGBA8888
    constexpr int BGRA16ToRGB32(uint src, ubyte alpha) {
        auto r = (uint8)(((src >> 10) & 31) * 255.0f / 31);
        auto g = (uint8)(((src >> 5) & 31) * 255.0f / 31);
        auto b = (uint8)((src & 31) * 255.0f / 31);
        //auto a = src >> 15 ? 0 : 255;
        return r | g << 8 | b << 16 | alpha << 24;
    }

    class ProceduralTextureBase {
        double NextTime = 0;
        wstring _name;

    protected:
        int FrameCount = 0;
        using Element = Outrage::ProceduralInfo::Element;
        List<uint32> _pixels;
        ubyte _index = 0;
        int _resMask;
        uint16 TotalSize; // Resolution * Resolution
        uint16 Resolution; // width-height

        Texture2D TextureBuffers[3]{}; // Buffers for temp results
        Texture2D* _readBuffer = &TextureBuffers[0];
        Texture2D* _writeBuffer = &TextureBuffers[1];
        std::atomic<Texture2D*> _availableBuffer = &TextureBuffers[2];
        std::atomic<bool> _readAvailable = false;

        bool _shouldSwapBuffers = false;

    public:
        Outrage::TextureInfo Info;
        Texture2D Texture; // Shader visible texture
        DescriptorHandle Handle;
        TexID ID; // Texture slot to replace with this procedural effect
        std::mutex CopyMutex;

        ProceduralTextureBase(const Outrage::TextureInfo& info, TexID baseTexture) {
            ID = baseTexture;
            Info = info;
            _name = Convert::ToWideString(Info.Name);
            Resolution = info.GetSize();
            _resMask = Resolution - 1;
            TotalSize = Resolution * Resolution;
            _pixels.resize(TotalSize);
            Texture.SetDesc(Resolution, Resolution);
            Texture.CreateOnDefaultHeap(Convert::ToWideString(Info.Name));

            for (int i = 0; i < std::size(TextureBuffers); i++) {
                TextureBuffers[i].SetDesc(Resolution, Resolution);
                TextureBuffers[i].CreateOnDefaultHeap(Convert::ToWideString(Info.Name + " Buffer"));
            }

            Handle = Render::Heaps->Procedurals.GetHandle(GetProceduralCount());
            Render::Device->CreateShaderResourceView(Texture.Get(), Texture.GetSrvDesc(), Handle.GetCpuHandle());
            SPDLOG_INFO("Procedural Base Ctor");
        }

        bool ShouldUpdate(double elapsedTime) const { return NextTime <= elapsedTime; }

        // Copies from buffer texture to main texture. Must call from main thread. (consumes buffer)
        bool CopyToTexture(ID3D12GraphicsCommandList* cmdList) {
            if (!_readAvailable) return false; // Can't read yet

            _readAvailable = false;

            _readBuffer = _availableBuffer.exchange(_readBuffer);

            Texture.CopyFrom(cmdList, *_readBuffer);

            // todo: only need to update descriptors on load
            auto& material = Render::Materials->Get(ID);
            auto destHandle = Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5);
            Render::Device->CopyDescriptorsSimple(1, destHandle, Handle.GetCpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            return true;
        }

        // Updates and uploads data to buffer texture (produce buffer)
        void Update(ID3D12GraphicsCommandList* cmdList, double elapsedTime) {
            if (!ShouldUpdate(elapsedTime)) return;

            OnUpdate();
            FrameCount++;
            NextTime = elapsedTime + Info.Procedural.EvalTime;
            //NextTime = Render::ElapsedTime + 1 / 30.0f;
            _index = 1 - _index; // swap buffers

            _writeBuffer->UploadData(cmdList, _pixels.data());
            _shouldSwapBuffers = true;
        }

        void WriteComplete() {
            if (_shouldSwapBuffers) {
                _writeBuffer = _availableBuffer.exchange(_writeBuffer);
                _readAvailable.store(true, std::memory_order_release);
                _shouldSwapBuffers = false;
            }
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

    namespace Debug {
        inline double ProceduralUpdateRate = 0;
    }
}
