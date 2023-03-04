#include "LightGrid.hlsli"

// Inputs
// DescriptorTable(SRV(t9, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL)
StructuredBuffer<LightData> LightBuffer : register(t9);
ByteAddressBuffer LightGrid : register(t10);
ByteAddressBuffer LightGridBitMask : register(t11);

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec) {
    float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
    specular = lerp(specular, 1, fresnel);
    diffuse = lerp(diffuse, 0, fresnel);
}

float3 ApplyLightCommon(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 lightDir, // World-space vector from point to light
    float3 lightColor // Radiance of directional light
) {
    float3 halfVec = normalize(lightDir - viewDir);
    float nDotH = saturate(dot(halfVec, normal));

    FSchlick(specularColor, diffuseColor, lightDir, halfVec);

    float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;
    float nDotL = saturate(dot(normal, lightDir));

    return nDotL * lightColor * (diffuseColor + specularFactor * specularColor);
}


float3 ApplyPointLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadiusSq,
    float3 lightColor // Radiance of directional light
) {
    float3 lightDir = lightPos - worldPos;
    float lightDistSq = dot(lightDir, lightDir);
    
    // Modifying the normal requires the original distance
    float invLightDist = rsqrt(lightDistSq);
    lightDir *= invLightDist;

    // clamp the distance to prevent pinpoint hotspots near surfaces
    float lightDistSq2 = max(lightDistSq, lightRadiusSq * 0.01);
    invLightDist = rsqrt(lightDistSq2);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    // scale gloss down to 0 when light is close to prevent hotspots
    gloss = smoothstep(0, 125, lightDistSq) * gloss;

    return distanceFalloff * ApplyLightCommon(
        diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        lightDir,
        lightColor
    );
}

cbuffer PSConstants : register(b2) {
    float3 SunDirection;
    float3 SunColor;
    float3 AmbientColor;
    float4 ShadowTexelSize;

    float4 InvTileDim;
    uint4 TileCount;
    uint4 FirstLightIndex;

    uint FrameIndexMod2;
}

void ShadeLights(inout float3 colorSum, uint2 pixelPos,
                 float3 diffuseAlbedo, // Diffuse albedo
                 float3 specularAlbedo, // Specular albedo
                 float specularMask, // Where is it shiny or dingy?
                 float gloss,
                 float3 normal,
                 float3 viewDir,
                 float3 worldPos
) {
    uint2 tilePos = GetTilePos(pixelPos, InvTileDim.xy);
    uint tileIndex = GetTileIndex(tilePos, TileCount.x);
    uint tileOffset = GetTileOffset(tileIndex);
    uint tileLightCount = LightGrid.Load(tileOffset + 0);
    uint tileLightCountSphere = (tileLightCount >> 0) & 0xff;
    //uint tileLightCountCone = (tileLightCount >> 8) & 0xff;
    //uint tileLightCountConeShadowed = (tileLightCount >> 16) & 0xff;

    uint tileLightLoadOffset = tileOffset + 4;

    // sphere
    for (uint n = 0; n < tileLightCountSphere; n++, tileLightLoadOffset += 4) {
        //uint g = LightGrid.Load(0);
        uint lightIndex = LightGrid.Load(tileLightLoadOffset);
        LightData lightData = LightBuffer[lightIndex];
        //LightData lightData = LightBuffer[0];

        colorSum += ApplyPointLight(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos, lightData.radiusSq, lightData.color
        );

        //colorSum += ApplyPointLight(
        //    diffuseAlbedo, specularAlbedo, specularMask, gloss,
        //    normal, viewDir, worldPos, float3(0, 0, -10), 750, float3(1 * g, 0, 0)
        //);
    }
}
