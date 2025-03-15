#include "Lighting.hlsli"
#include "Common.hlsli"
#include "ObjectVertex.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b1), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t6), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t7), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t8), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t11), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2)"

struct Constants {
    float4x4 WorldMatrix;
    float4 EmissiveLight; // for additive objects like lasers
    float4 Ambient;
    float4 PhaseColor;
    int TexIdOverride;
    float TimeOffset;
    float PhaseAmount;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Object : register(b1);
// lighting constants are register b2

SamplerState Sampler : register(s0);
SamplerState NormalSampler : register(s1);
StructuredBuffer<MaterialInfo> Materials : register(t5);
StructuredBuffer<VClip> VClips : register(t6);
Texture2D DissolveTexture : register(t7);
TextureCube Environment : register(t8);

Texture2D TextureTable[] : register(t0, space1);

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    centroid float3 normal : NORMAL;
    centroid float3 tangent : TANGENT;
    centroid float3 bitangent : BITANGENT;
    float3 world : TEXCOORD1;
    nointerpolation int texid: TEXID;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Object.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    //input.normal.z *= -1;
    output.normal = normalize(mul((float3x3)Object.WorldMatrix, input.normal));
    output.tangent = normalize(mul((float3x3)Object.WorldMatrix, input.tangent));
    output.bitangent = normalize(mul((float3x3)Object.WorldMatrix, input.bitangent));
    output.world = mul(Object.WorldMatrix, float4(input.pos, 1)).xyz;
    output.texid = input.texid;
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    //return Sample2D(TextureTable[2934 * 5], input.uv, Sampler, Frame.FilterMode) * input.col;
    float3 viewDir = normalize(input.world - Frame.Eye);
    float highlight = pow(saturate(dot(input.normal, -viewDir)), 1.5);
    return float4(Object.Ambient.rgb * highlight, 1);
}
