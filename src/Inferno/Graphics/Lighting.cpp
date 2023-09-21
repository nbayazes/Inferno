#include "pch.h"
#include "Lighting.h"
#include "Render.h"

namespace Inferno::Graphics {
    void FillLightGridCS::SetLightConstants(uint32 width, uint32 height) {
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

    void FillLightGridCS::SetLights(ID3D12GraphicsCommandList* cmdList, span<LightData> lights) {
        _lightUploadBuffer.Begin();
        _lightUploadBuffer.Copy(lights);
        _lightUploadBuffer.End();

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyResource(_lightData.Get(), _lightUploadBuffer.Get());
    }

    void FillLightGridCS::Dispatch(ID3D12GraphicsCommandList* cmdList, ColorBuffer& linearDepth) {
        //ScopedTimer _prof(L"FillLightGrid", gfxContext);
        PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, L"Fill Light Grid");

        //ColorBuffer& LinearDepth = g_LinearDepth[TemporalEffects::GetFrameIndexMod2()];

        auto linearDepthState = linearDepth.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        _lightGrid.Transition(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        _bitMask.Transition(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto renderWidth = int(_width * Render::RenderScale);
        auto renderHeight = int(_height * Render::RenderScale);

        uint32_t tileCountX = AlignedCeil(renderWidth, LIGHT_GRID);
        //uint32_t tileCountY = AlignedCeil((int)color.GetHeight(), LIGHT_GRID);

        float farClip = Inferno::Render::Camera.FarClip;
        float nearClip = Inferno::Render::Camera.NearClip;
        const float rcpZMagic = nearClip / (farClip - nearClip);

        CSConstants constants{};
        constants.ViewportWidth = renderWidth;
        constants.ViewportHeight = renderHeight;
        constants.InvTileDim = 1.0f / LIGHT_GRID;
        constants.RcpZMagic = rcpZMagic;
        constants.TileCount = tileCountX;

        auto& camera = Inferno::Render::Camera;
        //constants.ViewProjMatrix = camera.ViewProj();
        constants.ViewMatrix = camera.View;
        //constants.ViewProjMatrix = camera.Projection * camera.View;
        constants.InverseProjection = camera.Projection.Invert();

        _csConstants.Begin();
        _csConstants.Copy({ &constants, 1 });
        _csConstants.End();

        cmdList->SetComputeRootSignature(_rootSignature.Get());
        cmdList->SetComputeRootConstantBufferView(B0_Constants, _csConstants.GetGPUVirtualAddress());
        cmdList->SetComputeRootDescriptorTable(T0_LightBuffer, _lightData.GetSRV());
        cmdList->SetComputeRootDescriptorTable(T1_LinearDepth, linearDepth.GetSRV());
        cmdList->SetComputeRootDescriptorTable(U0_Grid, _lightGrid.GetUAV());
        cmdList->SetComputeRootDescriptorTable(U1_GridMask, _bitMask.GetUAV());
        cmdList->SetPipelineState(_pso.Get());

        //Context.Dispatch(tileCountX, tileCountY, 1);
        Dispatch2D(cmdList, renderWidth, renderHeight);

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _lightGrid.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _bitMask.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        //depth.Transition(cmdList, depthState);
        linearDepth.Transition(cmdList, linearDepthState);
        PIXEndEvent(cmdList);
    }

    using namespace Render;

    void ResetBuffer(Array<LightData, MAX_LIGHTS>& buffer) {
        for (int i = 0; i < buffer.size(); i++) {
            buffer[i].radiusSq = 0;
        }
    }

    void LightBuffer::Dispatch(ID3D12GraphicsCommandList* cmdList) {
        auto index = Adapter->GetCurrentFrameIndex();
        auto& lightBuffer = _lights[index];
        //UpdateDynamicLights(level, lightBuffer);
        LightGrid->SetLights(cmdList, lightBuffer);
        LightGrid->Dispatch(cmdList, Adapter->LinearizedDepthBuffer);

        // Clear the next buffer
        ResetBuffer(_lights[(index + 1) % 2]);
        //_levelIndex = _dynamicIndex = 0;
        _dispatchCount = _index;
        _index = 0;
    }

    void LightBuffer::AddLight(const LightData& light) {
        if (_index >= _lights->size()) return;
        _lights[Adapter->GetCurrentFrameIndex()][_index++] = light;
    }
}
