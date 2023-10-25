#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 60), "\
    "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

SamplerState sampler0 : register(s0);
Texture2D Diffuse : register(t0);
Texture2D Emissive : register(t2);

cbuffer Constants : register(b0) {
    float4x4 WorldMatrix;
    float4x4 ProjectionMatrix; // WVP
    float3 LightDirection[3];
    float4 Colors[3];
    float3 Eye;
    float Time;
};

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
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    output.normal = normalize(mul((float3x3) WorldMatrix, input.normal));
    output.world = mul(WorldMatrix, float4(input.pos, 1)).xyz;
    return output;
}

float4 Specular(float3 lightDir, float3 eyeDir, float3 normal) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    float3 specular = 1 + pow(saturate(theta), 4);
    return float4(specular, 1);
}

float4 Fresnel(float3 eyeDir, float3 normal, float4 color, float power) {
    float f = saturate(1 - dot(eyeDir, normal));
    return float4((pow(f, power) * color).xyz, 0);
}

float4 PSMain(PS_INPUT input) : SV_Target {
    float3 viewDir = normalize(Eye - input.world);
    float4 diffuse = Diffuse.Sample(sampler0, input.uv) * input.col;
    float4 emissive = Emissive.Sample(sampler0, input.uv) * diffuse;
    emissive.a = 0;

    float4 ambient = saturate(Colors[0]);
    float3 lightDir = float3(0, -1, 0);
    float4 light = ambient + emissive + pow(emissive, 3);
    light *= Specular(lightDir, viewDir, input.normal);
    light.a = 1;
    return diffuse * light;
}
