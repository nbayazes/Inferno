#include "Lighting.hlsli"
#include "Common.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
    "RootConstants(b1, num32BitConstants = 11), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t5), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t6, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t10), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t14), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t15), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t11), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t12), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t13), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b2), "
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
// t11, t12, t13
StructuredBuffer<MaterialInfo> Materials : register(t14);
TextureCube Environment : register(t15);

//static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

struct InstanceConstants {
    float2 Scroll, Scroll2; // scrolling needs to be separate? or part of texture info
    float LightingScale; // for unlit mode
    bool Distort;
    bool IsOverlay;
    bool HasOverlay;
    int Tex1, Tex2;
    float EnvStrength;
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
    float3 viewDir = normalize(input.world - Frame.Eye);
    float2 uvs = Args.IsOverlay ? input.uv2 : input.uv;
    float4 diffuse = Sample2D(Diffuse, uvs, Sampler, Frame.FilterMode);
    float3 normal = SampleNormal(Normal1, uvs, Sampler, Frame.FilterMode);

    MaterialInfo mat1 = Materials[Args.Tex1];
    normal.xy *= mat1.NormalStrength;
    normal = normalize(normal);

    // 'automap' shader?
    //float2 fw = fwidth(input.uv);
    //float fwd = pow(1 + (fw.x * fw.x + fw.y * fw.y), 0.4) - 1;
    //float fxy = fw.x + fw.y / 4;
    //float gx = clamp(fwd * 100, 0.01, 4);
    //return float4(0, gx * 1, 0, 1);

    float specularMask = Sample2D(Specular1, uvs, Sampler, Frame.FilterMode).r;
    specularMask *= mat1.SpecularStrength;

    float emissive = Sample2D(Emissive, uvs, Sampler, Frame.FilterMode).r * mat1.EmissiveStrength;
    //float emissive = Sample2D(GetTexture(input.Tex1, MAT_EMIS), input.uv, Sampler, Frame.FilterMode).r * mat1.EmissiveStrength;
    MaterialInfo material = mat1;

    if (emissive > 0 && mat1.LightReceived == 0)
        emissive = emissive + 1; // make lava and forcefields full bright

    if (Args.HasOverlay) {
        float overlay = Sample2D(Diffuse2, input.uv2, Sampler, Frame.FilterMode).a;
        float mask = Sample2D(StMask, input.uv2, Sampler, Frame.FilterMode).r;
        //return float4(mask, 0, 0, 1);

        if (mask > 0 || overlay == 1)
            discard; // Don't draw opaque pixels under overlay
    }

    if (diffuse.a <= 0)
        discard; // discard transparent areas

    // align normals
    float3x3 tbn = float3x3(input.tangent, input.bitangent, input.normal);
    normal = normalize(mul(normal, tbn));

    //return ApplyLinearFog(base * lighting, input.pos, 10, 500, float4(0.25, 0.35, 0.75, 1));
    float3 lighting = float3(0, 0, 0);

    float3 ambient = max(0, input.col.rgb);
    ambient.rgb = pow(ambient.rgb, 2.2); // sRGB to linear
    ambient = lerp(1, ambient, Args.LightingScale);

    if (!Frame.NewLightMode) {
        lighting.rgb += ambient;
        lighting.rgb = saturate(Luminance(lighting.rgb)/* + emissive*/); // Desaturate
        return float4(diffuse.rgb * lighting.rgb * Frame.GlobalDimming, diffuse.a);
    }
    else {
        float3 directLight = float3(0, 0, 0);
        uint2 pixelPos = uint2(input.pos.xy);
        ambient *= Frame.GlobalDimming; // Dim ambient during self destruct
        emissive *= Frame.GlobalDimming;

        ShadeLights(directLight, pixelPos, diffuse.rgb, specularMask, normal, viewDir, input.world, material);
        lighting += directLight * material.LightReceived;
        lighting += emissive * diffuse.rgb; // emissive
        lighting += emissive * diffuse.rgb * ambient * material.LightReceived * .5; // also tint emissive by ambient
        lighting += diffuse.rgb * ambient * 0.20 * material.LightReceived * (1 - material.Metalness); // ambient
        lighting += ApplyAmbientSpecular(Environment, Sampler, Frame.EyeDir + viewDir, normal, material, ambient * .2, diffuse.rgb, specularMask, .15) * diffuse.a;
        return float4(lighting, diffuse.a);
    }
}
