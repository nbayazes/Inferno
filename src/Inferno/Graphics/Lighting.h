#pragma once
#include "Camera.h"
#include "CameraContext.h"
#include "ComputeShader.h"
#include "LightInfo.h"

namespace Inferno::Debug {
    inline Vector3 LightPosition;
    inline bool InsideFrustum;
}

namespace Inferno::Graphics {
    constexpr int MAX_LIGHTS = 512;
    constexpr int DYNAMIC_LIGHTS = 128; // for dynamics
    constexpr int LEVEL_LIGHTS = MAX_LIGHTS - DYNAMIC_LIGHTS;

    // First four bytes is the number of lights in the tile
    // size per light must be a multiple of 4
    constexpr int TILE_SIZE = 12 + MAX_LIGHTS * 4;
    constexpr int LIGHT_GRID = 16;
    constexpr uint32 LIGHT_GRID_MIN_DIM = 8;

    // must keep in sync with HLSL
    struct LightData {
        Vector3 pos;
        float radius;

        Color color;

        Vector3 pos2;
        float tubeRadius;

        Vector3 normal;
        LightType type;

        Vector3 right;
        DynamicLightMode mode;

        Vector3 up;
        float coneAngle0; 

        float coneAngle1;
        float coneSpill;
        float _pad1, _pad2;
    };

    struct LightingConstants {
        Vector3 sunDirection;
        alignas(16) Vector3 sunLight;
        alignas(16) Vector3 ambientLight;
        alignas(16) float ShadowTexelSize[4];

        float InvTileDim[4];
        uint32_t TileCount[4];
        uint32_t FirstLightIndex[4];

        uint32_t FrameIndexMod2;
    };


    //LightData m_LightData[MAX_LIGHTS];
    //ComputePSO m_FillLightGridCS_16("Fill Light Grid 16 CS");
    //RootSignature m_FillLightRootSig;
    //StructuredBuffer m_LightBuffer;
    //ByteAddressBuffer m_LightGrid;

    //ByteAddressBuffer m_LightGridBitMask;

    class FillLightGridCS : public ComputeShader {
        ByteAddressBuffer _bitMask;
        ByteAddressBuffer _lightGrid;
        StructuredBuffer _lightData;
        UploadBuffer<LightData> _lightUploadBuffer{ MAX_LIGHTS, "Light data" };
        UploadBuffer<LightingConstants> _lightingConstantsBuffer{ 1, "Light constants" };

        struct alignas(16) CSConstants {
            uint32_t ViewportWidth, ViewportHeight;
            float InvTileDim;
            float RcpZMagic;
            uint32_t TileCount;
            alignas(16) Matrix ViewMatrix;
            alignas(16) Matrix InverseProjection;
        };

        UploadBuffer<CSConstants> _csConstants{ 1, "Light grid CS" };

        enum RootSig { B0_Constants, T0_LightBuffer, T1_LinearDepth, U0_Grid, U1_GridMask };

        uint32 _width = 1, _height = 1;

    public:
        FillLightGridCS() : ComputeShader(LIGHT_GRID, LIGHT_GRID) {}

        const ByteAddressBuffer& GetBitMask() { return _bitMask; }
        const ByteAddressBuffer& GetLightGrid() { return _lightGrid; }
        //const StructuredBuffer& GetLights() { return _lightData; }
        auto GetConstants() const { return _lightingConstantsBuffer.GetGPUVirtualAddress(); }

        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVTable() const { return _lightData.GetSRV(); }

        void CreateBuffers(uint32 width, uint32 height) {
            _width = width;
            _height = height;

            const uint32 lightGridCells = AlignedCeil(width, LIGHT_GRID_MIN_DIM) * AlignedCeil(height, LIGHT_GRID_MIN_DIM);
            uint32 lightGridSizeBytes = lightGridCells * TILE_SIZE;
            _lightGrid.Create("Light Grid", 1, lightGridSizeBytes);

            uint32 lightGridBitMaskSizeBytes = lightGridCells * 4 * 4;
            _bitMask.Create("Light Bit Mask", 1, lightGridBitMaskSizeBytes);
            _lightData.Create("Light Data", sizeof LightData, MAX_LIGHTS);

            _lightData.AddUnorderedAccessView(false);
            _lightGrid.AddUnorderedAccessView(false);
            _bitMask.AddUnorderedAccessView(false);

            // SRV order is important
            _lightData.AddShaderResourceView();
            _lightGrid.AddShaderResourceView();
            _bitMask.AddShaderResourceView();
        }

        void SetLightConstants(uint2 size);

        void SetLights(const GraphicsContext& ctx, span<LightData> lights);

        void Dispatch(const GraphicsContext& ctx, ColorBuffer& linearDepth);
    };

    class LightBuffer {
        List<LightData> _lights[2]{}; // double buffered
        int _index = 0;
        int _dispatchCount = 0;

    public:
        LightBuffer() {
            _lights[0].resize(MAX_LIGHTS);
            _lights[1].resize(MAX_LIGHTS);
        }

        void Dispatch(const GraphicsContext& ctx);

        void AddLight(const LightData&);

        size_t GetCount() const { return _dispatchCount; }
    };

    inline LightBuffer Lights;
}
