#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 6), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t2), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t10), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

Texture2D Diffuse1 : register(t0);
Texture2D Diffuse2 : register(t1);
Texture2D StMask : register(t2);

Texture2D Depth : register(t10);
SamplerState Sampler : register(s0);
SamplerState LinearSampler : register(s1);

//static const float PI = 3.14159265f;
//static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

struct InstanceConstants {
    float4 Color;
    bool Flat; // Disable shading
    bool HasOverlay;
};
//    float2 Scroll, Scroll2; // base and decal scrolling
//    float LightingScale; // for unlit mode
//    bool Distort;
//    bool IsOverlay;
//    bool HasOverlay;
//    int Tex1, Tex2;
//    float EnvStrength;
//    float _pad;
//    float4 LightColor; // Light color, if present
//};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<InstanceConstants> Args : register(b1);

struct Vertex {
    float3 pos : POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 world : TEXCOORD2;
};

[RootSignature(RS)]
PS_INPUT vsmain(Vertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    //output.col = float4(input.col.rgb, 1);
    output.color = input.color;
    output.normal = input.normal;
    output.world = input.pos; // level geometry is already in world coordinates
    output.uv = input.uv;
    output.uv2 = input.uv2;
    //output.col.a = clamp(output.col.a, 0, 1);
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float3 viewDir = normalize(input.world - Frame.Eye);

    // 'automap' shader?
    float dfwidth = Depth.Sample(LinearSampler, (input.pos.xy + 0.5) / Frame.Size).x;
    float d = Depth.Sample(LinearSampler, (input.pos.xy) / Frame.Size).x;
    float depth = pow(saturate(0.8 - saturate(d) * Frame.FarClip / 500), 2) + 0.025;
    float2 fw2 = fwidth(dfwidth);
    float highlight = pow(saturate(dot(input.normal, -viewDir)), 1.5);

    float alpha = Sample2D(Diffuse1, input.uv, Sampler, Frame.FilterMode).a;

    if (Args.HasOverlay) {
        float mask = 1 - Sample2D(StMask, input.uv2, Sampler, Frame.FilterMode).r;
        alpha *= mask;

        float4 src = Sample2D(Diffuse2, input.uv2, Sampler, Frame.FilterMode);
        alpha = src.a + alpha * (1 - src.a); // Add overlay texture
    }

    if ((Frame.FilterMode == FILTER_SMOOTH && alpha <= 0) || (Frame.FilterMode != FILTER_SMOOTH && alpha < 1))
        discard;

    //color.rgb += saturate(color.rgb - 0.5) * 2; // boost highlights
    //color.a = saturate(color.a);
    float2 screenUv = (input.pos.xy) / Frame.Size;
    float ratio = Frame.Size.y / Frame.Size.x;
    float scanline = 1 + saturate(sin(screenUv.y * 1000 * ratio) - .5);

    float scanline2 = 1 + saturate(sin(screenUv.x * 3000 * ratio) - .5);
    scanline += scanline2 * 0.15;
    scanline *= 1 + saturate(1 + cos(Frame.Time * 2 + screenUv.x * 50 + screenUv.y * -3)) * 0.3;
    //scanline += 0.5 * scanline2 * (1 + saturate(1 + cos(Frame.Time * 2 + screenUv.y * 50 + screenUv.x * -3)) * 0.3);

    //float3 automap = float3(fw2.x + fw2.y, 0, 0);
    //return float4(fw2.x + fw2.y, 0, 0, 1);
    float outline = saturate(fw2.x + fw2.y) * 2;
    float fill = Args.Flat ? 1 : highlight * 1;
    //float intensity = outline + fill * (1 + abs(scanline * scanline * 0.20));
    //return float4(1,1,1,1);
    float4 color = Args.Color;
    //color.rgb = pow(Args.Flat ? color.rgb : max(input.color.rgb, 0), 2.2);
    color.rgb = pow(color.rgb, 2.2);
    
    return color * fill;
    return (input.color + float4(0.15, 0.15, 0.15, 0)) * fill + outline;
    return input.color * (outline + fill * (1 + abs(scanline * scanline * 0.05)));

    //float2 fw = fwidth(d);
    //float fwd = pow(1 + (fw.x * fw.x + fw.y * fw.y), 0.4) - 1;
    //float fxy = fw.x + fw.y / 4;
    //float gx = clamp(fwd * 100, 0.01, 4);
    //return float4(0, gx * 1, 0, 1);
}
