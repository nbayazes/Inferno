#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0)"

#include "FrameConstants.hlsli"

struct LevelVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    centroid float depth : COLOR0;
};

[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(ViewProjectionMatrix, float4(input.pos, 1));
    output.depth = output.pos.z / output.pos.w;
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

float psmain(PS_INPUT input) : SV_Target {
    return LinearizeDepth(NearClip, FarClip, input.pos.z);
}
