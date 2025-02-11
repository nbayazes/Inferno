#include "Lighting.hlsli"
#include "Common.hlsli"
#include "ObjectVertex.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0), "\
    "CBV(b1), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t11), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2)"

struct Constants {
    float4x4 WorldMatrix;
    float4 Light;
    float3 LightDir;
    float Padding;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Terrain : register(b1);
// lighting constants are register b2

SamplerState Sampler : register(s0);
SamplerState NormalSampler : register(s1);

Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1); // not used but reserved for descriptor table
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);
Texture2D Normal1 : register(t4);

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    centroid float3 normal : NORMAL;
    centroid float3 tangent : TANGENT;
    centroid float3 bitangent : BITANGENT;
    float3 world : TEXCOORD1;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Terrain.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    //input.normal.z *= -1;
    output.normal = normalize(mul((float3x3)Terrain.WorldMatrix, input.normal));
    output.tangent = normalize(mul((float3x3)Terrain.WorldMatrix, input.tangent));
    output.bitangent = normalize(mul((float3x3)Terrain.WorldMatrix, input.bitangent));
    output.world = mul(Terrain.WorldMatrix, float4(input.pos, 1)).xyz;
    return output;
}

float3 Specular(float3 lightDir, float3 eyeDir, float3 normal, float power) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    return 1 + pow(saturate(theta), power);
}

float4 Fresnel(float3 eyeDir, float3 normal, float4 color, float power) {
    float f = saturate(1 - dot(eyeDir, normal));
    return float4((pow(f, power) * color).xyz, 0);
}

float4 psmain(PS_INPUT input) : SV_Target {
    //return Sample2D(TextureTable[2934 * 5], input.uv, Sampler, Frame.FilterMode) * input.col;
    //return float4(0, 1, 0, 1);
    float3 viewDir = normalize(input.world - Frame.Eye);
    float3 diffuse = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode).rgb;
    float3 light = Terrain.Light.rgb * Terrain.Light.a;
    light.rgb = pow(light.rgb, 2.2); // sRGB to linear

    //diffuse *= input.col;
    //MaterialInfo material = Materials[matid];

    float3 lighting = float3(0, 0, 0);

    //float3 normal = SampleNormal(Normal1, input.uv, NormalSampler, Frame.FilterMode);
    //normal.xy *= material.NormalStrength;
    //normal = normalize(normal);

    //float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
    //normal = normalize(mul(normal, tbn));

    float3 lightDir = Terrain.LightDir;

    //lighting += pow(HalfLambert(input.normal, -lightDir), 12) * 2.0;
    lighting += Lambert(input.normal, -lightDir) * 2;
    lighting += pow(HalfLambert(input.normal, -lightDir), 2) * 1;
    //lighting = pow(1 + lighting, 1.5) - 1;
    lighting *= light;
    lighting += light * 0.1; // ambient
    //lighting = pow(1 + lighting, 1.5) - 1;

    float nDotH = Lambert(input.normal, -viewDir);
    //return float4(nDotL, 0, 0, 1);
    //float nDotH = HalfLambert(float3(0, 1, 0), -viewDir);
    float gloss = RoughnessToGloss(0.85);
    float eyeTerm = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
    //lighting += eyeTerm * input.col.rgb;
    //lighting += nDotH * 3;

    // saturate metallic diffuse. It looks better and removes white highlights. Causes yellow to look orange.
    //diffuse.rgb = pow(diffuse.rgb, 1 + material.Metalness * .5);

    //{
    //    // Add some fake specular highlights so objects without direct lighting aren't completely flat
    //    float nDotH = HalfLambert(normal, -viewDir);
    //    float gloss = RoughnessToGloss(material.Roughness) / 4;
    //    //float gloss = 16;
    //    float eyeTerm = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
    //    float3 specularColor = diffuse.rgb * ambient * 3;
    //    lighting += eyeTerm * specularColor * input.col.rgb * material.SpecularStrength * 5;
    //    //lighting += ApplyAmbientSpecular(Environment, Sampler, Frame.EyeDir + viewDir, normal, material, ambient, diffuse.rgb, specularMask, .25) * nDotH;
    //}

    ////lighting *= Specular(lightDir, viewDir, input.normal, 32) * 5;
    //lighting += emissive * diffuse.rgb * material.EmissiveStrength * .2;

    return float4(diffuse.rgb * lighting, 1);
}
