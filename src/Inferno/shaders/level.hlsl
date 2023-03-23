#include "FrameConstants.hlsli"
#include "Lighting.hlsli"
#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 11), "\
    "DescriptorTable(SRV(t0, numDescriptors = 5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t5, numDescriptors = 5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t10, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t11, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2),"\
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_WRAP," \
        "addressV = TEXTURE_ADDRESS_WRAP," \
        "addressW = TEXTURE_ADDRESS_WRAP," \
        "maxAnisotropy = 16," \
        "filter = FILTER_ANISOTROPIC)" // FILTER_MIN_MAG_MIP_LINEAR, FILTER_MIN_MAG_MIP_POINT

Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1); // not used but reserved for descriptor table
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);
Texture2D Normal1 : register(t4);

Texture2D Diffuse2 : register(t5);
Texture2D StMask : register(t6);
Texture2D Emissive2 : register(t7);
Texture2D Specular2 : register(t8);
Texture2D Normal2 : register(t9);

Texture2D Depth : register(t10);
//StructuredBuffer<LightData> LightBuffer : register(t9);
//ByteAddressBuffer LightGrid : register(t10);
//ByteAddressBuffer LightGridBitMask : register(t11);

SamplerState Sampler : register(s0);
SamplerState LinearSampler : register(s1);


//Texture2DArray<float> lightShadowArrayTex : register(t10);

//static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

cbuffer InstanceConstants : register(b1) {
MaterialInfo Mat1;
float2 Scroll, Scroll2;
float LightingScale; // for unlit mode
bool Distort;
bool HasOverlay;
};

struct LevelVertex {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    centroid float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float3 world : TEXCOORD2;
};

/*
    Combined level shader
*/
[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(ViewProjectionMatrix, float4(input.pos, 1));
    //output.col = float4(input.col.rgb, 1);
    output.col = input.col;
    output.col.a = clamp(output.col.a, 0, 1);
    output.uv = input.uv + Scroll * Time * 200;
    output.uv2 = input.uv2 + Scroll2 * Time * 200;
    output.normal = input.normal;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.world = input.pos; // level geometry is already in world coordinates
    return output;
}

float4 Specular(float3 lightDir, float3 eyeDir, float3 normal, float power) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    float3 specular = pow(saturate(theta), power);
    return float4(specular, 0);
}

float4 ApplyLinearFog(float4 pixel, float4 pos, float start, float end, float4 fogColor) {
    float depth = Depth.Sample(LinearSampler, (pos.xy + 0.5) / FrameSize);
    float f = saturate((((end - start) / FarClip) - depth) / ((end - start) / FarClip));
    //float f = saturate(1 / exp(pow(depth * 5, 2)));
    return f * pixel + (1 - f) * fogColor;
}

float rand(float2 co) {
    float dt = dot(co.xy, float2(12.9898, 78.233));
    float sn = fmod(dt, 3.14);
    return frac(sin(sn) * 43758.5453);
}

float rand2(float2 co) {
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

float hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z).xxx;
}

float Noise(float2 st) {
    float2 i = floor(st);
    float2 f = frac(st);

    // Four corners in 2D of a tile
    float a = rand(i);
    float b = rand(i + float2(1.0, 0.0));
    float c = rand(i + float2(0.0, 1.0));
    float d = rand(i + float2(1.0, 1.0));

    // Smooth Interpolation
    float2 u = smoothstep(0, 1, f);

    // Mix 4 coorners percentages
    return lerp(a, b, u.x) +
        (c - a) * u.y * (1.0 - u.x) +
        (d - b) * u.x * u.y;
}

float3 hash(float3 p) // replace this by something better
{
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));

    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float Noise3D(in float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);

    float3 u = f * f * (3.0 - 2.0 * f);

    return lerp(lerp(lerp(dot(hash(i + float3(0.0, 0.0, 0.0)), f - float3(0.0, 0.0, 0.0)),
                          dot(hash(i + float3(1.0, 0.0, 0.0)), f - float3(1.0, 0.0, 0.0)), u.x),
                     lerp(dot(hash(i + float3(0.0, 1.0, 0.0)), f - float3(0.0, 1.0, 0.0)),
                          dot(hash(i + float3(1.0, 1.0, 0.0)), f - float3(1.0, 1.0, 0.0)), u.x), u.y),
                lerp(lerp(dot(hash(i + float3(0.0, 0.0, 1.0)), f - float3(0.0, 0.0, 1.0)),
                          dot(hash(i + float3(1.0, 0.0, 1.0)), f - float3(1.0, 0.0, 1.0)), u.x),
                     lerp(dot(hash(i + float3(0.0, 1.0, 1.0)), f - float3(0.0, 1.0, 1.0)),
                          dot(hash(i + float3(1.0, 1.0, 1.0)), f - float3(1.0, 1.0, 1.0)), u.x), u.y), u.z);
}

// Samples a texture randomly to make tiling less obvious
float3 TextureNoTile(Texture2D tex, float2 uv, float3 pos, float v) {
    //float k = texture(iChannel1, 0.005 * x).x; // cheap (cache friendly) lookup

    float2 duvdx = ddx(uv);
    float2 duvdy = ddy(uv);

    float k = Noise3D(pos);
    float l = k * 8.0;
    float f = frac(l);

    float ia = floor(l); // my method
    float ib = ia + 1.0;

    float2 offa = sin(float2(3.0, 7.0) * ia); // can replace with any other hash
    float2 offb = sin(float2(3.0, 7.0) * ib); // can replace with any other hash

    //float3 cola = tex.SampleGrad(LinearSampler, uv + v * offa, duvdx, duvdy);
    //float3 colb = tex.SampleGrad(LinearSampler, uv + v * offb, duvdx, duvdy);
    //float3 cola = Sample2DAA(tex, uv + v * offa);
    //float3 colb = Sample2DAA(tex, uv + v * offb);
    float3 cola = tex.Sample(Sampler, uv + v * offa);
    float3 colb = tex.Sample(Sampler, uv + v * offb);

    float3 diff = cola - colb;
    float sum = diff.x + diff.y + diff.z;
    return lerp(cola, colb, smoothstep(0.2, 0.8, f - 0.1 * sum));
}

float4 psmain(PS_INPUT input) : SV_Target {
    //return float4(input.normal.zzz, 1);
    float3 viewDir = normalize(input.world - Eye);
    // adding noise fixes dithering, but this is expensive. sample a noise texture instead
    //specular.rgb *= 1 + rand(input.uv * 5) * 0.1;

    float4 base = Sample2DAA(Diffuse, input.uv, LinearSampler);
    float3 normal = Sample2DAAData(Normal1, input.uv, LinearSampler).rgb * 2 - 1;
    //float3 normal = Normal1.Sample(Sampler, input.uv).rgb * 2 - 1; // map from 0..1 to -1..1
    //normal = float3(0, 0, 1);
    //return float4(pow(normal, 2.2), 1);
    //return float4(normal * 0.5 + 0.5, 1);
    //float3 normal = Normal1.SampleLevel(Sampler, input.uv, 1).rgb;
    //float normalStrength = 0.60;
    normal.xy *= Mat1.NormalStrength;
    normal = normalize(normal);

    float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
    normal = normalize(mul(normal, tbn));
    //return float4(normal * 0.5 + 0.5, 1);
    //normal = input.normal;
    //return float4(input.normal * 0.5 + 0.5, 1);

    //base.rgb = TextureNoTile(Diffuse, input.uv, input.world / 20, 1);
    //base += base * Sample2DAA(Specular1, input.uv) * specular * 1.5;
    float4 diffuse = base;

    float3 emissive = (Sample2DAA(Emissive, input.uv, LinearSampler)).rgb;

    if (HasOverlay) {
        // Apply supertransparency mask
        float mask = 1 - Sample2DAAData(StMask, input.uv2, LinearSampler).r; // only need a single channel
        base *= mask;

        float4 overlay = Sample2DAA(Diffuse2, input.uv2, LinearSampler);
        float out_a = overlay.a + base.a * (1 - overlay.a);
        float3 out_rgb = overlay.a * overlay.rgb + (1 - overlay.a) * base.rgb;
        diffuse = float4(out_rgb, out_a);
        emissive *= 1 - overlay.a; // Remove covered portion of emissive
        normal = normalize(lerp(normal, input.normal, overlay.a));
        // layer the emissive over the base emissive
        emissive += (Sample2DAAData(Emissive2, input.uv2, LinearSampler) * diffuse).rgb;
        //emissive2 += emissive * (1 - src.a); // mask the base emissive by the overlay alpha
        // Boost the intensity of single channel colors
        // 2 / 1 -> 2, 2 / 0.33 -> 6
        //emissive *= 1.0 / max(length(emissive), 0.5); // white -> 1, single channel -> 0.33
        //float multiplier = length(emissive.rgb); 
        //lighting.a = saturate(lighting.a);
        //output.Color = diffuse * lighting;

        // assume overlay is only emissive source for now
        //output.Emissive = float4(diffuse.rgb * src.a, 1) * emissive * 1;
        //output.Emissive = diffuse * (1 + lighting);
        //output.Emissive = float4(diffuse.rgb * src.a * emissive.rgb, out_a);
    }

    if (diffuse.a <= 0)
        discard; // discarding speeds up large transparent walls

    //return ApplyLinearFog(base * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));
    float3 lighting = float3(0, 0, 0);
    lighting += emissive * diffuse.rgb * 1.00;

    float3 vertexLighting = max(0, input.col.rgb);
    vertexLighting = lerp(1, vertexLighting, LightingScale);
    vertexLighting = pow(vertexLighting, 2.2); // sRGB to linear
#if 0
    lighting.rgb += vertexLighting;
    return float4(diffuse.rgb * lighting.rgb * GlobalDimming, diffuse.a);
#else
    //diffuse.rgb = 0.5;
    float3 colorSum = float3(0, 0, 0);
    uint2 pixelPos = uint2(input.pos.xy);
    float specularMask = Sample2DAAData(Specular1, input.uv, LinearSampler).r;
    //return float4(specularMask, specularMask, specularMask, 1);
    ShadeLights(colorSum, pixelPos, diffuse.rgb, specularMask, normal, viewDir, input.world, Mat1);
    lighting += colorSum;
    lighting += diffuse.rgb * vertexLighting * 0.20; // ambient
    //lighting.rgb += vertexLighting * 1.0;
    //lighting.rgb = max(lighting.rgb, vertexLighting * 0.40);
    //lighting.rgb = clamp(lighting.rgb, 0, float3(1, 1, 1) * 1.8);
    return float4(lighting * GlobalDimming, diffuse.a);
#endif
}
