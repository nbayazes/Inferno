#include "Common.hlsli"
#include "ObjectVertex.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "CBV(b1), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"


struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
};

struct Arguments {
    float4x4 WorldMatrix;
    float4 Color;
    float4 Ambient;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Arguments> Args : register(b1);
Texture2D FogDepth : register(t0); // front of fog linearized depth
SamplerState Sampler : register(s0);

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    PS_INPUT output;
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Args.WorldMatrix);
    output.pos = mul(wvp, float4(input.pos, 1));
    return output;
}

float4 LinearFog2(float depth, float start, float end) {
    return saturate((end - depth) / (end - start));
}

float4 psmain(PS_INPUT input) : SV_Target {
    float front = FogDepth.Sample(Sampler, (input.pos.xy) / Frame.Size).x;
    if (front == 1) front = 0;
    float back = LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
    float depth = saturate(back - front);

    float3 fog = pow(max(Args.Color.rgb, 0), 2.2);
    float density = Args.Color.a;
    //fog *= ExpFog(depth, density);

    float3 ambient = pow(max(Args.Ambient.rgb * Args.Ambient.a, 0), 2.2);
    float lum = Luminance(ambient);
    float alpha = saturate(ExpFog(depth, density));

    //fog = lerp(fog, fog * smoothstep(-1, 2, lum), 0.75);
    //fog = lerp(fog, fog * lum, depth * 0.5);
    //fog = lerp(fog, fog * ambient, saturate(1 - alpha - 0.5));
    fog = lerp(fog, fog * lerp(ambient, lum, 0.5), saturate(1 - alpha - 0.5));

    //fog.rgb = lerp(fog.rgb, fog.rgb * ambient, 0.75);

    //return float4(fog, ExpFog(depth, density * (0.75 + lum * 0.25)));
    return float4(fog, alpha);
}
