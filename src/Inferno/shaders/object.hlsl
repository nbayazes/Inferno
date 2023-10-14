#include "Lighting.hlsli"
#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "RootConstants(b1, num32BitConstants = 31), "\
    "DescriptorTable(SRV(t0, numDescriptors = 5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t6), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t7), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t11), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2)"

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

Texture2D TextureTable[] : register(t0, space1);

struct ObjectVertex {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    nointerpolation int texid : TEXID;
};

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
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
    //return Sample2D(TextureTable[2934 * 5], input.uv, Sampler, Frame.FilterMode) * input.col;
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

    float4 diffuse = Sample2D(TextureTable[texid * 5], input.uv, Sampler, Frame.FilterMode) * input.col;
    float emissive = Sample2D(TextureTable[texid * 5 + 2], input.uv, Sampler, Frame.FilterMode).r;
    float3 lighting = float3(0, 0, 0);

    float3 phaseColor = 0;
    //float argDissolve = frac(Frame.Time * .5);
    if (Object.PhaseAmount > 0) {
        float dissolveTex = .95 - Sample2D(DissolveTexture, input.uv + float2(Object.TimeOffset, Object.TimeOffset), Sampler, Frame.FilterMode).r;
        clip(Object.PhaseAmount - dissolveTex);
        phaseColor = Object.PhaseColor.rgb * step(Object.PhaseAmount - dissolveTex, 0.05);
    }

    if (!Frame.NewLightMode) {
        float3 lightDir = float3(0, -1, 0);
        //float sum = emissive.r + emissive.g + emissive.b; // is there a better way to sum this?
        //float mult = (1 + smoothstep(5, 1.0, sum) * 1); // magic constants!
        //lighting += Ambient + pow(emissive * mult, 4);
        lighting += Object.Ambient.rgb;
        lighting *= Specular(lightDir, viewDir, input.normal);
        return float4(diffuse.rgb * lighting * Frame.GlobalDimming, diffuse.a);
    }
    else {
        if (any(Object.EmissiveLight.rgb)) {
            lighting += Object.EmissiveLight.rgb * Object.EmissiveLight.a * diffuse.rgb;
        }
        else {
            //float specularMask = Specular1.Sample(Sampler, input.uv).r;
            float specularMask = Sample2D(TextureTable[texid * 5 + 3], input.uv, Sampler, Frame.FilterMode).r;

            MaterialInfo material = Materials[matid];
            float3 normal = SampleNormal(TextureTable[texid * 5 + 4], input.uv, NormalSampler);
            //return float4(normal, 1);
            normal.xy *= material.NormalStrength;
            normal = normalize(normal);

            float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
            normal = normalize(mul(normal, tbn));

            float3 colorSum = float3(0, 0, 0);
            uint2 pixelPos = uint2(input.pos.xy);
            //specularMask = 1;
            //normal = input.normal;
            ShadeLights(colorSum, pixelPos, diffuse.rgb, specularMask, normal, viewDir, input.world, material);
            lighting += colorSum * material.LightReceived * 1.5;
            lighting += emissive * diffuse.rgb * material.EmissiveStrength;
            lighting += emissive * diffuse.rgb * material.EmissiveStrength * Object.Ambient.rgb;
            lighting += Object.Ambient.rgb * diffuse.rgb * material.LightReceived;
        }

        
        lighting.rgb += phaseColor;

        //if (argDissolve - dissolveTex < 0) lighting.rgb = float3(1, 0, 1);
        return float4(lighting * Frame.GlobalDimming, diffuse.a);
    }
}
