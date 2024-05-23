#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 1), " \
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

struct Arguments {
    float DepthBias;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Arguments> Args : register(b1);
SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);

struct VS_INPUT {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 col : COLOR0;
};

[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    output.col = input.col.rgb * input.col.a;
    output.uv = input.uv;
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    //float4 diffuse = Diffuse.Sample(Sampler, input.uv);
    float4 diffuse = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode);
    float4 origDiffuse = diffuse;
    //diffuse.xyz = pow(diffuse.xyz, 2.2);
    diffuse.rgb *= input.col;
    diffuse.rgb *= float3(1, .96, .9);
    diffuse.a = saturate(pow(diffuse.r, 0.5));
    diffuse.rgb += pow(saturate(diffuse.rgb - 0.9) * 4, 4) * float3(1.0, 0.6, .35);
    //diffuse.rgb += origDiffuse.rgb;
    //diffuse.a = origDiffuse.a;

    //diffuse.a = saturate(diffuse.a);
    if (diffuse.a <= 0.0)
        discard;

    return diffuse;
    // highlights on sprites
    //float4 color = diffuse * input.col;
    //float4 specular = pow(saturate(color - 0.6) + 1, 5) - 1;
    //specular.a = 0;
    //return (color + specular) * d;
}
