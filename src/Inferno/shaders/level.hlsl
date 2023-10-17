#include "Lighting.hlsli"
#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "RootConstants(b1, num32BitConstants = 9), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t6, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t10), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t14), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t11), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2), "\
    "StaticSampler(s2," \
        "filter = FILTER_MIN_POINT_MAG_MIP_LINEAR,"\
        "addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP,"\
        "visibility=SHADER_VISIBILITY_PIXEL)"
Texture2D TextureTable[] : register(t0, space1);
Texture2D Diffuse : register(t0);
//Texture2D StMask : register(t1); // not used but reserved for descriptor table
Texture2D Emissive : register(t2);
Texture2D Specular1 : register(t3);
Texture2D Normal1 : register(t4);

Texture2D Diffuse2 : register(t5);
Texture2D StMask : register(t6);
Texture2D Emissive2 : register(t7);
Texture2D Specular2 : register(t8);
Texture2D Normal2 : register(t9);

Texture2D Depth : register(t10);
SamplerState Sampler : register(s0);
SamplerState NormalSampler : register(s1);
SamplerState NormalSampler2 : register(s2);
// t11, t12, t13
StructuredBuffer<MaterialInfo> Materials : register(t14);

//static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

struct InstanceConstants {
    float2 Scroll, Scroll2; // scrolling needs to be separate? or part of texture info
    float LightingScale;    // for unlit mode
    bool Distort;
    bool HasOverlay;
    int Tex1, Tex2;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<InstanceConstants> Args : register(b1);

struct LevelVertex {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    nointerpolation int Tex1 : BASE;
    nointerpolation int Tex2 : OVERLAY;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    centroid float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float3 world : TEXCOORD2;
    //nointerpolation int Tex1 : BASE;
    //nointerpolation int Tex2 : OVERLAY;
};

/*
    Combined level shader
*/
[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(Frame.ViewProjectionMatrix, float4(input.pos, 1));
    //output.col = float4(input.col.rgb, 1);
    output.col = input.col;
    output.col.a = clamp(output.col.a, 0, 1);
    output.uv = input.uv + Args.Scroll * Frame.Time * 200;
    output.uv2 = input.uv2 + Args.Scroll2 * Frame.Time * 200;
    output.normal = input.normal;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.world = input.pos; // level geometry is already in world coordinates
    //output.Tex1 = input.Tex1;
    //output.Tex2 = input.Tex2;
    return output;
}

float4 Specular(float3 lightDir, float3 eyeDir, float3 normal, float power) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    float3 specular = pow(saturate(theta), power);
    return float4(specular, 0);
}

//float4 ApplyLinearFog(float4 pixel, float4 pos, float start, float end, float4 fogColor) {
//    float depth = Depth.Sample(LinearSampler, (pos.xy + 0.5) / Frame.Size).x;
//    float f = saturate((((end - start) / Frame.FarClip) - depth) / ((end - start) / Frame.FarClip));
//    //float f = saturate(1 / exp(pow(depth * 5, 2)));
//    return f * pixel + (1 - f) * fogColor;
//}

float rand(float2 co) {
    float dt = dot(co.xy, float2(12.9898, 78.233));
    float sn = fmod(dt, 3.14);
    return frac(sin(sn) * 43758.5453);
}

float rand2(float2 co) {
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

float hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z).x;
}

float Noise(float2 st) {
    float2 i = floor(st);
    float2 f = frac(st);

    // Four corners in 2D of a tile
    float a = rand(i);
    float b = rand(i + float2(1.0, 0.0));
    float c = rand(i + float2(0.0, 1.0));
    float d = rand(i + float2(1.0, 1.0));

    // Smooth Interpolation
    float2 u = smoothstep(0, 1, f);

    // Mix 4 coorners percentages
    return lerp(a, b, u.x) +
        (c - a) * u.y * (1.0 - u.x) +
        (d - b) * u.x * u.y;
}

float3 hash(float3 p) // replace this by something better
{
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));

    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float Noise3D(in float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);

    float3 u = f * f * (3.0 - 2.0 * f);

    return lerp(lerp(lerp(dot(hash(i + float3(0.0, 0.0, 0.0)), f - float3(0.0, 0.0, 0.0)),
                          dot(hash(i + float3(1.0, 0.0, 0.0)), f - float3(1.0, 0.0, 0.0)), u.x),
                     lerp(dot(hash(i + float3(0.0, 1.0, 0.0)), f - float3(0.0, 1.0, 0.0)),
                          dot(hash(i + float3(1.0, 1.0, 0.0)), f - float3(1.0, 1.0, 0.0)), u.x), u.y),
                lerp(lerp(dot(hash(i + float3(0.0, 0.0, 1.0)), f - float3(0.0, 0.0, 1.0)),
                          dot(hash(i + float3(1.0, 0.0, 1.0)), f - float3(1.0, 0.0, 1.0)), u.x),
                     lerp(dot(hash(i + float3(0.0, 1.0, 1.0)), f - float3(0.0, 1.0, 1.0)),
                          dot(hash(i + float3(1.0, 1.0, 1.0)), f - float3(1.0, 1.0, 1.0)), u.x), u.y), u.z);
}

// Samples a texture randomly to make tiling less obvious
float3 TextureNoTile(Texture2D tex, float2 uv, float3 pos, float v) {
    //float k = texture(iChannel1, 0.005 * x).x; // cheap (cache friendly) lookup

    float2 duvdx = ddx(uv);
    float2 duvdy = ddy(uv);

    float k = Noise3D(pos);
    float l = k * 8.0;
    float f = frac(l);

    float ia = floor(l); // my method
    float ib = ia + 1.0;

    float2 offa = sin(float2(3.0, 7.0) * ia); // can replace with any other hash
    float2 offb = sin(float2(3.0, 7.0) * ib); // can replace with any other hash

    //float3 cola = tex.SampleGrad(LinearSampler, uv + v * offa, duvdx, duvdy);
    //float3 colb = tex.SampleGrad(LinearSampler, uv + v * offb, duvdx, duvdy);
    //float3 cola = Sample2DAA(tex, uv + v * offa);
    //float3 colb = Sample2DAA(tex, uv + v * offb);
    float3 cola = tex.Sample(Sampler, uv + v * offa).rgb;
    float3 colb = tex.Sample(Sampler, uv + v * offb).rgb;

    float3 diff = cola - colb;
    float sum = diff.x + diff.y + diff.z;
    return lerp(cola, colb, smoothstep(0.2, 0.8, f - 0.1 * sum));
}

void AntiAliasSpecular(inout float3 texNormal, inout float gloss) {
    float normalLenSq = dot(texNormal, texNormal);
    float invNormalLen = rsqrt(normalLenSq);
    texNormal *= invNormalLen;
    float normalLen = normalLenSq * invNormalLen;
    float flatness = saturate(1 - abs(ddx(normalLen)) - abs(ddy(normalLen)));
    gloss = exp2(lerp(0, log2(gloss), flatness));
}

Texture2D GetTexture(int index, int slot) {
    return TextureTable[index * 5 + slot];
}

float4 psmain(PS_INPUT input) : SV_Target {
    //return float4(input.normal.zzz, 1);
    float3 viewDir = normalize(input.world - Frame.Eye);
    // adding noise fixes dithering, but this is expensive. sample a noise texture instead
    //specular.rgb *= 1 + rand(input.uv * 5) * 0.1;

    float4 base = Sample2D(Diffuse, input.uv, Sampler, Frame.FilterMode);
    float3 normal = SampleNormal(Normal1, input.uv, Sampler);
    //float4 base = Sample2D(GetTexture(input.Tex1, MAT_DIFF), input.uv, Sampler, Frame.FilterMode);
    //float3 normal = SampleNormal(GetTexture(input.Tex1, MAT_NORM), input.uv, NormalSampler);

    //return float4(normal, 1);
    MaterialInfo mat1 = Materials[Args.Tex1];
    //MaterialInfo mat1 = Materials[input.Tex1];
    normal.xy *= mat1.NormalStrength;
    normal = normalize(normal);
    //return float4(normal, 1);

    // 'automap' shader?
    //float2 fw = fwidth(input.uv);
    //float fwd = pow(1 + (fw.x * fw.x + fw.y * fw.y), 0.4) - 1;
    //float fxy = fw.x + fw.y / 4;
    //float gx = clamp(fwd * 100, 0.01, 4);
    //return float4(0, gx * 1, 0, 1);

    float specularMask = Sample2D(Specular1, input.uv, Sampler, Frame.FilterMode).r;
    //float specularMask = Sample2D(GetTexture(input.Tex1, MAT_SPEC), input.uv, Sampler, Frame.FilterMode).r;

    //float3 normal = clamp(Normal1.Sample(Sampler, input.uv).rgb * 2 - 1, -1, 1); // map from 0..1 to -1..1
    //float3 normal = Normal1.SampleLevel(Sampler, input.uv, 1).rgb;
    //float normalStrength = 0.60;

    //return float4(normal * 0.5 + 0.5, 1);
    //normal = input.normal;
    //return float4(input.normal * 0.5 + 0.5, 1);

    //base.rgb = TextureNoTile(Diffuse, input.uv, input.world / 20, 1);
    //base += base * Sample2DAA(Specular1, input.uv) * specular * 1.5;
    float4 diffuse = base;

    float emissive = Sample2D(Emissive, input.uv, Sampler, Frame.FilterMode).r * mat1.EmissiveStrength;
    //float emissive = Sample2D(GetTexture(input.Tex1, MAT_EMIS), input.uv, Sampler, Frame.FilterMode).r * mat1.EmissiveStrength;
    MaterialInfo material = mat1;

    if (Args.HasOverlay) {
        MaterialInfo mat2 = Materials[Args.Tex2];
    //if (input.Tex2 > 0) {
    //    MaterialInfo mat2 = Materials[input.Tex2];

        // Apply supertransparency mask
        float mask = 1 - Sample2D(StMask, input.uv2, Sampler, Frame.FilterMode).r; // only need a single channel
        //float mask = 1 - Sample2D(GetTexture(input.Tex2, MAT_MASK), input.uv2, Sampler, Frame.FilterMode).r; // only need a single channel
        base *= mask;

        float4 overlay = Sample2D(Diffuse2, input.uv2, Sampler, Frame.FilterMode); // linear sampler causes artifacts
        //float4 overlay = Sample2D(GetTexture(input.Tex2, MAT_DIFF), input.uv2, Sampler, Frame.FilterMode); // linear sampler causes artifacts
        float out_a = overlay.a + base.a * (1 - overlay.a);
        float3 out_rgb = overlay.a * overlay.rgb + (1 - overlay.a) * base.rgb;
        diffuse = float4(out_rgb, out_a);
        emissive *= 1 - overlay.a; // Remove covered portion of emissive

        // AA sampling causes artifacts on sharp highlights. Use plain point sampling instead.
        //float3 overlayNormal = clamp(SampleData2D(Normal2, input.uv2, Sampler, Frame.FilterMode).rgb * 2 - 1, -1, 1);
        //float3 overlayNormal = SampleNormal(GetTexture(input.Tex2, MAT_NORM), input.uv2, NormalSampler);
        //return float4(pow(overlayNormal * 0.5 + 0.5, 2.2), 1);
        float3 overlayNormal = SampleNormal(Normal2, input.uv2, Sampler);
        overlayNormal.xy *= mat2.NormalStrength;
        overlayNormal = normalize(overlayNormal);

        normal = normalize(lerp(normal, overlayNormal, overlay.a));

        material.SpecularStrength = lerp(mat1.SpecularStrength, mat2.SpecularStrength, overlay.a);
        material.Metalness = lerp(mat1.Metalness, mat2.Metalness, overlay.a);
        material.NormalStrength = normalize(lerp(mat1.NormalStrength, mat2.NormalStrength, overlay.a));
        material.Roughness = lerp(mat1.Roughness, mat2.Roughness, overlay.a);
        material.LightReceived = lerp(mat1.LightReceived, mat2.LightReceived, overlay.a);

        float overlaySpecularMask = Sample2D(Specular2, input.uv2, Sampler, Frame.FilterMode).r;
        //float overlaySpecularMask = Sample2D(GetTexture(input.Tex2, MAT_SPEC), input.uv2, Sampler, Frame.FilterMode).r;
        specularMask = lerp(specularMask, overlaySpecularMask, overlay.a);
        // layer the emissive over the base emissive
        emissive += Sample2D(Emissive2, input.uv2, Sampler, Frame.FilterMode).r * mat2.EmissiveStrength * overlay.a;
        //emissive += Sample2D(GetTexture(input.Tex2, MAT_EMIS), input.uv2, Sampler, Frame.FilterMode).r * mat2.EmissiveStrength * overlay.a;
    }

    if (emissive > 0 && mat1.LightReceived == 0) 
        emissive = emissive + 1; // make lava and forcefields full bright

    // Use <= 0 to use cutout edge AA, but it introduces artifacts. < 1 causes aliasing.
    if (diffuse.a <= 0)
        discard; // discarding speeds up large transparent walls

    // align normals
    float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
    normal = normalize(mul(normal, tbn));
    //return float4(normal, 1);

    //return ApplyLinearFog(base * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));
    float3 lighting = float3(0, 0, 0);

    float3 vertexLighting = max(0, input.col.rgb);
    vertexLighting = lerp(1, vertexLighting, Args.LightingScale);
    vertexLighting = pow(vertexLighting, 2.2); // sRGB to linear

    if (!Frame.NewLightMode) {
        lighting.rgb += vertexLighting;
        return float4(diffuse.rgb * lighting.rgb * Frame.GlobalDimming, diffuse.a);
    }
    else {
        float3 colorSum = float3(0, 0, 0);
        uint2 pixelPos = uint2(input.pos.xy);
        //return float4(specularMask, specularMask, specularMask, 1);
        //normal = input.normal; // debug
        //diffuse.rgb = 0.5; // debug
        //specularMask = 0; // debug
        ShadeLights(colorSum, pixelPos, diffuse.rgb, specularMask, normal, viewDir, input.world, material);
        //float flatness = saturate(1 - abs(ddx(colorSum)) - abs(ddy(colorSum)));
        //gloss = exp2(lerp(0, log2(gloss), flatness));
        //colorSum *= flatness;
        lighting += colorSum * material.LightReceived;
        lighting += emissive * diffuse.rgb;
        lighting += emissive * diffuse.rgb * vertexLighting * material.LightReceived;
        lighting += diffuse.rgb * vertexLighting * 0.20 * material.LightReceived; // ambient

        //lighting.rgb += vertexLighting * 1.0;
        //lighting.rgb = max(lighting.rgb, vertexLighting * 0.40);
        //lighting.rgb = clamp(lighting.rgb, 0, float3(1, 1, 1) * 1.8);
        return float4(lighting * Frame.GlobalDimming, diffuse.a);
    }
}
