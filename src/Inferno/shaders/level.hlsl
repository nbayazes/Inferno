#include "FrameConstants.hlsli"
#include "Lighting.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 9), "\
    "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t4, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t8, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t9, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t10, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t11, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2),"\
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_WRAP," \
        "addressV = TEXTURE_ADDRESS_WRAP," \
        "addressW = TEXTURE_ADDRESS_WRAP," \
        "maxAnisotropy = 16," \
        "filter = FILTER_ANISOTROPIC)" // FILTER_MIN_MAG_MIP_LINEAR

Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1); // not used
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);

Texture2D Diffuse2 : register(t4);
Texture2D StMask : register(t5);
Texture2D Emissive2 : register(t6);
Texture2D Specular2 : register(t7);
Texture2D Depth : register(t8);
//StructuredBuffer<LightData> LightBuffer : register(t9);
//ByteAddressBuffer LightGrid : register(t10);
//ByteAddressBuffer LightGridBitMask : register(t11);

SamplerState Sampler : register(s0);
SamplerState LinearSampler : register(s1);


//Texture2DArray<float> lightShadowArrayTex : register(t10);

static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

cbuffer InstanceConstants : register(b1) {
    float2 Scroll, Scroll2;
    float LightingScale; // for unlit mode
    bool Distort;
    bool HasOverlay;
};

struct LevelVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
    // tangent, bitangent
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
    float3 world : TEXCOORD2;
    // tangent, bitangent
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
    float depth = Depth.Sample(Sampler, (pos.xy + 0.5) / FrameSize);
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

// Samples a texture with anti-aliasing. Intended for low resolution textures.
float4 Sample2DAA(Texture2D tex, float2 uv) {
    // if (disabled)
    // return tex.Sample(Sampler, uv);
    float width, height;
    tex.GetDimensions(width, height);
    float2 texsize = float2(width, height);
    float2 uv_texspace = uv * texsize;
    float2 seam = floor(uv_texspace + .5);
    uv_texspace = (uv_texspace - seam) / fwidth(uv_texspace) + seam;
    uv_texspace = clamp(uv_texspace, seam - .5, seam + .5);
    return tex.Sample(LinearSampler, uv_texspace / texsize);
}

float hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z).xxx;
}

float noise(in float2 st) {
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

float4 psmain(PS_INPUT input) : SV_Target {
    //return float4(input.normal.zzz, 1);
    float3 viewDir = normalize(input.world - Eye);
    //float4 specular = Specular(LightDirection, viewDir, input.normal);
    //float4 specular = Specular(-viewDir, viewDir, input.normal, 4);
    float4 specular = float4(0, 0, 0, 0);
    // adding noise fixes dithering, but this is expensive. sample a texture instead
    //specular.rgb *= 1 + rand(input.uv * 5) * 0.1;
    
    float4 lighting = lerp(1, max(0, input.col), LightingScale);
    lighting = float4(0, 0, 0, 0);
    //float d = dot(input.normal, viewDir);
    //lighting.rgb *= smoothstep(-0.005, -0.015, d); // remove lighting if surface points away from camera
    //return float4((input.normal + 1) / 2, 1);


    float4 base = Sample2DAA(Diffuse, input.uv);
    float4 emissive = Sample2DAA(Emissive, input.uv) * base;
    emissive.a = 0;
    base += base * Sample2DAA(Specular1, input.uv) * specular * 1.5;
    float4 diffuse;

    if (HasOverlay) {
        // Apply supertransparency mask
        float mask = Sample2DAA(StMask, input.uv2).r; // only need a single channel
        base *= mask.r > 0 ? (1 - mask.r) : 1;

        float4 src = Sample2DAA(Diffuse2, input.uv2);
        
        float out_a = src.a + base.a * (1 - src.a);
        float3 out_rgb = src.a * src.rgb + (1 - src.a) * base.rgb;
        diffuse = float4(out_rgb, out_a);
        
        if (diffuse.a < 0.01f)
            discard;
        
        // layer the emissive over the base emissive
        float4 emissive2 = Sample2DAA(Emissive2, input.uv2) * diffuse;
        emissive2.a = 0;
        emissive2 += emissive * (1 - src.a); // mask the base emissive by the overlay alpha

        lighting += diffuse * specular * src.a * 1.5;
        //lighting = max(lighting, emissive); // lighting should always be at least as bright as the emissive texture
        lighting += emissive2 * 3;
        // Boost the intensity of single channel colors
        // 2 / 1 -> 2, 2 / 0.33 -> 6
        //emissive *= 1.0 / max(length(emissive), 0.5); // white -> 1, single channel -> 0.33
        //float multiplier = length(emissive.rgb); 
        //lighting.a = saturate(lighting.a);
        //output.Color = diffuse * lighting;
        //return ApplyLinearFog(diffuse * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));
        
        // assume overlay is only emissive source for now
        //output.Emissive = float4(diffuse.rgb * src.a, 1) * emissive * 1;
        //output.Emissive = diffuse * (1 + lighting);
        //output.Emissive = float4(diffuse.rgb * src.a * emissive.rgb, out_a);
    }
    else {
        //lighting = max(lighting, emissive); // lighting should always be at least as bright as the emissive texture
        lighting += emissive * 3;
        //output.Color = base * lighting;
        diffuse = base;

        //base.rgb *= emissive.rgb * 0.5;
        //output.Emissive = base * lighting;
        if (diffuse.a < 0.01f)
            discard;
        
        //return ApplyLinearFog(base * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));
    }

    uint2 pixelPos = uint2(input.pos.xy);
    float3 colorSum = float3(0, 0, 0);
    //ShadeLights(colorSum, pixelPos, diffuse.rgb, float3(0, 0, 0), 0, 0, input.normal, viewDir, input.world);
    ShadeLights(colorSum, pixelPos, diffuse.rgb, float3(1, 1, 1), 0, 0, input.normal, viewDir, input.world);
    lighting.rgb += colorSum;

    return float4(diffuse.rgb * lighting.rgb * GlobalDimming, diffuse.a);
}
