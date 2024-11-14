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
// The CS for downsampling 16x16 blocks of pixels down to 8x8, 4x4, 2x2, and 1x1 blocks.

#define RS \
    "RootConstants(b0, num32BitConstants = 2), " \
    "DescriptorTable(UAV(u0))," \
    "DescriptorTable(SRV(t0))," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

struct Arguments {
    float2 InverseDimensions;
};


ConstantBuffer<Arguments> Args : register(b0);
Texture2D<float3> Source : register(t0);
RWTexture2D<float3> Result : register(u0);
SamplerState BiLinearClamp : register(s0);


groupshared float3 g_Tile[64]; // 8x8 input pixels

[RootSignature(RS)]
[numthreads(8, 8, 1)]
void main(uint GI : SV_GroupIndex, uint3 DTid : SV_DispatchThreadID) {
    // You can tell if both x and y are divisible by a power of two with this value
    uint parity = DTid.x | DTid.y;

    // Downsample and store the 8x8 block
    float2 centerUV = (float2(DTid.xy) * 2.0f + 1.0f) * Args.InverseDimensions;
    float3 avgPixel = Source.SampleLevel(BiLinearClamp, centerUV, 0.0f);
    g_Tile[GI] = avgPixel;
    Result[DTid.xy] = avgPixel;
}
