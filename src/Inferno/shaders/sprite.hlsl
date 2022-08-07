#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);
Texture2D Depth : register(t1);

#include "FrameConstants.hlsli"

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
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(ViewProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col;
    output.uv = input.uv;
    return output;
}

float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

float SaturateSoft(float depth, float contrast) {
    float Output = 0.5 * pow(saturate(2 * ((depth > 0.5) ? 1 - depth : depth)), contrast);
    return (depth > 0.5) ? 1 - Output : Output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float4 diffuse = Diffuse.Sample(Sampler, input.uv);
    if (diffuse.a <= 0.0)
        discard;
    
    float sceneDepth = Depth.Sample(Sampler, (input.pos.xy + 0.5) / FrameSize);
    float pixelDepth = LinearizeDepth(NearClip, FarClip, input.pos.z);
    //float d = saturate((sceneDepth - pixelDepth) * FarClip);
    float d = SaturateSoft((sceneDepth - pixelDepth) * FarClip, 4);
    return diffuse * input.col * d;
    
    // highlights on sprites
    //float4 color = diffuse * input.col;
    //float4 specular = pow(saturate(color - 0.6) + 1, 5) - 1;
    //specular.a = 0;
    //return (color + specular) * d;
}
