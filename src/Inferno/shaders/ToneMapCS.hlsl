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
// HDR input with Typed UAV loads (32 bit -> 24 bit assignment) to a SDR output

#include "utility.hlsli"

#define RS \
    "RootConstants(b0, num32BitConstants = 4), " \
    "DescriptorTable(UAV(u0))," \
    "DescriptorTable(UAV(u1))," \
    "DescriptorTable(SRV(t0))," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

Texture2D<float3> Bloom : register(t0);

RWTexture2D<float3> ColorRW : register(u0);
RWTexture2D<float> OutLuma : register(u1);

SamplerState LinearSampler : register(s0);

cbuffer CB0 : register(b0) {
    float2 g_RcpBufferDim;
    float g_BloomStrength;
    float g_Exposure;
};


// The Reinhard tone operator.  Typically, the value of k is 1.0, but you can adjust exposure by 1/k.
// I.e. TM_Reinhard(x, 0.5) == TM_Reinhard(x * 2.0, 1.0)
float3 TM_Reinhard(float3 hdr, float k = 1.0) {
    return hdr / (hdr + k);
}

// The inverse of Reinhard
float3 ITM_Reinhard(float3 sdr, float k = 1.0) {
    return k * sdr / (k - sdr);
}

// This is the new tone operator.  It resembles ACES in many ways, but it is simpler to evaluate with ALU.  One
// advantage it has over Reinhard-Squared is that the shoulder goes to white more quickly and gives more overall
// brightness and contrast to the image.

float3 TM_Stanard(float3 hdr) {
    return TM_Reinhard(hdr * sqrt(hdr), sqrt(4.0 / 27.0));
}

float3 ITM_Stanard(float3 sdr) {
    return pow(ITM_Reinhard(sdr, sqrt(4.0 / 27.0)), 2.0 / 3.0);
}

[RootSignature(RS)]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    float2 TexCoord = (DTid.xy + 0.5) * g_RcpBufferDim;

    // Load HDR and bloom
    float3 hdrColor = ColorRW[DTid.xy];
    hdrColor += g_BloomStrength * Bloom.SampleLevel(LinearSampler, TexCoord, 0);
    hdrColor *= g_Exposure;

    // Tone map to SDR
    //ColorRW[DTid.xy] = TM_Stanard(hdrColor);
    ColorRW[DTid.xy] = hdrColor;


    // Load HDR and bloom
    //float3 hdrColor = ColorRW[DTid.xy];
    //hdrColor += TM_Stanard(g_BloomStrength * Bloom.SampleLevel(LinearSampler, TexCoord, 0));
    //hdrColor += g_BloomStrength * Bloom.SampleLevel(LinearSampler, TexCoord, 0);
    //hdrColor += saturate(g_BloomStrength * Bloom.SampleLevel(LinearSampler, TexCoord, 0)) * 0.25;
    //hdrColor *= g_Exposure;

    //ColorRW[DTid.xy] = hdrColor;

    //ColorRW[DTid.xy] += saturate(g_BloomStrength * Bloom.SampleLevel(LinearSampler, TexCoord, 0)) * g_Exposure;
    //ColorRW[DTid.xy] += hdrColor;

    //ColorRW[DTid.xy] = TM_Stanard(hdrColor);
}
