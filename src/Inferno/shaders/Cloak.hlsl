#include "Common.hlsli"
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

struct ObjectVertex {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    nointerpolation int texid : TEXID;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    float3 normal : NORMAL;
    //float3 tangent : TANGENT;
    //float3 bitangent : BITANGENT;
    //float3 world : TEXCOORD1;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Instance.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;


    // transform from object space to world space
    output.normal = normalize(mul((float3x3)Instance.WorldMatrix, input.normal));
    //output.normal =  normalize(mul((float3x3)wvp, input.normal));
    //output.tangent = normalize(mul((float3x3)Instance.WorldMatrix, input.tangent));
    //output.bitangent = normalize(mul((float3x3)Instance.WorldMatrix, input.bitangent));
    //output.world = mul(Instance.WorldMatrix, float4(input.pos, 1)).xyz;
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float3 deltax = ddx(input.pos.xyz);
    float3 deltay = ddy(input.pos.xyz);
    float3 normal = normalize(cross(deltax, deltay));
    float2 scale = 5 / Frame.Size * Frame.RenderScale * Frame.RenderScale;

    float noise = 0.98 - (Instance.Noise * 0.4);
    float time = Frame.Time + Instance.TimeOffset;
    float2 offset = float2(sin(input.pos.y * scale.y + time), cos(input.pos.x * scale.x + time) * 3);
    float2 samplePos = (input.pos.xy + offset) / Frame.Size * Frame.RenderScale;
    samplePos = saturate(samplePos + normal.xy * 30 * noise * Frame.RenderScale);

    float3 sample = float3(0, 0, 0);
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos, 0).rgb;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(scale.x, -scale.y), 0).rgb * 0.5;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(-scale.x, -scale.y), 0).rgb * 0.5;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(-scale.x, -scale.y), 0).rgb * 0.5;
    sample += FrameTexture.SampleLevel(LinearBorder, samplePos + float2(scale.x, scale.y), 0).rgb * 0.5;
    sample /= 8;
    return float4(sample.rgb * noise, 0.80);
}
