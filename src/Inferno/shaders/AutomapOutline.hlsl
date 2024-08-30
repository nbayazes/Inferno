#include "Common.hlsli"

#define RS \
    "RootFlags(0),"\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL),"\
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"


ConstantBuffer<FrameConstants> Frame : register(b0);
Texture2D Depth : register(t0);
SamplerState Sampler : register(s0);

//struct Arguments {
//    float4 AtmosphereColor;
//};

//ConstantBuffer<Arguments> Args : register(b1);

struct PS_INPUT {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD0;
};

// Create a full screen triangle. Call with Draw(3).
[RootSignature(RS)]
PS_INPUT vsmain(in uint VertID : SV_VertexID) {
    PS_INPUT output;
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    output.uv = float2(uint2(VertID, VertID << 1) & 2);
    output.pos = float4(lerp(float2(-1, 1), float2(1, -1), output.uv), 0, 1);
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float2 uv = input.pos.xy / Frame.Size;
    float2 p = uv - 0.5;
    p.x *= Frame.Size.x / Frame.Size.y;
    return Depth.Sample(Sampler, p);

    return float4(1,0,0,0.5);
}

