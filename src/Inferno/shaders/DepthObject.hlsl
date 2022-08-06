#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 16), "

#include "FrameConstants.hlsli"

cbuffer Constants : register(b1) {
    float4x4 WorldMatrix;
};

struct VS_INPUT {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    float4 pos : SV_Position;
    float4 depth : COLOR0;
};

[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    float4x4 wvp = WorldMatrix * ViewProjectionMatrix;
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.depth = output.pos.z / output.pos.w;
    return output;
}

float LinearizeDepth(float n, float f, float depth) {
    return n / (f + depth * (n - f));
}

float psmain(PS_INPUT input) : SV_Target {
    return LinearizeDepth(NearClip, FarClip, input.pos.z);
}
