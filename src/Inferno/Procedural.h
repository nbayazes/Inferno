#pragma once
#include "OutrageTable.h"
#include "DirectX.h"
#include "Graphics/GpuResources.h"

namespace Inferno {
    int GetProceduralCount();

    constexpr auto MAX_PROCEDURALS = 30;
    constexpr auto MAX_PROCEDURAL_HANDLES = MAX_PROCEDURALS * 3; // Ring buffer needs extra handles

    // Converts BGRA5551 to RGBA8888
    constexpr int BGRA16ToRGB32(uint src, ubyte alpha) {
        auto r = (uint8)(((src >> 10) & 31) * 255.0f / 31);
        auto g = (uint8)(((src >> 5) & 31) * 255.0f / 31);
        auto b = (uint8)((src & 31) * 255.0f / 31);
        //auto a = src >> 15 ? 0 : 255;
        return r | g << 8 | b << 16 | alpha << 24;
    }

    class ProceduralTextureBase {
        double _nextTime = 0;
        string _name;

    protected:
        int _frameCount = 0;
        using Element = Outrage::ProceduralInfo::Element;
        List<uint32> _pixels;
        ubyte _index = 0;
        int _resMask;
        uint16 _totalSize; // Resolution * Resolution
        uint16 _resolution; // width-height

        Texture2D _textureBuffers[3]{}; // Buffers for temp results
        Texture2D* _readBuffer = &_textureBuffers[0];
        Texture2D* _writeBuffer = &_textureBuffers[1];
        std::atomic<Texture2D*> _availableBuffer = &_textureBuffers[2];
        std::atomic<bool> _readAvailable = false;
        bool _shouldSwapBuffers = false; // Signals the procedural changed and needs to be copied to GPU
        D3D12_GPU_DESCRIPTOR_HANDLE _gpuHandle = {};

    public:
        Outrage::TextureInfo Info;
        TexID ID; // Texture slot to replace with this procedural effect
        bool Enabled = false;

        ProceduralTextureBase(const Outrage::TextureInfo& info, TexID baseTexture);

        D3D12_GPU_DESCRIPTOR_HANDLE GetHandle() const;

        // Copies from buffer texture to main texture. Must call from main thread. (consumes buffer)
        bool CopyToTexture(ID3D12GraphicsCommandList* cmdList, Texture2D& dest) {
            if (!_readAvailable) return false; // Can't read yet

            _readAvailable = false;

            _readBuffer = _availableBuffer.exchange(_readBuffer);
            _gpuHandle = dest.GetSRV();
            dest.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
            dest.CopyFrom(cmdList, *_readBuffer);
            dest.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            return true;
        }

        // Updates and uploads data to buffer texture (produce buffer)
        bool Update(ID3D12GraphicsCommandList* cmdList, double currentTime) {
            if (_nextTime > currentTime || !Enabled) return false;

            OnUpdate(currentTime);
            _frameCount++;
            _nextTime = currentTime + Info.Procedural.EvalTime;
            _index = 1 - _index; // swap buffers

            _writeBuffer->CopyFrom(cmdList, _pixels.data());
            _shouldSwapBuffers = true;
            return true;
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
        virtual void OnUpdate(double currentTime) = 0;

        bool ShouldDrawElement(const Element& elem) const {
            return elem.Frequency == 0 || _frameCount % elem.Frequency == 0;
        }

        static int Rand(int min, int max) {
            assert(max > min);
            auto range = max - min + 1;
            auto value = int(Random() * range);
            return value + min;
        }
    };

    ProceduralTextureBase* GetProcedural(TexID id);
    // Only water procedurals need an image
    void AddProcedural(Outrage::TextureInfo& info, TexID dest, const PigBitmap* image = nullptr);
    void EnableProcedural(TexID id, bool enabled = true);
    void CopyProceduralsToMainThread();
    void StartProceduralWorker();
    void StopProceduralWorker();
    void FreeProceduralTextures();
    void EnableProceduralTextures(bool);

    namespace Debug {
        inline double ProceduralUpdateRate = 0;
    }
}
