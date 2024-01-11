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

#include "Utility.hlsli"

#ifndef SUPPORT_TYPED_UAV_LOADS
#define SUPPORT_TYPED_UAV_LOADS 1
#endif

#define RS \
    "RootConstants(b0, num32BitConstants = 8), " \
    "DescriptorTable(UAV(u0))," \
    "DescriptorTable(UAV(u1))," \
    "DescriptorTable(SRV(t0))," \
    "DescriptorTable(SRV(t1))," \
    "DescriptorTable(SRV(t2))," \
    "DescriptorTable(SRV(t3))," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

Texture2D<float3> Bloom : register(t0);
Texture3D<float3> TonyMcMapfaceLUT : register(t1);
Texture2D<float3> Dirt : register(t2);

#if SUPPORT_TYPED_UAV_LOADS
RWTexture2D<float3> ColorRW : register(u0);
#else
RWTexture2D<uint> DstColor : register(u0);
Texture2D<float3> SrcColor : register(t3);
#endif

RWTexture2D<float> OutLuma : register(u1);

SamplerState LinearSampler : register(s0);

struct Constants {
    float2 RcpBufferDim;
    float BloomStrength;
    float Exposure;
    bool NewLightMode;
    int ToneMapper;
    bool EnableDirt;
    bool EnableBloom;
};

ConstantBuffer<Constants> Args : register(b0);

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

float Luminance(float3 v) {
    return dot(v, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ChangeLuma(float3 color, float lumaOut) {
    float luma = Luminance(color);
    return color * (lumaOut / luma);
}

float3 reinhard_extended_luminance(float3 v, float max_white_l) {
    float luma = Luminance(v);
    float numerator = luma * (1.0f + (luma / (max_white_l * max_white_l)));
    float destLuma = numerator / (1.0f + luma);
    return ChangeLuma(v, destLuma);
}

float3 GammaRamp(float3 color, float gamma) {
    return pow(color, 1 / gamma);
}

float3 Uncharted2ToneMapping(float3 color) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    float exposure = 2.0;
    color *= exposure;
    color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    color /= white;
    return color;
}

float3 lumaBasedReinhardToneMapping(float3 color, float gamma = 1) {
    float luma = Luminance(color);
    float toneMappedLuma = luma / (1. + luma);
    color *= toneMappedLuma / luma;
    color = pow(color, float3(1. / gamma, 1. / gamma, 1. / gamma));
    return color;
}

// https://github.com/h3r2tic/tony-mc-mapface
float3 TonyMcMapface(float3 stimulus) {
    // Apply a non-linear transform that the LUT is encoded with.
    const float3 encoded = stimulus / (stimulus + 1.0);

    // Align the encoded range to texel centers.
    const float LUT_DIMS = 48.0;
    const float3 uv = encoded * ((LUT_DIMS - 1.0) / LUT_DIMS) + 0.5 / LUT_DIMS;
    return TonyMcMapfaceLUT.SampleLevel(LinearSampler, uv, 0);
}

[RootSignature(RS)]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    float2 TexCoord = (DTid.xy + 0.5) * Args.RcpBufferDim;

    // Load HDR and bloom
#if SUPPORT_TYPED_UAV_LOADS
    float3 hdrColor = ColorRW[DTid.xy];
#else
    float3 hdrColor = SrcColor[DTid.xy];
#endif

    if (Args.EnableBloom) {
        float3 bloom = Args.BloomStrength * Bloom.SampleLevel(LinearSampler, TexCoord, 0);
        hdrColor += bloom;

        // todo: dirt texture needs to maintain aspect ratio when scaling
        if (Args.EnableDirt)
            hdrColor += Dirt.SampleLevel(LinearSampler, TexCoord, 0) * clamp(bloom * 20, 0, 4) * 0.4;
    }

    hdrColor *= Args.Exposure;

    float3 sdrColor = hdrColor;

    // Tone map to SDR
    if (Args.NewLightMode) {
        switch (Args.ToneMapper) {
            case 0:
                sdrColor = Uncharted2ToneMapping(hdrColor);
                break;
            case 1:
                sdrColor = TonyMcMapface(hdrColor);
                break;
            //case 2:
            //    toneMappedColor = reinhard_extended_luminance(hdrColor, 8.0);
            //    break;
        }

        // blend with the original color to preserve reds
        //float lum = Luminance(hdrColor);

        // Using a lower lum comparison results in more saturated colors but causes clipping
        const float3 whitepoint = float3(0.75, 0.75, 0.75);
        float lum = dot(sqrt(hdrColor), whitepoint);

        // lum = (hdrColor.r + hdrColor.b + hdrColor.g) / 3; // this renders lava correctly but clips very bright light
        // lowering the lower bound introduces more of the tone mapping, causing reds to be more pink
        // but also causes bright areas like reactor highlights to be smoother
        // it also causes bright white areas to blend more smoothly
        //float t0 = max(0, smoothstep(0.2, 0.4, lum));
        float t0 = max(0, smoothstep(0, 2, lum));
        sdrColor = sdrColor * t0 + hdrColor * (1 - t0); // slightly darker and more contrast in high ranges
    }

    sdrColor = pow(sdrColor, 1.0 / 2.2); // linear to sRGB

#if SUPPORT_TYPED_UAV_LOADS
    ColorRW[DTid.xy] = sdrColor;
#else
    DstColor[DTid.xy] = Pack_R11G11B10_FLOAT(sdrColor);
#endif
}
