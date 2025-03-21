#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "CBV(b1),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

struct Arguments {
    float4x4 ProjectionMatrix;
    float4 Color;
    float Scanline;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Arguments> Args : register(b1);
SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);

struct VS_INPUT {
    float2 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uvScreen : TEXCOORD1;
};

[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(Args.ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
    output.col = input.col;
    output.uv = input.uv;
    output.uvScreen = (input.pos.xy) /* * float2(1 / 64.0, 1 / 64.0)*/; // todo from screen res
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float2 uv = float2(0, 0);
    float4 color = Diffuse.SampleLevel(Sampler, input.uv + uv, 0);
    color.rgb *= pow(input.col.rgb, 2.2);
    color.a *= input.col.a;
    color.a = saturate(color.a);
    //color *= pow(input.col, 2.2);

    if (Args.Scanline > 0.1) {
        float2 screenUv = (input.pos.xy) / Frame.Size;
        float scanline = saturate(abs(sin(screenUv.y * Frame.Size.y / 2)));
        //float scanline = 1 - saturate(saturate(abs(sin(screenUv.y * Frame.Size.y / 2)) * -2) - .5) * Args.Scanline * 2;
        color.rgb *= (1 - scanline * Args.Scanline);
        //color.rgb += saturate(color.rgb - 0.5) * 2 * scanline; // boost highlights
        color.a = saturate(color.a);
        //color.rgb = scanline;
    }
    else {
        //color.rgb = color.rgb;
    }
    //float4 color = lerp(Diffuse.SampleLevel(Sampler, input.uv + uv, 0), float4(0, 0, 0, 0), apply);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(0.01, 0.01), 0)  * float4(0.5, 0.5, 0.5, 0.01);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(-0.01, -0.01), 0) * float4(0.5, 0.5, 0.5, 0.01);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(0.02, 0.02), 0)  * float4(0.5, 0.5, 0.5, 0.01);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(-0.02, -0.02), 0) * float4(0.5, 0.5, 0.5, 0.01);
    //color.rgb += lerp(saturate(color.rgb - 0.8) * 1, float3(0, 0, 0), apply);

    return color;
    //return input.col * Diffuse.Sample(Sampler, input.uv) * 2;
}
