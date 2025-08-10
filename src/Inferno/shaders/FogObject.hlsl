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
Texture2D Depth : register(t0); // front of fog linearized depth
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
    float front = Depth.Sample(Sampler, (input.pos.xy) / Frame.Size).x;
    if (front == 1) front = 0;
    float back = LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
    float depth = saturate(back - front);

    float4 fog = float4(pow(Args.Color.rgb, 2.2), 1);
    float density = Args.Color.a;
    fog *= ExpFog(depth, density);

    float3 ambient = pow(Args.Ambient.rgb * Args.Ambient.a, 2.2);
    fog.rgb = lerp(fog.rgb, fog.rgb * ambient, 0.75);

    return fog;
}
