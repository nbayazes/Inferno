//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//
// The CS for extracting bright pixels and downsampling them to an unblurred bloom buffer.


#include "utility.hlsli"

#define RS \
    "RootConstants(b0, num32BitConstants = 6), " \
    "DescriptorTable(UAV(u0))," \
    "DescriptorTable(UAV(u1))," \
    "DescriptorTable(SRV(t0))," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"
//"DescriptorTable(SRV(t1))," \

RWTexture2D<float3> BloomResult : register(u0);
RWTexture2D<uint> LumaResult : register(u1);

Texture2D<float3> SourceTex : register(t0);

SamplerState BiLinearClamp : register(s0);

cbuffer cb0 {
    float2 g_inverseOutputSize;
    float g_BloomThreshold;
    float g_Exposure;
    float g_MinLog;
    float g_RcpLogRange;
}

float CustomLuminance(float3 x) {
    return dot(x, float3(0.299, 0.587, 0.114)); // Digital ITU BT.601
}

float ChannelMult(float3 c) {
    //float mult = 4 / (1 + c.r + c.g + c.b);
    //return 0.25 / (1 + c) / mult;
    //c = clamp(c, 0, 60);
    return 7 / (1 + c.r * 2 + c.g * 2 + c.b * 2);

    //return 7 / (1 + c.r * c.r + c.g * c.g + c.b * c.b);
    // return 20 / (1 + 10*c);

}

[RootSignature(RS)]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // We need the scale factor and the size of one pixel so that our four samples are right in the middle
    // of the quadrant they are covering.
    float2 uv = (DTid.xy + 0.5) * g_inverseOutputSize;
    float2 offset = g_inverseOutputSize * 0.25;

    // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    float3 color1 = SourceTex.SampleLevel(BiLinearClamp, uv + float2(-offset.x, -offset.y), 0);
    float3 color2 = SourceTex.SampleLevel(BiLinearClamp, uv + float2(offset.x, -offset.y), 0);
    float3 color3 = SourceTex.SampleLevel(BiLinearClamp, uv + float2(-offset.x, offset.y), 0);
    float3 color4 = SourceTex.SampleLevel(BiLinearClamp, uv + float2(offset.x, offset.y), 0);

    //float luma1 = RGBToLuminance(color1);
    //float luma2 = RGBToLuminance(color2);
    //float luma3 = RGBToLuminance(color3);
    //float luma4 = RGBToLuminance(color4);

    //float luma1 = RGBToLogLuminance(color1);
    //float luma2 = RGBToLogLuminance(color2);
    //float luma3 = RGBToLogLuminance(color3);
    //float luma4 = RGBToLogLuminance(color4);

    float luma1 = CustomLuminance(color1);
    float luma2 = CustomLuminance(color2);
    float luma3 = CustomLuminance(color3);
    float luma4 = CustomLuminance(color4);

    // boost single channel colors
    //color1 *= ChannelMult(color1);
    //color2 *= ChannelMult(color2);
    //color3 *= ChannelMult(color3);
    //color4 *= ChannelMult(color4);

    //float luma1 = (color1.r + color1.g + color1.b) / 3;
    //float luma2 = (color2.r + color2.g + color2.b) / 3;
    //float luma3 = (color3.r + color3.g + color3.b) / 3;
    //float luma4 = (color4.r + color4.g + color4.b) / 3;

    const float kSmallEpsilon = 0.0001;

    float ScaledThreshold = g_BloomThreshold * g_Exposure; // BloomThreshold / Exposure

    // We perform a brightness filter pass, where lone bright pixels will contribute less.
    color1 *= max(kSmallEpsilon, luma1 - ScaledThreshold) / (luma1 + kSmallEpsilon);
    color2 *= max(kSmallEpsilon, luma2 - ScaledThreshold) / (luma2 + kSmallEpsilon);
    color3 *= max(kSmallEpsilon, luma3 - ScaledThreshold) / (luma3 + kSmallEpsilon);
    color4 *= max(kSmallEpsilon, luma4 - ScaledThreshold) / (luma4 + kSmallEpsilon);
    

    // The shimmer filter helps remove stray bright pixels from the bloom buffer by inversely weighting
    // them by their luminance.  The overall effect is to shrink bright pixel regions around the border.
    // Lone pixels are likely to dissolve completely.  This effect can be tuned by adjusting the shimmer
    // filter inverse strength.  The bigger it is, the less a pixel's luminance will matter.
    const float kShimmerFilterInverseStrength = 1.0f;
    float weight1 = 1.0f / (luma1 + kShimmerFilterInverseStrength);
    float weight2 = 1.0f / (luma2 + kShimmerFilterInverseStrength);
    float weight3 = 1.0f / (luma3 + kShimmerFilterInverseStrength);
    float weight4 = 1.0f / (luma4 + kShimmerFilterInverseStrength);
    float weightSum = weight1 + weight2 + weight3 + weight4;

    BloomResult[DTid.xy] = (color1 * weight1 + color2 * weight2 + color3 * weight3 + color4 * weight4) / weightSum;

    float luma = (luma1 + luma2 + luma3 + luma4) * 0.25;

    // Prevent log(0) and put only pure black pixels in Histogram[0]
    if (luma == 0.0) {
        LumaResult[DTid.xy] = 0;
    }
    else {
        float logLuma = saturate((log2(luma) - g_MinLog) * g_RcpLogRange); // Rescale to [0.0, 1.0]
        LumaResult[DTid.xy] = logLuma * 254.0 + 1.0; // Rescale to [1, 255]
    }
}
