static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units

SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);
            
cbuffer vertexBuffer : register(b0)
{
    float4x4 ProjectionMatrix;
};

cbuffer constsBuffer : register(b1)
{
    float Time;
    float FrameTime;
    float2 Scroll;
    bool Distort;
    bool Overlay;
};

struct VS_INPUT
{
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
            
struct PS_INPUT
{
    float4 screen : SV_POSITION;
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
            
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    output.pos = float4(input.pos.xyz, 1);
    output.screen = mul(ProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col;
    output.uv = input.uv + Scroll;
    return output;
}


float4 PSMain(PS_INPUT input) : SV_Target
{
    // Distortion effect for waterfalls. Assumes they are mostly vertical (Y axis).
    if (Distort)
    {
        float2 uv = input.uv;
        float3 pos = input.pos.xyz / GAME_UNIT;
        uv.x += (cos(Time + pos.y * 2 * PI + pos.z * 2 * PI) + 1.0) / 150.0;
        uv.y += (sin(Time + pos.x * 2 * PI + pos.x * 2 * PI) + 1.0) / 125.0;
        
        // make a little less uniform
        //uv.x += (cos(pos.y * 4 * PI + pos.z * 4 * PI)) / 500.0;
        //uv.y += (sin(pos.x * 4 * PI + pos.x * 4 * PI)) / 400.0;
        //float n = noise(uv);
        float4 subsurface = texture0.Sample(sampler0, input.uv + 0.5 + Scroll * 0.25);
        float4 base = texture0.Sample(sampler0, uv);
        float brightness = (base.r + base.g + base.b) / 3; // only allow subsurface through on brighter portions
        return base + (brightness * subsurface);
        
        // 1 - (1 - base) * (1 - (subsurface));
        //return lerp(texture0.Sample(sampler0, uv), texture0.Sample(sampler0, input.uv + 0.5 + Scroll * 0.25), 0.5);
        
        // kinda trippy screen space distortion, not very useful
        //float2 uv = input.uv;
        //uv.x += 0.05 * (cos(input.pos.y / 50.0) + 2.0);
        //uv.y += 0.05 * (sin(input.pos.x / 50.0) + 2.0);
        // simple sine distortion
        // uv.x += (cos(uv.y * 2 * PI) + 1.0) / 16.0;
        
        //float4 base = texture0.Sample(sampler0, uv);
        
        //float2 uv2 = input.uv;
        //uv2.x += (sin((input.pos.y + 10) / 20 * 2 * PI) + 1.0) / 64.0;
        
        //float2 uv2 = input.uv * 2 + 0.5;
        //uv2.x += (sin((input.pos.y + 10) / 20 * 2 * PI * 2) + 1.0) / 64.0;
        //uv2.x += (cos(input.pos.y / 20 * 2 * PI) + 1.0) / 16.0;
        //uv2.y += (sin(input.pos.x / 20 * 2 * PI) + 1.0) / 16.0;
        //float4 overlay = texture0.Sample(sampler0, uv2);
        //overlay.a *= 0.5;
        //uv.x += 1;
        //uv.y += 0.05 * (sin(uv.x) + 2.0);
        //return lerp(base, overlay, 0.75);
        //return base + overlay;
        //return base * 0.8 + overlay * 0.3;
    }
    else
    {
        return texture0.Sample(sampler0, input.uv) * input.col;
    }
}

struct VS_INPUT2
{
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
};

struct PS_INPUT2
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float4 col : COLOR0;
};

PS_INPUT2 VSMultiTexture(VS_INPUT2 input)
{
    PS_INPUT2 output;
    output.pos = mul(ProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col;
    output.uv = input.uv;
    output.uv2 = input.uv2;
    return output;
}

float4 PSMultiTexture(PS_INPUT2 input) : SV_Target
{
    float4 overlay = texture1.Sample(sampler0, input.uv2) * input.col;
    float4 base = texture0.Sample(sampler0, input.uv) * input.col;
    // supertransparency mask. If overlay texture transparency is 0.5, see through base texture
    const float a = overlay.a;
    int superMask = a < 0.45 || a > 0.55; // need to pass a flag to enable super mask... causes problems with smooth alpha
    //overlay.a = step(0.5, overlay.a);
    //overlay.a = overlay.a * overlay.a;
    // int superMask = step(a, 0.4) || step(0.60, a);
    //return lerp(base, overlay, a) * superMask;
    //float3 rgb = lerp(base.rgb, overlay.rgb, overlay.a);
    // return float4(rgb, 1);
    float4 src = overlay;
    float4 dst = base;
    // non-premultiplied alpha blending
    //float out_a = src.a + dst.a * (1 - src.a);
    //float3 out_rgb = (src.rgb * src.a + dst.rgb * dst.a * (1 - src.a)) / out_a;
    //return float4(out_rgb, out_a);
    float out_a = src.a + dst.a * (1 - src.a);
    float3 out_rgb = src.a * src.rgb + (1 - src.a) * dst.rgb;
    return float4(out_rgb, out_a);
}

/*
    Line shaders
*/
struct VS_LINE
{
    float3 pos : POSITION;
    float4 col : COLOR0;
};
            
struct PS_LINE
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};


PS_LINE VSLine(VS_LINE input)
{
    PS_LINE output;
    output.pos = mul(ProjectionMatrix, float4(input.pos.xyz, 1));
    output.col = input.col;
    return output;
}

float4 PSLine(PS_LINE input) : SV_Target
{
    return input.col;
}