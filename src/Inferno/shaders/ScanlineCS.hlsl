//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
#define RS \
    "RootConstants(b0, num32BitConstants = 2), " \
    "DescriptorTable(UAV(u0))," \
    "DescriptorTable(SRV(t0))," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

RWTexture2D<float4> Result : register(u0);

Texture2D<float4> Source : register(t0);

SamplerState Sampler : register(s0);

cbuffer cb0 {
    float2 g_inverseOutputSize;
}

float getCenterDistance(float2 coord) {
    return distance(coord, float2(0.45, 0.5)) * 2.0; //return difference between point on screen and the center with -1 and 1 at either edge
}

float4 Vignette(float4 color, float2 uv, float amount, float scale, float power) {
    float centerDist = getCenterDistance(uv);
    float darkenAmount = centerDist / scale; //get amount to darken current fragment by scaled distance from center
    darkenAmount = pow(darkenAmount, power); //bias darkenAmount towards edges of distance
    darkenAmount = min(1.0, darkenAmount); //clamp maximum darkenAmount to 1 so amount param can lighten outer regions of vignette 
    color.rgb -= darkenAmount * amount; //darken rgb colors by given vignette amount
    return color;
}

[RootSignature(RS)]
[numthreads(8, 8, 1)]
void main(uint3 dtID : SV_DispatchThreadID) {
    float2 uv = (dtID.xy + 0.5) * g_inverseOutputSize;
    
    float2 dc = abs(0.5 - uv);
    dc *= dc;
    float warp = 0.0;
    float scan = 0.4;
    
    // warp the fragment coordinates
    uv.x -= 0.5;
    uv.x *= 1.0 + (dc.y * (0.3 * warp));
    uv.x += 0.5;
    uv.y -= 0.5;
    uv.y *= 1.0 + (dc.x * (0.4 * warp));
    uv.y += 0.5;
    
        // sample inside boundaries, otherwise set to black
    if (uv.y > 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0) {
        Result[dtID.xy] = float4(0.0, 0.0, 0.0, 0.0);
    }
    else {
        float apply = abs(sin(dtID.y) * 0.5 * scan);
        float4 color = lerp(Source.SampleLevel(Sampler, uv, 0), float4(0, 0, 0, 0), apply);
        color *= 1.35;
        //Result[dtID.xy] = color;
        color = Vignette(color, uv, 2.0, 2.2, 2.7);
        Result[dtID.xy] = color;
        // Result[dtID.xy] = float4(uv.x, uv.y, 0 , 1);
        //Result[dtID.xy] = float4(distance(uv, float2(0.45, 0.5)) * 2.0, 0, 0, 1);
        
        //float number = 600;
        //float amount = 0.15;
        //float power = 2;
        ////float drift = 0.1;
        //float darkenAmount = 0.5 + 0.5 * cos(dtID.y * 6.28 * number); //get darken amount as cos wave between 0 and 1, over number of lines across height
        //darkenAmount = pow(darkenAmount, power); //bias darkenAmount towards wider light areas
        
        //float4 color = Source.SampleLevel(Sampler, uv, 0);
        //color.rgb -= darkenAmount * amount; //darken rgb colors by given gap darkness amount
        //Result[dtID.xy] = color;
    }
    //Result[dtID.xy] = Source.SampleLevel(Sampler, uv, 0);
}

