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

#include "LightGrid.hlsli"

#define RS \
    "RootFlags(0), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0))," \
    "DescriptorTable(SRV(t1))," \
    "DescriptorTable(UAV(u0))," \
    "DescriptorTable(UAV(u1))"

struct Arguments {
    uint ViewportWidth, ViewportHeight;
    float InvTileDim; // 1 / LIGHT_GRID_DIM = 1 / 16
    float RcpZMagic; // near / (far - near)
    uint TileCountX;
    float4x4 ViewMatrix;
    float4x4 InverseProjection;
};

ConstantBuffer<Arguments> Args : register(b0);
StructuredBuffer<LightData> Lights : register(t0);
Texture2D<float> depthTex : register(t1);
RWByteAddressBuffer lightGrid : register(u0);
RWByteAddressBuffer lightGridBitMask : register(u1);

#define BLOCK_SIZE 8
#define WORK_GROUP_SIZE_X 16
#define WORK_GROUP_SIZE_Y 16
#define WORK_GROUP_SIZE_Z 1

#define FLT_MIN         1.175494351e-38F        // min positive value
#define FLT_MAX         3.402823466e+38F        // max value
#define PI				3.1415926535f
#define TWOPI			6.283185307f

groupshared uint minDepthUInt;
groupshared uint maxDepthUInt;

groupshared uint pointLightCount;
groupshared uint tubeLightCount;
groupshared uint rectLightCount;

groupshared uint pointLightIndices[MAX_LIGHTS];
groupshared uint tubeLightIndices[MAX_LIGHTS];
groupshared uint rectLightIndices[MAX_LIGHTS];

//groupshared uint4 tileLightBitMask;

struct Plane {
    float3 N; // Plane normal.
    float d; // Distance to origin.
};

struct Frustum {
    Plane planes[4]; // left, right, top, bottom frustum planes.
};

// Convert screen space coordinates to view space.
float3 ScreenToView(float4 screen) {
    // Convert to normalized texture coordinates
    float2 texCoord = screen.xy / float2(Args.ViewportWidth, Args.ViewportHeight);
    texCoord.y = 1 - texCoord.y; // flip y axis
    // Convert to clip space. * 2 - 1 transforms from -1, 1 to 0 1
    float4 clip = float4(texCoord * 2.0f - 1.0f, screen.z, screen.w);
    float4 view = mul(Args.InverseProjection, clip);
    return view.xyz / view.w;
}

// Compute a plane from 3 noncollinear points that form a triangle.
// This equation assumes a right-handed (counter-clockwise winding order) 
// coordinate system to determine the direction of the plane normal.
Plane ComputePlane(float3 p0, float3 p1, float3 p2) {
    Plane plane;
    float3 v0 = p1 - p0;
    float3 v2 = p2 - p0;
    plane.N = normalize(cross(v0, v2));

    // Compute the distance to the origin using p0.
    //plane.d = dot(plane.N, p0);
    plane.d = 0; // view plane always crosses origin
    return plane;
}

float PlaneDist(Plane plane, float3 pos) {
    return dot(plane.N, pos) - plane.d;
}

bool SphereBehindPlane(float3 pos, float radius, Plane plane) {
    return dot(plane.N, pos) - plane.d < -radius;
}

[RootSignature(RS)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(uint2 group : SV_GroupID,
          uint2 groupThread : SV_GroupThreadID,
          uint groupIndex : SV_GroupIndex,
          uint3 dispatchThreadID : SV_DispatchThreadID) {
    // initialize shared data
    if (groupIndex == 0) {
        pointLightCount = 0;
        tubeLightCount = 0;
        rectLightCount = 0;
        //tileLightBitMask = 0;
        minDepthUInt = 0xffffffff;
        maxDepthUInt = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // Read all depth values for this tile and compute the tile min and max values
    for (uint dx = groupThread.x; dx < WORK_GROUP_SIZE_X; dx += BLOCK_SIZE) {
        for (uint dy = groupThread.y; dy < WORK_GROUP_SIZE_Y; dy += BLOCK_SIZE) {
            uint2 DTid = group * uint2(WORK_GROUP_SIZE_X, WORK_GROUP_SIZE_Y) + uint2(dx, dy);

            // If pixel coordinates are in bounds...
            if (DTid.x < Args.ViewportWidth && DTid.y < Args.ViewportHeight) {
                // Load and compare depth
                uint depthUInt = asuint(depthTex[DTid.xy]);
                InterlockedMin(minDepthUInt, depthUInt);
                InterlockedMax(maxDepthUInt, depthUInt);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Compute the 4 corner points on the far clipping plane to use as the 
    // frustum vertices.
    const float z = -1.0f;
    const float w = 1.0f;
    float4 screenSpace[4];
    // Top left point
    screenSpace[0] = float4(float2(group.x, group.y) * WORK_GROUP_SIZE_X, z, w);
    // Top right point
    screenSpace[1] = float4(float2(group.x + 1, group.y) * WORK_GROUP_SIZE_X, z, w);
    // Bottom left point
    screenSpace[2] = float4(float2(group.x, group.y + 1) * WORK_GROUP_SIZE_X, z, w);
    // Bottom right point
    screenSpace[3] = float4(float2(group.x + 1, group.y + 1) * WORK_GROUP_SIZE_X, z, w);

    float3 viewSpace[4];
    // Now convert the screen space points to view space
    for (int i = 0; i < 4; i++) {
        viewSpace[i] = ScreenToView(screenSpace[i]);
    }

    // View space eye position is always at the origin.
    const float3 eyePos = float3(0, 0, 0);

    Plane planes[4]; // planes are in view space
    planes[0] = ComputePlane(eyePos, viewSpace[2], viewSpace[0]); // Left plane
    planes[1] = ComputePlane(eyePos, viewSpace[1], viewSpace[3]); // Right plane
    planes[2] = ComputePlane(eyePos, viewSpace[0], viewSpace[1]); // Top plane
    planes[3] = ComputePlane(eyePos, viewSpace[3], viewSpace[2]); // Bottom plane

    float tileMinDepth = asfloat(minDepthUInt);
    float tileMaxDepth = asfloat(maxDepthUInt);
    float zFar = tileMaxDepth / Args.RcpZMagic;
    float zNear = tileMinDepth / Args.RcpZMagic;
    zNear = max(zNear, FLT_MIN); // don't allow a zNear of 0
    uint tileIndex = GetTileIndex(group.xy, Args.TileCountX);
    uint tileOffset = GetTileOffset(tileIndex);

    // find set of lights that overlap this tile
    for (uint lightIndex = groupIndex; lightIndex < MAX_LIGHTS; lightIndex += BLOCK_SIZE * BLOCK_SIZE) {
        LightData lightData = Lights[lightIndex];
        //float3 lightWorldPos = lightData.pos;
        //lightWorldPos = float3(0, 0, 0); // makes all pass the plane check
        float lightRadius = lightData.radius;
        bool inside = true;

        // project light from world to view space (depth is zNear to zFar)
        float3 lightPos = mul(Args.ViewMatrix, float4(lightData.pos, 1)).xyz;

        if (lightData.radius > 0 && lightData.type == 2) {
            // extend radius by largest width
            lightRadius += max(length(lightData.right), length(lightData.up));
        }

        // cull the light if is behind the camera (negative z is behind) or too far
        if (lightPos.z + lightRadius < zNear || lightPos.z - lightRadius > zFar) {
            inside = false;
        }

        for (int i = 0; i < 4; i++) {
            Plane plane = planes[i]; // planes are in view space
            float dist = dot(plane.N, lightPos) + plane.d; // distance from plane
            if (dist > lightRadius)
                inside = false;
        }

        if (!inside || lightData.radius <= 0)
            continue;

        uint slot;

        switch (lightData.type) {
            case 0: // point
                InterlockedAdd(pointLightCount, 1, slot);
                pointLightIndices[slot] = lightIndex;
                break;

            case 1: // tube
                InterlockedAdd(tubeLightCount, 1, slot);
                tubeLightIndices[slot] = lightIndex;
                break;

            case 2: // rect
                InterlockedAdd(rectLightCount, 1, slot);
                rectLightIndices[slot] = lightIndex;
                break;
        }

        // update bitmask
        //InterlockedOr(tileLightBitMask[lightIndex / 32], 1 << (lightIndex % 32));
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0) {
        // store the light counts for each type
        lightGrid.Store(tileOffset, pointLightCount);
        lightGrid.Store(tileOffset + 4, tubeLightCount);
        lightGrid.Store(tileOffset + 8, rectLightCount);

        // store the index for each light type
        uint storeOffset = tileOffset + TILE_HEADER_SIZE;

        uint n = 0;
        for (n = 0; n < pointLightCount; n++) {
            lightGrid.Store(storeOffset, pointLightIndices[n]);
            storeOffset += 4;
        }

        for (n = 0; n < tubeLightCount; n++) {
            lightGrid.Store(storeOffset, tubeLightIndices[n]);
            storeOffset += 4;
        }

        for (n = 0; n < rectLightCount; n++) {
            lightGrid.Store(storeOffset, rectLightIndices[n]);
            storeOffset += 4;
        }

        //lightGridBitMask.Store4(tileIndex * 16, tileLightBitMask);
    }
}
