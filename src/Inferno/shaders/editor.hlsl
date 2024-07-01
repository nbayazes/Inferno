#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), CBV(b0)"

struct Arguments {
    float4x4 ProjectionMatrix;
    float4 Tint;
};

ConstantBuffer<Arguments> Args : register(b0);

struct VS_INPUT {
    float3 pos : POSITION;
    float4 col : COLOR0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(Args.ProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col * Args.Tint;
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    return pow(max(input.col, float4(0, 0, 0, 0)), 2.2);
    return input.col;
}
