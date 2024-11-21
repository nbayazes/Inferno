#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

struct PS_INPUT {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD0;
};

// Create a full screen triangle. Call with Draw(3).
[RootSignature(RS)]
PS_INPUT vsmain(in uint VertID : SV_VertexID) {
    PS_INPUT output;
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    output.uv = float2(uint2(VertID, VertID << 1) & 2);
    output.pos = float4(lerp(float2(-1, 1), float2(1, -1), output.uv), 0, 1);
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    return Texture.Sample(Sampler, input.uv);
}

