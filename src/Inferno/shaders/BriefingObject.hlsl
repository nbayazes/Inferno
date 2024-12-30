#include "Lighting.hlsli"
#include "Common.hlsli"
#include "ObjectVertex.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b1), "\
    "DescriptorTable(SRV(t0, numDescriptors = 5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t6), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t7), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t8), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t11), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2)"


// should match object.hlsl
struct Constants {
    float4x4 WorldMatrix;
    float4 EmissiveLight; // for additive objects like lasers
    float4 Ambient;
    float4 PhaseColor;
    int TexIdOverride;
    float TimeOffset;
    float PhaseAmount;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Object : register(b1);
// lighting constants are register b2

SamplerState Sampler : register(s0);
SamplerState NormalSampler : register(s1);
StructuredBuffer<MaterialInfo> Materials : register(t5);
StructuredBuffer<VClip> VClips : register(t6);
Texture2D DissolveTexture : register(t7);
TextureCube Environment : register(t8);

Texture2D TextureTable[] : register(t0, space1);

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    centroid float3 normal : NORMAL;
    centroid float3 tangent : TANGENT;
    centroid float3 bitangent : BITANGENT;
    float3 world : TEXCOORD1;
    nointerpolation int texid: TEXID;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Object.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    //input.normal.z *= -1;
    output.normal = normalize(mul((float3x3)Object.WorldMatrix, input.normal));
    output.tangent = normalize(mul((float3x3)Object.WorldMatrix, input.tangent));
    output.bitangent = normalize(mul((float3x3)Object.WorldMatrix, input.bitangent));
    output.world = mul(Object.WorldMatrix, float4(input.pos, 1)).xyz;
    output.texid = input.texid;
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
    float3 viewDir = normalize(input.world - Frame.Eye);

    int texid = input.texid;
    int matid = input.texid;

    if (Object.TexIdOverride >= 0) {
        matid = texid = Object.TexIdOverride;
    }

    // Lookup VClip texids
    if (texid > VCLIP_RANGE) {
        matid = VClips[texid - VCLIP_RANGE].Frames[0];
        texid = VClips[texid - VCLIP_RANGE].GetFrame(Frame.Time + Object.TimeOffset);
    }

    float4 diffuse = Sample2D(TextureTable[NonUniformResourceIndex(texid * 5)], input.uv, Sampler, Frame.FilterMode);
    float emissive = Sample2D(TextureTable[NonUniformResourceIndex(texid * 5 + 2)], input.uv, Sampler, Frame.FilterMode).r;

    float3 ambient = 0.75.rrr;
    MaterialInfo material = Materials[matid];

    float3 lighting = ambient;
    diffuse *= input.col;

    float specularMask = Sample2D(TextureTable[NonUniformResourceIndex(texid * 5 + 3)], input.uv, Sampler, Frame.FilterMode).r;
    float3 normal = SampleNormal(TextureTable[NonUniformResourceIndex(texid * 5 + 4)], input.uv, NormalSampler, Frame.FilterMode);
    normal.xy *= material.NormalStrength;
    normal = normalize(normal);

    float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
    normal = normalize(mul(normal, tbn));

    float3 lightDir = normalize(float3(0, 1, 1));

    // saturate metallic diffuse. It looks better and removes white highlights. Causes yellow to look orange.
    diffuse.rgb = pow(diffuse.rgb, 1 + material.Metalness * .5);
    specularMask *= material.SpecularStrength;

    {
        // Add some fake specular highlights so objects without direct lighting aren't completely flat
        float nDotH = HalfLambert(normal, -viewDir);
        float gloss = RoughnessToGloss(material.Roughness);
        ambient *= material.LightReceived;
        float3 specularColor = diffuse.rgb * material.SpecularStrength * 20;

        //{
        //    float eyeTerm = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
        //    lighting += eyeTerm * specularColor * specularMask * 2;
        //}

        // second layer of rough gloss based on environment to simulate indirect lighting
        {
            gloss /= 16;
            float envGloss = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
            lighting += envGloss * specularColor * 2;
        }
    }

    //lighting *= Specular(lightDir, viewDir, input.normal, 32) * 5;
    lighting += emissive * diffuse.rgb * material.EmissiveStrength;

    return float4(diffuse.rgb * lighting * Frame.GlobalDimming, diffuse.a);
}
