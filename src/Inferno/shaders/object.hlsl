#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 37), "\
    "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);
Texture2D Normal1 : register(t4);

#include "FrameConstants.hlsli"
#include "Lighting.hlsli"

cbuffer Constants : register(b1) {
    float4x4 WorldMatrix;
    float3 EmissiveLight; // for untextured objects like lasers
    float3 Ambient;
};

cbuffer MaterialConstants : register(b2) {
    MaterialInfo Material;
}

struct VS_INPUT {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 world : TEXCOORD1;
};

[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    float4x4 wvp = mul(ViewProjectionMatrix, WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    //input.normal.z *= -1;
    output.normal = normalize(mul((float3x3) WorldMatrix, input.normal));
    output.world = mul(WorldMatrix, float4(input.pos, 1)).xyz;
    return output;
}

float3 Specular(float3 lightDir, float3 eyeDir, float3 normal) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    return 1 + pow(saturate(theta), 4);
}

float4 Fresnel(float3 eyeDir, float3 normal, float4 color, float power) {
    float f = saturate(1 - dot(eyeDir, normal));
    return float4((pow(f, power) * color).xyz, 0);
}

float4 psmain(PS_INPUT input) : SV_Target {
    float3 viewDir = normalize(Eye - input.world);
    float4 diffuse = Diffuse.Sample(Sampler, input.uv) * input.col;
    diffuse.xyz = pow(diffuse.xyz, 2.2);
    float3 emissive = Emissive.Sample(Sampler, input.uv).rgb;
    emissive = EmissiveLight + emissive * diffuse.rgb;
    float3 lighting = float3(0, 0, 0);

    //if (!NewLightMode) {
        float3 lightDir = float3(0, -1, 0);
        //float sum = emissive.r + emissive.g + emissive.b; // is there a better way to sum this?
        //float mult = (1 + smoothstep(5, 1.0, sum) * 1); // magic constants!
        //lighting += Ambient + pow(emissive * mult, 4);
    lighting += Ambient;
    lighting += emissive * 4;
        lighting *= Specular(lightDir, viewDir, input.normal);
        return float4(diffuse.rgb * lighting * GlobalDimming, diffuse.a);
    //} else {
    //    float specularMask = Specular1.Sample(Sampler, input.uv).r;

    //    float3 normal = clamp(Normal1.Sample(Sampler, input.uv).rgb * 2 - 1, -1, 1);
    //    normal.xy *= Material.NormalStrength;
    //    normal = normalize(normal);

    //    //float3 normal = float3(0, 0, 1);
    //    float3 colorSum = float3(0, 0, 0);
    //    uint2 pixelPos = uint2(input.pos.xy);
    //    ShadeLights(colorSum, pixelPos, diffuse.rgb, specularMask, normal, viewDir, input.world, Material);

    //    lighting += colorSum * Material.LightReceived * GLOBAL_LIGHT_MULT;
    //    lighting += emissive * diffuse.rgb;

    //    return float4(lighting * GlobalDimming, diffuse.a);
    //}
}
