#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0)"

ConstantBuffer<FrameConstants> Frame : register(b0);

struct LevelVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 world : TEXCOORD2;
};

[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos.xyz, 1));
    //output.col = float4(1, 0.72, 0.25, 1);
    output.col = float4(0.8, 0.8, 0.8, 1);
    output.normal = input.normal;
    output.world = input.pos; // level geometry is already in world coordinates
    return output;
}

float4 Specular(float3 lightDir, float3 eyeDir, float3 normal) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    float3 specular = pow((theta), 2);
    return float4(specular, 0);
}

float4 psmain(PS_INPUT input) : SV_Target {
    float3 lightDir = normalize(float3(-1, -2, 0));
    float4 color = input.col;
    color *= 0.5 - dot(lightDir, input.normal) * 0.5;
    return saturate(color);
}
