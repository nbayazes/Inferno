#include "LightGrid.hlsli"

static const float SMOL_EPS = .0000002;
static const float PI = 3.14159265f;

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
    float3 lightColor // Radiance of directional light
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
    return NdotX / max(NdotX * (1. - k) + k, SMOL_EPS);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float roughnessplusone = roughness + 1.;
    float k = roughnessplusone * roughnessplusone / 8.;

    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

float normalDistributionGGXLine(float NdotH, float alpha, float alphaPrime) {
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    return (alpha2 * alphaPrime) / pow(NdotH2 * (alpha2 - 1.) + 1., 2.);
}

float4 lineLight(float3 pos,
                 float3 norm,
                 float3 viewDir,
                 float3 refl, // Reflect direction
                 float3 f0, // vec3 F0 = mix(reflectance, albedo, metalness);
                 float NdotV, // float NdotV = max(dot(normal, viewDirection), 0.);
                 float roughness,
                 out float3 fresnel, out float attenuation,
                 float3 lineStart,
                 float3 lineEnd,
                 float lightRadius,
                 float volumeRadius, float intensity) {
    float4 result = float4(0, 0, 0, 0);
    float3 l0 = lineStart - pos, l1 = lineEnd - pos;
    float lengthL0 = length(l0), lengthL1 = length(l1);
    float NdotL0 = dot(norm, l0) / (2. * lengthL0);
    float NdotL1 = dot(norm, l1) / (2. * lengthL1);
    result.w = (2. * saturate(NdotL0 + NdotL1)) /
        (lengthL0 * lengthL1 + dot(l0, l1) + 2.); // NdotL

    float3 ld = l1 - l0;
    float RdotL0 = dot(refl, l0);
    float RdotLd = dot(refl, ld);
    float L0dotLd = dot(l0, ld);
    float distLd = length(ld);

    float t = (RdotL0 * RdotLd - L0dotLd) / (distLd * distLd - RdotLd * RdotLd);

    // point on the line
    float3 closestPoint = l0 + ld * saturate(t);
    // point on the tube based on its radius
    float3 centerToRay = dot(closestPoint, refl) * refl - closestPoint;
    closestPoint = closestPoint + centerToRay * saturate(lightRadius / length(centerToRay));
    float3 l = normalize(closestPoint);
    float3 h = normalize(viewDir + l);
    float lightDist = length(closestPoint);

    float NdotH = max(dot(norm, h), 0.);
    float VdotH = dot(h, viewDir);

    float denom = lightDist / volumeRadius;
    attenuation = 1. / (denom * denom + 1.);

    // attenuation *= softShadow(Ray(p + n * EPS, normalize(l0 + ld * .5)));

    float alpha = roughness * roughness;
    float alphaPrime = saturate(alpha + (lightRadius / (2. * lightDist)));

    fresnel = fresnelSchlick(f0, VdotH);
    result.xyz = normalDistributionGGXLine(NdotH, alpha, alphaPrime)
        * geometrySmith(NdotV, result.w, roughness)
        * fresnel;

    float4 lineLightDiffSpec = result;

    //vec3 lineLightKd = 1. - lineLightFresnel;
    //lineLightKd *= 1. - metalness;

    //float3 col = (lineLightKd * PI_INV * albedo + lineLightDiffSpec.xyz)
    //        * intensity * lineLightDiffSpec.w * lineLightAttenuation;

    return result;
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
    //float invLightDist = rsqrt(lightDistSq);

    lightDir = normalize(lightDir);

    // clamp the distance to prevent pinpoint hotspots near surfaces
    //float lightDistSq2 = max(lightDistSq, lightRadiusSq * 0.01);
    //float invLightDist = rsqrt(lightDistSq2);
    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);

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

    // Modifying the normal requires the original distance
    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);
    lightDir = normalize(closestPoint);

    // clamp the distance to prevent pinpoint hotspots near surfaces
    //float lightDistSq2 = max(lightDistSq, lightRadiusSq * 0.01);
    //invLightDist = rsqrt(lightDistSq2);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    // scale gloss down to 0 when light is close to prevent hotspots
    gloss = smoothstep(0, 125, lightDistSq) * gloss;


    //float3 halfVec = normalize(lightDir - viewDir);
    //float nDotH = saturate(dot(halfVec, normal));

    //float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;
    //float nDotL = saturate(dot(normal, lightDir));

    //float3 l = normalize(closestPoint);
//    {
//        float3 h = normalize(viewDir + lightDir);
//        float lightDist = length(closestPoint);
    
//        float NdotL = max(dot(normal, lightDir), 0);
//        float NdotH = max(dot(normal, h), 0);
//        float NdotV = max(dot(normal, viewDir), 0.);
//        float VdotH = max(dot(h, viewDir), 0);

//        const float metalness = 0;
//        float attenuation = pow(saturate(1 - pow(lightDist / sphereRadius, 4)), 2) / (lightDist * lightDist + 1);
//    //float3 f0 = lerp(reflectance, albedo, metalness);
//        float3 f0 = SILVER_F0;
//        float3 fresnel = fresnelSchlick(f0, VdotH);

//        const float roughness = 0.5; // 1-gloss?

//        float alpha = roughness * roughness;
//        float alphaPrime = saturate(alpha + (sphereRadius / (2. * lightDist)));
//        float3 specular = normalDistributionGGXSphere(NdotH, alpha, alphaPrime)
//        * geometrySmith(NdotV, NdotL, roughness)
//        * fresnel;
    
//        float4 diffSpec = float4(specular, NdotL);

//        float3 sphereLightKd = 1. - fresnel;
//        sphereLightKd *= 1. - metalness;

//    //return float3(1, 0, 0);
//#define SPHERE_LIGHT_INTENSITY 256.
//        return (sphereLightKd * PI_INV * diffuseColor + diffSpec.xyz) * SPHERE_LIGHT_INTENSITY * diffSpec.w * attenuation;
//    }

    //FSchlick(specularColor, diffuseColor, lightDir, halfVec);
    //return distanceFalloff * (nDotL * lightColor * (diffuseColor + specularFactor * specularColor));

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
    //float3 ab = lightPos2 - lightPos;
    //float t = dot(worldPos - lightPos, ab) / dot(ab, ab);
    //t = saturate(t);
    //float3 pointOnLine = lightPos + t * ab;

    //float3 lightDir = pointOnLine - worldPos;
    //float r = sqrt(lightRadiusSq);

    //float4 result = float4(0, 0, 0, 0);
    //float3 l0 = lightPos - worldPos;
    //float3 l1 = lightPos2 - worldPos;
    //float lengthL0 = length(l0);
    //float lengthL1 = length(l1);
    //float NdotL0 = dot(normal, l0) / (2. * lengthL0);
    //float NdotL1 = dot(normal, l1) / (2. * lengthL1);
    //result.w = (2. * saturate(NdotL0 + NdotL1)) /
    //    (lengthL0 * lengthL1 + dot(l0, l1) + 2.); // NdotL

    //float3 L = input.lightPositionView.xyz - positionView;
    //float3 centerToRay = (dot(L, r) * r) - L;
    //float3 closestPoint = L + centerToRay * saturate(sphereRadius / length(centerToRay));
    //L = normalize(closestPoint);
    //float distLight = length(closestPoint);

    //// point on the line
    //float3 closestPoint = l0 + ld * saturate(t);
    //// point on the tube based on its radius
    //float3 centerToRay = dot(closestPoint, r) * r - closestPoint;
    //closestPoint = closestPoint + centerToRay * saturate(tubeRadius / length(centerToRay));
    //float3 l = normalize(closestPoint);
    //float3 h = normalize(viewDir + l);
    //float lightDist = length(closestPoint) * length(closestPoint);

    //float3 r = reflect(viewDir, normal);
    //float3 L0 = lightPos - worldPos;
    //float3 L1 = lightPos2 - worldPos;

    //float distL0 = length(L0);
    //float distL1 = length(L1);
    //float NoL0 = dot(L0, normal) / (2.0 * distL0);
    //float NoL1 = dot(L1, normal) / (2.0 * distL1);
    //float NoL = (2.0 * saturate(NoL0 + NoL1)) / (distL0 * distL1 + dot(L0, L1) + 2.0);
    //float3 Ldist = L1 - L0;
    //float RoLd = dot(r, Ldist);
    //float distLd = length(Ldist);
    //float t = (dot(r, L0) * RoLd - dot(L0, Ldist)) / (distLd * distLd - RoLd * RoLd);

    //float3 closestPoint = L0 + Ldist * saturate(t); // point on line
    //closestPoint = lightPos;
    //float3 centerToRay = dot(closestPoint, r) * r - closestPoint;
    //// move point to surface of tube
    const float tubeRadius = 2;
    //closestPoint = closestPoint + centerToRay * saturate(tubeRadius / length(centerToRay));
    //float3 L = normalize(closestPoint);
    ////float distLight = length(closestPoint);
    //float lightDist = length(closestPoint - worldPos);

    float3 r = reflect(-viewDir, normal);
    float3 L = lightPos - worldPos;
    float3 centerToRay = (dot(L, r) * r) - L;
    float3 closestPoint = L + centerToRay * saturate(tubeRadius / length(centerToRay));
    L = normalize(closestPoint);
    float lightDist = length(closestPoint);

    //float3 A = lightPos - worldPos;
    //float3 B = lightPos2 - worldPos;
    //float a2 = dot(A, A);
    //float c = dot(B, A) / a2;
    //float3 v = B - A * c;
    //float lightDistSq = dot(v, v);

    //float distL0 = length(L0);
    //float distL1 = length(L1);
    //float NoL0 = dot(L0, normal) / (2.0 * distL0);
    //float NoL1 = dot(L1, normal) / (2.0 * distL1);
    //float NoL = (2.0 * saturate(NoL0 + NoL1)) / (distL0 * distL1 + dot(L0, L1) + 2.0);
    //float3 Ldist = L1 - L0;
    //float3 r = reflect(-viewDir, normal);
    //float RoLd = dot(r, Ldist);
    //float distLd = length(Ldist);
    //float t = (dot(r, L0) * RoLd - dot(L0, Ldist)) / (distLd * distLd - RoLd * RoLd);

    //float3 closestPoint = L0 + Ldist * saturate(t);
    //float3 centerToRay = dot(closestPoint, r) * r - closestPoint;
    //closestPoint = closestPoint + centerToRay * saturate(sqrt(lightRadiusSq) / length(centerToRay));
    //float3 lightDir = h;
    //float distLight = length(closestPoint);
    //float lightDistSq = distLight * distLight;


    //float lightDistSq = dot(lightDir, lightDir);
    float lightDistSq = lightDist * lightDist;

    // Modifying the normal requires the original distance
    float invLightDist = rsqrt(lightDistSq);
    float3 lightDir = normalize(closestPoint - worldPos);

    //lightDir *= invLightDist;

    // clamp the distance to prevent pinpoint hotspots near surfaces
    //float lightDistSq2 = max(lightDistSq, lightRadiusSq * 0.01);
    //invLightDist = rsqrt(lightDistSq2);

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
    float3 lightColor // Radiance of directional light
) {
    //float3 lightDir = lightPos - worldPos;
    //float lightDistSq = dot(lightDir, lightDir);
    //float invLightDist = rsqrt(lightDistSq);

    //lightDir = normalize(lightDir);

    float3 planeNormal = float3(0, -1, 0);
    float3 planeRight = float3(1, 0, 0);
    float3 planeUp = float3(0, 0, 1);
    Rect rect;
    float vWidth = 10;
    float vHeight = 2.5;

    //float windingCheck = dot(cross(planeRight, planeUp), lightPos - worldPos);
    //if (windingCheck > 0.)
    //    return float3(0, 0, 0);

    // reconstruct the rectangle
    rect.a = lightPos + planeRight * vWidth + planeUp * vHeight;
    rect.b = lightPos - planeRight * vWidth + planeUp * vHeight;
    rect.c = lightPos - planeRight * vWidth - planeUp * vHeight;
    rect.d = lightPos + planeRight * vWidth - planeUp * vHeight;

    float3 v0 = rect.a - worldPos;
    float3 v1 = rect.b - worldPos;
    float3 v2 = rect.c - worldPos;
    float3 v3 = rect.d - worldPos;
    
    float solidAngle = rectSolidAngle(v0, v1, v2, v3);

    float nDotL = solidAngle * 0.2 * (
        saturate(dot(normalize(v0), normal)) +
        saturate(dot(normalize(v1), normal)) +
        saturate(dot(normalize(v2), normal)) +
        saturate(dot(normalize(v3), normal)) +
        saturate(dot(normalize(lightPos - worldPos), normal)));

    {
    // specular
        float3 r = reflect(viewDir, normal);
        float3 intersectVec = rayPlaneIntersect(worldPos, r, lightPos, planeNormal) - lightPos;
    // project point on the plane on which the rectangle lies
        float2 intersectPlane = float2(dot(intersectVec, planeRight), dot(intersectVec, planeUp));
    // translate the point to the top-right quadrant of the rectangle, project it on
    // the rectangle or its edge and translate back using sign of the original point.
        float2 c = min(abs(intersectPlane), float2(vWidth, vHeight)) * sign(intersectPlane);
        float3 L = lightPos + planeRight * c.x + planeUp * c.y - worldPos;

        //float2 nearest2DPoint = float2(clamp(intersectPlane.x, -vWidth, vWidth), clamp(intersectPlane.y, -vHeight, vHeight));
        //float3 nearestPoint = lightPos + (planeRight * nearest2DPoint.x + planeUp * nearest2DPoint.y);
        //L = nearestPoint;
    //float lightDistSq = diffuse; // maybe?

        float3 l = normalize(L);
        float3 h = normalize(viewDir + l);
        float lightDist = length(L);

        // modify 1/d^2 * R^2 to fall off at a fixed radius
        // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
        //{
        //    float lightDistSq = dot(L, L);
        //    float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);
        //    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
        //    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));
        //    return max(0, diffuseColor * NdotL * distanceFalloff);

        //    //float3 lightDir = L;
        //    //float3 halfVec = normalize(lightDir - viewDir);
        //    //float nDotH = saturate(dot(halfVec, normal));
        //    ////float NdotH = max(dot(normal, h), 0.);
        //    //float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;
        //    //FSchlick(specularColor, diffuseColor, lightDir, halfVec);

        //    // intersect the view ray with the plane
        //    float3 planeIntersection = CalculatePlaneIntersection(worldPos, r, planeNormal, lightPos);

        //    // find the closest point on the rectangle
        //    float3 intersectionVector = planeIntersection - lightPos;
        //    float2 intersectPlanePoint = float2(dot(intersectionVector, planeRight), dot(intersectionVector, planeUp));
        //    float2 nearest2DPoint = float2(clamp(intersectPlanePoint.x, -vWidth, vWidth), clamp(intersectPlanePoint.y, -vHeight, vHeight));
        //    float3 nearestPoint = lightPos + (planeRight * nearest2DPoint.x + planeUp * nearest2DPoint.y);

        //    float dist = distance(worldPos, nearestPoint);
        //    float falloff = 1.0 - saturate(dist * dist / sqrt(lightRadiusSq)); // linear falloff
        //    float diffuseFactor = 4;
        //    float luminosity = 1;
        //    float specularFactor = 1;
        //    float3 light = (specularFactor + diffuseFactor) * falloff * lightColor * luminosity;
        //    return max(0, light);
        //    return max(0, diffuseColor * NdotL * 50);

        //    //float nDotL = saturate(dot(normal, lightDir));
        //    //return distanceFalloff * (NdotL * lightColor * (diffuseColor + specularFactor * specularColor));
        //}

        float NdotH = max(dot(normal, h), 0);
        float VdotH = dot(h, viewDir);

        float roughness = 0.5;
        float alpha = roughness * roughness;
        float alphaPrime = saturate(alpha + (sqrt(lightRadiusSq) / (2 * lightDist)));

    //float3 f0 = mix(reflectance, albedo, metalness);
        float3 f0 = SILVER_F0;
        float metalness = 0;
        float NdotV = max(dot(normal, viewDir), 0.);

        float fresnel = fresnelSchlick(f0, VdotH);
        float spec = normalDistributionGGXRect(NdotH, alpha, alphaPrime)
            * geometrySmith(NdotV, nDotL, roughness)
            * fresnel;

        //float fresnel = 0;
        
        float3 kd = 1. - fresnel;
        kd *= 1. - metalness;

        const float intensity = 500;

        //float attenuation = softShadow(Ray(p + n * EPS, normalize(rect.center)));
        float attenuation = 1;

        float3 lightDir = L;
        float3 halfVec = normalize(lightDir - viewDir);
        float nDotH = saturate(dot(halfVec, normal));
            //float NdotH = max(dot(normal, h), 0.);
        float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;
        FSchlick(specularColor, diffuseColor, lightDir, halfVec);

        // (rectLightKd * PI_INV * albedo + rectLightDiffSpec.xyz) * intensity * rectLightDiffSpec.w * rectLightAttenuation;
        float3 color = (kd * PI_INV * diffuseColor + spec) * intensity * nDotL * attenuation;
        color = diffuseColor * nDotL * 25 + specularFactor * float3(0, 1, 0);
        return max(0, color);
    }

    //float invLightDist = InvLightDist(lightDistSq, lightRadiusSq);

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    //float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
    //distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    // scale gloss down to 0 when light is close to prevent hotspots
    //gloss = smoothstep(0, 125, lightDistSq) * gloss;

    
    //return distanceFalloff * ApplyLightCommon(
    //    diffuseColor,
    //    specularColor,
    //    specularMask,
    //    gloss,
    //    normal,
    //    viewDir,
    //    lightDir,
    //    lightColor
    //);

    return max(0, 100 * nDotL * lightColor * diffuseColor);
}

float3 ApplyRectLight2(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadiusSq,
    float3 lightColor // Radiance of light
) {
    float3 planeNormal = float3(0, -1, 0);
    float3 planeRight = float3(1, 0, 0);
    float3 planeUp = float3(0, 0, 1);
    Rect rect;
    float vWidth = 10;
    float vHeight = 10.0;

    // reconstruct the rectangle
    rect.a = lightPos + planeRight * vWidth + planeUp * vHeight;
    rect.b = lightPos - planeRight * vWidth + planeUp * vHeight;
    rect.c = lightPos - planeRight * vWidth - planeUp * vHeight;
    rect.d = lightPos + planeRight * vWidth - planeUp * vHeight;

    const float3 v0 = rect.a - worldPos;
    const float3 v1 = rect.b - worldPos;
    const float3 v2 = rect.c - worldPos;
    const float3 v3 = rect.d - worldPos;
    const float3 vLight = lightPos - worldPos;

    lightColor = float3(1, 0, 0);
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

    specularColor = float3(0, 1, 0); // testing
    // specular
    float3 specularFactor = float3(0, 0, 0);
    float falloff = 1;
    float2 nearest2DPoint = float2(0, 0);
    float roughness = 0.30;
    float lightRadius = sqrt(lightRadiusSq);
    float dist = distance(worldPos, lightPos);
    float cutoff = lightRadius * 0.60; // cutoff distance for fading to black

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

        nearest2DPoint = float2(clamp(intersectPlanePoint.x, -vWidth, vWidth), clamp(intersectPlanePoint.y, -vHeight, vHeight));
        float specularAmount = dot(r, vLight);
        //float specDist = length(nearest2DPoint - intersectPlanePoint);
        //specDist = max(specDist, lightRadiusSq * 0.01); // clamp the nearby spec dist to prevent point highlights
        float specFactor = 1.0 - saturate(length(nearest2DPoint - intersectPlanePoint) * pow(1 - roughness, 4));
        //if (dist < 10)
        //    specFactor = 0;

        //if (outside) {
            //float3 nearestPoint = input.lightPositionViewCenter.xyz + (right * nearest2DPoint.x + up * nearest2DPoint.y);
            //float dist = distance(positionView, nearestPoint);
            //float falloff = 1.0 - saturate(dist / lightRadius);

        //}
        float specCutoff = lightRadius * 0.80; // cutoff distance for fading to black
        if (dist > specCutoff) {
            specFactor = saturate(lerp(specFactor, 0, (dist - specCutoff) / (lightRadius - specCutoff)));
        }
        specularFactor += specularColor * specFactor * specularAmount * nDotL;
    }

    // Distance falloff
    if (dist > cutoff) {
        falloff = lerp(falloff, 0, (dist - cutoff) / (lightRadius - cutoff));
    }

    float3 color = diffuseColor * nDotL * lightRadius * 0.25 * falloff * lightColor + specularFactor;
    // float3 light = (specularFactor + diffuseFactor) * falloff * lightColor * luminosity;	
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

        //lightData.pos += float3(0, -6, 0); // shift to center
#if 0
        colorSum += ApplyPointLight(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos, 
            lightData.radiusSq, lightData.color
        );
#elif 0
        float sphereRadius = 5;
        colorSum += ApplySphereLight(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos, sphereRadius,
            lightData.radiusSq, lightData.color
        );
#elif 1
        colorSum += ApplyRectLight2(
            diffuseAlbedo, specularAlbedo, specularMask, gloss,
            normal, viewDir, worldPos, lightData.pos,
            lightData.radiusSq, lightData.color
        );
#endif
    }
}
