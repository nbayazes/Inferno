#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0)"

struct FlatVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
};

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    //centroid float depth : COLOR0;
};

ConstantBuffer<FrameConstants> Frame : register(b0);

[RootSignature(RS)]
PS_INPUT vsmain(FlatVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    //output.depth = output.pos.z / output.pos.w;
    return output;
}

float psmain(PS_INPUT input) : SV_Target {
    return LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
}
