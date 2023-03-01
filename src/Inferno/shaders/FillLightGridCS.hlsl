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

cbuffer CSConstants : register(b0) {
uint ViewportWidth, ViewportHeight;
float InvTileDim; // 1 / LIGHT_GRID_DIM = 1 / 16
float RcpZMagic;
uint TileCountX;
float4x4 ViewMatrix;
float4x4 InverseProjection;
};

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

groupshared uint tileLightCountSphere;
groupshared uint tileLightCountCone;
groupshared uint tileLightCountConeShadowed;

groupshared uint tileLightIndicesSphere[MAX_LIGHTS];
groupshared uint tileLightIndicesCone[MAX_LIGHTS];
groupshared uint tileLightIndicesConeShadowed[MAX_LIGHTS];

groupshared uint4 tileLightBitMask;

struct Plane {
    float3 N; // Plane normal.
    float d; // Distance to origin.
};

struct Frustum {
    Plane planes[4]; // left, right, top, bottom frustum planes.
};

// Convert clip space coordinates to view space
float4 ClipToView(float4 clip) {
    // View space position.
    float4 view = mul(InverseProjection, clip);
    // Perspective projection (undivide)
    return view / view.w;
}

// Convert screen space coordinates to view space.
float4 ScreenToView(float4 screen) {
    // Convert to normalized texture coordinates
    float2 texCoord = screen.xy / float2(ViewportWidth, ViewportHeight);
    texCoord.y = 1 - texCoord.y;
    // Convert to clip space. * 2 - 1 transforms from -1, 1 to 0 1
    float4 clip = float4(float2(texCoord.x, texCoord.y) * 2.0f - 1.0f, screen.z, screen.w);
    return ClipToView(clip);
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
    plane.d = 0;
    return plane;
}

float PlaneDist(Plane plane, float3 pos) {
    float dist = dot(plane.N, pos) - plane.d;
    return dist;
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
        tileLightCountSphere = 0;
        tileLightCountCone = 0;
        tileLightCountConeShadowed = 0;
        tileLightBitMask = 0;
        minDepthUInt = 0xffffffff;
        maxDepthUInt = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // Read all depth values for this tile and compute the tile min and max values
    for (uint dx = groupThread.x; dx < WORK_GROUP_SIZE_X; dx += BLOCK_SIZE) {
        for (uint dy = groupThread.y; dy < WORK_GROUP_SIZE_Y; dy += BLOCK_SIZE) {
            uint2 DTid = group * uint2(WORK_GROUP_SIZE_X, WORK_GROUP_SIZE_Y) + uint2(dx, dy);

            // If pixel coordinates are in bounds...
            if (DTid.x < ViewportWidth && DTid.y < ViewportHeight) {
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
    screenSpace[0] = float4(float2(dispatchThreadID.x, dispatchThreadID.y) * BLOCK_SIZE, z, w);
    // Top right point
    screenSpace[1] = float4(float2(dispatchThreadID.x + 1, dispatchThreadID.y) * BLOCK_SIZE, z, w);
    // Bottom left point
    screenSpace[2] = float4(float2(dispatchThreadID.x, dispatchThreadID.y + 1) * BLOCK_SIZE, z, w);
    // Bottom right point
    screenSpace[3] = float4(float2(dispatchThreadID.x + 1, dispatchThreadID.y + 1) * BLOCK_SIZE, z, w);

    float3 viewSpace[4];
    // Now convert the screen space points to view space
    for (int i = 0; i < 4; i++) {
        viewSpace[i] = ScreenToView(screenSpace[i]).xyz;
    }

    // View space eye position is always at the origin.
    const float3 eyePos = float3(0, 0, 0);

    Plane planes[4]; // planes are in view space
    planes[0] = ComputePlane(eyePos, viewSpace[2], viewSpace[0]); // Left plane
    planes[1] = ComputePlane(eyePos, viewSpace[3], viewSpace[1]); // Right plane
    planes[2] = ComputePlane(eyePos, viewSpace[0], viewSpace[1]); // Top plane
    planes[3] = ComputePlane(eyePos, viewSpace[2], viewSpace[3]); // Bottom plane

    // this assumes inverted depth buffer
    //float tileMinDepth = (rcp(asfloat(maxDepthUInt)) - 1.0) * RcpZMagic;
    //float tileMaxDepth = (rcp(asfloat(minDepthUInt)) - 1.0) * RcpZMagic;
    float tileMinDepth = asfloat(minDepthUInt);
    float tileMaxDepth = asfloat(maxDepthUInt);
    //float zNear= lerp(1, 3000, tileMinDepth);
    //float zNear = tileMinDepth / RcpZMagic;
    float zFar = tileMaxDepth / RcpZMagic;
    //zNear = max(zNear, FLT_MIN); // don't allow a depth range of 0
    float zNear = 1; // strange artifacts on transparent objects if using tileMinDepth / zMagic
    //float invTileDepthRange = rcp(tileDepthRange);
    // TODO: near/far clipping planes seem to be falling apart at or near the max depth with infinite projections

    // construct transform from world space to tile space (projection space constrained to tile area)
    //float2 invTileSize2X = float2(ViewportWidth, ViewportHeight) * InvTileDim;
    // D3D-specific [0, 1] depth range ortho projection
    // (but without negation of Z, since we already have that from the projection matrix)
    //float3 tileBias = float3(-2.0 * float(group.x) + invTileSize2X.x - 1.0,
    //                         -2.0 * float(group.y) + invTileSize2X.y - 1.0,
    //                         -tileMinDepth * invTileDepthRange);

    //tileBias = float3(0, 0, 0);
    // tile bias is scale?
    // ortho projection matrix for a section of the screen?
    //float4x4 projToTile = float4x4(invTileSize2X.x, 0, 0, tileBias.x,
    //                               0, -invTileSize2X.y, 0, tileBias.y,
    //                               0, 0, invTileDepthRange, tileBias.z,
    //                               0, 0, 0, 1);

    //float4x4 tileMVP = mul(projToTile, ViewProjMatrix);

    // extract frustum planes (these will be in world space)
    // create normals for each plane
    //float4 frustumPlanes[6];
    //float4 tilePos = tileMVP[3];
    //frustumPlanes[0] = tilePos + tileMVP[0];
    //frustumPlanes[1] = tilePos - tileMVP[0];
    //frustumPlanes[2] = tilePos + tileMVP[1];
    //frustumPlanes[3] = tilePos - tileMVP[1];
    //frustumPlanes[4] = tilePos + tileMVP[2];
    //frustumPlanes[5] = tilePos - tileMVP[2];
    //for (int n = 0; n < 6; n++) {
    //    // 1 / sqrt(n * n) -> 1 / (sqrt(length^2)) -> 1 / len
    //    // normalize
    //    frustumPlanes[n] *= rsqrt(dot(frustumPlanes[n].xyz, frustumPlanes[n].xyz));
    //}

    uint tileIndex = GetTileIndex(group.xy, TileCountX);
    uint tileOffset = GetTileOffset(tileIndex);

    // find set of lights that overlap this tile
    for (uint lightIndex = groupIndex; lightIndex < MAX_LIGHTS; lightIndex += BLOCK_SIZE * BLOCK_SIZE) {
        LightData lightData = Lights[lightIndex];
        //float3 lightWorldPos = lightData.pos;
        //lightWorldPos = float3(0, 0, 0); // makes all pass the plane check
        const float lightRadius = sqrt(lightData.radiusSq);

        bool inside = true;

        // project light from world to view space
        float3 lightPos = mul(ViewMatrix, float4(lightData.pos, 1)).xyz;

        // cull the light if is behind the camera (negative z is behind)
        if (lightPos.z + lightRadius < zNear || lightPos.z - lightRadius > zFar) {
            inside = false;
        }

        for (int i = 0; i < 4; i++) {
            Plane plane = planes[i];
            float dist = PlaneDist(plane, lightPos); // positive value is inside
            if (dist < -lightRadius) {
                inside = false;
            }
        }

        if (!inside)
            continue;

        uint slot;
        InterlockedAdd(tileLightCountSphere, 1, slot);
        tileLightIndicesSphere[slot] = lightIndex;

        //switch (lightData.type) {
        //    case 0: // sphere
        //        InterlockedAdd(tileLightCountSphere, 1, slot);
        //        tileLightIndicesSphere[slot] = lightIndex;
        //        break;

        //    case 1: // cone
        //        InterlockedAdd(tileLightCountCone, 1, slot);
        //        tileLightIndicesCone[slot] = lightIndex;
        //        break;

        //    case 2: // cone w/ shadow map
        //        InterlockedAdd(tileLightCountConeShadowed, 1, slot);
        //        tileLightIndicesConeShadowed[slot] = lightIndex;
        //        break;
        //}

        // update bitmask
        //InterlockedOr(tileLightBitMask[lightIndex / 32], 1 << (lightIndex % 32));
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0) {
        uint lightCount =
            ((tileLightCountSphere & 0xff) << 0) |
            ((tileLightCountCone & 0xff) << 8) |
            ((tileLightCountConeShadowed & 0xff) << 16);
        lightGrid.Store(tileOffset + 0, lightCount); // tile light count

        uint storeOffset = tileOffset + 4;
        uint n;
        for (n = 0; n < tileLightCountSphere; n++) {
            lightGrid.Store(storeOffset, tileLightIndicesSphere[n]);
            storeOffset += 4;
        }
        //for (n = 0; n < tileLightCountCone; n++) {
        //    lightGrid.Store(storeOffset, tileLightIndicesCone[n]);
        //    storeOffset += 4;
        //}
        //for (n = 0; n < tileLightCountConeShadowed; n++) {
        //    lightGrid.Store(storeOffset, tileLightIndicesConeShadowed[n]);
        //    storeOffset += 4;
        //}

        //lightGridBitMask.Store4(tileIndex * 16, tileLightBitMask);
    }
}
