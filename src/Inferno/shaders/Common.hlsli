#ifndef __COMMON_H__
#define __COMMON_H__

#define MATERIAL_COUNT 4000

// Must match the Material enum
static const int MAT_DIFF = 0; // Diffuse
static const int MAT_MASK = 1; // Supertransparent mask
static const int MAT_EMIS = 2; // Emissive
static const int MAT_SPEC = 3; // Specular
static const int MAT_NORM = 4; // Normal

static const int VCLIP_RANGE = 10000;

struct FrameConstants {
    float4x4 ViewProjectionMatrix;
    float4x4 ViewMatrix;
    float3 Eye; // Camera position
    float Time; // elapsed game time in seconds
    float2 Size; // Frame width and height
    float NearClip, FarClip;
    float3 EyeDir; // Camera direction
    float GlobalDimming;
    float3 EyeUp; //Camera up
    bool NewLightMode; // dynamic light mode
    int FilterMode; // 0: Point, 1: AA point, 2: smooth - must match TextureFilterMode
    float RenderScale; // Resolution scaling
};

struct VClip {
    float PlayTime; // total time (in seconds) of clip
    int NumFrames; // Valid frames in Frames
    float FrameTime; // time (in seconds) of each frame
    int pad0;
    int Frames[30];
    int pad1, pad2;

    // Returns the frame for the vclip based on elapsed time
    int GetFrame(float time) {
        //if (NumFrames == 0) return 0;
        int frame = (int)floor(abs(time) / FrameTime) % NumFrames;
        return Frames[frame];
    }
};

static const int FILTER_POINT = 0;
static const int FILTER_ENHANCED_POINT = 1;
static const int FILTER_SMOOTH = 2;

//float Fix16ToFloat(min16int fix) {
//    return (float)fix / (float)(1 << 10);
//}

//float2 UnpackFloats(uint packed) {

//    return float2(Fix16ToFloat(min16int(packed >> 16)), Fix16ToFloat(min16int(packed & 0xFFFF)));
//}

// Samples a 2D texture. Also supports anti-aliasing pixel boundaries.
float4 Sample2D(Texture2D tex, float2 uv, SamplerState texSampler, int filterMode) {
    if (filterMode == FILTER_POINT)
        return tex.SampleLevel(texSampler, uv, 0); // always use biggest LOD in point mode
    if (filterMode == FILTER_SMOOTH)
        return tex.Sample(texSampler, uv); // Normal filtering for smooth mode

    // Point samples a texture with anti-aliasing along the pixel edges
    // https://www.shadertoy.com/view/csX3RH
    float width, height;
    tex.GetDimensions(width, height);
    float2 texsize = float2(width, height); // 64x64
    float2 uvTex = uv * texsize; // 0.1 * 64 -> 6.4
    float2 seam = floor(uvTex + .5); // 6.4 + .5 -> 6
    uvTex = (uvTex - seam) / fwidth(uvTex) + seam;

    uvTex = clamp(uvTex, seam - .5, seam + .5);
    return tex.SampleBias(texSampler, uvTex / texsize, -1); // Negative LOD bias reduces artifacting at low res
}

// Alternative implementation of Sample2D, no visible difference?
//float4 Sample2D__(Texture2D tex, float2 uv, SamplerState texSampler, int filterMode) {
//    if (filterMode == FILTER_POINT)
//        return tex.SampleLevel(texSampler, uv, 0); // always use biggest LOD in point mode
//    if (filterMode == FILTER_SMOOTH)
//        return tex.Sample(texSampler, uv); // Normal filtering for smooth mode

//    float width, height;
//    tex.GetDimensions(width, height);
//    float2 texsize = float2(width, height); // 64x64

//    float2 boxSize = clamp(fwidth(uv) * 1 * texsize, 1e-5, 1);
//    float2 tx = uv * texsize - 0.5 * boxSize;
//    float2 txOffset = smoothstep(1 - boxSize, 1, frac(tx));
//    float2 sampleuv = (floor(tx) + 0.5 + txOffset) / texsize;
//    return tex.SampleGrad(texSampler, sampleuv, ddx(uv), ddy(uv));
//}

float3 SampleNormal(Texture2D tex, float2 uv, SamplerState texSampler, int filterMode) {
    float3 color;

    if (filterMode == FILTER_POINT) {
        color = tex.SampleLevel(texSampler, uv, 0).rgb; // always use biggest LOD in point mode
    }
    else if (filterMode == FILTER_SMOOTH) {
        color = tex.Sample(texSampler, uv).rgb; // Normal filtering for smooth mode
    }
    else {
        // Point samples a texture with anti-aliasing along the pixel edges
        // https://www.shadertoy.com/view/csX3RH
        float width, height;
        tex.GetDimensions(width, height);
        float2 texsize = float2(width, height); // 64x64
        float2 uvTex = uv * texsize; // 0.1 * 64 -> 6.4
        float2 seam = floor(uvTex + .5); // 6.4 + .5 -> 6
        //seam.x = uvTex.x > 0.5 ? floor(uvTex.x - .125 / 2) : ceil(uvTex.x + .125 / 2);
        //seam.y = uvTex.y > 0.5 ? floor(uvTex.y - .125 / 2) : ceil(uvTex.y + .125 / 2);
        uvTex = (uvTex - seam) / (1.5 * fwidth(uvTex)) + seam;
        uvTex = clamp(uvTex, seam - .5, seam + .5);
        //uvTex = clamp(uvTex, seam, seam + .25);
        color = tex.SampleBias(texSampler, uvTex / texsize, -1).rgb; // Negative LOD bias reduces artifacting at low res
        //return color;
    }

    //color = Sample2D(tex, uv, texSampler, filterMode);

    float3 normal = clamp(color.rgb * 2 - 1, -1, 1); // expand
    normal.z = sqrt(1 - saturate(dot(normal.xy, normal.xy))); // convert mustard normals (no blue channel)
    return normal;
}

// 'rotated disco' sampling
//float2 dx = ddx(uv) * 0.25f;
//float2 dy = ddx(uv) * 0.25f;
//float3 color =
//    (tex.Sample(texSampler, uv - dx - dy).rgb + // top left
//     tex.Sample(texSampler, uv + dx - dy).rgb + // top right
//     tex.Sample(texSampler, uv + dx + dy).rgb + // bottom left
//     tex.Sample(texSampler, uv + dx + dy).rgb) * 0.25; // bottom right
//return clamp(color * 2 - 1, -1, 1);


float LinearizeDepth(float near, float far, float depth) {
    return near / (far + depth * (near - far));
}

// Exponential fog factor
float ExpFog(float depth, float density) {
    return 1 - exp(-depth * density);
}

float ExpFog2(float depth, float density) {
    return 1 - exp(-pow(depth * density, 2));
}

float Luminance(float3 v) {
    return dot(v, float3(0.2126f, 0.7152f, 0.0722f));
}

#define SPRITE_RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "CBV(b0),"\
    "RootConstants(b1, num32BitConstants = 7), " \
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t2), visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

#endif
