#include "Common.hlsli"

struct Arguments {
    float4 FogColor;
    float DepthBias;
    float Softness;
    int FilterMode;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Arguments> Args : register(b1);
SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);
Texture2D Depth : register(t1);
Texture2D FogDepth : register(t2); // front of fog linearized depth

struct VS_INPUT {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 col : COLOR0;
};

[RootSignature(SPRITE_RS)]
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    output.pos.z += -Args.DepthBias / max(output.pos.w, 0.0001); // depth bias
    output.col = input.col.rgb * input.col.a;
    output.uv = input.uv;
    return output;
}

float SaturateSoft(float depth, float contrast) {
    float Output = 0.5 * pow(saturate(2 * ((depth > 0.5) ? 1 - depth : depth)), contrast);
    return (depth > 0.5) ? 1 - Output : Output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    //float4 diffuse = Diffuse.Sample(Sampler, input.uv);
    float4 diffuse = Sample2D(Diffuse, input.uv, Sampler, Args.FilterMode);
    //diffuse.xyz = pow(diffuse.xyz, 2.2);
    diffuse.rgb *= input.col;
    diffuse.a = clamp(diffuse.a, 0, 1);
    if (diffuse.a <= 0.0)
        discard;

    // (1 - (1-2*(Target-0.5)) * (1-Blend))
    //if (length(input.col.rgb) > 1 && length(diffuse.rgb) > 0.75)
    //    diffuse.rgb *= input.col.rgb;
    //(1 - (1 - 2 * target - 0.5)) * (1 - blend)
    //diffuse.rgb += clamp(diffuse.rgb - 0.5, 0, 1) * clamp(diffuse.rgb - 0.5, 0, 1);

    float sceneDepth = Depth.Sample(Sampler, (input.pos.xy + 0.5) / Frame.Size).x;
    if (sceneDepth <= 0.0f)
        return diffuse; // don't apply softening to particles against the background

    float pixelDepth = LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
    const float DEPTH_EXPONENT = 1.5;

    if (Args.Softness != 0) {
        float depthScale = clamp(1 - Args.Softness, 0.05, 1); // sprite turns invisible under 0.05
        diffuse.a *= SaturateSoft((sceneDepth - pixelDepth) * Frame.FarClip * depthScale, DEPTH_EXPONENT);
    }

    if (Args.FogColor.a > 0) {
        float front = FogDepth.Sample(Sampler, (input.pos.xy) / Frame.Size).x;
        if (front == 1) front = 0;
        float depth = saturate(pixelDepth - front);
        float3 fog = pow(max(Args.FogColor.rgb, 0), 2.2);
        float density = Args.FogColor.a;
        float lum = Luminance(input.col);
        float alpha = saturate(ExpFog(depth, density));
        //diffuse.rgb = lerp(diffuse.rgb, fog * lerp(input.col, lum, 0.5), saturate(1 - alpha - 0.5));
        //diffuse.rgb = lerp(diffuse.rgb, fog * lerp(input.col, lum, 0.5), 1);
        diffuse.rgb = lerp(diffuse.rgb, fog, alpha * diffuse.a);
    }

    return diffuse;

    // highlights on sprites
    //float4 color = diffuse * input.col;
    //float4 specular = pow(saturate(color - 0.6) + 1, 5) - 1;
    //specular.a = 0;
    //return (color + specular) * d;
}
