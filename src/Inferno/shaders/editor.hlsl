#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), RootConstants(b0, num32BitConstants = 20)"

static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;

cbuffer vertexBuffer : register(b0) {
    float4x4 ProjectionMatrix;
    float4 Tint;
};

struct VS_INPUT {
    float3 pos : POSITION;
    float4 col : COLOR0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

[RootSignature(RS)]
PS_INPUT VSFlat(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col * Tint;
    return output;
}

float4 PSFlat(PS_INPUT input) : SV_Target
{
    return input.col;
}
