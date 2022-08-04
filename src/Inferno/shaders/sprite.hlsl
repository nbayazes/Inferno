#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 16), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

SamplerState sampler0 : register(s0);
Texture2D Diffuse : register(t0);
Texture2D LinearZ : register(t1);

cbuffer vertexBuffer : register(b0) {
    float4x4 ProjectionMatrix;
};

struct VS_INPUT {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

[RootSignature(RS)]
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col;
    output.uv = input.uv;
    return output;
}

float4 PSMain(PS_INPUT input) : SV_Target {
    float4 diffuse = Diffuse.Sample(sampler0, input.uv);
    if (diffuse.a <= 0.0)
        discard;
    return diffuse * input.col;
}
