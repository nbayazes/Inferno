#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 9), "\
    "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t4, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t8, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1); // not used
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);

Texture2D Diffuse2 : register(t4);
Texture2D StMask : register(t5);
Texture2D Emissive2 : register(t6);
Texture2D Specular2 : register(t7);
Texture2D Depth : register(t8);

SamplerState Sampler : register(s0);

static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units
            
#include "FrameConstants.hlsli"

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

float4 Specular(float3 lightDir, float3 eyeDir, float3 normal) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    float3 specular = pow(saturate(theta), 4);
    return float4(specular, 0);
}

float4 ApplyLinearFog(float4 pixel, float4 pos, float start, float end, float4 fogColor) {
    float depth = Depth.Sample(Sampler, (pos.xy + 0.5) / FrameSize);
    float f = saturate((((end - start) / FarClip) - depth) / ((end - start) / FarClip));
    //float f = saturate(1 / exp(pow(depth * 5, 2)));
    return f * pixel + (1 - f) * fogColor;
}

float4 psmain(PS_INPUT input) : SV_Target {
    //return float4(input.normal.zzz, 1);
    float3 viewDir = normalize(input.world - Eye);
    //float4 specular = Specular(LightDirection, viewDir, input.normal);
    //float4 specular = Specular(-viewDir, viewDir, input.normal);
    float4 lighting = lerp(1, max(0, input.col), LightingScale);
    float d = dot(input.normal, viewDir);
    lighting.rgb *= smoothstep(-0.005, -0.015, d); // remove lighting if surface points away from camera
    //return float4((input.normal + 1) / 2, 1);

    float4 base = Diffuse.Sample(Sampler, input.uv);
    float4 emissive = Emissive.Sample(Sampler, input.uv) * base;
    //float d = dot(input.normal, viewDir);
    //if (d > -0.001)
    //    lighting = 0;
    //return base * lighting;
    emissive.a = 0;
    
    if (HasOverlay) {
        // Apply supertransparency mask
        float mask = StMask.Sample(Sampler, input.uv2).r; // only need a single channel
        base *= mask.r > 0 ? (1 - mask.r) : 1;

        float4 src = Diffuse2.Sample(Sampler, input.uv2);
        
        float out_a = src.a + base.a * (1 - src.a);
        float3 out_rgb = src.a * src.rgb + (1 - src.a) * base.rgb;
        float4 diffuse = float4(out_rgb, out_a);
        
        if (diffuse.a < 0.01f)
            discard;
        
        // layer the emissive over the base emissive
        float4 emissive2 = Emissive2.Sample(Sampler, input.uv2) * diffuse;
        emissive2.a = 0;
        emissive2 += emissive * (1 - src.a); // mask the base emissive by the overlay alpha

        //lighting += specular * src.a * 0.125; // reduce specularity until specular maps are in
        //lighting = max(lighting, emissive); // lighting should always be at least as bright as the emissive texture
        lighting += emissive2 * 3;
        // Boost the intensity of single channel colors
        // 2 / 1 -> 2, 2 / 0.33 -> 6
        //emissive *= 1.0 / max(length(emissive), 0.5); // white -> 1, single channel -> 0.33
        //float multiplier = length(emissive.rgb); 
        //lighting.a = saturate(lighting.a);
        //output.Color = diffuse * lighting;
        return diffuse * lighting;
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

        //base.rgb *= emissive.rgb * 0.5;
        //output.Emissive = base * lighting;
        if (base.a < 0.01f)
            discard;
        
        return base * lighting;
        //return ApplyLinearFog(base * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));
    }
}
