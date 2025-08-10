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

struct FlatVertex {
    float3 pos : POSITION;
    float4 color : COLOR0;
};

struct PS_INPUT {
    centroid float4 pos : SV_POSITION;
    float3 color : COLOR0;
    centroid float3 world : TEXCOORD1;
};

struct Arguments {
    float4 Color;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Arguments> Args : register(b1);
Texture2D Depth : register(t0); // front of fog linearized depth
SamplerState Sampler : register(s0);

[RootSignature(RS)]
PS_INPUT vsmain(FlatVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    output.color.rgb = pow(max(input.color.rgb * input.color.a, 0), 2.2);
    output.world = input.pos;
    return output;
}

float4 ApplyLinearFog(float depth, float start, float end) {
    float f = saturate((((end - start) / Frame.FarClip) - depth) / ((end - start) / Frame.FarClip));
    //float f = saturate(1 / exp(pow(depth * 5, 2)));
    //return f * pixel + (1 - f) * fogColor;
    return (1 - f);
}

float LinearFog2(float depth, float start, float end) {
    return saturate((end - depth) / (end - start));
}

float2 random(float2 uv) {
    uv = float2(dot(uv, float2(127.1, 311.7)),
                dot(uv, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(uv) * 43758.5453123);
}

float noise(float2 uv, float seed_h, float seed_v) {
    uv.x = uv.x + seed_h;
    uv.y = uv.y + seed_v;

    float2 uv_index = floor(uv);
    float2 uv_fract = frac(uv);

    float2 blur = smoothstep(0.0, 1.0, uv_fract);

    return lerp(lerp(dot(random(uv_index + float2(0.0, 0.0)), uv_fract - float2(0.0, 0.0)),
                     dot(random(uv_index + float2(1.0, 0.0)), uv_fract - float2(1.0, 0.0)), blur.x),
                lerp(dot(random(uv_index + float2(0.0, 1.0)), uv_fract - float2(0.0, 1.0)),
                     dot(random(uv_index + float2(1.0, 1.0)), uv_fract - float2(1.0, 1.0)), blur.x), blur.y) + 0.5;
}


float fbm(float2 uv, float seed_h, float seed_v, int octaves, float amplitude, float frequency) {
    float value = 0.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(frequency * uv, noise(uv.xy, seed_h, seed_h) - 0.5, noise(uv.yx, seed_v, seed_v));
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

float4 rgbaNoise(float2 uv) {
    uv -= floor(uv / 289.0) * 289.0;
    uv += float2(223.35734, 550.56781);
    uv *= uv;

    float xy = uv.x * uv.y;

    return float4(frac(xy * 0.00000012),
                  frac(xy * 0.00000543),
                  frac(xy * 0.00000192),
                  frac(xy * 0.00000423));
}

float3 randomGradient(float3 pt) {
    int2 spt = int2(pt.xy + 101.0 * pt.z) % 256;

    float4 t = rgbaNoise(spt);

    return t.xyz * 2.0 - 1.0;
}

float dotGridGradient(float3 corner, float3 p) {
    float3 gradient = randomGradient(corner);

    float3 d = corner - p;

    return dot(d, gradient);
}

float perlin3(float3 p, float octave) {
    p *= octave;
    float3 p0 = floor(p);
    float3 p1 = p0 + 1;

    float3 s = p - p0;

    float4 gvv0 = float4(
        dotGridGradient(lerp(p0, p1, float3(0, 0, 0)), p),
        dotGridGradient(lerp(p0, p1, float3(1, 0, 0)), p),
        dotGridGradient(lerp(p0, p1, float3(0, 1, 0)), p),
        dotGridGradient(lerp(p0, p1, float3(1, 1, 0)), p));
    float4 gvv1 = float4(
        dotGridGradient(lerp(p0, p1, float3(0, 0, 1)), p),
        dotGridGradient(lerp(p0, p1, float3(1, 0, 1)), p),
        dotGridGradient(lerp(p0, p1, float3(0, 1, 1)), p),
        dotGridGradient(lerp(p0, p1, float3(1, 1, 1)), p));

    s = smoothstep(0.0, 1.0, s);

    float4 gvvx = lerp(gvv0, gvv1, s.z);
    float2 gvxx = lerp(gvvx.xy, gvvx.zw, s.y);
    return lerp(gvxx.x, gvxx.y, s.x);
}


float4 psmain(PS_INPUT input) : SV_Target {
    float front = Depth.Sample(Sampler, (input.pos.xy) / Frame.Size).x;
    //return float4(front.xxx, 1);
    if (front == 1) front = 0; // make the inside visible
    float back = LinearizeDepth(Frame.NearClip, Frame.FarClip, input.pos.z);
    //return float4(back.xxx, 1);

    //float horizontal_oscillation_factor = 0.2;
    //float vertical_oscillation_factor = 0.15;
    //int octaves = 5;
    //float amplitude = .85;
    //float frequency = 1.5;

    //float horizontal_seed = Frame.Time * horizontal_oscillation_factor;
    //float vertical_seed = Frame.Time * vertical_oscillation_factor;
    //float2 uvoffset = float2(0.01, 0.005) * Frame.Time;
    //float n = fbm((input.pos.xy) / Frame.Size + uvoffset, horizontal_seed, vertical_seed, octaves, amplitude, frequency);

    //float3 d = input.world.xyz - Frame.Eye;
    //float3 len = length(d);
    //float dist = max(front, 1);
    //float3 dir = normalize(d);
    //float3 intersect = Frame.Eye + dir * dist;
    //float4x4 t =  transpose(Frame.ViewProjectionMatrix);
    //float3 pixelWorld = mul(t, input.pos.xyz);
    //float g = 0.0;
    //float octave = 0.5;
    //for (int i = 0; i < 5; i++)
    //{
    //    octave *= 2.0;
    //    g += perlin3(input.world / 20 + float3(.1,.1,.1) * Frame.Time, octave) / octave;
    //    //g += perlin3(input.pos.xyz / 100 /*+ float3(.1,.1,.1) * Frame.Time*/, octave) / octave;
    //}

    //g = (g + 1.0) * 0.5;

    //return float4(g.xxx ,1);
    //back -= pow(saturate(n * n), 3) * .04;

    //return float4(pow(n.xxx, 2), 1);
    float depth = saturate(back - front);
    //depth *= saturate(n * n*.75);
    //depth *= (n * n * n) * 2 + 0.2;
    //depth *= g;

    float4 fog = float4(pow(max(Args.Color.rgb, 0), 2.2), 1);
    float density = Args.Color.a;
    //return float4(ex.rrr, 1);

    fog *= ExpFog(depth, density);
    //fog *= 1 - LinearFog2(depth, 0, 0.0075 * density);

    float3 ambient = input.color.rgb;
    // clamp ambient light to prevent oversaturation
    //ambient = clamp(ambient, 0, 2);

    //ambient = smoothstep(0, 3, ambient);
    //ambient = ambient < 1 ? ambient : smoothstep(1, 2, ambient);

    //return float4(Luminance(ambient).xxx, 1);
    //return float4(smoothstep(-1, 2, Luminance(ambient).xxx), 1);

    float3 normal = normalize(cross(ddx(input.world), ddy(input.world)));

    fog.rgb = lerp(fog.rgb, fog.rgb * smoothstep(-1, 2, Luminance(ambient)), 0.5);
    float3 dir = normalize(input.world - Frame.Eye);
    float d = saturate(pow(dot(normal, -dir), 2) * 4);
    //fog.rgb *= d;
    //return float4(d.xxx, 1);
    //fog.rgb = lerp(fog.rgb, fog.rgb * smoothstep(-1, 2, ambient), 0.5);
    //fog.rgb = lerp(fog.rgb, fog.rgb * smoothstep(0, 3, ambient), 0.5);
    return fog;
}
