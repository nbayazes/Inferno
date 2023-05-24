#include "Common.hlsli"

#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 16), "

struct ObjectArgs {
    float4x4 WorldMatrix;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<ObjectArgs> Object : register(b1);

struct VS_INPUT {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    centroid float4 pos : SV_Position;
};

[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Object.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

float psmain(PS_INPUT input) : SV_Target {
    return LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
}
