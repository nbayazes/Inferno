#include "Common.hlsli"

#define RS \
    "RootFlags(0),"\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL),"\
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"


ConstantBuffer<FrameConstants> Frame : register(b0);
Texture2D Depth : register(t0);
SamplerState Sampler : register(s0);

//struct Arguments {
//    float4 AtmosphereColor;
//};

//ConstantBuffer<Arguments> Args : register(b1);

struct PS_INPUT {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD0;
};

// Create a full screen triangle. Call with Draw(3).
[RootSignature(RS)]
PS_INPUT vsmain(in uint VertID : SV_VertexID) {
    PS_INPUT output;
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    output.uv = float2(uint2(VertID, VertID << 1) & 2);
    output.pos = float4(lerp(float2(-1, 1), float2(1, -1), output.uv), 0, 1);
    return output;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float2 uv = input.pos.xy / Frame.Size;
    float2 pixel = 1 / Frame.Size / 2;

    float d0 = Depth.Sample(Sampler, uv).x;
    float d1 = Depth.Sample(Sampler, uv + pixel.x + pixel.y).x;
    float d2 = Depth.Sample(Sampler, uv - pixel.x + pixel.y).x;
    float d4 = Depth.Sample(Sampler, uv - pixel.x - pixel.y).x;
    float d3 = Depth.Sample(Sampler, uv + pixel.x - pixel.y).x;
    //float d1 = Depth.Sample(Sampler, uv + pixel.x).x;
    //float d2 = Depth.Sample(Sampler, uv - pixel.x).x;
    //float d4 = Depth.Sample(Sampler, uv - pixel.y).x;
    //float d3 = Depth.Sample(Sampler, uv + pixel.y).x;

    //float davg = (d0 + d1 * .4 + d2 * .4 + d3 * .4 + d4 * .4) / 3;
    float wt = .5;
    //float davg = (d0 + d1 * wt + d2 * wt + d3 * wt + d4 * wt) / 5;
    float dmax = max(d0, max(d1, max(d2, max(d3, d4)))) / 1000;
    if(dmax <= 0) dmax = 1;
    float depthMult = pow(saturate(1 - dmax * Frame.FarClip / 1000 + 0.0), 2);

    const float fwt = 0.5;
    float alpha = fwidth(d0)
        + fwidth(d1) * fwt + fwidth(d2) * fwt
        + fwidth(d3) * fwt + fwidth(d4) * fwt;

    //return float4(d0, 0, 0, 1);

    //return float4(1 - dmax * 3000, 0, 0, 1);
    //alpha *= depthMult;
    //alpha *= 4.5;
    alpha = saturate(alpha * 2);

    float scanline = saturate(abs(sin(uv.y * Frame.Size.y / 2))) + 0.25;
    //alpha *= scanline;
    //alpha = saturate(min(alpha, (1 - dmax * 4000) ));

    return float4(0, alpha, 0, alpha);
    //return float4(alpha * .5, alpha * 4, alpha * .5, 1);
}
