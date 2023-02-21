#pragma once
#include "ComputeShader.h"

//namespace Inferno::Render {
//    extern Inferno::Camera Camera;
//}

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
            alignas(16) Matrix ViewProjMatrix;
        };
        UploadBuffer<CSConstants> _csConstants{ 1 };

        enum RootSig { B0_Constants, T0_LightBuffer, T1_LinearDepth, U0_Grid, U1_GridMask };
        uint32 _width = 1, _height = 1;
    public:
        FillLightGridCS() : ComputeShader(LIGHT_GRID, LIGHT_GRID), _lightUploadBuffer(MAX_LIGHTS) {  }

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

        void SetLightConstants(uint32 width, uint32 height) {
            LightingConstants psConstants{};
            //psConstants.sunDirection = m_SunDirection;
            //psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
            //psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
            //psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
            psConstants.InvTileDim[0] = 1.0f / LIGHT_GRID;
            psConstants.InvTileDim[1] = 1.0f / LIGHT_GRID;
            psConstants.TileCount[0] = AlignedCeil(width, (uint32)LIGHT_GRID);
            psConstants.TileCount[1] = AlignedCeil(height, (uint32)LIGHT_GRID);
            //psConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
            //psConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
            psConstants.FrameIndexMod2 = Render::Adapter->GetCurrentFrameIndex();

            _lightingConstantsBuffer.Begin();
            _lightingConstantsBuffer.Copy({ &psConstants, 1 });
            _lightingConstantsBuffer.End();
        }

        void SetLights(ID3D12GraphicsCommandList* cmdList /*List<LightData> lights*/) {
            std::array<LightData, MAX_LIGHTS> lights{};
            //List<LightData> lights;
            //LightData light{};

            constexpr float radiusSq = 30 * 30;
            lights[0].pos = { 0, 0, 40 };
            lights[0].color = { 1, 0, 0 };
            lights[0].radiusSq = radiusSq;
            //lights.push_back(light);

            //light.pos = Vector3(0, 0, -20);
            lights[1].pos = { 20, 0, 45 };
            lights[1].color = { 0, 1, 0 };
            lights[1].radiusSq = radiusSq;
            //lights.push_back(light);

            //light.pos = Vector3(0, 0, -60);
            lights[2].pos = { -20, 0, 45 };
            lights[2].color = { 0, 0, 1 };
            lights[2].radiusSq = radiusSq;
            //lights.push_back(light);

            //light.pos = Vector3{-661.167603, 1413.05164, -584.823120};
            //light.radiusSq = 970531.875;
            //light.color = Color{ 0.136739269, 0.272178650, 0.250080407 };
            //light.type = 0;

            _lightUploadBuffer.Begin();
            for (auto& light : lights) {
                _lightUploadBuffer.Copy(light);
            }
            _lightUploadBuffer.End();

            //uint8_t* dataBegin;
            //CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
            //ThrowIfFailed(_lightBuffer->Map(0, &readRange, reinterpret_cast<void**>(&dataBegin)));
            //memcpy(dataBegin, lights.data(), sizeof(LightData) * lights.size());
            //_lightBuffer->Unmap(0, nullptr);

            //DirectX::TransitionResource(cmdList, _lightUploadBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);
            _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList->CopyResource(_lightData.Get(), _lightUploadBuffer.Get());

            //DirectX::TransitionResource(cmdList, _lightUploadBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_GENERIC_READ);

            //TransitionResource(Dest, D3D12_RESOURCE_STATE_COPY_DEST);
            //TransitionResource(Src, D3D12_RESOURCE_STATE_COPY_SOURCE);
            //FlushResourceBarriers();
            //m_CommandList->CopyResource(Dest.GetResource(), Src.GetResource());

        }

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

        void Dispatch(ID3D12GraphicsCommandList* cmdList, ColorBuffer& linearDepth) {
            //ScopedTimer _prof(L"FillLightGrid", gfxContext);
            PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, L"Fill Light Grid");

            //ColorBuffer& LinearDepth = g_LinearDepth[TemporalEffects::GetFrameIndexMod2()];

            //auto depthState = depth.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            auto linearDepthState = linearDepth.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            //color.Transition(cmdList, 

            _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            _lightGrid.Transition(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            _bitMask.Transition(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            uint32_t tileCountX = AlignedCeil((int)_width, LIGHT_GRID);
            //uint32_t tileCountY = AlignedCeil((int)color.GetHeight(), LIGHT_GRID);

            float farClip = Inferno::Render::Camera.FarClip;
            float nearClip = Inferno::Render::Camera.NearClip;
            const float rcpZMagic = nearClip / (farClip - nearClip);


            CSConstants constants{};
            constants.ViewportWidth = _width;
            constants.ViewportHeight = _height;
            constants.InvTileDim = 1.0f / LIGHT_GRID;
            constants.RcpZMagic = rcpZMagic;
            constants.TileCount = tileCountX;

            auto& camera = Inferno::Render::Camera;
            constants.ViewProjMatrix = camera.ViewProj();
            constants.ViewProjMatrix = camera.Projection * camera.View;
            auto proj = camera.Projection;
            auto view = camera.View;
            auto projView = camera.Projection * camera.View;
            auto viewProj = camera.ViewProj();

            //Matrix lhView = DirectX::XMMatrixPerspectiveFovLH(0.785398185, 1.35799503, 1, 1000);
            Matrix proj2 = GetProjMatrixTest(0.785398185, 1.35799503, 1, 1000);
            //Matrix rhView = DirectX::XMMatrixPerspectiveFovRH(0.785398185, 1.35799503, 1, 1000);
            // lhview2 aspect width is 3.278 instead of 1.7777

            //Matrix view2 = DirectX::XMMatrixLookAtLH(camera.Position, camera.Target, camera.Up);
            Matrix view2 = SetLookDirection(camera.Target - camera.Position, camera.Up);

            auto viewProj2 = proj2 * view2;

            // for look at
            // this matches
            /*Vector3 testForward(-0.859759986, -0.509800017, 0.0302756485);
            Vector3 testUp(-0.509484231, 0.860292912, 0.0179410148);
            Matrix testLookAt = SetLookDirection(testForward, testUp);*/

            // hard code
            //constants.ViewportWidth = 1920;
            //constants.ViewportHeight = 1080;
            //constants.InvTileDim = 0.0625000000; // 1 / 16
            //constants.RcpZMagic = 0.000100010002;
            //constants.TileCount = 120;

            //constants.ViewProjMatrix.m[0][0] = -0.0477907397;
            //constants.ViewProjMatrix.m[0][1] = -1.13123333;
            //constants.ViewProjMatrix.m[0][2] = 8.82812164e-05;
            //constants.ViewProjMatrix.m[0][3] = -0.882723868;

            //constants.ViewProjMatrix.m[1][0] = 0.00000000;
            //constants.ViewProjMatrix.m[1][1] = 2.13240504;
            //constants.ViewProjMatrix.m[1][2] = 4.68909566e-05;
            //constants.ViewProjMatrix.m[1][3] = -0.468862653;

            //constants.ViewProjMatrix.m[2][0] = -1.35715377;
            //constants.ViewProjMatrix.m[2][1] = 0.0398351960;
            //constants.ViewProjMatrix.m[2][2] = -3.10872883e-06;
            //constants.ViewProjMatrix.m[2][3] = 0.0310841799;

            //constants.ViewProjMatrix.m[3][0] = -0.000165770878;
            //constants.ViewProjMatrix.m[3][1] = -144.797104;
            //constants.ViewProjMatrix.m[3][2] = 0.872433782;
            //constants.ViewProjMatrix.m[3][3] = 1276.53467;

            constants.ViewProjMatrix = viewProj2;

            _csConstants.Begin();
            _csConstants.Copy({ &constants, 1 });
            _csConstants.End();

            cmdList->SetComputeRootSignature(_rootSignature.Get());
            //cmdList->SetComputeRoot32BitConstants(B0_Constants, 28, &constants, 0);
            cmdList->SetComputeRootConstantBufferView(B0_Constants, _csConstants.GetGPUVirtualAddress());
            cmdList->SetComputeRootDescriptorTable(T0_LightBuffer, _lightData.GetSRV());
            cmdList->SetComputeRootDescriptorTable(T1_LinearDepth, linearDepth.GetSRV());
            cmdList->SetComputeRootDescriptorTable(U0_Grid, _lightGrid.GetUAV());
            cmdList->SetComputeRootDescriptorTable(U1_GridMask, _bitMask.GetUAV());
            cmdList->SetPipelineState(_pso.Get());

            //Context.Dispatch(tileCountX, tileCountY, 1);
            Dispatch2D(cmdList, _width, _height);

            _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _lightGrid.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _bitMask.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            //depth.Transition(cmdList, depthState);
            linearDepth.Transition(cmdList, linearDepthState);
            PIXEndEvent(cmdList);
        }
    };
}
