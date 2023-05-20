#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 6),"\
    "DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t1, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t2, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

Texture2D Diffuse : register(t0);
Texture2D Overlay : register(t1);
Texture2D StMask : register(t2);
SamplerState Sampler : register(s0);


struct InstanceConstants {
    // Instance constants
    float2 Scroll, Scroll2;
    bool HasOverlay;
    float Threshold;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<InstanceConstants> Args : register(b1);

struct LevelVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    centroid float2 uv : TEXCOORD0;
    centroid float2 uv2 : TEXCOORD1;
};

[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    output.uv = input.uv + Args.Scroll * Frame.Time * 200;
    output.uv2 = input.uv2 + Args.Scroll2 * Frame.Time * 200;
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

float psmain(PS_INPUT input) : SV_Target {
    float alpha = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode).a;

    if (Args.HasOverlay) {
        float mask = SampleData2D(StMask, input.uv2, Sampler, Frame.FilterMode).r; // only need a single channel
        alpha *= mask.r > 0 ? (1 - mask.r) : 1;

        float4 src = Sample2D(Overlay, input.uv2, Sampler, Frame.FilterMode);
        alpha = src.a + alpha * (1 - src.a); // Add overlay texture
    }

    if (alpha < 1) // alpha of 1 so that AA works properly
        discard;

    return LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
}
