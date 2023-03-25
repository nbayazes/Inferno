#include "LightGrid.hlsli"

static const float SMOL_EPS = .000002;
static const float PI = 3.14159265f;

// Inputs
// DescriptorTable(SRV(t9, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL)
StructuredBuffer<LightData> LightBuffer : register(t11);
ByteAddressBuffer LightGrid : register(t12);
ByteAddressBuffer LightGridBitMask : register(t13);

struct MaterialInfo {
    float NormalStrength;
    float SpecularStrength;
    float Metalness;
    float Roughness;
};

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


float InvLightDist(float distSq, float radiusSq) {
    // clamp the distance to prevent pinpoint hotspots near surfaces
    float clamped = max(distSq, radiusSq * 0.01);
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

float SpecularMultFromRoughness(float roughness) {
    //float mult = 1 * sqrt(1 - roughness);
    //return clamp(mult, 0, 0.4);
    return sqrt(saturate(1 - roughness));
    return 1 - roughness;
    //return 1 - sqrt(roughness);
    return pow(saturate(1 - roughness), 4); // fade specular based on roughness (empirical)
}


void CutoffLightValue(float lightRadius, float dist, float cutoff, inout float value) {
    float specCutoff = lightRadius * cutoff; // cutoff distance for fading to black
    if (dist > specCutoff)
        value = saturate(lerp(value, 0, (dist - specCutoff) / (lightRadius - specCutoff)));
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
    float lightRadiusSq,
    float3 lightColor // Radiance of directional light
) {
    specularColor *= 0.25;
    float3 lightDir = lightPos - worldPos;
    float lightDistSq = dot(lightDir, lightDir);
    lightDir = normalize(lightDir);
    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float falloff = lightRadiusSq * (invLightDist * invLightDist);
    falloff = max(0, falloff - rsqrt(falloff));

    float3 halfVec = normalize(lightDir - viewDir);
    float nDotH = saturate(dot(halfVec, normal));
    
    float gloss = RoughnessToGloss(pow(roughness, 1.5));
    float nDotL = saturate(dot(normal, lightDir));

    float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
    specularFactor *= SpecularMultFromRoughness(roughness);
    specularFactor *= 1 + pow(1 - saturate(dot(lightDir, halfVec)), 5); // fresnel
    float3 specular = specularColor * specularFactor * nDotL * falloff;
    return falloff * nDotL * lightColor * diffuse + specular;
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
    float lightRadiusSq,
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

    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);
    lightDir = normalize(closestPoint);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
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
    endMult = 1;

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
    float lightRadiusSq,
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

    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);
    lightDir = L; //normalize(closestPoint);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    gloss = ClampGloss(gloss, lightDistSq);
    gloss = 0;


    float roughness = 0.5;
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

float3 rayPlaneIntersect(float3 rayPos, float3 rayDir, float3 planeCenter, float3 planeNormal) {
    return rayPos + rayDir * (dot(planeNormal, planeCenter - rayPos) / dot(planeNormal, rayDir));
}

float normalDistributionGGXRect(float NdotH, float alpha, float alphaPrime) {
    float alpha2 = alpha * alpha;
    float alphaPrime3 = alphaPrime * alphaPrime * alphaPrime;
    float NdotH2 = NdotH * NdotH;
    return (alpha2 * alphaPrime3) / (pow(NdotH2 * (alpha2 - 1.) + 1., 2.));
}

float3 CalculatePlaneIntersection(float3 viewPosition, float3 reflectionVector, float3 lightDirection, float3 rectangleLightCenter) {
    return viewPosition + reflectionVector * (dot(lightDirection, rectangleLightCenter - viewPosition) / dot(lightDirection, reflectionVector));
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

float3 ClosestPointOnRectangle(float3 pt, float3 origin, float3 normal, float3 right, float3 up) {
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

    float len0 = LengthSq(p0 - pt);
    float len1 = LengthSq(p1 - pt);
    float len2 = LengthSq(p2 - pt);
    float len3 = LengthSq(p3 - pt);
    float len4 = LengthSq(origin - pt);

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
    float lightRadiusSq,
    float3 lightColor, // Radiance of light
    float3 planeNormal,
    float3 planeRight,
    float3 planeUp
) {
    // https://alextardif.com/arealights.html
    // https://www.shadertoy.com/view/3dsBD4
    specularColor *= 1.5; // tweak to match point lights

    lightPos -= planeNormal * 1.1; // hack: workaround for light being 1 unit off of surface for some reason

    // shift the rectangle off of the surface so it lights it more evenly
    // note that this does not affect the position of the reflection
    float3 surfaceOffset = planeNormal * 3;
    //planeUp *= 0.8; // shrink the diffuse plane slightly to prevent-near wall hotspots
    float vWidth = length(planeRight);
    float vHeight = length(planeUp);
    float3 closestDiffusePoint = ClosestPointOnRectangle(worldPos, lightPos + surfaceOffset, planeNormal, planeRight, planeUp);

    planeRight = normalize(planeRight);
    planeUp = normalize(planeUp);

    //float windingCheck = dot(cross(planeRight, planeUp), lightPos - worldPos);

    float3 specular = float3(0, 0, 0);

    {
        // Calculate specular
        // reconstruct the rectangle
        Rect rect;
        rect.a = lightPos + planeRight + planeUp;
        rect.b = lightPos - planeRight + planeUp;
        rect.c = lightPos - planeRight - planeUp;
        rect.d = lightPos + planeRight - planeUp;

        const float3 v0 = rect.a - worldPos;
        const float3 v1 = rect.b - worldPos;
        const float3 v2 = rect.c - worldPos;
        const float3 v3 = rect.d - worldPos;
        const float3 vLight = lightPos - worldPos;

        // Next, we approximate the solid angle (visible portion of rectangle) of lighting by taking the average of the
        // four corners and center point of the rectangle.
        // See the Frostbite paper for different ways of doing this with varying degrees of accuracy.
        //float solidAngle = rectSolidAngle(v0, v1, v2, v3);
        //solidAngle = saturate(solidAngle);
        //return float3(solidAngle, solidAngle, solidAngle);

        // Average each point
        float nDotL = /*solidAngle **/ 0.2 * (
            saturate(dot(normalize(v0), normal)) +
            saturate(dot(normalize(v1), normal)) +
            saturate(dot(normalize(v2), normal)) +
            saturate(dot(normalize(v3), normal)) +
            saturate(dot(normalize(vLight), normal)));

        // find the closest point on the rectangle
        float3 r = reflect(viewDir, normal);
        float3 reflectedIntersect = CalculatePlaneIntersection(worldPos, r, planeNormal, lightPos);

        // We then find the difference between that point and the center of the light,
        // and find that result represented in the 2D space on the light's plane in view space.
        float3 reflectedDir = reflectedIntersect - lightPos;
        float2 reflectedPlanePoint = float2(dot(reflectedDir, planeRight),
                                            dot(reflectedDir, planeUp));
        //bool outside =
        //    intersectPlanePoint.x > vWidth || intersectPlanePoint.x < -vWidth ||
        //    intersectPlanePoint.y > vHeight || intersectPlanePoint.y < -vHeight;

        float2 nearestReflectedPoint = float2(clamp(reflectedPlanePoint.x, -vWidth, vWidth),
                                              clamp(reflectedPlanePoint.y, -vHeight, vHeight));
        //float2 c = min(abs(reflectedPlanePoint), float2(vWidth, vHeight)) * sign(reflectedPlanePoint);

        // fade out the specularity as it gets further from the reflected plane
        float planeFactor = 1.0 - saturate(length(nearestReflectedPoint - reflectedPlanePoint) * pow(1 - roughness, 2.2));
        //float specFactor = 1.0 - saturate(length(nearestReflectedPoint - reflectedPlanePoint) * smoothstep(0, 1, roughness));

        float3 L = lightPos + planeRight * nearestReflectedPoint.x + planeUp * nearestReflectedPoint.y - worldPos;
        float3 l = normalize(L);
        float3 h = normalize(viewDir - l); // half angle
        //float lightDist = length(L);

        //specFactor *= 1 + pow(1 - vDotH, 5) * 2; // fresnel (boosted)
        //float nDotH = max(dot(normal, h), 0); // half angle
        //float vDotH = dot(h, viewDir);

        //float3 halfVec = normalize(lightDir - viewDir);
        float nDotH = saturate(dot(-h, normal));
        float gloss = RoughnessToGloss(roughness);

        float specFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8; // blinn-phong
        specFactor *= planeFactor;
        //float fresnel = pow(1 - saturate(dot(lightDir, halfVec)), 5);
        //float3 lightDir = normalize(lightPos - worldPos);

        //float3 halfVec = normalize(lightDir - viewDir);
        specFactor *= 1 + pow(1 - saturate(dot(viewDir, h)), 5); // fresnel
        //FSchlick(specularColor, diffuse, l, h);

        //float3 halfVec = normalize(L - viewDir);
        //float nDotH = saturate(dot(-h, normal));
        //float nDotL = saturate(dot(normal, lightDir));

        // fade out specular as it gets closer to view angle
        float viewFactor = dot(cross(planeRight, planeUp), L);
        specFactor *= saturate(viewFactor - 1.3);

        //float nDotV = dot(-normal, viewDir);
        //specFactor *= geometrySmith(nDotV, nDotL, roughness);
        float rDotL = dot(r, normalize(vLight));
        //specFactor *= pow(1 + specFactor, 4);
        //float rDotL = clamp(dot(r, vLight), 0, 2); // should this be normalized?
        //specFactor *= specularMask * rDotL * nDotL;
        specFactor *= SpecularMultFromRoughness(roughness);
        specFactor *= 0.35; // fudge
        specular = specFactor * rDotL * nDotL * specularColor;
    }
    // float3 light = (specular + diffuseFactor) * falloff * lightColor * luminosity;	
    //if (windingCheck < 0)
    //    falloff = 0; // Don't show specular on surfaces behind the light

    //gloss = ClampGloss(gloss, lightDistSq);
    float nDotL2 = saturate(dot(normal, normalize(closestDiffusePoint - worldPos)));
    float lightRadius = sqrt(lightRadiusSq);
    // subtract the light's rectangular area from the light radius so it doesn't extend past the cull radius. 1.5 is diagonal distance.
    lightRadiusSq = pow(lightRadius + max(vWidth, vHeight), 2);
    float3 lightDir = closestDiffusePoint - worldPos;
    float lightDistSq = dot(lightDir, lightDir);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);
    float falloff = lightRadiusSq * (invLightDist * invLightDist);
    falloff = falloff - rsqrt(falloff);
    falloff = clamp(falloff, 0, 15); // clamp nearby surface distance to prevent hotspots

    //falloff * nDotL * lightColor * diffuse
    return max(0, falloff * (lightColor * nDotL2 * diffuse + specular));
}

float3 ApplyRectLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadiusSq,
    float3 lightColor, // Radiance of light
    float3 planeNormal,
    float3 planeRight,
    float3 planeUp
) {
    // shift the rectangle off of the surface so it lights it more evenly
    // note that this does not affect the position of the reflection
    float3 surfaceOffset = planeNormal * 0;

    // reconstruct the rectangle
    Rect rect;
    rect.a = lightPos + planeRight + planeUp + surfaceOffset;
    rect.b = lightPos - planeRight + planeUp + surfaceOffset;
    rect.c = lightPos - planeRight - planeUp + surfaceOffset;
    rect.d = lightPos + planeRight - planeUp + surfaceOffset;

    float vWidth = length(planeRight);
    float vHeight = length(planeUp);

    planeRight = normalize(planeRight);
    planeUp = normalize(planeUp);

    float windingCheck = dot(cross(planeRight, planeUp), lightPos - worldPos);

    const float3 v0 = rect.a - worldPos;
    const float3 v1 = rect.b - worldPos;
    const float3 v2 = rect.c - worldPos;
    const float3 v3 = rect.d - worldPos;
    const float3 vLight = lightPos - worldPos;

    // Next, we approximate the solid angle (visible portion of rectangle) of lighting by taking the average of the
    // four corners and center point of the rectangle.
    // See the Frostbite paper for different ways of doing this with varying degrees of accuracy.
    float solidAngle = rectSolidAngle(v0, v1, v2, v3);

    // Average each point
    float nDotL = solidAngle * 0.2 * (
        saturate(dot(normalize(v0), normal)) +
        saturate(dot(normalize(v1), normal)) +
        saturate(dot(normalize(v2), normal)) +
        saturate(dot(normalize(v3), normal)) +
        saturate(dot(normalize(vLight), normal)));

    float3 specularFactor = float3(0, 0, 0);
    float falloff = 1;
    float roughness = 0.20;

    float lightRadius = sqrt(lightRadiusSq);
    float dist = distance(worldPos, lightPos);

    {
        // find the closest point on the rectangle
        float3 r = reflect(viewDir, normal);
        float3 intersectPoint = CalculatePlaneIntersection(worldPos, r, planeNormal, lightPos);

        // We then find the difference between that point and the center of the light,
        // and find that result represented in the 2D space on the light's plane in view space.
        float3 intersectionVector = intersectPoint - lightPos;
        float2 intersectPlanePoint = float2(dot(intersectionVector, planeRight), dot(intersectionVector, planeUp));
        //bool outside =
        //    intersectPlanePoint.x > vWidth || intersectPlanePoint.x < -vWidth ||
        //    intersectPlanePoint.y > vHeight || intersectPlanePoint.y < -vHeight;

        float2 nearestReflectedPoint = float2(clamp(intersectPlanePoint.x, -vWidth, vWidth), clamp(intersectPlanePoint.y, -vHeight, vHeight));
        float specularAmount = dot(r, vLight);
        //float specDist = length(nearest2DPoint - intersectPlanePoint);
        //specDist = max(specDist, lightRadiusSq * 0.01); // clamp the nearby spec dist to prevent point highlights
        float specFactor = 1.0 - saturate(length(nearestReflectedPoint - intersectPlanePoint) * pow(1 - roughness, 4));

        //if (outside) {
        //float3 nearestPoint = input.lightPositionViewCenter.xyz + (right * nearest2DPoint.x + up * nearest2DPoint.y);
        //float dist = distance(positionView, nearestPoint);
        //float falloff = 1.0 - saturate(dist / lightRadius);
        //}

        CutoffLightValue(lightRadius, dist, 0.8, specFactor);

        if (windingCheck < 0)
            specFactor = 0; // Don't show specular on surfaces behind the light

        specularFactor += specularColor * specFactor * specularAmount * nDotL;
    }
    // float3 light = (specularFactor + diffuseFactor) * falloff * lightColor * luminosity;	

    // Distance falloff
    float cutoff = lightRadius * 0.80; // cutoff distance for fading to black
    if (dist > cutoff) {
        falloff = lerp(falloff, 0, (dist - cutoff) / (lightRadius - cutoff));
    }

    float3 color = diffuseColor * nDotL * lightRadius * falloff * lightColor + specularFactor;
    return max(0, color); // goes to inf when behind the light
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

void ShadeLights(inout float3 colorSum,
                 uint2 pixelPos,
                 float3 diffuse,
                 float specularMask, // Where is it shiny or dingy?
                 float3 normal,
                 float3 viewDir,
                 float3 worldPos,
                 MaterialInfo material
) {
    uint2 tilePos = GetTilePos(pixelPos, InvTileDim.xy);
    uint tileIndex = GetTileIndex(tilePos, TileCount.x);
    uint tileOffset = GetTileOffset(tileIndex);
    uint pointLightCount = LightGrid.Load(tileOffset);
    uint tubeLightCount = LightGrid.Load(tileOffset + 4);
    uint rectLightCount = LightGrid.Load(tileOffset + 8);

    uint tileLightLoadOffset = tileOffset + TILE_HEADER_SIZE;
    float diffuseMult = 0.5;
    const float metalDiffuseFactor = 0.7;
    const float metalSpecularFactor = 10;
    uint n = 0;
    for (n = 0; n < pointLightCount; n++, tileLightLoadOffset += 4) {
        uint lightIndex = LightGrid.Load(tileLightLoadOffset);
        LightData light = LightBuffer[lightIndex];

        float3 specularColor = lerp(light.color, 0, material.Metalness) + lerp(0, diffuse * light.color * metalSpecularFactor, material.Metalness);
        specularColor *= material.SpecularStrength;
        light.color = lerp(light.color, 0, material.Metalness) + lerp(0, diffuse * light.color * metalDiffuseFactor, material.Metalness);
#if 1
        colorSum += ApplyPointLight(
            diffuse, specularColor, specularMask, material.Roughness,
            normal, viewDir, worldPos, light.pos,
            light.radiusSq, light.color * diffuseMult
        );
#elif 0
        float sphereRadius = 5;
        lightData.pos += float3(0, -6, 0); // shift to center
        colorSum += ApplySphereLight(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos, sphereRadius,
            lightData.radiusSq, lightData.color
        );
#elif 0
        lightData.pos += float3(0, -6, 0); // shift to center
        float3 lightEnd = lightData.pos + float3(20, 0, 0);
        colorSum += ApplyCylinderLight(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos, lightEnd,
            lightData.radiusSq, lightData.color
        );
#endif
    }

    for (n = 0; n < tubeLightCount; n++, tileLightLoadOffset += 4) {
        uint lightIndex = LightGrid.Load(tileLightLoadOffset);
        LightData light = LightBuffer[lightIndex];

        float3 fresnel = float3(0, 0, 0);
        //float nDotV = max(dot(normal, viewDir), 0);
        float3 r = reflect(viewDir, normal);
        float4 diffSpec = LineLight(worldPos, normal, viewDir, r, float3(1, 1, 1), material.Roughness, sqrt(light.radiusSq), light.tubeRadius * 2, light.pos, light.pos2, fresnel);

        float3 lineLightKd = 1. - fresnel;
        lineLightKd *= 1. - material.Metalness;
        float LINE_LIGHT_INTENSITY = 25;

        colorSum += (lineLightKd * PI_INV * light.color + diffSpec.xyz) * LINE_LIGHT_INTENSITY * diffSpec.w /** lineLightAttenuation*/;
    }

    for (n = 0; n < rectLightCount; n++, tileLightLoadOffset += 4) {
        uint lightIndex = LightGrid.Load(tileLightLoadOffset);
        LightData light = LightBuffer[lightIndex];

        float3 specularColor = lerp(light.color, 0, material.Metalness) + lerp(0, diffuse * light.color * metalSpecularFactor, material.Metalness);
        specularColor *= material.SpecularStrength;
        light.color = lerp(light.color, 0, material.Metalness) + lerp(0, diffuse * light.color * metalDiffuseFactor, material.Metalness);

        colorSum += ApplyRectLight2(
            diffuse, specularColor, specularMask, material.Roughness,
            normal, viewDir, worldPos, light.pos,
            light.radiusSq, light.color * diffuseMult, light.normal, light.right, light.up
        );
    }
}
