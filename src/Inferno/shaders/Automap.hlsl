#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t10), visibility=SHADER_VISIBILITY_PIXEL), "\
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

Texture2D Depth : register(t10);
SamplerState Sampler : register(s0);

//static const float PI = 3.14159265f;
//static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

//struct InstanceConstants {
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
//ConstantBuffer<InstanceConstants> Args : register(b1);


struct Vertex
{
    float3 pos : POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL;
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
    //output.col.a = clamp(output.col.a, 0, 1);
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float3 viewDir = normalize(input.world - Frame.Eye);

    // 'automap' shader?
    float dfwidth = Depth.Sample(Sampler, (input.pos.xy + 0.5) / Frame.Size).x;
    float d = Depth.Sample(Sampler, (input.pos.xy) / Frame.Size).x;
    float depth = pow(saturate(0.8 - saturate(d) * Frame.FarClip / 500), 2) + 0.025;
    float2 fw2 = fwidth(dfwidth);
    float highlight = pow(saturate(dot(input.normal, -viewDir)), 1.5);

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
    float fill = depth * highlight * 1;
    //float intensity = outline + fill * (1 + abs(scanline * scanline * 0.20));
    return (input.color + float4(0.15,0.15,0.15, 0)) * fill + outline;
    return input.color * (outline + fill * (1 + abs(scanline * scanline * 0.05)));

    //float2 fw = fwidth(d);
    //float fwd = pow(1 + (fw.x * fw.x + fw.y * fw.y), 0.4) - 1;
    //float fxy = fw.x + fw.y / 4;
    //float gx = clamp(fwd * 100, 0.01, 4);
    //return float4(0, gx * 1, 0, 1);
}
