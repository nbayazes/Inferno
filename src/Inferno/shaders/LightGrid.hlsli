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
// Author(s):	Alex Nankervis
//

// keep in sync with C code
#define MAX_LIGHTS 512
#define TILE_HEADER_SIZE 12
#define TILE_SIZE (TILE_HEADER_SIZE + MAX_LIGHTS * 4)

struct LightData {
    float3 pos;
    float radius;

    float4 color; // color and intensity

    float3 pos2; // for tube lights
    float tubeRadius;

    float3 normal; // rectangular and cone lights
    uint type;

    float3 right; // rectangular light
    float _pad1;

    float3 up; // rectangular light
    float _pad2;

    //float3 coneDir;
    //float pad;
    //float2 coneAngles; // x = 1.0f / (cos(coneInner) - cos(coneOuter)), y = cos(coneOuter)

    //float4x4 shadowTextureMatrix;
};

uint2 GetTilePos(float2 pos, float2 invTileDim) {
    return pos * invTileDim;
}

uint GetTileIndex(uint2 tilePos, uint tileCountX) {
    return tilePos.y * tileCountX + tilePos.x;
}

uint GetTileOffset(uint tileIndex) {
    return tileIndex * TILE_SIZE;
}
