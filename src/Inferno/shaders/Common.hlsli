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
    float3 Eye; // Camera direction
    float Time; // elapsed game time in seconds
    float2 Size; // Frame width and height
    float NearClip, FarClip;
    float GlobalDimming;
    bool NewLightMode; // dynamic light mode
    int FilterMode; // 0: Point, 1: AA point, 2: smooth - must match TextureFilterMode
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
    float2 uvTex = uv * texsize;            // 0.1 * 64 -> 6.4
    float2 seam = floor(uvTex + .5);        // 6.4 + .5 -> 6
    uvTex = (uvTex - seam) / fwidth(uvTex) + seam;

    uvTex = clamp(uvTex, seam - .5, seam + .5);
    float4 color = tex.Sample(texSampler, uvTex / texsize);
    return color;
}

float3 SampleNormal(Texture2D tex, float2 uv, SamplerState texSampler) {
    // AA sampling causes artifacts on sharp highlights when using AA mode. Use plain point sampling instead.
    return clamp(tex.Sample(texSampler, uv).rgb * 2 - 1, -1, 1);
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
    