#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0), CBV(b1)"

#include "Common.hlsli"
#include "ObjectVertex.hlsli"

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    centroid float depth : COLOR0;
};

struct Constants {
    float4x4 WorldMatrix;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Terrain : register(b1);

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    // transform from object space to world space
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Terrain.WorldMatrix);

    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.depth = output.pos.z / output.pos.w;
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

float psmain(PS_INPUT input) : SV_Target {
    return LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
}
