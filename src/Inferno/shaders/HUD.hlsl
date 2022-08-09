#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 23, visibility=SHADER_VISIBILITY_ALL), "\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0," \
        "filter = FILTER_MIN_MAG_MIP_POINT,"\
        "addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP,"\
        "comparisonFunc=COMPARISON_ALWAYS," \
        "borderColor=STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
        "visibility=SHADER_VISIBILITY_PIXEL)"

SamplerState Sampler : register(s0);
Texture2D Diffuse : register(t0);

cbuffer Constants : register(b0) {
    float4x4 ProjectionMatrix;
    float4 Color;
    float ScanlinePitch, ScanlineIntensity;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
            
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uvScreen : TEXCOORD1;
};
            
[RootSignature(RS)]
PS_INPUT vsmain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
    output.col = input.col * Color;
    output.uv = input.uv;
    output.uvScreen = (input.pos.xy) /* * float2(1 / 64.0, 1 / 64.0)*/; // todo from screen res
    return output;
}
            
float4 psmain(PS_INPUT input) : SV_Target {
    float2 uv = float2(0, 0);
    
    
    float4 color = Diffuse.SampleLevel(Sampler, input.uv + uv, 0) * input.col;
    if (ScanlinePitch > 0.1) {
        //dc *= dc;
        // warp the fragment coordinates
        //float2 dc = abs(0.5 - input.uvScreen);
        //float warp = 0.0;
        //uv.x -= 0.1;
        //uv.x *= 1.0 + (dc.y/* * (0.3 * warp)*/);
        //uv.x += 0.1;
        //uv.y -= 0.1;
        //uv.y *= 1.0 + (dc.x * (0.4 * warp));
        //uv.y += 0.1;
        
        const float offset = 0.0005;
        const float ins = 0.5;
        //color += Diffuse.SampleLevel(Sampler, input.uv + float2(offset, 0.00), 0) * float4(0.5, 0.5, 0.5, 0.01);
        //color += Diffuse.SampleLevel(Sampler, input.uv + float2(-offset, -0.00), 0) * float4(0.5, 0.5, 0.5, 0.01);
        color.a = saturate(color.a);
        float apply = abs(sin(input.uvScreen.y) * 0.5 * ScanlinePitch) * 0.5;
        color = lerp(color, float4(0, 0, 0, 0), apply);
        
        color.rgb += saturate(color.rgb - 0.5) * 2;
        //color.rgb *= 0.6;
    }
    //float4 color = lerp(Diffuse.SampleLevel(Sampler, input.uv + uv, 0), float4(0, 0, 0, 0), apply);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(0.01, 0.01), 0)  * float4(0.5, 0.5, 0.5, 0.01);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(-0.01, -0.01), 0) * float4(0.5, 0.5, 0.5, 0.01);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(0.02, 0.02), 0)  * float4(0.5, 0.5, 0.5, 0.01);
    //color += Diffuse.SampleLevel(Sampler, input.uv + float2(-0.02, -0.02), 0) * float4(0.5, 0.5, 0.5, 0.01);
    //color.rgb += lerp(saturate(color.rgb - 0.8) * 1, float3(0, 0, 0), apply);
    
    return color;
    //return input.col * Diffuse.Sample(Sampler, input.uv) * 2;
}