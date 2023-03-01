#pragma once
#include "ComputeShader.h"

//namespace Inferno::Render {
//    extern Inferno::Camera Camera;
//}
namespace Inferno::Debug {
    inline Vector3 LightPosition;
    inline bool InsideFrustum;
}

namespace Inferno::Graphics {

    constexpr int MAX_LIGHTS = 128; // todo: this is too low
    constexpr int LIGHT_GRID = 16;
    constexpr int LIGHT_GRID_MIN_DIM = 8;

    // must keep in sync with HLSL
    struct LightData {
        std::array<float, 3> pos;
        float radiusSq;
        std::array<float, 3> color;

        uint32_t type;
        //std::array<float, 3> coneDir[3];
        //float pad;
        //std::array<float, 2> coneAngles[2];

        //std::array<float, 16> shadowTextureMatrix;

        //Vector3 pos;
        //float radiusSq;

        //DirectX::XMFLOAT3A color;
        //uint32_t type;

        ////DirectX::XMFLOAT3A coneDir;
        ////DirectX::XMFLOAT2A coneDir;
        //float coneDir[3];
        //float coneAngles[2];

        //float shadowTextureMatrix[16];
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
        const StructuredBuffer& GetLights() { return _lightData; }
        auto GetConstants() const { return _lightingConstantsBuffer.GetGPUVirtualAddress(); }

        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVTable() const { return _lightData.GetSRV(); }

        void CreateBuffers(uint32 width, uint32 height) {
            _width = width;
            _height = height;

            // Assumes max resolution of 3840x2160
            // todo: use width / height
            constexpr uint32_t lightGridCells = AlignedCeil(3840, LIGHT_GRID_MIN_DIM) * AlignedCeil(2160, LIGHT_GRID_MIN_DIM);
            uint32_t lightGridSizeBytes = lightGridCells * (4 + MAX_LIGHTS * 4);
            _lightGrid.Create(L"Light Grid", 1, lightGridSizeBytes);

            uint32_t lightGridBitMaskSizeBytes = lightGridCells * 4 * 4;
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

        void SetLights(ID3D12GraphicsCommandList* cmdList /*List<LightData> lights*/);

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

        bool Compute(const CSConstants& c);


        bool Compute2(const CSConstants& c, Vector2 threadId);

        void Dispatch(ID3D12GraphicsCommandList* cmdList, ColorBuffer& linearDepth);
    };
}
