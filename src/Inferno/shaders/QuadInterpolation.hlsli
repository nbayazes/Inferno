#ifndef QUAD_INTERPOLATION_HLSLI
#define QUAD_INTERPOLATION_HLSLI

/*
    Bilinear interpolation on quads using barycentric coordinates
    https://gpuopen.com/learn/bilinear-interpolation-quadrilateral-barycentric-coordinates/

    Each level side is emitted as 6 consecutive vertices (two triangles) in a single vertex buffer
    drawn with a base vertex of 0. The vertex shader reads all 4 corners of its side from the raw
    vertex buffer, so every pixel can bilinearly interpolate the corner attributes instead of only
    using its triangle's 3 vertices. This removes the seam along the diagonal that splits the side.

    Corners: c0 = A (u=0,v=0), c1 = B (1,0), c2 = C (1,1), c3 = D (0,1)
    Bilinear surface: X(u,v) = A + u*R + v*P + u*v*Q, with R = B - A, P = D - A, Q = A - B + C - D

    (beta, gamma) are barycentrics of X in the frame of triangle ABD: X = A + beta*R + gamma*P.
    They are affine in position, so the per-vertex values are exact when interpolated by
    the rasterizer regardless of which diagonal the side is split on.
    Projecting Q into that frame gives Q = a*R + b*P, which in the article's notation is
    a = A_u / B and b = -A_v / B.

    In this frame Inigo Quilez's inverse bilinear (https://iquilezles.org/articles/ibilinear) has
    e = (1,0), f = (0,1), g = (a,b), h = (beta,gamma), and reduces to the same quadratics.

    Usage:
      - The root signature must bind the level vertex buffer as an SRV at t16, visible to the vertex shader
      - Add a QuadInterpolants member to the VS output / PS input
      - VS: output.quad = GetQuadInterpolants(vertexId, uvOffset, uv2Offset)
      - PS: InterpolateQuad(input.quad, uv, uv2, col, lightDir)
*/

// Raw level vertex buffer. Must match LevelVertex in VertexTypes.h (checked by static_asserts in ShaderLibrary.h)
ByteAddressBuffer LevelVertices : register(t16);
static const uint LEVEL_VERTEX_STRIDE = 92; // sizeof(LevelVertex)

// Corner order of each triangle vertex for the two split types (Segment.h TRI02 / TRI13)
static const uint TRI02_CORNERS[6] = { 0, 1, 2, 0, 2, 3 };
static const uint TRI13_CORNERS[6] = { 0, 1, 3, 3, 1, 2 };

// Data passed from the vertex shader to the pixel shader
struct QuadInterpolants {
    centroid float2 coord : QUADCOORD; // (beta, gamma) barycentrics relative to triangle c0 c1 c3
    nointerpolation float3 constants : QUADCONST; // xy: quad shape constants, z: 1 if valid
    nointerpolation float4 uv[4] : QUADUV; // per corner: xy = uv, zw = uv2
    nointerpolation float4 col[4] : QUADCOLOR; // per corner vertex color
    nointerpolation float3 lightDir[4] : QUADLIGHTDIR; // per corner light dir
};

struct QuadCorner {
    float3 pos;
    float2 uv;
    float4 col;
    float2 uv2;
    float3 lightDir;
};

QuadCorner LoadQuadCorner(uint index) {
    uint offset = index * LEVEL_VERTEX_STRIDE;
    QuadCorner c;
    c.pos = asfloat(LevelVertices.Load3(offset + 0));
    c.uv = asfloat(LevelVertices.Load2(offset + 12));
    c.col = asfloat(LevelVertices.Load4(offset + 20));
    c.uv2 = asfloat(LevelVertices.Load2(offset + 36));
    c.lightDir = asfloat(LevelVertices.Load3(offset + 80));
    return c;
}

// Returns (a, b, valid). Valid is 0 for degenerate or non-convex quads,
// which fall back to the regular per-triangle interpolation.
float3 CalculateQuadConstants(float3 c0, float3 c1, float3 c2, float3 c3) {
    float3 r = c1 - c0;
    float3 p = c3 - c0;
    float3 q = c0 - c1 + c2 - c3;
    float3 rxp = cross(r, p); // unnormalized face normal of ABD, projects C into the ABD plane
    float k2 = dot(rxp, rxp);
    float len2 = max(dot(r, r), dot(p, p));
    if (k2 <= 1e-8 * len2 * len2)
        return 0; // degenerate

    float a = dot(cross(q, p), rxp) / k2;
    float b = dot(cross(r, q), rxp) / k2;

    // C = (1 + a, 1 + b) in the ABD frame. Convexity requires all of these to be positive.
    if (min(min(1 + a, 1 + b), 1 + a + b) < 1e-3)
        return 0;

    return float3(a, b, 1);
}

// Solves for the bilinear coordinates (u, v) from the ABD barycentrics (beta, gamma).
// From beta = u*(1 + a*v) and gamma = v*(1 + b*u):
// v: a*v^2 + (1 + b*beta - a*gamma)*v - gamma = 0, solved with the numerically stable root form
//    (reduces to v = gamma for parallelograms without a branch)
// u: substituted back as u = beta / (1 + a*v), avoiding a second sqrt (as in iquilezles.org/articles/ibilinear)
// Clamping v first keeps the divisor >= min(1, 1 + a), which CalculateQuadConstants guarantees is positive.
float2 CalculateBilinear(float2 bary, float2 k) {
    float beta = bary.x, gamma = bary.y;
    float a = k.x, b = k.y;
    float kv = 1 + b * beta - a * gamma;
    float v = saturate(2 * gamma / max(kv + sqrt(max(kv * kv + 4 * a * gamma, 0)), 1e-6));
    float u = saturate(beta / (1 + a * v));
    return float2(u, v);
}

float4 Bilerp(float4 c0, float4 c1, float4 c2, float4 c3, float2 st) {
    return lerp(lerp(c0, c1, st.x), lerp(c3, c2, st.x), st.y);
}

float3 Bilerp(float3 c0, float3 c1, float3 c2, float3 c3, float2 st) {
    return lerp(lerp(c0, c1, st.x), lerp(c3, c2, st.x), st.y);
}

// Vertex shader: loads the four corners of the side this vertex belongs to.
// uvOffset and uv2Offset are added to the corner uvs (texture scrolling).
QuadInterpolants GetQuadInterpolants(uint vertexId, float2 uvOffset, float2 uv2Offset) {
    QuadInterpolants quad;

    uint slot = vertexId % 6;
    uint quadStart = vertexId - slot;

    // TRI02 repeats corner 0 in slot 3, TRI13 has corner 3 there
    float3 slot0 = asfloat(LevelVertices.Load3(quadStart * LEVEL_VERTEX_STRIDE));
    float3 slot3 = asfloat(LevelVertices.Load3((quadStart + 3) * LEVEL_VERTEX_STRIDE));
    bool tri02 = all(slot0 == slot3);

    QuadCorner corners[4];
    corners[0] = LoadQuadCorner(quadStart + 0);
    corners[1] = LoadQuadCorner(quadStart + 1);
    corners[2] = LoadQuadCorner(quadStart + (tri02 ? 2 : 5));
    corners[3] = LoadQuadCorner(quadStart + (tri02 ? 5 : 2));

    float3 k = CalculateQuadConstants(corners[0].pos, corners[1].pos, corners[2].pos, corners[3].pos);
    quad.constants = k;

    // ABD barycentrics of each corner
    float2 cornerBary[4] = { float2(0, 0), float2(1, 0), float2(1 + k.x, 1 + k.y), float2(0, 1) };
    uint corner = tri02 ? TRI02_CORNERS[slot] : TRI13_CORNERS[slot];
    quad.coord = cornerBary[corner];

    [unroll]
    for (uint i = 0; i < 4; i++) {
        quad.uv[i] = float4(corners[i].uv + uvOffset, corners[i].uv2 + uv2Offset);
        quad.col[i] = float4(corners[i].col.rgb, saturate(corners[i].col.a));
        quad.lightDir[i] = corners[i].lightDir;
    }

    return quad;
}

// Pixel shader: replaces the per-triangle attributes with bilinear interpolation across the quad.
// Leaves them unchanged if the quad was degenerate or non-convex.
void InterpolateQuad(QuadInterpolants quad, inout float2 uv, inout float2 uv2, inout float4 col, inout float3 lightDir) {
    if (quad.constants.z <= 0)
        return;

    float2 st = CalculateBilinear(quad.coord, quad.constants.xy);
    float4 quadUV = Bilerp(quad.uv[0], quad.uv[1], quad.uv[2], quad.uv[3], st);
    uv = quadUV.xy;
    uv2 = quadUV.zw;
    col = Bilerp(quad.col[0], quad.col[1], quad.col[2], quad.col[3], st);
    lightDir = Bilerp(quad.lightDir[0], quad.lightDir[1], quad.lightDir[2], quad.lightDir[3], st);
}

#endif
