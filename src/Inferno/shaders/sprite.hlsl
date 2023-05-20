#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

ConstantBuffer<FrameConstants> Frame : register(b0);
SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);
Texture2D Depth : register(t1);

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
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
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
    //float4 diffuse = Diffuse.Sample(Sampler, input.uv);
    float4 diffuse = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode);
    //diffuse.xyz = pow(diffuse.xyz, 2.2);
    diffuse *= input.col;
    diffuse.a = clamp(diffuse.a, 0, 1);
    if (diffuse.a <= 0.0)
        discard;
    
    // (1 - (1-2*(Target-0.5)) * (1-Blend))
    //if (length(input.col.rgb) > 1 && length(diffuse.rgb) > 0.75)
    //    diffuse.rgb *= input.col.rgb;
        //(1 - (1 - 2 * target - 0.5)) * (1 - blend)
    //diffuse.rgb += clamp(diffuse.rgb - 0.5, 0, 1) * clamp(diffuse.rgb - 0.5, 0, 1);
    
    float sceneDepth = Depth.Sample(Sampler, (input.pos.xy + 0.5) / Frame.FrameSize).x;
    if (sceneDepth <= 0.0f)
        return diffuse; // don't apply softening to particles against the background
    
    float pixelDepth = LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
    const float DEPTH_SCALE = 0.85; // larger explosions want a smaller scale to blend into the surroundings better (0.85)
    const float DEPTH_EXPONENT = 1.5;
    float d = saturate((sceneDepth - pixelDepth) * 1000);
    //float d = SaturateSoft((sceneDepth - pixelDepth) * FarClip * DEPTH_SCALE, DEPTH_EXPONENT);
    return diffuse;
    
    // highlights on sprites
    //float4 color = diffuse * input.col;
    //float4 specular = pow(saturate(color - 0.6) + 1, 5) - 1;
    //specular.a = 0;
    //return (color + specular) * d;
}
