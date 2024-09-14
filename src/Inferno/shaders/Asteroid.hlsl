#include "Common.hlsli"
#include "ObjectVertex.hlsli"
#include "Lighting.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
    "CBV(b0),"\
    "CBV(b1),"

struct Constants {
    float4x4 WorldMatrix;
    float4 Ambient;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Object : register(b1);

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
    output.normal = normalize(mul((float3x3)Object.WorldMatrix, input.normal));
    output.tangent = normalize(mul((float3x3)Object.WorldMatrix, input.tangent));
    output.bitangent = normalize(mul((float3x3)Object.WorldMatrix, input.bitangent));
    output.world = mul(Object.WorldMatrix, float4(input.pos, 1)).xyz;
    output.texid = input.texid;
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float3 viewDir = normalize(input.world - Frame.Eye);
    //float highlight = pow(saturate(dot(input.normal, -viewDir)), 2.5);
    float3 lightDir = normalize(float3(0,0,0) - input.world); // light source is at origin
    float highlight = Lambert(input.normal, lightDir);
    //return float4(0, input.uv.x, 0, 1);
    //float3 rgb = Object.Ambient.rgb;
    float3 rgb = float3(.9, .9, 1.3);
    float3 ambient = pow(0.05.xxx, 2.2);
    float3 sunColor = float3(1.1, 1, 1) * 2;
    return float4(highlight * sunColor + ambient, 1);

    //return float4(1, 1, 1, 1);
}
