#include "Common.hlsli"

#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b1), " \
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), "\

struct ObjectArgs {
    float4x4 WorldMatrix;
    float DissolveAmount;
    float TimeOffset;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<ObjectArgs> Object : register(b1);
Texture2D DissolveTexture : register(t0);
SamplerState Sampler : register(s0);
Texture2D TextureTable[] : register(t0, space1);
StructuredBuffer<VClip> VClips : register(t1);

struct ObjectVertex {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    nointerpolation int texid : TEXID;
};

struct PS_INPUT {
    centroid float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation int texid: TEXID;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Object.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.uv = input.uv;
    output.texid = input.texid;
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

float psmain(PS_INPUT input) : SV_Target {
    if (Object.DissolveAmount > 0) {
        float dissolveTex = 1 - Sample2D(DissolveTexture, input.uv + float2(Object.TimeOffset, Object.TimeOffset), Sampler, Frame.FilterMode).r;
        clip(Object.DissolveAmount - dissolveTex);
    }

    int texid = input.texid;
    if (texid > VCLIP_RANGE) {
        texid = VClips[texid - VCLIP_RANGE].GetFrame(Frame.Time + Object.TimeOffset);
    }

    float alpha = Sample2D(TextureTable[texid * 5], input.uv, Sampler, Frame.FilterMode).a;
    
    // Use <= 0 to use cutout edge AA, but it introduces artifacts. < 1 causes aliasing.
    if (alpha < 1)
        discard;

    return LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
}
