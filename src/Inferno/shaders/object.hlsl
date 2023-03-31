#define RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 37), "\
    "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);
Texture2D Emissive : register(t2);

#include "FrameConstants.hlsli"

cbuffer Constants : register(b1) {
    float4x4 WorldMatrix;
    float3 EmissiveLight; // for untextured objects like lasers
    float3 Ambient;
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
    //float3 viewDir = normalize(float3(229, 0, 340) - input.world);
    //return float4(viewDir, 1);
    //return float4(input.normal, 1);
    // shade faces pointing away from light

    //float3 light = 1 + saturate(dot(-input.normal, lightDir));
    //float3 shadow = 1 - saturate(dot(input.normal, lightDir)) * 0.5;

    float4 diffuse = Diffuse.Sample(Sampler, input.uv) * input.col;
    diffuse.xyz = pow(diffuse.xyz, 2.2);
    float3 emissive = Emissive.Sample(Sampler, input.uv).rgb;
    emissive = EmissiveLight + emissive * diffuse.rgb;
    //emissive.rgb = float3(0.8, 0, 0.5);
    //emissive.r = emissive.r > 0.70 ? emissive.r * 2.5 : emissive.r * 0.25;
    //emissive = emissive > 0.70 ? emissive * 2.5 : emissive * 0.25;
    //emissive = smoothstep(emissive * 0.25, emissive * 2, 1) - (emissive * 0.25);
    //emissive = max(0, 2 * emissive - 1) * 5;
    //emissive =  smoothstep(0, 1, 2 * emissive - 1) * emissive * 8;
    //emissive *= max(emissive, (5 * pow(emissive, 2) - 4 * emissive));
    //emissive += max(emissive, 10 * pow(emissive - 0.4, 3));
    //emissive = (5 * pow(emissive, 2) - 4 * emissive);
    //emissive.r = 1;
    //emissive.r = smoothstep(emissive.r * 0.5, emissive.r * 4.5, 5 * emissive.r - 4);
    //emissive.r = pow(emissive.r, 3) * 5;
    float3 lightDir = float3(0, -1, 0);
    //float4 lightColor = 1.0;
    //float3 lightDir = LightDirection[0];
    //float4 lightColor = LightColor[0];
    //float4 specular = Specular(lightDir, viewDir, input.normal) - 1;
    float sum = emissive.r + emissive.g + emissive.b; // is there a better way to sum this?
    float mult = (1 + smoothstep(5, 1.0, sum) * 1); // magic constants!
    float3 light = Ambient * GlobalDimming + pow(emissive * mult, 4);
    //float3 light = Ambient + emissive + pow(emissive, 4) * 10;
    
    light *= Specular(lightDir, viewDir, input.normal);
    //light += saturate(dot(-input.normal, lightDir)) * 0.25;
    //light -= saturate(dot(input.normal, lightDir)) * 0.5;
    //light = max(light, emissive) + ambient;
    //color *= float4(light, 1) * lightColor * specular;
    //color = color;

    //[unroll]
    //for (int i = 0; i < 3; i++) {
    //    float3 lightDir = LightDirection[i];
    //    float4 lightColor = LightColor[i];
    //    float4 specular = Specular(lightDir, viewDir, input.normal);
    //    light += saturate(dot(-input.normal, lightDir)) * lightColor * specular;
    //    light -= saturate(dot(input.normal, lightDir)) * 0.25;
    //    //color *= float4(light, 1) * lightColor * specular;
    //}

    //float4 fresnel = Fresnel(viewDir, input.normal, float4(1, -0.5, -0.5, 1), 4);
    //output.Color = color * (1 + float4(light, 1));
    //emissive *= 1;
    //output.Color = diffuse * light + emissive + pow(emissive, 3);
    //light.a = clamp(light.a, 0, 1);
    diffuse.rgb *= light;
    return diffuse;
    //output.Color = diffuse + 1 + emissive;
    //float4 emissiveLight = smoothstep(0, output.Color, light);
    //float4 emissiveLight = output.Color > 0.5 ? output.Color : 0;
    //float4 emissiveHighlight = highlight * diffuse > 0.9 ? highlight : 0;
    //return color * (1 + float4(light, 1));
}
