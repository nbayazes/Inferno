#include "pch.h"
#include "Game.EscapeSequence.h"
#include "Bezier.h"
#include "Editor/Editor.h"
#include "Formats/BBM.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Reactor.h"
#include "Game.Segment.h"
#include "GameTimer.h"
#include "Graphics/Render.Debug.h"
#include "Resources.h"
#include "VisualEffects.h"
#include "SoundSystem.h"

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
        int CameraPathIndex = 0;
        double Elapsed = 0;
        List<Vector3> Path;
        Vector3 Up;
        GameTimer ExplosionTimer;
        GameTimer ExplosionSoundTimer;
        bool FinalExplosion = false;
        Vector3 DesiredCameraPosition;
        //Vector3 OutsideStartUp;
        Vector3 OutsideCameraStartPos;
        Vector3 OutsideCameraStartTarget;
        Matrix3x3 OutsideCameraStartRotation;
        float OutsideCameraLerp = 0;

        float CameraRoll = 0;
        float CameraRollLerp = 0;
        bool ZoomingOut = false;
        bool StopRoll = false;
        int RollSign = 0;
    };

    namespace {
        EscapeState State;
        Camera CinematicCamera;
    }

    //void MoveShipTowardsGoal() {
    //    while (State.PathIndex < State.Path.size()) {}
    //}

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


    void LoadTerrain(const Bitmap2D& bitmap, TerrainInfo& info, uint cellDensity, float heightScale = 1.0f, float gridScale = 40.0f) {
        List<uint8> terrain;
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

    void CreateEscapePath(Level& level, TerrainInfo& info) {
        // Find exit tunnel start
        auto curSeg = FindExit(level);
        if (!curSeg) return;

        auto& points = info.EscapePath;
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
                    auto exitTag = Tag(cside.Segment, opp.Side);

                    foundSurface = true;
                    points.push_back(curvePoints[1]);
                    points.push_back(curvePoints[2]);
                    points.push_back(curvePoints[3]);

                    auto& bottom = seg.GetSide(SideID::Bottom);

                    auto forward = seg.GetSide(opp.Side).Center - seg.Center;
                    forward.Normalize();

                    auto up = seg.Center - bottom.Center;
                    up.Normalize();

                    info.Transform = VectorToRotation(forward, up);
                    info.InverseTransform = Matrix3x3(info.Transform.Invert());
                    info.Transform.Translation(bottom.Center);
                    info.ExitTransform = Matrix::CreateRotationY(DirectX::XM_PI) * Matrix::CreateTranslation(Vector3(0, 9, 10)) * info.Transform;
                    info.ExitTag = exitTag;
                    break;
                }

                points.push_back((curvePoints[0] + curvePoints[1]) / 2);
                points.push_back((curvePoints[2] + curvePoints[3]) / 2);
                curSeg = opp;
            }
        }

        if (!foundSurface) points.clear();
        if (points.empty()) return;

        info.SurfacePathIndex = (int)points.size() - 1;

        // Add path to station
        {
            auto& end = points.back();
            auto normal = end - points[points.size() - 2];
            normal.Normalize();

            constexpr auto STATION_DIST = 500.0f;
            auto stationPos = info.StationDir * STATION_DIST * 0.5f;
            stationPos.y = STATION_DIST;
            stationPos = Vector3::Transform(stationPos, info.Transform);

            auto stationDir = stationPos - (end + normal * 250);
            stationDir.Normalize();

            BezierCurve curve = {
                end,
                end + normal * 250,
                stationPos - stationDir * 250,
                stationPos,
            };

            auto curvePoints = DivideCurveIntoSteps(curve.Points, 40);
            Seq::append(points, curvePoints);
        }

        ASSERT(Seq::inRange(points, info.SurfacePathIndex));
        info.LookbackPathIndex = info.SurfacePathIndex / 3;
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

            auto dir = Vector3::UnitZ;
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

        CreateEscapePath(level, info);

        if (auto data = Resources::ReadBinaryFile(info.Heightmap); !data.empty()) {
            auto bitmap = ReadBbm(data);
            LoadTerrain(bitmap, info, 64);
        }

        info.ExitModel = Resources::GameData.ExitModel;
        return info;
    }

    constexpr float SHIP_MAX_SPEED = 100;

    void MoveShipAlongPath(Object& ship, span<Vector3> path, float acceleration, float turnRate, int& pathIndex, float dt) {
        // turn and move towards the next node
        constexpr float PATH_TOLERANCE = 25;

        for (; pathIndex < path.size(); pathIndex++) {
            if (Vector3::Distance(path[pathIndex], ship.Position) > PATH_TOLERANCE)
                break;
        }

        if (pathIndex >= path.size()) return;

        auto& node = path[pathIndex];
        auto dir = node - ship.Position;
        dir.Normalize();

        if (ship.Physics.Velocity.Length() < SHIP_MAX_SPEED)
            ship.Physics.Velocity += dir * acceleration * dt;

        TurnTowardsDirection(ship, dir, turnRate);
    }

    void MoveCameraAlongPath(Camera& camera, span<Vector3> path, int& pathIndex, float dt, float speed) {
        constexpr float PATH_TOLERANCE = 10;

        for (; pathIndex < path.size(); pathIndex++) {
            if (Vector3::Distance(path[pathIndex], camera.Position) > PATH_TOLERANCE)
                break;
        }

        if (pathIndex >= path.size()) return;

        auto& node = path[pathIndex];
        auto dir = node - camera.Position;
        dir.Normalize();

        camera.Position += dir * speed * dt;
    }

    bool UpdateEscapeSequence(float dt) {
        auto& player = Game::GetPlayerObject();

        switch (State.Scene) {
            case EscapeScene::None:
                return false; // Do nothing
            case EscapeScene::Start:
                if (State.PathIndex >= Game::Terrain.LookbackPathIndex) {
                    State.Scene = EscapeScene::LookBack;

                    // Set the camera roughly 20 units away on the path
                    State.CameraPathIndex = State.PathIndex + 1;
                    auto cameraDir = Game::Terrain.EscapePath[State.CameraPathIndex] - player.Position;
                    cameraDir.Normalize();
                    CinematicCamera.Position = player.Position + cameraDir * 20;
                }
                break;
            case EscapeScene::LookBack:

                break;
            case EscapeScene::Outside:

                break;
        }

        if (player.Segment == Game::Terrain.ExitTag.Segment) {
            Settings::Editor.ShowTerrain = true;
            //player.Segment = SegID::Terrain;
            RelinkObject(Game::Level, player, SegID::Terrain);
            Game::OnTerrain = true;
        }

        float acceleration = 110.0f; // u/s
        float turnRate = 0.25f;
        MoveShipAlongPath(player, Game::Terrain.EscapePath, acceleration, turnRate, State.PathIndex, dt);

        if (Game::OnTerrain) {
            // Random roll
            player.Physics.AngularVelocity.z += 0.024f;
            //player.Physics.AngularVelocity.z += 0.004f;
        }

        if (!State.FinalExplosion) {
            if (State.ExplosionTimer < 0) {
                if (auto seg = Game::Level.TryGetSegment(player.Segment)) {
                    if (auto e = EffectLibrary.GetExplosion("reactor_small_explosions")) {
                        e->Variance = 5.0f;
                        e->Instances = 2;
                        e->Delay = { 0, 0.15f };
                        auto verts = seg->GetVertices(Game::Level);

                        for (int i = 0; i < 8; i++) {
                            if (RandomInt(8) > 6) continue;
                            auto dir = seg->Center - *verts[i];
                            dir.Normalize();
                            CreateExplosion(*e, player.Segment, *verts[i] + dir * e->Variance);
                        }
                    }
                }

                if (auto e = EffectLibrary.GetExplosion("tunnel chase fireball")) {
                    auto pos = player.Position + player.Rotation.Backward() * 10;
                    CreateExplosion(*e, player.Segment, pos);
                }

                // Random roll turbulence
                auto signX = RandomInt(1) ? 1 : -1;
                player.Physics.AngularVelocity.z += signX * .14f;

                State.ExplosionTimer += 0.2f;
            }

            // Create explosion sounds behind the player
            if (State.ExplosionSoundTimer < 0) {
                State.ExplosionSoundTimer += 0.30f + Random() * 0.15f;
                auto pos = player.Position + player.Rotation.Backward() * 10;
                Inferno::Sound::Play({ SoundID::ExplodingWall }, pos, player.Segment);
            }
        }


        auto mineExplosionPos = Game::Terrain.ExitTransform.Translation() + Game::Terrain.ExitTransform.Forward() * 15;
        constexpr float MINE_EXPLODE_CLEARANCE = 40; // How far the ship must be before the mine will explode

        // Blow up once the player is outside and far enough away
        if (!State.FinalExplosion &&
            State.PathIndex >= Game::Terrain.SurfacePathIndex + 1 &&
            Vector3::Distance(Game::Terrain.ExitTransform.Translation(), player.Position) > MINE_EXPLODE_CLEARANCE) {
            if (auto e = EffectLibrary.GetExplosion("mine collapse fireball")) {
                CreateExplosion(*e, SegID::Terrain, mineExplosionPos);
            }

            if (auto e = EffectLibrary.GetExplosion("mine collapse huge fireball")) {
                CreateExplosion(*e, SegID::Terrain, mineExplosionPos, 0, e->Delay.GetRandom());
            }

            Game::StopSelfDestruct();
            State.FinalExplosion = true;
            Game::Terrain.ExitModel = Resources::GameData.DestroyedExitModel;

            State.Scene = EscapeScene::Outside;
            //State.OutsideStartUp = CinematicCamera.Up;
        }

        return true;
    }

    inline float AngleBetweenVectors2(const Vector3& va, const Vector3& vb, const Vector3& normal) {
        assert(IsNormalized(va));
        assert(IsNormalized(vb));
        assert(IsNormalized(normal));
        auto cross = va.Cross(vb);
        auto angle = atan2(cross.Dot(normal), va.Dot(va));
        if (normal.Dot(cross) < 0) angle = -angle;
        return angle;
    }

    //auto roll2 = AngleBetweenVectors2(CinematicCamera.Up, Game::Terrain.ExitTransform.Up(), CinematicCamera.GetForward());

    // Returns sign
    int AlignCameraRollToTerrain(float dt) {
        auto roll = AngleBetweenVectors(CinematicCamera.Up, Game::Terrain.ExitTransform.Up(), CinematicCamera.GetForward());

        if (roll > 0) {
            CinematicCamera.Roll(std::min(dt, roll));
        }
        else if (roll < 0) {
            CinematicCamera.Roll(std::max(-dt, roll));
        }

        return Sign(roll);
    }

    void UpdateEscapeCamera(float dt) {
        auto& player = Game::GetPlayerObject();

        switch (State.Scene) {
            case EscapeScene::Start:
                // Use first person camera
                Game::MoveCameraToObject(CinematicCamera, player, Game::LerpAmount);
            //CinematicCamera.SetFov(Settings::Graphics.FieldOfView);
                player.Render.Type = RenderType::None;
            //CinematicCamera.Position = player.Position + player.Rotation.Forward() * 20;
                break;
            case EscapeScene::LookBack:
            {
                // Use third person camera
                float speed = player.Physics.Velocity.Length();
                //float distance = 20;
                //MoveCameraAlongPath(CinematicCamera, player, Game::Terrain.EscapePath, State.PathIndex, distance, dt, speed);
                MoveCameraAlongPath(CinematicCamera, Game::Terrain.EscapePath, State.CameraPathIndex, dt, speed);
                //Game::GameCamera.SetFov(45);
                //CinematicCamera.Up = player.Rotation.Up();
                auto target = player.GetPosition(Game::LerpAmount);
                CinematicCamera.Target = target;
                auto targetDir = target - CinematicCamera.Position;
                targetDir.Normalize();


                if (State.PathIndex >= Game::Terrain.SurfacePathIndex * 0.75f) {
                    //AlignCameraRollToTerrain(dt * 2);
                }
                else {
                    auto rotation = VectorToRotation(targetDir, player.GetRotation(Game::LerpAmount).Up());
                    CinematicCamera.Up = rotation.Up();
                }

                player.Render.Type = RenderType::Model;

                //Render::Debug::DrawLine(CinematicCamera.Position, target, Color(1, 1, 0));
                break;
            }
            case EscapeScene::Outside:
            {
                //const auto targetPos = transform.Translation() + transform.Forward() * 100 + transform.Right() * 50 + transform.Up() * 5;
                //CinematicCamera.Target = (player.GetPosition(Game::LerpAmount)) / 2.0f;
                //auto shipPosition = player.GetPosition(Game::LerpAmount);
                //CinematicCamera.Target = Vector3::Lerp(shipPosition, (transform.Translation() + shipPosition) / 2, State.OutsideCameraLerp);
                //CinematicCamera.Target = Vector3::Lerp(shipPosition, transform.Translation(), State.OutsideCameraLerp);
                //CinematicCamera.Target = transform.Translation() + transform.Forward() * 20;
                //auto rotation = Matrix::Lerp(State.OutsideCameraStartRotation, transform, State.OutsideCameraLerp);
                //CinematicCamera.Up = rotation.Up();

                //CinematicCamera.Roll(dt);
                //CinematicCamera.Target();

                //CinematicCamera.Position = Vector3::Lerp(State.OutsideCameraStartPos, targetPos, State.OutsideCameraLerp);
                //CinematicCamera.SetFov(std::lerp(45, 80, State.OutsideCameraLerp));

                //AlignCameraRollToTerrain(dt);
                break;
            }
        }

        auto& exit = Game::Terrain.ExitTransform;

        //if (State.PathIndex >= Game::Terrain.SurfacePathIndex * 0.9 && !State.ZoomingOut) {
        if (Game::OnTerrain && !State.ZoomingOut) {
            State.OutsideCameraStartPos = CinematicCamera.Position;
            State.OutsideCameraStartTarget = player.GetPosition(Game::LerpAmount);
            State.OutsideCameraStartRotation = Matrix3x3(CinematicCamera.GetForward(), CinematicCamera.Up);
            State.CameraRoll = AngleBetweenVectors(CinematicCamera.Up, Game::Terrain.ExitTransform.Up(), CinematicCamera.GetForward());
            //CinematicCamera.Roll(State.CameraRoll);

            //auto qroll = Matrix::CreateFromAxisAngle(CinematicCamera.GetForward(), State.CameraRoll);
            //CinematicCamera.Up = Vector3::Transform(CinematicCamera.Up, qroll);
            //Up.Normalize();

            SPDLOG_INFO("CAMERA ROLL {}", State.CameraRoll);
            State.OutsideCameraLerp = 0;
            State.ZoomingOut = true;
        }

        // Align the camera roll to the terrain when near the exit
        //if (State.PathIndex > Game::Terrain.SurfacePathIndex * 3 / 4 && State.CameraRollLerp < 1) {
        if (State.ZoomingOut) {
            //if (abs(State.CameraRoll) > 0.1f) {
            //    State.CameraRollLerp += dt;

            //    //if (abs(State.CameraRoll) > 0.1f) {
            //    if (State.CameraRoll > 0) {
            //        CinematicCamera.Roll(dt);
            //        State.CameraRoll -= dt;
            //    }
            //    else {
            //        CinematicCamera.Roll(-dt);
            //        State.CameraRoll += dt;
            //    }
            //}

            if (!State.StopRoll) {
                auto sign = AlignCameraRollToTerrain(dt * 1.5f);
                if (State.RollSign != 0 && sign != State.RollSign) State.StopRoll = true;
                State.RollSign = sign;
            }

            CinematicCamera.Target = Vector3::Lerp(State.OutsideCameraStartTarget, exit.Translation() + exit.Forward() * 20, State.OutsideCameraLerp);
            const auto targetPos = exit.Translation() + exit.Forward() * 160 /*+ exit.Right() * 50 */ + exit.Up() * 25;
            CinematicCamera.Position = Vector3::Lerp(State.OutsideCameraStartPos, targetPos, State.OutsideCameraLerp);

            State.OutsideCameraLerp += dt * .60f;
            if (State.OutsideCameraLerp > 1) State.OutsideCameraLerp = 1;
        }

        Game::SetActiveCamera(CinematicCamera);
    }

    void BeginEscapeSequence() {
        State = { .Scene = EscapeScene::Start };
        State.ExplosionTimer = 0; // todo: set this to 0 when camera switches?
        State.ExplosionSoundTimer = 0;

        //if (Seq::inRange(Game::Terrain.EscapePath, State.CameraPathIndex)) {
        //    State.CameraPathIndex = Game::Terrain.LookbackPathIndex + 2;
        //    CinematicCamera.Position = Game::Terrain.EscapePath[State.CameraPathIndex];
        //}
    }

    void DebugEscapeSequence() {
        if (Game::Level.Objects.empty()) return;

        auto exit = FindExit(Game::Level);
        if (!exit) return;

        auto [seg, side] = Game::Level.GetSegmentAndSide(exit);
        for (auto& obj : seg.Objects) {
            if (auto o = Game::Level.TryGetObject(obj))
                DestroyObject(*o);
        }

        auto facing = side.Center - seg.Center;
        facing.Normalize();
        auto rotation = VectorToRotation(-facing); // ugh, reversed z on objects

        auto position = side.Center - facing * 15;
        auto& player = Game::GetPlayerObject();
        TeleportObject(player, exit.Segment, &position, &rotation);

        Game::BeginSelfDestruct();
        Game::CountdownTimer -= 4; // Skip the intro
        Settings::Editor.EnablePhysics = true;
        Settings::Editor.ShowTerrain = false;
        Game::OnTerrain = false;

        //static EffectID lightHandle{};
        //StopEffect(lightHandle);

        //LightEffectInfo light;
        //light.Radius = 30;
        //light.LightColor = Color(1, 0.8f, 0.2f, 4);
        //lightHandle = AttachLight(light, Game::GetObjectRef(Game::GetPlayerObject()), {});

        Game::Level.Terrain.VolumeLight = Color(.90, 0.90f, 1.0f, 3);
        Game::Terrain.ExitModel = Resources::GameData.ExitModel;

        BeginEscapeSequence();
        Game::PlayMusic("endlevel", false);
    }

    //void LoadEscape(span<byte> data) {
    //    DecodeText(data);
    //    auto lines = String::ToLines(String::OfBytes(data));
    //}
}
