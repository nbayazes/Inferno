#include "pch.h"
#include <numeric>
#include "Polymodel.h"
#include "Streams.h"
#include "Utility.h"

namespace Inferno {
    constexpr int16 MAX_POINTS_PER_POLY = 64;

    enum class OpCode {
        End = 0,
        DefPoints = 1,
        FlatPoly = 2,
        MappedPoly = 3,
        SortNormal = 4,
        RodBitmap = 5,
        CallSubobject = 6,
        DefpointStart = 7,
        Glow = 8
    };

    // 'Expands' vertices in each submodel to a buffer for each texture
    void Expand(Model& model, span<Vector3> points) {
        for (auto& sm : model.Submodels) {
            // expand submodel vertices from indices for use in buffers
            // +1 slot is for flat polygons
            sm.ExpandedIndices.resize(model.TextureCount + 1);

            for (uint16 i = 0; i < sm.Indices.size(); i++) {
                auto& tmap = sm.TMaps[i / 3]; // textures are stored per face (3 indices per triangle)
                // store indices by texture.
                // this creates empty arrays for texture slots that are not used
                sm.ExpandedIndices[tmap].push_back(i);
                auto& p = points[sm.Indices[i]];
                sm.ExpandedPoints.push_back(p);
                sm.ExpandedColors.push_back({ 1, 1, 1, 1 });
            }

            auto flatOffset = (uint16)sm.ExpandedPoints.size();

            // Append flat indices
            for (uint16 i = 0; i < sm.FlatIndices.size(); i++) {
                auto& color = sm.FlatVertexColors[i / 3];
                auto& p = points[sm.FlatIndices[i]];
                sm.ExpandedIndices[model.TextureCount].push_back(flatOffset + i);
                sm.ExpandedPoints.push_back(p);
                sm.ExpandedColors.push_back(color);
            }
        }
    }

    void UpdateGeometricProperties(Model& model) {
        for (auto& sm : model.Submodels) {
            if (sm.ExpandedPoints.empty()) continue;

            for (auto& p : sm.ExpandedPoints) {
                sm.Min = Vector3::Min(p, sm.Min);
                sm.Max = Vector3::Max(p, sm.Max);
                sm.Center += p;
            }

            sm.Center /= (float)sm.ExpandedPoints.size();
            sm.Radius = Vector3::Distance(sm.Max, sm.Min) / 2;
        }
    }

    void ReadPolymodel(Model& model, span<ubyte> data, const Palette* palette) {
        // 'global' state for the interpreter
        int16 highestTex = -1;
        StreamReader reader(data);
        int16 glow = -1;
        int16 glowIndex = 0, flatGlowIndex = 0;
        List<Vector3> points;
        auto& angles = model.angles;

        // must use std::function instead of auto here to allow recursive calls
        std::function<void(size_t, Submodel&)> readChunk = [&](size_t chunkStart, Submodel& submodel) {
            reader.Seek(chunkStart);
            auto op = (OpCode)reader.ReadInt16();

            while (op != OpCode::End) {
                int chunkLen = 0; // chunk length

                switch (op) {
                    case OpCode::DefPoints:
                        throw NotImplementedException(); // unused

                    case OpCode::DefpointStart: // indicates the start of a submodel
                    {
                        auto n = reader.ReadInt16();
                        // Discard the point index. This assumes that vertices are always stored in submodel order
                        // which holds true for all official models.
                        /*auto pointIndex =*/ reader.ReadInt16();
                        auto zero = reader.ReadInt16();
                        if (zero != 0) throw Exception("Defpoint Start must equal zero");

                        for (int i = 0; i < n; i++)
                            points.push_back(reader.ReadVector());

                        chunkLen = n * 12 /*sizeof(vector)*/ + 8;
                        break;
                    }
                    case OpCode::FlatPoly:
                    {
                        auto n = reader.ReadInt16();
                        if (n <= 2 || n >= MAX_POINTS_PER_POLY)
                            throw Exception("Polygon must have between 3 and 64 points");

                        // vectors used for normal facing checks (no longer needed)
                        /*auto v0 =*/ reader.ReadVector(); // @4
                        /*auto v1 =*/ reader.ReadVector(); // @16

                        auto color = reader.ReadUInt16();
                        auto colorf = UnpackColor(color);

                        // D1 maps colors to palette entries
                        if (palette && color < palette->Data.size()) {
                            auto& c = palette->Data[color];
                            colorf = ColorFromRGB(c.r, c.g, c.b, c.a);
                        }

                        int16 p0 = reader.ReadInt16();
                        int16 px = reader.ReadInt16();

                        // convert triangle fans to triangle lists
                        for (int i = 0; i < n - 2; i++) { // @30
                            auto p = reader.ReadInt16();
                            submodel.FlatIndices.push_back(p0);
                            submodel.FlatIndices.push_back(px);
                            submodel.FlatIndices.push_back(p);
                            submodel.FlatVertexColors.push_back(colorf);
                            px = p;

                            if (glow >= 0)
                                submodel.Glows.push_back({ flatGlowIndex, glow });

                            flatGlowIndex++;
                        }

                        int padding = n & 1 ? 0 : 2; // check if odd
                        reader.SeekForward(padding);

                        chunkLen = 30 + ((n & ~1) + 1) * 2;
                        if (reader.Position() != chunkStart + chunkLen)
                            throw Exception("Bad chunk length in POF FlatPoly");

                        break;
                    }
                    case OpCode::MappedPoly:
                    {
                        // polygons are stored as triangle fans and can have more than 3 points
                        auto n = reader.ReadInt16(); // @2
                        if (n <= 2 || n >= MAX_POINTS_PER_POLY)
                            throw Exception("Polygon must have between 3 and 64 points");

                        // vectors used for normal facing checks (no longer needed)
                        /*auto v0 =*/ reader.ReadVector(); // @4
                        /*auto v1 =*/ reader.ReadVector(); // @16
                        auto tmap = reader.ReadInt16(); // @28
                        if (tmap > highestTex) highestTex = tmap;

                        int16 p0 = reader.ReadInt16();
                        int16 px = reader.ReadInt16();

                        // this assert holds for official models, but not custom ones. however they can still draw properly regardless
                        // assert(p0 < points.size() && px < points.size()); 

                        // convert triangle fans to triangle lists
                        for (int i = 0; i < n - 2; i++) { // @30
                            auto p = reader.ReadInt16();
                            submodel.Indices.push_back(p0);
                            submodel.Indices.push_back(px);
                            submodel.Indices.push_back(p);
                            submodel.TMaps.push_back(tmap);
                            px = p;

                            if (glow >= 0)
                                submodel.Glows.push_back({ glowIndex, glow });

                            glowIndex++;
                        }

                        int padding = n & 1 ? 0 : 2; // check if odd
                        reader.SeekForward(padding);

                        auto uv0 = reader.ReadVector();
                        auto uvx = reader.ReadVector();

                        for (int i = 0; i < n - 2; i++) {
                            auto uv = reader.ReadVector();
                            submodel.UVs.push_back(uv0);
                            submodel.UVs.push_back(uvx);
                            submodel.UVs.push_back(uv);
                            uvx = uv;
                        }

                        chunkLen = 30 + ((n & ~1) + 1) * 2 + n * 12; // n & ~1 skips index 0 and 1
                        if (reader.Position() != chunkStart + chunkLen)
                            throw Exception("Bad chunk length in POF MappedPoly");

                        glow = -1;
                        break;
                    }
                    case OpCode::SortNormal:
                    {
                        reader.Seek(chunkStart + 28);
                        auto offset1 = reader.ReadInt16();
                        auto offset2 = reader.ReadInt16();
                        readChunk(chunkStart + offset2, submodel);
                        readChunk(chunkStart + offset1, submodel);
                        chunkLen = 32;
                        break;
                    }
                    case OpCode::RodBitmap:
                        // Unused. Might have been intended for the energy drain robot.
                        throw NotImplementedException();

                    case OpCode::CallSubobject:
                    {
                        angles.push_back(reader.ReadAngleVec());
                        reader.Seek(chunkStart + 16);
                        auto offset = reader.ReadInt16();
                        readChunk(chunkStart + offset, submodel);
                        chunkLen = 20;
                        break;
                    }
                    case OpCode::Glow:
                        // glow gets consumed by the next textured polygon
                        // glow 0: engine, brightness based on velocity
                        // glow 1: player ship headlight
                        glow = reader.ReadInt16();
                        chunkLen = 4;
                        break;

                    default:
                        throw Exception("Error parsing POF data");
                }

                assert(chunkLen != 0); // forgot to set the length of a chunk
                reader.Seek(chunkStart + chunkLen);
                chunkStart = reader.Position();
                op = (OpCode)reader.ReadInt16();
            }
        };

        // Submodels must be loaded in order of pointer offset.
        // Doing this allows extracting all mesh data ahead of time instead of per frame.
        List<int> pointers = Seq::map(model.Submodels, [](const auto& sm) { return sm.Pointer; });
        List<int16> loadOrder(pointers.size());
        std::iota(loadOrder.begin(), loadOrder.end(), (int16)0); // fill range 0 .. n
        Seq::sortBy(loadOrder, [&pointers](auto a, auto b) {
            return pointers[a] < pointers[b]; // sort load order by pointer offsets
        });

        // Load the sorted submodels
        for (auto& i : loadOrder) {
            auto& submodel = model.Submodels[i];
            readChunk(submodel.Pointer, submodel);
        }

        Expand(model, points);
        UpdateGeometricProperties(model);

        if (highestTex >= model.TextureCount) throw Exception("Model contains too many textures");
        if (model.Submodels.size() > MAX_SUBMODELS) throw Exception("Model contains too many submodels");
    }
}

