#include "pch.h"
#include "Formats/BBM.h"
#include "Game.EscapeSequence.h"
#include "OpenSimplex2.h"

namespace Inferno {
    constexpr float Cubic(float d0, float d2, float d3, float dx, float a0) {
        float a1 = -1.0f / 3 * d0 + d2 - 1.0f / 6 * d3;
        float a2 = 1.0f / 2 * d0 + 1.0f / 2 * d2;
        float a3 = -1.0f / 6 * d0 - 1.0f / 2 * d2 + 1.0f / 6 * d3;
        return a0 + a1 * dx + a2 * dx * dx + a3 * dx * dx * dx;
    }

    constexpr float FilterCubic(float x, float b, float c) {
        float y = 0.0f;
        float x2 = x * x;
        float x3 = x * x * x;

        if (x < 1)
            y = (12.0f - 9 * b - 6 * c) * x3 + (-18 + 12 * b + 6 * c) * x2 + (6 - 2 * b);
        else if (x <= 2)
            y = (-b - 6.0f * c) * x3 + (6 * b + 30 * c) * x2 + (-12 * b - 48 * c) * x + (8 * b + 24 * c);

        return y / 6.0f;
    }


    List<float> CubicResize(const Bitmap2D& bitmap, uint destWidth, uint destHeight) {
        List<float> heights;
        heights.resize(destWidth * destHeight);

        float xRatio = float(bitmap.Width) / destWidth;
        float yRatio = float(bitmap.Height) / destHeight;

        Array<float, 5> curve = {};

        for (uint y = 0; y < destHeight; ++y) {
            for (uint x = 0; x < destWidth; ++x) {
                auto xPixel = int(x * xRatio);
                auto yPixel = int(y * yRatio);

                float dx = xRatio * x - xPixel;
                float dy = yRatio * y - yPixel;

                // accumulate samples in adjacent rows
                for (uint samples = 0; samples < 4; samples++) {
                    auto row = yPixel - 1 + samples;

                    float a0 = bitmap.GetPixel(xPixel, row).r; // getBicPixelChannel(img, x_int, o_y, channel)
                    float d0 = bitmap.GetPixel(xPixel - 1, row).r - a0; // getBicPixelChannel(img, x_int - 1, o_y, channel) - a0
                    float d2 = bitmap.GetPixel(xPixel + 1, row).r - a0; //getBicPixelChannel(img, x_int + 1, o_y, channel) - a0
                    float d3 = bitmap.GetPixel(xPixel + 2, row).r - a0; // getBicPixelChannel(img, x_int + 2, o_y, channel) - a0

                    curve[samples] = Cubic(d0, d2, d3, dx, a0);
                }

                // Interpolate across rows
                float d0 = curve[0] - curve[1];
                float d2 = curve[2] - curve[1];
                float d3 = curve[3] - curve[1];
                float a0 = curve[1];

                heights[y * destWidth + x] = Cubic(d0, d2, d3, dy, a0);
            }
        }

        return heights;
    }

    //List<float> CubicResize(const Bitmap2D& source, uint dest_width, uint dest_height) {
    //    Array<float, 5> curve = {};
    //    const float xRatio = float(source.Width) / dest_width;
    //    const float yRatio = float(source.Height) / dest_height;

    //    List<float> heights;
    //    heights.resize(dest_width * dest_height);

    //    for (uint y = 0; y < dest_height; ++y) {
    //        for (uint x = 0; x < dest_width; ++x) {
    //            const float xStep = xRatio * float(x);
    //            const float yStep = yRatio * float(y);
    //            const float dx = xRatio * x - xStep;
    //            const float dy = yRatio * y - yStep;

    //            //x_int = int(wi * x_rate)
    //            //y_int = int(hi * y_rate)

    //            //dx = x_rate * wi - x_int
    //            //dy = y_rate * hi - y_int

    //            //for (int k = 0; k < 3; ++k) {
    //            for (int sample = 0; sample < 4; ++sample) {
    //                const int idx = yStep - 1 + sample;
    //                float a0 = source.GetPixel((int)xStep, idx).r; // get_subpixel(bmap, idx, x, k);
    //                float d0 = source.GetPixel((int)xStep - 1, idx).r; //get_subpixel(bmap, idx, x - 1, k) - a0;
    //                float d2 = source.GetPixel((int)xStep + 1, idx).r; //get_subpixel(bmap, idx, x + 1, k) - a0;
    //                float d3 = source.GetPixel((int)xStep + 2, idx).r; //get_subpixel(bmap, idx, x + 2, k) - a0;

    //                float a1 = -(1.0f / 3.0f) * d0 + d2 - (1.0f / 6.0f) * d3;
    //                float a2 = 0.5f * d0 + 0.5f * d2;
    //                float a3 = -(1.0f / 6.0f) * d0 - 0.5f * d2 + (1.0f / 6.0f) * d3;
    //                curve[sample] = a0 + a1 * dx + a2 * dx * dx + a3 * dx * dx * dx;
    //            }

    //            float d0 = curve[0] - curve[1];
    //            float d2 = curve[2] - curve[1];
    //            float d3 = curve[3] - curve[1];
    //            float a0 = curve[1];

    //            float a1 = -(1.0f / 3.0f) * d0 + d2 - (1.0f / 6.0f) * d3;
    //            float a2 = 0.5f * d0 + 0.5f * d2;
    //            float a3 = -(1.0f / 6.0f) * d0 - 0.5f * d2 + (1.0f / 6.0f) * d3;
    //            //heights[i * row_stride + j * channels + k] = Saturate(a0 + a1 * dy + a2 * dy * dy + a3 * dy * dy * dy);
    //            //heights[i * dest_width + j] = Saturate(a0 + a1 * dy + a2 * dy * dy + a3 * dy * dy * dy);
    //            heights[y * dest_width + x] = a0 + a1 * dy + a2 * dy * dy + a3 * dy * dy * dy;
    //            //}
    //        }
    //    }

    //    return heights;
    //}


    //constexpr float CubicPolate(float v0, float v1, float v2, float v3, float fracy) {
    //    float A = 0.5f * (v3 - v0) + 1.5f * (v1 - v2);
    //    float B = 0.5f * (v0 + v2) - v1 - A;
    //    float C = 0.5f * (v2 - v0);
    //    float D = v1;
    //    return D + fracy * (C + fracy * (B + fracy * A));
    //}

    //float CubicInterpolate2(Matrix data) {
    //    float x1 = CubicPolate(ndata[0, 0], ndata[1, 0], ndata[2, 0], ndata[3, 0], fracx);
    //    float x2 = CubicPolate(ndata[0, 1], ndata[1, 1], ndata[2, 1], ndata[3, 1], fracx);
    //    float x3 = CubicPolate(ndata[0, 2], ndata[1, 2], ndata[2, 2], ndata[3, 2], fracx);
    //    float x4 = CubicPolate(ndata[0, 3], ndata[1, 3], ndata[2, 3], ndata[3, 3], fracx);
    //    float y1 = CubicPolate(x1, x2, x3, x4, fracy);
    //}

    float SampleTerrain(const Bitmap2D& bitmap, Vector2 uv, int radius) {
        float sum = 0;
        float totalWeight = 0.0f;
        float m1 = 0;
        float m2 = 0;
        float mWeight = 0.0f;
        Vector2 size = { (float)bitmap.Width, (float)bitmap.Height };

        for (int y = -radius; y < radius; y++) {
            for (int x = -radius; x < radius; x++) {
                Vector2 offset((float)x, (float)y);
                Vector2 samplePos = uv * size + offset;
                samplePos.x = std::round(samplePos.x);
                samplePos.y = std::round(samplePos.y);

                Vector2 sampleDist = { std::abs(offset.x), std::abs(offset.y) };

                float weight =
                    FilterCubic(sampleDist.x, 1 / 3.0f, 1 / 3.0f) *
                    FilterCubic(sampleDist.y, 1 / 3.0f, 1 / 3.0f);

                auto sample = bitmap.GetPixel((uint)samplePos.x, (uint)samplePos.y).r;
                sum += sample * weight;
                totalWeight += weight;

                m1 += sample;
                m2 += sample * sample;
                mWeight += 1.0f;
            }
        }

        return m1 / mWeight;
    }

    void GenerateTerrain(TerrainInfo& info, const TerrainGenerationInfo& args) {
        auto& vertices = info.Vertices;
        auto& indices = info.Indices;

        vertices.clear();
        indices.clear();

        const auto density = args.Density;
        const float cellSize = args.Size / density; // distance between each vertex
        const float uvStep = cellSize / args.TextureScale;

        List<Vector3> vertexPositions;
        vertexPositions.resize(density * density);

        // Fill vertex positions
        for (uint y = 0; y < density; y++) {
            for (uint x = 0; x < density; x++) {
                auto index = y * density + x;
                vertexPositions[index] = Vector3{ x * cellSize, 0, y * cellSize };
                auto percentX = x / (float)density;
                auto percentY = y / (float)density;

                vertexPositions[index].y += OpenSimplex2::Noise2(args.Seed, percentX * args.NoiseScale, percentY * args.NoiseScale) * args.Height;
                vertexPositions[index].y += OpenSimplex2::Noise2(args.Seed, percentX * args.NoiseScale2 + 0.5f, percentY * args.NoiseScale2 + 0.5f) * args.Height2;
            }
        }

        auto getVertex = [&vertexPositions, density](uint x, uint y) -> Vector3& {
            x = std::clamp(x, 0u, density - 1u);
            y = std::clamp(y, 0u, density - 1u);
            return vertexPositions[y * density + x];
        };

        auto halfRow = density / 2 - 1;
        Vector3 center = vertexPositions[density * halfRow + halfRow];

        // Flatten area around exit
        if (args.FlattenRadius > 0) {
            for (uint y = 0; y < density; y++) {
                for (uint x = 0; x < density; x++) {
                    auto dist = Vector3::Distance(Vector3(x * cellSize, 0, y * cellSize), { center.x, 0, center.z });
                    auto& vert = getVertex(x, y);
                    auto heightDiff = center.y - vert.y;

                    if (dist < args.FlattenRadius) {
                        vert.y += heightDiff * SmoothStep(1, 0, dist / args.FlattenRadius);
                    }
                }
            }
        }

        if (args.CraterStrength > 0) {
            for (uint y = 0; y < density; y++) {
                for (uint x = 0; x < density; x++) {
                    auto dist = Vector3::Distance(Vector3(x * cellSize, 0, y * cellSize), { center.x, 0, center.z });
                    auto& vert = getVertex(x, y);
                    //auto heightDiff = center.y - vert.y;
                    //auto maxHeight = args.Height + args.Height2;
                    vert.y += args.CraterStrength * SmoothStep(0, 1, dist / args.Size);
                    //if (vert.y > maxHeight) vert.y = maxHeight;
                }
            }
        }

        auto addVertex = [&](uint x, uint y) {
            Vector2 uv = { (float)x * uvStep, (float)y * uvStep };
            auto dx = getVertex(x + 1, y) - getVertex(x - 1, y);
            auto dy = getVertex(x, y + 1) - getVertex(x, y - 1);
            dx.Normalize();
            dy.Normalize();
            auto normal = dy.Cross(dx);
            normal.Normalize();

            // Fix normals being flipped at edges
            if (normal.y < 0) normal = -normal;

            ObjectVertex vertex{
                .Position = getVertex(x, y),
                .UV = uv,
                .Color = Color(1, 1, 1),
                .Normal = normal,
                .Tangent = dx,
                .Bitangent = dy,
                .TexID = (int)TexID::None // Rely on override
            };

            vertices.push_back(vertex);
        };


        // Generate faces
        for (uint y = 0; y < density - 1; y++) {
            for (uint x = 0; x < density - 1; x++) {
                auto startIndex = (uint16)vertices.size();
                addVertex(x, y);
                addVertex(x, y + 1);
                addVertex(x + 1, y + 1);
                addVertex(x + 1, y);

                indices.push_back(startIndex);
                indices.push_back(startIndex + 1);
                indices.push_back(startIndex + 2);

                indices.push_back(startIndex);
                indices.push_back(startIndex + 2);
                indices.push_back(startIndex + 3);
            }
        }

        // Center the mesh
        for (auto& vertex : vertices) {
            vertex.Position -= center;
        }
    }

    void LoadTerrain(const Bitmap2D& bitmap, TerrainInfo& info, uint cellDensity, float heightScale, float gridScale) {
        auto& vertices = info.Vertices;
        auto& indices = info.Indices;

        const float cellScale = (float)bitmap.Width / cellDensity;
        const float uvStep = cellScale * 0.25f; // Repeat texture every four of the original cells

        auto heights = CubicResize(bitmap, cellDensity, cellDensity);
        List<Vector3> vertexPositions;
        vertexPositions.resize(cellDensity * cellDensity);

        // Fill vertex positions
        for (uint y = 0; y < cellDensity; y++) {
            for (uint x = 0; x < cellDensity; x++) {
                auto index = y * cellDensity + x;
                vertexPositions[index] = Vector3{ x * cellScale * gridScale, heights[index] * heightScale, y * cellScale * gridScale };
                //constexpr float noiseScale = 0.10f;
                //vertexPositions[index].y += OpenSimplex2::Noise2(321818, (double)x * noiseScale, (double)y * noiseScale) * 3.5f;
            }
        }

        auto getVertex = [&vertexPositions, cellDensity](uint x, uint y) -> Vector3& {
            x = std::clamp(x, 0u, cellDensity - 1u);
            y = std::clamp(y, 0u, cellDensity - 1u);
            return vertexPositions[y * cellDensity + x];
        };

        auto addVertex = [&](uint x, uint y) {
            Vector2 uv = { (float)x * uvStep, (float)y * uvStep };
            auto dx = getVertex(x + 1, y) - getVertex(x - 1, y);
            auto dy = getVertex(x, y + 1) - getVertex(x, y - 1);
            dx.Normalize();
            dy.Normalize();
            auto normal = dy.Cross(dx);
            normal.Normalize();

            // Fix normals being flipped at edges
            if (normal.y < 0) normal = -normal;

            ObjectVertex vertex{
                .Position = getVertex(x, y),
                .UV = uv,
                .Color = Color(1, 1, 1),
                .Normal = normal,
                .Tangent = dx,
                .Bitangent = dy,
                .TexID = (int)TexID::None // Rely on override
            };

            vertices.push_back(vertex);
        };


        //float aspect = 1;
        //auto projection = DirectX::XMMatrixPerspectiveFovLH(60 * DegToRad, aspect, 1, 300);
        //Vector3 exitPosition = info.ExitTransform.Translation();
        //exitPosition += info.ExitTransform.Forward() * 10;

        Vector3 center = vertexPositions[cellDensity * (cellDensity / 2 - 1) + (cellDensity / 2 - 1)];

        //Vector3 center = { gridScale * 8, 0, gridScale * 8 };
        //Vector3 exitPosition = { 0, 10, 30 };
        //exitPosition += center;
        //auto view = DirectX::XMMatrixLookAtLH(Position, Target, Up);

        //auto view = DirectX::XMMatrixLookAtLH(exitPosition, Vector3(0, 10, 0) + center, Vector3::Up);
        //auto frustum = GetFrustum(exitPosition, view, projection);

        //DirectX::BoundingFrustum frustum = { transform, orientation };

        // Generate faces
        for (uint y = 0; y < cellDensity - 1; y++) {
            for (uint x = 0; x < cellDensity - 1; x++) {
                auto startIndex = (uint16)vertices.size();

                //Vector2 center = { x * gridScale * cellScale, y * gridScale * cellScale };
                //auto cellCenter = (getVertex(x, y) + getVertex(x + 1, y) + getVertex(x, y + 1) + getVertex(x + 1, y + 1)) / 4;

                //if (frustum.Contains(cellCenter))
                //    continue;

                addVertex(x, y);
                addVertex(x, y + 1);
                addVertex(x + 1, y + 1);
                addVertex(x + 1, y);

                indices.push_back(startIndex);
                indices.push_back(startIndex + 1);
                indices.push_back(startIndex + 2);

                indices.push_back(startIndex);
                indices.push_back(startIndex + 2);
                indices.push_back(startIndex + 3);
            }
        }

        // Center the mesh
        for (auto& vertex : vertices) {
            vertex.Position -= center;
        }
    }
}
