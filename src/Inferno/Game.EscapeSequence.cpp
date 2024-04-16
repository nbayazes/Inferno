#include "pch.h"
#include "Game.EscapeSequence.h"
#include "Bezier.h"
#include "Formats/BBM.h"
#include "Graphics/ShaderLibrary.h"
#include "Resources.h"

namespace Inferno {
    enum class EscapeScene {
        None,
        Start, // Camera still in first person
        LookBack, // Camera looking backwards at player
        Outside
    };

    struct EscapeState {
        EscapeScene Scene{};
        int PathIndex = 0;
        double Elapsed = 0;
        List<Vector3> Path;
        Vector3 Up;
    };

    inline EscapeState Escape;

    void BeginEscapeSequence() {
        Escape = { .Scene = EscapeScene::Start };
    }

    void MoveShipTowardsGoal() {
        while (Escape.PathIndex < Escape.Path.size()) {}
    }

    void UpdateEscapeSequence(float dt) {
        switch (Escape.Scene) {
            case EscapeScene::None:
                break;
            case EscapeScene::Start:
                // Move ship
                break;
            case EscapeScene::LookBack:
                break;
            case EscapeScene::Outside:
                break;
        }
    }

    void LoadTerrain(const Bitmap2D& bitmap, TerrainInfo& dest, float heightScale = 1.0f, float gridScale = 40.0f) {
        List<uint8> terrain;
        auto& vertices = dest.Vertices;
        auto& indices = dest.Indices;

        auto getPosition = [&](int x, int y) {
            x = std::clamp(x, 0, (int)bitmap.Width - 1);
            y = std::clamp(y, 0, (int)bitmap.Height - 1);

            Vector3 position;
            position.x = x * gridScale;
            position.z = y * gridScale;
            position.y = (float)bitmap.Data[y * bitmap.Width + x].r * heightScale;
            return position;
        };

        auto addVertex = [&](int x, int y, const Vector3& position) {
            auto v = getPosition(x, y);
            auto dx = (getPosition(x + 1, y) - v) + (v - getPosition(x - 1, y));
            auto dy = (getPosition(x, y + 1) - v) + (v - getPosition(x, y - 1));

            auto normal = dy.Cross(dx);
            normal.Normalize();

            dx.Normalize();
            dy.Normalize();

            ObjectVertex vertex{
                .Position = position,
                .UV = Vector2(0.25f * x, 0.25f * y),
                .Color = Color(1, 1, 1),
                .Normal = normal,
                .Tangent = dx,
                .Bitangent = dy,
                .TexID = (int)TexID::None // Rely on override
            };

            vertices.push_back(vertex);
        };


        // Each cell UV is 0.25
        for (uint y = 0; y < bitmap.Height - 1; y++) {
            for (uint x = 0; x < bitmap.Width - 1; x++) {
                auto v0 = getPosition(x, y); // bl
                auto v1 = getPosition(x, y + 1); // tl
                auto v2 = getPosition(x + 1, y + 1); // tr
                auto v3 = getPosition(x + 1, y); // br

                auto startIndex = (uint16)vertices.size();
                addVertex(x, y, v0);
                addVertex(x, y + 1, v1);
                addVertex(x + 1, y + 1, v2);
                addVertex(x + 1, y, v3);

                indices.push_back(startIndex);
                indices.push_back(startIndex + 1);
                indices.push_back(startIndex + 2);

                indices.push_back(startIndex);
                indices.push_back(startIndex + 2);
                indices.push_back(startIndex + 3);
            }
        }

        // Center the mesh
        //float halfWidth = bitmap.Width * gridScale * 0.5f;
        //float halfHeight = bitmap.Height * gridScale * 0.5f;
        Vector3 center = getPosition(bitmap.Width / 2, bitmap.Height / 2);

        for (auto& vertex : vertices) {
            vertex.Position -= center;
            //vertex.Position.x += halfWidth;
            //vertex.Position.z += halfHeight;
            //vertex.Position.y -= center.y;
        }
    }

    Tag FindExitSegment(Level& level) {
        for (int segid = 0; segid < level.Segments.size(); segid++) {
            for (auto& sideid : SIDE_IDS) {
                Tag tag{ SegID(segid), sideid };

                if (auto trigger = level.TryGetTrigger(tag)) {
                    if (trigger->Type == TriggerType::Exit || trigger->HasFlag(TriggerFlagD1::Exit))
                        return tag;
                }
            }
        }

        return {};
    }

    void CreateEscapePath(Level& level, TerrainInfo& info) {
        // Find exit tunnel start
        auto curSeg = FindExitSegment(level);
        if (!curSeg) return;

        auto& points = info.PlayerPath;
        bool foundSurface = false;

        while (curSeg) {
            if (auto cside = level.GetConnectedSide(curSeg)) {
                auto [seg, side] = level.GetSegmentAndSide(cside);
                auto opp = GetOppositeSide(cside);

                auto& oppSide = seg.GetSide(opp.Side);

                BezierCurve curve = {
                    side.Center,
                    side.Center + side.AverageNormal * 10,
                    oppSide.Center + oppSide.AverageNormal * 2,
                    oppSide.Center
                };

                auto curvePoints = DivideCurveIntoSteps(curve.Points, 4);

                if (seg.GetConnection(opp.Side) == SegID::Exit) {
                    foundSurface = true;
                    points.push_back(curvePoints[1]);
                    points.push_back(curvePoints[2]);
                    points.push_back(curvePoints[3]);

                    auto& bottom = seg.GetSide(SideID::Bottom);

                    auto forward = seg.GetSide(opp.Side).Center - seg.Center;
                    forward.Normalize();

                    auto up = seg.Center - bottom.Center;
                    up.Normalize();
                    //up = Vector3::UnitY;
                    //forward = Vector3::UnitZ;
                    //auto right = up.Cross(forward);
                    //up = right.Cross(forward);

                    info.Transform = VectorToRotation(forward, up);
                    info.InverseTransform = Matrix3x3(info.Transform.Invert());
                    info.Transform.Translation(bottom.Center);

                    break;
                }

                points.push_back((curvePoints[0] + curvePoints[1]) / 2);
                points.push_back((curvePoints[2] + curvePoints[3]) / 2);

                //curSeg = Level.GetConnectedSide(opp);
                curSeg = opp;
            }
        }

        if (!foundSurface) points.clear();
    }

    TerrainInfo ParseEscapeInfo(Level& level, span<string> lines) {
        if (lines.size() < 7)
            throw Exception("Not enough lines in level escape data. 7 required");

        TerrainInfo info{};

        info.SurfaceTexture = lines[0]; // moon01.bbm
        info.Heightmap = lines[1]; // lev01ter.bbm

        auto exitPos = String::Split(lines[2], ',');
        if (exitPos.size() >= 2) {
            String::TryParse(exitPos[0], info.ExitX);
            String::TryParse(exitPos[1], info.ExitY);
        }

        if (String::TryParse(lines[3], info.ExitAngle))
            info.ExitAngle /= 360.0f;

        info.SatelliteTexture = lines[4];

        if (String::Contains(info.SatelliteTexture, "sun")) {
            info.SatelliteAdditive = true;
            //info.SatelliteAspectRatio = 0.9f;
            info.SatelliteColor = Color(3, 3, 3);
        }

        if (String::Contains(info.SatelliteTexture, "earth")) {
            info.SatelliteAspectRatio = 64.0f / 54.0f; // The earth bitmap only uses 54 of 64 pixels in height
            info.SatelliteColor = Color(2, 2, 2);
        }


        auto parseDirection = [](const List<string>& tokens) {
            float heading{}, pitch{};
            String::TryParse(tokens[0], heading); // heading
            String::TryParse(tokens[1], pitch); // pitch

            auto dir = Vector3::UnitX;
            dir = Vector3::Transform(dir, Matrix::CreateRotationZ(pitch * DegToRad));
            dir = Vector3::Transform(dir, Matrix::CreateRotationY(heading * DegToRad));
            return dir;
        };

        auto satelliteDir = String::Split(lines[5], ',');
        if (satelliteDir.size() >= 2)
            info.SatelliteDir = parseDirection(satelliteDir);

        String::TryParse(lines[6], info.SatelliteSize);

        auto stationDir = String::Split(lines[7], ',');
        if (stationDir.size() >= 2)
            info.StationDir = parseDirection(satelliteDir);

        if (auto data = Resources::ReadBinaryFile(info.Heightmap); !data.empty()) {
            auto bitmap = ReadBbm(data);
            LoadTerrain(bitmap, info);
        }

        CreateEscapePath(level, info);

        return info;
    }

    //void LoadEscape(span<byte> data) {
    //    DecodeText(data);
    //    auto lines = String::ToLines(String::OfBytes(data));
    //}
}
