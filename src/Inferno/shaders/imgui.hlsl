#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 16, visibility=SHADER_VISIBILITY_VERTEX), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0," \
        "comparisonFunc=COMPARISON_ALWAYS," \
        "borderColor=STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
        "visibility=SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_POINT)"

struct Arguments {
    float4x4 ProjectionMatrix;
};

ConstantBuffer<Arguments> Args : register(b0);
SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

struct VS_INPUT {
    float2 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
            
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
            
[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(Args.ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
    output.col = input.col;
    output.uv = input.uv;
    return output;
}
            
float4 psmain(PS_INPUT input) : SV_Target {
    float4 tex = texture0.Sample(sampler0, input.uv);
    return input.col * pow(tex, 1 / 2.2); // Textures are SRGB but UI is not
}