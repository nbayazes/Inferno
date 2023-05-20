#include "Lighting.hlsli"
#include "Common.hlsli"

#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 37), "\
    "DescriptorTable(SRV(t0, numDescriptors = 5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t11, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2)"

SamplerState Sampler : register(s0);
SamplerState NormalSampler : register(s1);
Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1);
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);
Texture2D Normal1 : register(t4);

struct Constants {
    float4x4 WorldMatrix;
    float3 EmissiveLight; // for untextured objects like lasers
    float3 Ambient;
    int TexID;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Args : register(b1);
// lighting constants are register b2

//StructuredBuffer<MaterialInfo> Materials : register(t5);

struct ObjectVertex {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float3 world : TEXCOORD1;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Args.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    //input.normal.z *= -1;
    output.normal = normalize(mul((float3x3)Args.WorldMatrix, input.normal));
    output.tangent = normalize(mul((float3x3)Args.WorldMatrix, input.tangent));
    output.bitangent = normalize(mul((float3x3)Args.WorldMatrix, input.bitangent));
    output.world = mul(Args.WorldMatrix, float4(input.pos, 1)).xyz;
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
    float3 viewDir = normalize(input.world - Frame.Eye);
    float4 diffuse = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode) * input.col;
    diffuse.xyz = pow(diffuse.xyz, 2.2);
    float3 emissive = SampleData2D(Emissive, input.uv, Sampler, Frame.FilterMode).rgb;
    emissive = Args.EmissiveLight + emissive * diffuse.rgb;
    float3 lighting = float3(0, 0, 0);

    if (!Frame.NewLightMode) {
        float3 lightDir = float3(0, -1, 0);
        //float sum = emissive.r + emissive.g + emissive.b; // is there a better way to sum this?
        //float mult = (1 + smoothstep(5, 1.0, sum) * 1); // magic constants!
        //lighting += Ambient + pow(emissive * mult, 4);
        lighting += Args.Ambient;
        lighting += emissive * 4;
        lighting *= Specular(lightDir, viewDir, input.normal);
        return float4(diffuse.rgb * lighting * Frame.GlobalDimming, diffuse.a);
    }
    else {
        float specularMask = Specular1.Sample(Sampler, input.uv).r;

        // todo: load material from a buffer
        //MaterialInfo material = Materials[0];
        MaterialInfo material;
        material.Metalness = 0.9;
        material.Roughness = 0.55;
        material.SpecularStrength = 1;
        material.EmissiveStrength = 1;
        material.LightReceived = 1;
        material.NormalStrength = 0.25;

        float3 normal = SampleNormal(Normal1, input.uv, NormalSampler);
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
        lighting += emissive * diffuse.rgb * 20; // todo: emissive mult
        lighting += Args.Ambient * 0.5f * diffuse.rgb * material.LightReceived; 

        return float4(lighting * Frame.GlobalDimming, diffuse.a);
    }
}
