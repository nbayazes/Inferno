#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 23), "\
    "RootConstants(b1, num32BitConstants = 9), "\
    "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t4, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1); // not used
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);

Texture2D Diffuse2 : register(t4);
Texture2D StMask : register(t5);
Texture2D Emissive2 : register(t6);
Texture2D Specular2 : register(t7);

SamplerState sampler0 : register(s0);

static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units
            
cbuffer FrameConstants : register(b0) {
    float4x4 ProjectionMatrix;
    float3 Eye;
    float3 LightDirection;
};

cbuffer InstanceConstants : register(b1) {
    float Time;
    float FrameTime;
    float2 Scroll, Scroll2;
    float LightingScale;
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
PS_INPUT VSLevel(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos, 1));
    //output.col = float4(input.col.rgb, 1);
    output.col = input.col;
    output.col.a = clamp(output.col.a, 0, 1);
    //output.uv = input.uv + Scroll * Time * 100;
    output.uv = input.uv + Scroll * Time * 100;
    output.uv2 = input.uv2 + Scroll2 * Time * 100;

    //float s = sin(3.14 * 4 / 2);
    //float c = cos(3.14 * 4 / 2);
    //float2x2 rotationMatrix = float2x2(c, -s, s, c);
    //output.uv2 = mul(output.uv, rotationMatrix); // this is not the right offset for sliding textures.
    //output.uv2 = mul(input.uv, rotationMatrix) + Scroll * Time * 100;
    //output.uv2 = mul(input.uv, rotationMatrix) + mul(Scroll, rotationMatrix) * Time * 100;
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

float4 PSLevel(PS_INPUT input) : SV_Target {
    //return float4(input.normal.zzz, 1);
    float3 viewDir = normalize(input.world - Eye);
    //float4 specular = Specular(LightDirection, viewDir, input.normal);
    float4 lighting = lerp(1, input.col, LightingScale);
    float4 specular = Specular(-viewDir, viewDir, input.normal);
    //float4 base;

    //// Distortion effect for waterfalls.
    //if (Distort) {
    //    float2 uv = input.uv;
    //    float3 pos = input.pos.xyz / 500;
    //    // todo: clamp the input of cos / sin, otherwise as time gets large behavior will change
    //    uv.x += (cos(Time + pos.y * 2 * PI + pos.z * 2 * PI) + 1.0) / 175.0;
    //    uv.y += (sin(Time + pos.x * 2 * PI + pos.x * 2 * PI) + 1.0) / 250.0;
    //    base = Diffuse.Sample(sampler0, uv);

    //    //float4 subsurface = Diffuse.Sample(sampler0, input.uv + 0.5 + Scroll * 0.25);
    //    //float brightness = (base.r + base.g + base.b) / 4; // only allow subsurface through on brighter portions
    //    //base.rgb = base - (brightness * subsurface) * 0.5;
    //}
    //else {
    //    base = Diffuse.Sample(sampler0, input.uv);
    //}
    
    float4 base = Diffuse.Sample(sampler0, input.uv);
    float4 emissive = Emissive.Sample(sampler0, input.uv) * base;
    emissive.a = 0;

    if (HasOverlay) {
        // Apply supertransparency mask
        float mask = StMask.Sample(sampler0, input.uv2).r; // only need a single channel
        base *= mask.r > 0 ? (1 - mask.r) : 1;

        float4 src = Diffuse2.Sample(sampler0, input.uv2);
        
        //if (src.a < 1) {
        //    //float4 s1 = float4(0, 0, 0, 1) * Diffuse2.Sample(sampler0, input.uv2 + float2(-2 / 64.0, -2 / 64.0)).a * 0.35;
        //    //float4 s2 = float4(0, 0, 0, 1) * Diffuse2.Sample(sampler0, input.uv2 + float2(-1 / 64.0, -1 / 64.0)).a * 0.30;
        //    //src += s1 + s2;
        //    src += float4(0, 0, 0, 1) * Diffuse2.Sample(sampler0, input.uv2 + float2(-1 / 64.0, -1 / 64.0)).a * 0.20; // bottom edge
        //    src += float4(0, 0, 0, 1) * Diffuse2.Sample(sampler0, input.uv2 + float2(1 / 64.0, 1 / 64.0)).a * 0.19;
        //    src += float4(0, 0, 0, 1) * Diffuse2.Sample(sampler0, input.uv2 + float2(1 / 64.0, -1 / 64.0)).a * 0.18;
        //    src += float4(0, 0, 0, 1) * Diffuse2.Sample(sampler0, input.uv2 + float2(-1 / 64.0, 1 / 64.0)).a * 0.17;
        //}
        
        float4 dst = base;
        float out_a = src.a + dst.a * (1 - src.a);
        float3 out_rgb = src.a * src.rgb + (1 - src.a) * dst.rgb;
        float4 diffuse = float4(out_rgb, out_a);
        
        if (diffuse.a < 0.05f)
            discard;
        
        // layer the emissive over the base emissive
        float4 emissive2 = Emissive2.Sample(sampler0, input.uv2) * diffuse;
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
        if (base.a < 0.02f)
            discard;
        return base * lighting;
    }
}
