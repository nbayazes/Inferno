#include "Common.hlsli"
#include "ObjectVertex.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "CBV(b1), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, addressU = TEXTURE_ADDRESS_BORDER, addressV = TEXTURE_ADDRESS_BORDER, addressW = TEXTURE_ADDRESS_BORDER, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, filter = FILTER_MIN_MAG_MIP_LINEAR)"

struct Constants {
    float4x4 WorldMatrix;
    float TimeOffset;
    float Noise, Noise2;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Instance : register(b1);
Texture2D FrameTexture : register(t0);
//Texture2D NormalTexture : register(t1);
SamplerState LinearBorder : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    centroid float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    centroid float3 normal : NORMAL;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Instance.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;
    output.normal = normalize(mul((float3x3)wvp, input.normal));
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    const float NORMAL_STRENGTH = 30;
    const float BLUR_STRENGTH = 10;

    float3 viewDir = normalize(input.pos.xyz - Frame.Eye);

    float3 rf = refract(viewDir, input.normal, 2.4);
    float2 muv = mul(Frame.ViewMatrix, float4(rf, 0)).xy * 0.5 + 0.5;
    //muv.y = 1 - muv.y;

    //return float4(muv, 0, 1);

    float noise = 1 - (Instance.Noise * 0.3);
    float2 scale =/* noise **/ BLUR_STRENGTH / Frame.Size * Frame.RenderScale * Frame.RenderScale;

    float time = Frame.Time * 2 + Instance.TimeOffset;
    float2 offset = float2(sin(input.pos.x * scale.x + time), cos(input.pos.y * scale.y + time));
    muv.x += noise * .2;
    float2 samplePos = (input.pos.xy) / Frame.Size * Frame.RenderScale;
    samplePos += NORMAL_STRENGTH * muv / Frame.Size * Frame.RenderScale;

    float3 sample = float3(0, 0, 0);
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos, 0).rgb;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(scale.x, -scale.y), 0).rgb * 0.25;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(-scale.x, -scale.y), 0).rgb * 0.25;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(-scale.x, -scale.y), 0).rgb * 0.25;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(scale.x, scale.y), 0).rgb * 0.25;

    sample /= 8;
    return float4(sample.rgb * noise, 0.95); // make slightly transparent so powerups and sprites are visible
    //return float4(0, 0, 0, 0.8);
}
