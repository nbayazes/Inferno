// Depth cutout shader for walls
// Walls are drawn individually and can make use of per-instance textures

#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "RootConstants(b1, num32BitConstants = 6),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t2), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

Texture2D TextureTable[] : register(t0, space1);
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
    float3 lightdir: LIGHTDIR;
    //nointerpolation int Tex1 : BASE;
    //nointerpolation int Tex2 : OVERLAY;
};

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    centroid float2 uv : TEXCOORD0;
    centroid float2 uv2 : TEXCOORD1;
    //nointerpolation int Tex1 : BASE;
    //nointerpolation int Tex2 : OVERLAY;
};

[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    output.uv = input.uv + Args.Scroll * Frame.Time * 200;
    output.uv2 = input.uv2 + Args.Scroll2 * Frame.Time * 200;
    //output.Tex1 = input.Tex1;
    //output.Tex2 = input.Tex2;
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

Texture2D GetTexture(int index, int slot) {
    return TextureTable[index * 5 + slot];
}

float psmain(PS_INPUT input) : SV_Target {
    //float alpha = Sample2D(GetTexture(input.Tex1, MAT_DIFF), input.uv, Sampler, Frame.FilterMode).a;
    float alpha = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode).a;

    if (Args.HasOverlay) {
        float mask = 1 - Sample2D(StMask, input.uv2, Sampler, Frame.FilterMode).r;
        alpha *= mask;

        float src = Sample2D(Overlay, input.uv2, Sampler, Frame.FilterMode).a;
        alpha = src + alpha * (1 - src); // Add overlay texture
    }

    // Smooth filtering must discard <= 0 because otherwise the semi-transparent pixels will cull incorrectly
    // alpha < 1 provides better edge quality with enhanced point filtering and AA
    if ((Frame.FilterMode == FILTER_SMOOTH && alpha <= 0) || (Frame.FilterMode != FILTER_SMOOTH && alpha < 1))
        discard;

    return LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
}
