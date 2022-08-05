#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 19)"

static const float PI = 3.14159265f;
static const float PIDIV2 = PI / 2;
static const float GAME_UNIT = 20; // value of 1 UV tiling in game units
            
cbuffer FrameConstants : register(b0) {
    float4x4 ProjectionMatrix;
    float3 Eye;
};

struct LevelVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float depth : COLOR0;
};

/*
    Combined level shader
*/ 
[RootSignature(RS)]
PS_INPUT VSLevel(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos, 1));
    output.normal = input.normal;
    output.depth = output.pos.z / output.pos.w;
    return output;
}

float4 Specular(float3 lightDir, float3 eyeDir, float3 normal) {
    float3 r = reflect(lightDir, normal);
    float3 theta = dot(r, eyeDir);
    float3 specular = pow((theta), 2);
    return float4(specular, 0);
}


float linearizeDepth(in float depth) {
    float n = 0.1;
    float f = 90.0f;
    return n / (f - depth * (f - n)) * f;
}

struct PS_OUTPUT {
    //float4 Color : SV_Target0;
    float Depth : SV_Target0;
};

PS_OUTPUT PSLevel(PS_INPUT input) : SV_Target {
    PS_OUTPUT output;
    //float3 lightDir = normalize(float3(-1, -2, 0));
    //float4 color = float4(0.8, 0.8, 0.8, 1);
    //color *= 0.6 - dot(lightDir, input.normal) * 0.4;
    //color.a = 0.75;
    //output.Color = saturate(color);
    output.Depth = input.depth;
    return output;
}
