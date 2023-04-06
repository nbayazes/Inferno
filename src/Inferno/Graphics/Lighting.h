#pragma once
#include "ComputeShader.h"
#include "LightInfo.h"

namespace Inferno::Debug {
    inline Vector3 LightPosition;
    inline bool InsideFrustum;
}

namespace Inferno::Graphics {
    constexpr int MAX_LIGHTS = 512;
    constexpr int RESERVED_LIGHTS = 128; // for dynamics

    // First four bytes is the number of lights in the tile
    // size per light must be a multiple of 4
    constexpr int TILE_SIZE = 12 + MAX_LIGHTS * 4;
    constexpr int LIGHT_GRID = 16;
    constexpr uint32 LIGHT_GRID_MIN_DIM = 8;

    // must keep in sync with HLSL
    struct LightData {
        Vector3 pos;
        float radiusSq;

        Vector3 color;
        LightType type;

        Vector3 pos2;
        float tubeRadius;

        Vector3 normal;
        float _pad0;

        Vector3 right;
        float _pad1;

        Vector3 up;
        float _pad2;
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
    //ComputePSO m_FillLightGridCS_16(L"Fill Light Grid 16 CS");
    //RootSignature m_FillLightRootSig;
    //StructuredBuffer m_LightBuffer;
    //ByteAddressBuffer m_LightGrid;

    //ByteAddressBuffer m_LightGridBitMask;

    class FillLightGridCS : public ComputeShader {
        ByteAddressBuffer _bitMask;
        ByteAddressBuffer _lightGrid;
        StructuredBuffer _lightData;
        UploadBuffer<LightData> _lightUploadBuffer;
        UploadBuffer<LightingConstants> _lightingConstantsBuffer{ 1 };

        struct alignas(16) CSConstants {
            uint32_t ViewportWidth, ViewportHeight;
            float InvTileDim;
            float RcpZMagic;
            uint32_t TileCount;
            alignas(16) Matrix ViewMatrix;
            alignas(16) Matrix InverseProjection;
        };

        UploadBuffer<CSConstants> _csConstants{ 1 };

        enum RootSig { B0_Constants, T0_LightBuffer, T1_LinearDepth, U0_Grid, U1_GridMask };

        uint32 _width = 1, _height = 1;

    public:
        FillLightGridCS() : ComputeShader(LIGHT_GRID, LIGHT_GRID), _lightUploadBuffer(MAX_LIGHTS) { }

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
            _lightGrid.Create(L"Light Grid", 1, lightGridSizeBytes);

            uint32 lightGridBitMaskSizeBytes = lightGridCells * 4 * 4;
            _bitMask.Create(L"Light Bit Mask", 1, lightGridBitMaskSizeBytes);
            _lightData.Create(L"Light Data", sizeof LightData, MAX_LIGHTS);

            _lightData.AddUnorderedAccessView(false);
            _lightGrid.AddUnorderedAccessView(false);
            _bitMask.AddUnorderedAccessView(false);

            // SRV order is important
            _lightData.AddShaderResourceView();
            _lightGrid.AddShaderResourceView();
            _bitMask.AddShaderResourceView();
        }

        void SetLightConstants(uint32 width, uint32 height);

        void SetLights(ID3D12GraphicsCommandList* cmdList, span<LightData> lights);

        Matrix GetProjMatrixTest(float yFov, float aspect, float nearClip, float farClip) {
            //float    SinFov;
            //float    CosFov;
            //DirectX::XMScalarSinCos(&SinFov, &CosFov, 0.5f * yFov);

            //float Height = CosFov / SinFov;
            //float Width = Height / aspect;
            //float fRange = farClip / (farClip - nearClip);

            //=====
            float Y = 1.0f / std::tanf(yFov * 0.5f);
            float X = Y * aspect;
            float Q1 = farClip / (nearClip - farClip);
            float Q2 = Q1 * nearClip;

            return Matrix{
                Vector4(X, 0.0f, 0.0f, 0.0f),
                Vector4(0.0f, Y, 0.0f, 0.0f),
                Vector4(0.0f, 0.0f, Q1, -1.0f),
                Vector4(0.0f, 0.0f, Q2, 0.0f)
            };
        }

        Matrix SetLookDirection(Vector3 forward, Vector3 up) {
            //auto invSqrt = [](auto x) { return 1 / std::sqrt(x); };

            forward.Normalize();

            // Given, but ensure normalization
            /*auto forwardLenSq = forward.LengthSquared();
            InvSqrt();
            forward = Select(forward * invSqrt(forwardLenSq), -Vector3(kZUnitVector), forwardLenSq < 0.000001f);*/

            // Deduce a valid, orthogonal right vector
            Vector3 right = forward.Cross(up);
            right.Normalize();

            //auto rightLenSq = right.LengthSquared();
            //right = Select(right * invSqrt(rightLenSq), Quaternion(Vector3(kYUnitVector), -XM_PIDIV2) * forward, rightLenSq < Scalar(0.000001f));

            // Compute actual up vector
            up = right.Cross(forward);

            // Finish constructing basis
            Matrix basis(right, up, -forward);
            auto q = Quaternion::CreateFromRotationMatrix(basis);
            Quaternion q2;
            q.Conjugate(q2);
            return Matrix::CreateFromQuaternion(q2);
            //m_CameraToWorld.SetRotation(Quaternion(m_Basis));
        }

        void Dispatch(ID3D12GraphicsCommandList* cmdList, ColorBuffer& linearDepth);
    };
}
