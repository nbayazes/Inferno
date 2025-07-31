#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 4), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_POINT)"

struct FogVertex {
    float3 pos : POSITION;
    float4 color : COLOR0;
};

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    float3 color : COLOR0;
};

struct Arguments {
    float4 Color;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Arguments> Args : register(b1);
Texture2D Depth : register(t0); // front of fog linearized depth
SamplerState Sampler : register(s0);

[RootSignature(RS)]
PS_INPUT vsmain(FogVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    output.color.rgb = pow(max(input.color.rgb * input.color.a, 0), 2.2);
    return output;
}

float4 ApplyLinearFog(float depth, float start, float end, float4 fogColor) {
    float f = saturate((((end - start) / Frame.FarClip) - depth) / ((end - start) / Frame.FarClip));
    //float f = saturate(1 / exp(pow(depth * 5, 2)));
    //return f * pixel + (1 - f) * fogColor;
    return (1 - f) * fogColor;
}

float LinearFog2(float depth, float start, float end) {
    return saturate((end - depth) / (end - start));
}

float4 psmain(PS_INPUT input) : SV_Target {
    float front = Depth.Sample(Sampler, (input.pos.xy) / Frame.Size).x;
    if (front == 1) front = 0; // make the inside visible

    //return float4(pow(input.color.rgb * input.color.a, 2.2), 1);
    
    float back = LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
    float depth = saturate(back - front);

    // when the camera crosses the fog plane the depth goes to 0.
    // ignore the front value in this case
    //if (depth = 0) depth = back;

    float4 fog = float4(pow(max(Args.Color.rgb, 0), 2.2), 1);
    float density = Args.Color.a;
    fog *= ExpFog(depth, density);
    //fog.rgb *= (input.color.rgb * input.color.a);

    float3 ambient = input.color.rgb;
    fog.rgb = lerp(fog.rgb, pow(max(fog.rgb * ambient, 0), 1), 0.75);
    //fog.rgb = fog.rgb * (exp(-depth * density * 0.5) * ambient);

    //fog.rgb *= pow(ambient, 2);
    //fog.rgb *= exp(-depth * ambient);

    //if(depth < 0.001)  return float4(0, 1, 0, 1);

    //return float4(depth.xxx, 1);

    //if(depth < 0.01) fog *= 0;
    //fog *= lerp(0, 1, back * 100);
    //depth = 0.05;
    //float4 fog = ApplyLinearFog(depth, 100, 200, float4(0.05, 0.01, 0.05, 1));
    //fog *= 1 - max(LinearFog2(depth, 0 / Frame.FarClip, 50 / Frame.FarClip), 0.03);
    //float4 fog = float4(1, 0.5, 0.5, 0.05) ;
    //return ApplyLinearFog(base * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));

    //fog.rgb += fog.rgb * input.color.rgb;
    // want to weight the fog color by the light color, but that's difficult due to open sides.
    //fog.rgb = lerp(fog.rgb, fog.rgb * input.color.rgb, 0.5);
    //return float4(input.color.rgb, 1);
    //fog.rgb *= 0.1;
    return fog;
}
