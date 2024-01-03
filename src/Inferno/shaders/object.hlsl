#include "Lighting.hlsli"
#include "Common.hlsli"

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

    float4 diffuse = Sample2D(TextureTable[NonUniformResourceIndex(texid * 5)], input.uv, Sampler, Frame.FilterMode);
    float emissive = Sample2D(TextureTable[NonUniformResourceIndex(texid * 5 + 2)], input.uv, Sampler, Frame.FilterMode).r;
    float3 lighting = float3(0, 0, 0);

    float3 phaseColor = 0;
    //float argDissolve = frac(Frame.Time * .5);
    if (Object.PhaseAmount > 0) {
        float dissolveTex = .95 - Sample2D(DissolveTexture, input.uv + float2(Object.TimeOffset, Object.TimeOffset), Sampler, Frame.FilterMode).r;
        clip(Object.PhaseAmount - dissolveTex);
        phaseColor = Object.PhaseColor.rgb * step(Object.PhaseAmount - dissolveTex, 0.05);
    }

    float3 ambient = Object.Ambient.rgb;

    if (!Frame.NewLightMode) {
        float3 lightDir = float3(0, -1, 0);
        lighting += saturate(ambient * 4);
        lighting.rgb = saturate(Luminance(lighting.rgb)); // Desaturate
        lighting *= Specular(lightDir, viewDir, input.normal);
        diffuse *= input.col;
        return float4(diffuse.rgb * lighting * Frame.GlobalDimming, diffuse.a);
    }
    else {
        if (any(Object.EmissiveLight.rgb)) {
            lighting += Object.EmissiveLight.rgb * Object.EmissiveLight.a * diffuse.rgb;
        }
        else {
            //float specularMask = Specular1.Sample(Sampler, input.uv).r;
            float specularMask = Sample2D(TextureTable[NonUniformResourceIndex(texid * 5 + 3)], input.uv, Sampler, Frame.FilterMode).r;

            MaterialInfo material = Materials[matid];
            float3 normal = SampleNormal(TextureTable[NonUniformResourceIndex(texid * 5 + 4)], input.uv, NormalSampler, Frame.FilterMode);
            normal.xy *= material.NormalStrength;
            normal = normalize(normal);
            emissive *= material.EmissiveStrength;

            if (emissive > 0 && material.LightReceived == 0) {
                emissive = emissive + 1; // make lava and forcefields full bright
            }
            else {
                diffuse *= input.col; // apply per-poly color when not using fullbright textures
            }

            float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
            normal = normalize(mul(normal, tbn));

            float3 colorSum = float3(0, 0, 0);
            uint2 pixelPos = uint2(input.pos.xy);
            ambient *= Frame.GlobalDimming;
            specularMask *= material.SpecularStrength;
            
            lighting += emissive * diffuse.rgb;
            lighting += diffuse.rgb * ambient * 0.20 * material.LightReceived * (1 - material.Metalness * .20); // ambient
            ShadeLights(colorSum, pixelPos, diffuse.rgb, specularMask, normal, viewDir, input.world, material);
            lighting += colorSum * material.LightReceived;

            {
                // Add some fake specular highlights so objects without direct lighting aren't completely flat
                float nDotH = HalfLambert(normal, -viewDir);
                float gloss = RoughnessToGloss(material.Roughness) / 4;
                //float gloss = 16;
                float eyeTerm = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
                // saturate metallic diffuse. It looks better and removes white highlights. Causes yellow to look orange.
                diffuse.rgb = pow(diffuse.rgb, 1 + material.Metalness);
                float3 specularColor = diffuse.rgb * ambient * 3;
                lighting += eyeTerm * specularColor /** specularMask*/ * input.col.rgb * material.SpecularStrength;
                //lighting += ApplyAmbientSpecular(Environment, Sampler, Frame.EyeDir + viewDir, normal, material, ambient, diffuse.rgb, specularMask, .25) * nDotH;
            }
        }

        lighting.rgb += phaseColor;

        return float4(lighting, diffuse.a);
    }
}
