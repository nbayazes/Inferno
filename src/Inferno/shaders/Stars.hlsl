// Based on https://www.shadertoy.com/view/ltcGDj

#include "Common.hlsli"

#define RS "RootFlags(0), CBV(b0), RootConstants(b1, num32BitConstants = 4)"

struct Arguments {
    float4 AtmosphereColor;
};

ConstantBuffer<Arguments> Args : register(b1);

static const float FAR = 400.0f;

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

ConstantBuffer<FrameConstants> Frame : register(b0);

// From Dave_Hoskins (https://www.shadertoy.com/view/4djSRW)
float3 hash33(float3 p) {
    p = frac(p * float3(443.8975, 397.2973, 491.1871));
    p += dot(p.zxy, p.yxz + 19.27);
    return frac(float3(p.x * p.y, p.z * p.x, p.y * p.z));
}

float3 Starfield(in float3 p, float scale, float density) {
    float3 c = float3(0, 0, 0);
    //float res = Frame.Size.x / Frame.Size.y;
    float res = Frame.Size.y * scale;

    for (float i = 0.; i < 3.0; i++) {
        float3 q = frac(p * (.15 * res)) - 0.5;
        float3 id = floor(p * (.15 * res));
        float2 rn = hash33(id).xy;
        float c2 = 1. - smoothstep(0., .6, length(q));
        c2 *= step(rn.x, .0005 * density + i * i * 0.001 * density);
        //c += c2 * (lerp(float3(0.5, 0.49, 0.1), float3(0.75, 0.9, 1.), rn.y) * 0.25 + 0.75);
        //c += c2 * (lerp(float3(0.5, 0.49 * i, 0.1) , float3(0.75, 0.9, 2.50) * i, rn.y) * 0.25 + 0.3);
        c += c2 * (lerp(float3(0.25, 0.49 * i, 1.0) , float3(0.25, 0.9, 1.750) * i, rn.y) * 0.25 + 0.3);
        p *= 1.5; // Increases density
    }

    return c * c * 75;
    //return pow(1 + c * c, 5) - 1;
}

float linstep(in float mn, in float mx, in float x) {
    return saturate((x - mn) / (mx - mn));
}

// faked atmospheric scattering
float3 Atmosphere(float3 origin, float3 rd) {
    float dtp = 15. - (origin + rd * (FAR)).y * 3.5;
    float horizon = (linstep(-800., 0.0, dtp) - linstep(11., 500., dtp));
    const float3 lgt = float3(-.523, .41, -.747);
    float sd = max(dot(lgt, rd) * 0.5 + 0.5, 0.);
    horizon *= pow(sd, .01);

    float3 col = float3(0, 0, 0);
    //col += pow(horizon, 500.) * float3(0.15, 0.4, 1) * 3.0;
    //col += pow(horizon, 200.) * float3(0.15, 0.4, 1) * 3.0;
    col += pow(horizon, 11.) * 0.5; // float3(0.4, 0.5, 1) 
    col += pow(horizon, 1.5) * 0.015; // float3(0.6, 0.7, 1)
    //col += pow(horizon, 1.);
    return col;
}

float4 psmain(PS_INPUT input) : SV_Target {
    float2 uv = input.pos.xy / Frame.Size;
    float2 p = uv - 0.5;
    p.x *= Frame.Size.x / Frame.Size.y;

    //float3 ro = float3(650., sin(Frame.Time * 0.2) * 0.25 + 10., -Frame.Time);
    float3 right = cross(Frame.EyeUp, Frame.EyeDir);

    // scattering curvature
    const float radius = 1.7;
    float3 rd = normalize((p.x * right + p.y * -Frame.EyeUp) * radius + Frame.EyeDir); // ray direction
    rd = normalize(rd);
    
    float3 origin = float3(0., 15, 0); // position of scattering
    float3 rdScatter = rd;
    // TODO: adjust due to FOV
    rdScatter.y -= abs(p.x * p.x * 0.035); // surface curvature
    float4 atmosphere = float4(Atmosphere(origin, rdScatter), 1) * Args.AtmosphereColor;

    // Scale starfield to same pixel density regardless of resolution
    // This does mean higher resolutions will have more stars, but they look odd when larger than a few pixels
    float scale = 1440 / Frame.Size.y; 
    float density = 0.25  / Frame.RenderScale; // Increase density at lower render scales to keep similar overall density
    float3 starfield = Starfield(rd * Frame.RenderScale, scale, density) * Frame.RenderScale;
    float3 bg = starfield * (1 - saturate(dot(atmosphere.rgb, float3(1, 1, 1))));
    return float4(bg, 1) + atmosphere;
}
