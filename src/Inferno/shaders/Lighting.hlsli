#include "LightGrid.hlsli"

static const float SMOL_EPS = .000002;
static const float PI = 3.14159265f;
static const float GLOBAL_LIGHT_MULT = 50;
static const float FRESNEL_MULT = 50;

struct MaterialInfo {
    float NormalStrength;
    float SpecularStrength;
    float Metalness;
    float Roughness;
    float EmissiveStrength;
    float LightReceived; // 0 for unlit
    int ID; // texid
    int VClip; // Effect clip
    //float pad0; // Pad to 32 bytes
};

struct LightingArgs {
    float3 SunDirection;
    float3 SunColor;
    float3 AmbientColor;
    float4 ShadowTexelSize;

    float4 InvTileDim;
    uint4 TileCount;
    uint4 FirstLightIndex;

    uint FrameIndexMod2;
};

// Inputs
// DescriptorTable(SRV(t9, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL)
ConstantBuffer<LightingArgs> LightArgs : register(b2);
StructuredBuffer<LightData> LightBuffer : register(t11);
ByteAddressBuffer LightGrid : register(t12);
ByteAddressBuffer LightGridBitMask : register(t13);

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec) {
    float fresnel = pow(1 - saturate(dot(lightDir, halfVec)), 5);
    specular = lerp(specular, 1, fresnel);
    diffuse = lerp(diffuse, 0, fresnel);
}

// Schlick's approximation for Fresnel equation
float3 fresnelSchlick(float3 F0, float dotProd) {
    return F0 + (1 - F0) * pow(1 - dotProd, 5);
}

float FresnelRoughnessSimple(float cosTheta, float roughness) {
    return (1.0 - roughness) * pow(max(1.0 - cosTheta, 0), 5.0);
}

float Lambert(float3 normal, float3 lightDir) {
    return saturate(dot(normal, lightDir));
}

float HalfLambert(float3 normal, float3 lightDir) {
    //return Lambert(normal, lightDir);
    float nDotL = pow(dot(normal, lightDir) * 0.5 + 0.5, 2);
    return saturate(nDotL);
}

float3 ApplyLightCommon(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 lightDir, // World-space vector from point to light
    float3 lightColor // Radiance of light
) {
    float3 halfVec = normalize(lightDir - viewDir);
    float nDotH = saturate(dot(halfVec, normal));
    float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;
    FSchlick(specularColor, diffuseColor, lightDir, halfVec);
    float nDotL = saturate(dot(normal, lightDir));
    return nDotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

// Schlick-Beckmann GGX approximation used for smith's method
float geometrySchlickGGX(float NdotX, float k) {
    return NdotX / max(NdotX * (1 - k) + k, SMOL_EPS);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float roughnessplusone = roughness + 1.0;
    float k = roughnessplusone * roughnessplusone / 8.0;

    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

float normalDistributionGGXLine(float NdotH, float alpha, float alphaPrime) {
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    return (alpha2 * alphaPrime) / pow(NdotH2 * (alpha2 - 1.) + 1., 2.);
}

#define SILVER_F0 float3(.95, .93, .88)
#define PI_INV .3183098861

float normalDistributionGGXSphere(float NdotH, float alpha, float alphaPrime) {
    float alpha2 = alpha * alpha;
    float alphaPrime2 = alphaPrime * alphaPrime;
    float NdotH2 = NdotH * NdotH;
    return (alpha2 * alphaPrime2) / pow(NdotH2 * (alpha2 - 1.) + 1., 2.);
}


float InvLightDist(float distSq, float radius) {
    // clamp the distance to prevent pinpoint hotspots near surfaces
    float clamped = max(distSq, radius * radius * 0.01);
    return rsqrt(clamped);
}

// scale gloss down to 0 when light is close to prevent hotspots
float ClampGloss(float gloss, float lightDistSq) {
    return smoothstep(0, 128, lightDistSq) * gloss;
}

float RoughnessToGloss(float roughness) {
    roughness = clamp(roughness, 0.2, 1); // the light models behave poorly under 0.2 roughness
    return max(2 / pow(roughness, 4) - 2, 0.1);
    //return 2 / pow(roughness, 4) - 2;
}

void CutoffLightValue(float lightRadius, float dist, float cutoff, inout float value) {
    float specCutoff = lightRadius * cutoff; // cutoff distance for fading to black
    if (dist > specCutoff)
        value = saturate(lerp(value, 0, (dist - specCutoff) / (lightRadius - specCutoff)));
}

float Attenuate(float lightDistSq, float lightRadius) {
    // https://google.github.io/filament/Filament.md.html#lighting/directlighting/punctuallights
    float factor = lightDistSq / (lightRadius * lightRadius); // 0 to 1
    //float smoothFactor = max(1 - factor, 0); // 0 to 1, original
    //float falloff = (smoothFactor * smoothFactor) / max(lightDistSq, 1e-4); // original
    float smoothFactor = max(1 - pow(factor, 0.5), 0); // 0 to 1
    float falloff = (smoothFactor * smoothFactor) / max(sqrt(lightDistSq), 1e-4);
    //float falloff = (smoothFactor * smoothFactor) / max(pow(lightDistSq, 0.75), 1e-4);
    return falloff;
    //return clamp(falloff * GLOBAL_LIGHT_MULT, 0, 10); // clamp nearby surface distance to prevent hotspots
}

float3 ApplyPointLight(
    float3 diffuse,
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float roughness, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadius,
    float3 lightColor, // Radiance of directional light
    float3 planeNormal
) {
    float3 lightDir = lightPos - worldPos;
    float lightDistSq = dot(lightDir, lightDir);
    lightDir = normalize(lightDir);

    float falloff = Attenuate(lightDistSq, lightRadius);

    float3 halfVec = normalize(lightDir - viewDir);
    float nDotH = saturate(dot(halfVec, normal));

    float gloss = RoughnessToGloss(roughness);
    float nDotL = HalfLambert(normal, lightDir);

    float specularFactor = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
    specularFactor *= 1 + FresnelRoughnessSimple(dot(lightDir, halfVec), roughness) * FRESNEL_MULT;
    //specularFactor = clamp(specularFactor, 0, MAX_SPEC_MULT);

    // clip specular behind the light plane. todo: move to hemisphere light
    if (any(planeNormal)) {
        //lightPos -= planeNormal * 3; // undo offset
        float planeFactor = -dot(planeNormal, lightPos - worldPos);
        specularFactor *= saturate(planeFactor); // *2 to fade quicker
        falloff *= saturate(planeFactor * 4 + 1);
    }

    float3 specular = max(0, specularFactor * specularColor * specularMask);
    return nDotL * falloff * (lightColor * diffuse + specular) * GLOBAL_LIGHT_MULT;
}


float3 ApplySphereLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float sphereRadius,
    float lightRadius,
    float3 lightColor // Radiance of directional light
) {
    // https://alextardif.com/arealights.html
    // https://www.shadertoy.com/view/3dsBD4
    float3 r = reflect(viewDir, normal);
    float3 lightDir = lightPos - worldPos; // vector to light center
    float3 centerToRay = dot(lightDir, r) * r - lightDir;
    float3 closestPoint = lightDir + centerToRay * saturate(sphereRadius / length(centerToRay));
    float lightDistSq = dot(closestPoint, closestPoint);

    // https://donw.io/post/distant-sphere-lights/
    //float3 D = dot(lightDir, r) * r - lightDir;
    //float3 closestPoint = lightDir + D * saturate(sphereRadius / length(lightDir) * rsqrt(dot(D, D)));
    //float lightDistSq = dot(closestPoint, closestPoint);

    float invLightDist = InvLightDist(lightDistSq, lightRadius);
    lightDir = normalize(closestPoint);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadius * lightRadius * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    gloss = ClampGloss(gloss, lightDistSq);

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

float4 LineLight(float3 p, float3 n, float3 v, float3 r, float3 f0, float roughness, float lightRadius, float tubeRadius, float3 lineStart, float3 lineEnd, out float3 fresnel) {
    float4 result = float4(0, 0, 0, 0);
    float3 l0 = lineStart - p;
    float3 l1 = lineEnd - p;
    float lengthL0 = length(l0);
    float lengthL1 = length(l1);
    float NdotL0 = dot(n, l0) / (2. * lengthL0);
    float NdotL1 = dot(n, l1) / (2. * lengthL1);
    float3 lightCenter = (lineStart + lineEnd) / 2;
    result.w = (2.0 * saturate(NdotL0 + NdotL1)) / (lengthL0 * lengthL1 + dot(l0, l1) + 2.);

    float3 ld = l1 - l0;
    float RdotL0 = dot(r, l0);
    float RdotLd = dot(r, ld);
    float L0dotLd = dot(l0, ld);
    float distLd = length(ld);

    // point on the line
    float t = (RdotL0 * RdotLd - L0dotLd) / (distLd * distLd - RdotLd * RdotLd);

    result.xyz = float3(0, 0, 0);
    float3 closestPoint = l0 + ld * saturate(t);
    // point on the tube based on its radius
    float3 centerToRay = dot(closestPoint, r) * r - closestPoint;
    closestPoint += centerToRay * saturate(tubeRadius / length(centerToRay));

    // taper the ends
    float endMult = smoothstep(1, 0, (t - 1) * distLd * 0.75);
    endMult *= smoothstep(0, 1, t * distLd * 0.75);
    //endMult = 1;

    float3 l = normalize(closestPoint);
    float3 h = normalize(l - v);
    float lightDist = length(closestPoint);

    float nDotH = max(dot(n, h), 0);
    float vDotH = dot(h, -v);

    float alpha = roughness * roughness;
    float alphaPrime = saturate(alpha + (tubeRadius / (2 * lightDist)));

    // specular cutoff
    float specCutoff = lightRadius * 0.80; // cutoff distance for fading to black
    float specFactor = 1;
    if (lightDist > specCutoff) {
        specFactor = saturate(lerp(specFactor, 0, (lightDist - specCutoff) / (lightRadius - specCutoff)));
    }

    // diffuse cutoff
    float cutoff = lightRadius * 0.60; // cutoff distance for fading to black
    float lightCenterDist = length(lightCenter - p);
    if (lightCenterDist > cutoff) {
        result.w = lerp(result.w, 0, (lightCenterDist - cutoff) / (lightRadius - cutoff));
    }

    float nDotV = max(0, dot(n, -v));
    float nDotL = dot(n, l);
    fresnel = fresnelSchlick(f0, vDotH);
    result.xyz = normalDistributionGGXLine(nDotH, alpha, alphaPrime)
        * geometrySmith(nDotV, nDotL, roughness)
        * fresnel * endMult * specFactor;

    return result;
}


float3 ApplyCylinderLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position start
    float3 lightPos2, // World-space light position end
    float lightRadius,
    float3 lightColor // Radiance of directional light
) {
    float tubeRadius = 2.0;

    float3 r = reflect(viewDir, normal);
    float3 ab = lightPos2 - lightPos;
    float t = dot(worldPos - lightPos, ab) / dot(ab, ab);
    t = saturate(t);
    float3 closestPoint = lightPos + t * ab;

    float3 lightDir = closestPoint - worldPos;
    float3 centerToRay = dot(lightDir, r) * r - lightDir;
    // point on the tube based on its radius
    closestPoint += centerToRay * saturate(tubeRadius / length(centerToRay));
    closestPoint -= worldPos; // vec to closest


    float3 L = normalize(closestPoint);
    float lightDistSq = dot(closestPoint, closestPoint);

    //float distanceFalloff = pow(saturate(1.0 - pow(distLight / lightRadius, 4)), 2) / ((distLight * distLight) + 1.0);

    //float3 specularFactor = float3(0, 0, 0);
    //float3 diffuseFactor = float3(1, 1, 1);
    //float3 light = (specularFactor + diffuseFactor) * falloff * lightColor * 1000 /** luminosity*/;
    //return max(0, light); // goes to inf when behind the light

    //float3 closestPoint = lightDir + centerToRay * saturate(lightRadiusSq / length(centerToRay));
    //float lightDistSq = dot(closestPoint, closestPoint);

    // https://donw.io/post/distant-sphere-lights/
    //float3 D = dot(lightDir, r) * r - lightDir;
    //float3 closestPoint = lightDir + D * saturate(sphereRadius / length(lightDir) * rsqrt(dot(D, D)));
    //float lightDistSq = dot(closestPoint, closestPoint);

    float invLightDist = InvLightDist(lightDistSq, lightRadius);
    lightDir = L; //normalize(closestPoint);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadius * lightRadius * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    gloss = ClampGloss(gloss, lightDistSq);
    //gloss = 0;


    //float roughness = 0.5;
    float specularAmount = dot(r, L);
    //float specFactor = 1.0 - saturate(length(closestPoint) * pow(1 - roughness, 4));

    //float3 color = diffuseColor * lightRadius * 0.2* lightColor * distanceFalloff /*+ specularFactor*/;
    // float3 light = (specularFactor + diffuseFactor) * falloff * lightColor * luminosity;	

    return distanceFalloff * ApplyLightCommon(
        diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        lightDir,
        lightColor
    ) + float3(0, 1, 0) * specularAmount;
}

struct Rect {
    float3 a, b, c, d;
};

float rectSolidAngle(float3 v0, float3 v1, float3 v2, float3 v3) {
    float3 n0 = normalize(cross(v0, v1));
    float3 n1 = normalize(cross(v1, v2));
    float3 n2 = normalize(cross(v2, v3));
    float3 n3 = normalize(cross(v3, v0));

    float g0 = acos(dot(-n0, n1));
    float g1 = acos(dot(-n1, n2));
    float g2 = acos(dot(-n2, n3));
    float g3 = acos(dot(-n3, n0));
    // acos -> 0 to PI. 0 to 4 PI - 2 PI -> -2 PI to 2 PI
    return g0 + g1 + g2 + g3 - 2.0 * PI;
}

float3 RayPlaneIntersect(float3 rayPos, float3 rayDir, float3 planeCenter, float3 planeNormal) {
    return rayPos + rayDir * (dot(planeNormal, planeCenter - rayPos) / dot(planeNormal, rayDir));
}

float normalDistributionGGXRect(float NdotH, float alpha, float alphaPrime) {
    float alpha2 = alpha * alpha;
    float alphaPrime3 = alphaPrime * alphaPrime * alphaPrime;
    float NdotH2 = NdotH * NdotH;
    return (alpha2 * alphaPrime3) / (pow(NdotH2 * (alpha2 - 1.) + 1., 2.));
}

float3 IntersectPlane(float3 rayPos, float3 rayDir, float3 lightDir, float3 lightPos) {
    return rayPos + rayDir * (dot(lightDir, lightPos - rayPos) / dot(lightDir, rayDir));
}

float PlaneDistance(float3 pt, float3 origin, float3 normal) {
    return dot(normal, pt - origin);
}

float3 ClosestPointOnLine(float3 p, float3 a, float3 b) {
    // Project p onto ab, computing the paramaterized position d(t) = a + t * (b - a)
    float3 ab = b - a;
    float t = dot(p - a, ab) / dot(ab, ab);
    return a + saturate(t) * ab;
}

float3 LengthSq(float3 v) {
    return dot(v, v);
}


float3 ClosestPointInRectangle(float3 pt, float3 origin, float3 normal, float3 right, float3 up) {
    float3 v0 = origin + right + up;
    float3 v1 = origin - right + up;
    float3 v2 = origin - right - up;
    float3 v3 = origin + right - up;

    float3 pointOnPlane = pt - dot(pt - origin, normal);

    float3 ab = v1 - v0;
    float3 ad = v3 - v0;
    float3 am = pointOnPlane - v0;
    float amDotAb = dot(am, ab);
    float amDotAd = dot(am, ad);

    if (amDotAb < dot(ab, ab) &&
        amDotAd < dot(ad, ad))
        return pointOnPlane;

    if (0 < amDotAb && amDotAb < dot(ab, ab) &&
        0 < amDotAd && amDotAd < dot(ad, ad))
        return pointOnPlane;

    return v0;
}

float3 ClosestPointOnRectangleEdge(float3 pt, float3 origin, float3 normal, float3 right, float3 up) {
    //normal = float3(-1, 0, 0);
    //right = float3(0, 0, -1);
    //up = float3(0, 20, 0);
    float3 v0 = origin + right + up;
    float3 v1 = origin - right + up;
    float3 v2 = origin - right - up;
    float3 v3 = origin + right - up;

    // check if point is inside rectangle
    // https://math.stackexchange.com/questions/190111/how-to-check-if-a-point-is-inside-a-rectangle
    //float3 pointOnPlane = pt - dot(pt - origin, normal);

    //float3 ab = v1 - v0;
    //float3 ad = v3 - v0;
    //float am = pointOnPlane - v0;
    //float amDotAb = dot(am, ab);
    //float amDotAd = dot(am, ad);

    //if (amDotAb < dot(ab, ab) &&
    //    amDotAd < dot(ad, ad))
    //    return pointOnPlane;

    //if (0 < amDotAb && amDotAb < dot(ab, ab) &&
    //    0 < amDotAd && amDotAd < dot(ad, ad))
    //    return pointOnPlane;

    float3 p0 = ClosestPointOnLine(pt, v0, v1);
    float3 p1 = ClosestPointOnLine(pt, v1, v2);
    float3 p2 = ClosestPointOnLine(pt, v2, v3);
    float3 p3 = ClosestPointOnLine(pt, v3, v0);

    float len0 = LengthSq(p0 - pt).x;
    float len1 = LengthSq(p1 - pt).x;
    float len2 = LengthSq(p2 - pt).x;
    float len3 = LengthSq(p3 - pt).x;
    float len4 = LengthSq(origin - pt).x;

    float minLen = min(len0, min(len1, min(len2, min(len3, len4))));
    if (minLen == len0)
        return p0;
    if (minLen == len1)
        return p1;
    if (minLen == len2)
        return p2;
    if (minLen == len3)
        return p3;

    return origin;
}

float3 ApplyRectLight2(
    float3 diffuse,
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float roughness,
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadius,
    float3 lightColor, // Radiance of light
    float3 planeNormal,
    float3 planeRight,
    float3 planeUp
) {
    // https://alextardif.com/arealights.html
    // https://www.shadertoy.com/view/3dsBD4

    //lightPos += planeNormal; // shift diffuse out of wall slightly

    // shift the rectangle off of the surface so it lights it more evenly
    // note that this does not affect the position of the reflection
    float3 surfaceOffset = planeNormal * 1.5; // 1.5 needed for uneven surfaces
    float vWidth = length(planeRight);
    float vHeight = length(planeUp);
    planeRight = normalize(planeRight);
    planeUp = normalize(planeUp);
    // find the closest point on the rectangle
    float3 diffPlaneIntersect = IntersectPlane(worldPos, planeNormal, planeNormal, lightPos);
    float3 diffDir = diffPlaneIntersect - lightPos;
    float2 diffPlanePoint = float2(dot(diffDir, planeRight),
                                   dot(diffDir, planeUp));
    float2 nearestDiffPoint = float2(clamp(diffPlanePoint.x, -vWidth, vWidth),
                                     clamp(diffPlanePoint.y, -vHeight, vHeight));
    float3 closestDiffusePoint = lightPos + planeRight * nearestDiffPoint.x + planeUp * nearestDiffPoint.y;
    closestDiffusePoint += surfaceOffset;

    float3 specular = float3(0, 0, 0);
    {
        lightPos -= planeNormal * 1; // shift specular back to surface (unsure why 1 unit off)

        // Calculate reflected point
        float3 r = reflect(viewDir, normal);
        float3 reflectedIntersect = IntersectPlane(worldPos, r, planeNormal, lightPos);

        // We then find the difference between that point and the center of the light,
        // and find that result represented in the 2D space on the light's plane in view space.
        float3 reflectedDir = reflectedIntersect - lightPos;
        float2 reflectedPlanePoint = float2(dot(reflectedDir, planeRight),
                                            dot(reflectedDir, planeUp));

        float2 nearestReflectedPoint = float2(clamp(reflectedPlanePoint.x, -vWidth, vWidth),
                                              clamp(reflectedPlanePoint.y, -vHeight, vHeight));

        //float specFactor = 1.0 - saturate(length(nearestReflectedPoint - reflectedPlanePoint) * smoothstep(0, 1, roughness));
        float3 l = lightPos + planeRight * nearestReflectedPoint.x + planeUp * nearestReflectedPoint.y - worldPos;
        float3 h = normalize(viewDir - normalize(l)); // half angle


        //float lightDist = length(L);
        //float vDotH = dot(h, viewDir);
        //float nDotH = max(dot(-h, normal), 0);

        //float3 halfVec = normalize(lightDir - viewDir);
        float nDotH = saturate(dot(-h, normal));
        float gloss = RoughnessToGloss(roughness);
        float specularFactor = pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong

        //float planeDist = length(nearestReflectedPoint - reflectedPlanePoint);
        //float planeDist = dot(planeNormal, nearestReflectedPoint - reflectedPlanePoint);
        //plane dist = dot(planeNormal, lightPos) + plane.d

        //planeDist = min(planeDist, 0);
        //float planeFactor = 1.0 - planeDist * (1 - roughness) * (1 - roughness);

        //float fresnel = pow(1 - saturate(dot(lightDir, halfVec)), 5);
        //float3 lightDir = normalize(lightPos - worldPos);

        //float3 halfVec = normalize(lightDir - viewDir);

        // Fade distant highlights
        specularFactor *= max(1 - length(nearestReflectedPoint - reflectedPlanePoint) / lightRadius / 8, 0);
        //specularFactor *= (1 - roughness * 0.66); // Additional roughness falloff

        specularColor *= 0.85; // tweak to match point lights and compensate for extra point specular
        specularFactor *= 1 + FresnelRoughnessSimple(dot(h, viewDir), roughness) * FRESNEL_MULT;

        {
            // point specular. helps minimize shimmering due to inaccuracies in the nearest point calculations
            float3 lightDir = normalize(lightPos - worldPos);
            float3 halfVec = normalize(lightDir - viewDir);
            float nDotH2 = saturate(dot(halfVec, normal));
            float phong = pow(nDotH2, gloss) * (gloss + 2) / 8; // blinn-phong
            //float nDotL = HalfLambert(normal, lightDir);
            //float nDotL = dot(normal, lightDir);
            specularFactor = max(specularFactor, phong /** nDotL*/); // take the max between the two!
        }

        // fade specular close to the light plane. it behaves very oddly with individual points appearing.
        specularFactor *= saturate(1 - dot(normal, planeNormal)); 

        const float3 vLight = lightPos - worldPos;
        float rDotL = dot(r, normalize(vLight));
        //float rDotL = HalfLambert(r, normalize(vLight));
        specular = max(0, specularMask * specularFactor * specularColor * rDotL);
        //specular = specularColor * specularFactor * rDotL;
    }

    // clip specular behind the light plane
    float planeFactor = -dot(planeNormal, lightPos - worldPos); // Is the pixel behind the plane?
    float diffCutoff = saturate(planeFactor - .25); // Adding less offset decreases brightness
    float specCutoff = saturate(planeFactor - 1);

    float nDotL = HalfLambert(normal, normalize(closestDiffusePoint - worldPos));

    float3 lightDir = closestDiffusePoint - worldPos;
    float lightDistSq = dot(lightDir, lightDir);

    float minR = min(vHeight, vWidth);
    float falloff = Attenuate(lightDistSq, lightRadius/* + minR*/);
    //return max(0, falloff * specular);
    return max(0, falloff * nDotL * (lightColor * diffuse * diffCutoff + specular * specCutoff) * GLOBAL_LIGHT_MULT);
    //return nDotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

float Luminance(float3 v) {
    //0.299,0.587,0.114
    return dot(v, float3(0.2126f, 0.7152f, 0.0722f));
}

static const float METAL_DIFFUSE_FACTOR = 3; // overall brightness of metal
static const float METAL_SPECULAR_FACTOR = 1; // reduce this after increasing specular exponent
static const float METAL_SPECULAR_EXP = 2.0; // increase this to get more diffuse color contribution

float3 GetMetalDiffuse(float3 diffuse) {
    float3 intensity = dot(diffuse, float3(0.299, 0.587, 0.114));
    return lerp(intensity, diffuse, 2.0); // boost the saturation of the diffuse texture
}

void GetLightColors(LightData light, MaterialInfo material, float3 diffuse, out float3 specularColor, out float3 lightColor) {
    const float3 lightRgb = light.color.rgb * light.color.a;
    lightColor = lerp(lightRgb, 0, material.Metalness);
    //float3 metalDiffuse = GetMetalDiffuse(diffuse);

    specularColor = lightColor + lerp(0, (pow(diffuse + 1, METAL_SPECULAR_EXP) - 1) * lightRgb * METAL_SPECULAR_FACTOR, material.Metalness);
    specularColor *= material.SpecularStrength;
    //specularColor = clamp(specularColor, 0, 10); // clamp overly bright specular as it causes bloom flickering
    lightColor += lerp(0, diffuse * lightRgb * METAL_DIFFUSE_FACTOR, material.Metalness);
    //lightColor *= (1 - material.Metalness);
    //lightColor *= 1;
}

void ShadeLights(inout float3 colorSum,
                 uint2 pixelPos,
                 float3 diffuse,
                 float specularMask, // Where is it shiny or dingy?
                 float3 normal,
                 float3 viewDir,
                 float3 worldPos,
                 MaterialInfo material) {
    uint2 tilePos = GetTilePos(pixelPos, LightArgs.InvTileDim.xy);
    uint tileIndex = GetTileIndex(tilePos, LightArgs.TileCount.x);
    uint tileOffset = GetTileOffset(tileIndex);
    uint pointLightCount = LightGrid.Load(tileOffset);
    uint tubeLightCount = LightGrid.Load(tileOffset + 4);
    uint rectLightCount = LightGrid.Load(tileOffset + 8);
    uint tileLightLoadOffset = tileOffset + TILE_HEADER_SIZE;

    uint n = 0;
    for (n = 0; n < pointLightCount; n++, tileLightLoadOffset += 4) {
        uint lightIndex = LightGrid.Load(tileLightLoadOffset);
        LightData light = LightBuffer[lightIndex];

        float3 lightColor, specularColor;
        GetLightColors(light, material, diffuse, specularColor, lightColor);

        //float3 lightColor = lerp(light.color, 0, material.Metalness);
        //float3 specularColor = lightColor + lerp(0, (pow(diffuse + 1, 16) - 1) * light.color * METAL_SPECULAR_FACTOR, material.Metalness);
        //specularColor *= material.SpecularStrength;
        //lightColor += lerp(0, diffuse * light.color * METAL_DIFFUSE_FACTOR, material.Metalness);
#if 1
        colorSum += ApplyPointLight(
            diffuse, specularColor, specularMask, material.Roughness,
            normal, viewDir, worldPos, light.pos,
            light.radius, lightColor, light.normal
        );

        //colorSum += float3(0.05, 0, 0);
#elif 0
        float sphereRadius = 5;
        lightData.pos += float3(0, -6, 0); // shift to center
        colorSum += ApplySphereLight(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos, sphereRadius,
            lightData.radiusSq, lightData.color
        );
#endif
    }

    //for (n = 0; n < tubeLightCount; n++, tileLightLoadOffset += 4) {
    //    uint lightIndex = LightGrid.Load(tileLightLoadOffset);
    //    LightData light = LightBuffer[lightIndex];

    //    float3 fresnel = float3(0, 0, 0);
    //    //float nDotV = max(dot(normal, viewDir), 0);
    //    float3 r = reflect(viewDir, normal);
    //    float4 diffSpec = LineLight(worldPos, normal, viewDir, r, float3(1, 1, 1), material.Roughness, light.radius, light.tubeRadius * 2, light.pos, light.pos2, fresnel);

    //    float3 lineLightKd = 1. - fresnel;
    //    lineLightKd *= 1. - material.Metalness;
    //    float LINE_LIGHT_INTENSITY = 25;

    //    colorSum += (lineLightKd * PI_INV * light.color.rgb * light.color.a + diffSpec.xyz) * LINE_LIGHT_INTENSITY * diffSpec.w /** lineLightAttenuation*/;
    //}

    for (n = 0; n < rectLightCount; n++, tileLightLoadOffset += 4) {
        uint lightIndex = LightGrid.Load(tileLightLoadOffset);
        LightData light = LightBuffer[lightIndex];

        float3 lightColor, specularColor;
        GetLightColors(light, material, diffuse, specularColor, lightColor);
        //float3 lightColor = lerp(light.color, 0, material.Metalness);
        //float3 specularColor = lightColor + lerp(0, (pow(diffuse + 1, 16) - 1) * light.color * METAL_SPECULAR_FACTOR, material.Metalness);
        //specularColor *= material.SpecularStrength;
        //lightColor += lerp(0, diffuse * light.color * METAL_DIFFUSE_FACTOR, material.Metalness);

        colorSum += ApplyRectLight2(
            diffuse, specularColor, specularMask, material.Roughness,
            normal, viewDir, worldPos, light.pos,
            light.radius, lightColor, light.normal, light.right, light.up
        );
        //colorSum += float3(0.05, 0, 0);
    }
}
