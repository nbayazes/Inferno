#include "pch.h"
#define NOMINMAX
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "Input.h"
#include "Editor/Editor.Object.h"
#include "Graphics/Render.Debug.h"
#include "SoundSystem.h"
#include "Editor/Events.h"
#include "Graphics/Render.Particles.h"
#include "Game.Wall.h"
#include "Editor/Editor.Segment.h"

using namespace DirectX;

namespace Inferno {
    constexpr auto PlayerTurnRollScale = FixToFloat(0x4ec4 / 2) * XM_2PI;
    constexpr auto PlayerTurnRollRate = FixToFloat(0x2000) * XM_2PI;

    // Wraps a UV value to 0-1
    void WrapUV(Vector2& uv) {
        float rmx{};
        if (uv.x < 0)
            uv.x = std::abs(std::modf(uv.x, &rmx));

        uv.x = std::fmodf(uv.x, 1);

        if (uv.y < 0)
            uv.y = std::abs(std::modf(uv.y, &rmx));

        uv.y = std::fmodf(uv.y, 1);
    }

    // Returns the UVs on a face closest to a point in world coordinates
    Vector2 IntersectFaceUVs(Level& level, const Vector3& point, Segment& seg, Tag tag, int tri) {
        auto face = Face::FromSide(level, seg, tag.Side);
        auto indices = face.Side.GetRenderIndices();
        auto& v0 = face[indices[tri * 3 + 0]];
        auto& v1 = face[indices[tri * 3 + 1]];
        auto& v2 = face[indices[tri * 3 + 2]];

        Vector2 uvs[3]{};
        for (int i = 0; i < 3; i++)
            uvs[i] = face.Side.UVs[indices[tri * 3 + i]];

        // Vectors of two edges
        auto xAxis = v1 - v0;
        xAxis.Normalize();
        auto zAxis = xAxis.Cross(v2 - v0);
        zAxis.Normalize();
        auto yAxis = xAxis.Cross(zAxis);

        // Project triangle to 2D
        Vector2 z0(0, 0);
        Vector2 z1((v1 - v0).Length(), 0);
        Vector2 z2((v2 - v0).Dot(xAxis), (v2 - v0).Dot(yAxis));
        Vector2 hit((point - v0).Dot(xAxis), (point - v0).Dot(yAxis)); // project point onto plane

        // barycentric coords of hit
        auto bx = (z1 - z0).Cross(hit - z0).x;
        auto by = (z2 - z1).Cross(hit - z1).x;
        auto bz = (z0 - z2).Cross(hit - z2).x;
        auto ba = Vector3(bx, by, bz) / (bx + by + bz);

        return Vector2::Barycentric(uvs[1], uvs[2], uvs[0], ba.x, ba.y);
    }

    void FixOverlayRotation(uint& x, uint& y, int width, int height, OverlayRotation rotation) {
        uint t = 0;

        switch (rotation) // adjust for overlay rotation
        {
            case OverlayRotation::Rotate0:
                break;
            case OverlayRotation::Rotate90:
                t = y;
                y = x;
                x = width - t - 1;
                break;
            case OverlayRotation::Rotate180:
                y = height - y - 1;
                x = width - x - 1;
                break;
            case OverlayRotation::Rotate270:
                t = x;
                x = y;
                y = height - t - 1;
                break;
        }
    }

    // Returns true if the point was transparent
    bool WallPointIsTransparent(Level& level, const Vector3& pnt, Segment& seg, Tag tag, int tri) {
        if (!seg.SideIsSolid(tag.Side, level)) return true;

        auto uv = IntersectFaceUVs(level, pnt, seg, tag, tri);
        auto& side = seg.GetSide(tag.Side);
        auto tmap = side.TMap2 > LevelTexID::Unset ? side.TMap2 : side.TMap;
        auto& bitmap = Resources::GetBitmap(Resources::LookupTexID(tmap));
        auto wrap = [](float x, uint16 size) {
            // -1 so that x = 1.0 results in width - 1, correcting for the array index
            return (uint)Mod(uint16(x * (float)size - 1.0f), size);
        };

        auto& info = bitmap.Info;
        auto x = wrap(uv.x, info.Width);
        auto y = wrap(uv.y, info.Height);

        // for overlay textures, check the supertransparent mask
        if (side.TMap2 > LevelTexID::Unset) {
            FixOverlayRotation(x, y, info.Width, info.Height, side.OverlayRotation);
            const int idx = y * info.Width + x;
            if (!bitmap.Mask.empty() && bitmap.Mask[idx] == Palette::SUPER_MASK)
                return true; // supertransparent overlay

            if (bitmap.Data[idx].a != 0)
                return false; // overlay wasn't transparent

            // Check the base texture
            auto& tmap1 = Resources::GetBitmap(Resources::LookupTexID(side.TMap));
            x = wrap(uv.x, info.Width);
            y = wrap(uv.y, info.Height);
            return tmap1.Data[idx].a == 0;
        }
        else {
            return bitmap.Data[y * info.Width + x].a == 0;
        }
    }

    // returns true if overlay was destroyed
    bool CheckDestroyableOverlay(Level& level, const Vector3& point, Tag tag, int tri, const Object& source) {
        tri = std::clamp(tri, 0, 1);

        auto seg = level.TryGetSegment(tag);
        if (!seg) return false;

        auto& side = seg->GetSide(tag.Side);
        if (side.TMap2 <= LevelTexID::Unset) return false;

        auto& tmi = Resources::GetLevelTextureInfo(side.TMap2);
        if (tmi.EffectClip == EClipID::None && tmi.DestroyedTexture == LevelTexID::None)
            return false;

        auto& eclip = Resources::GetEffectClip(tmi.EffectClip);
        if (eclip.OneShotTag) return false; // don't trigger from one-shot effects

        bool hasEClip = eclip.DestroyedTexture != LevelTexID::None || eclip.DestroyedEClip != EClipID::None;
        if (!hasEClip && tmi.DestroyedTexture == LevelTexID::None)
            return false;

        // Don't allow non-players to destroy triggers
        if (source.Control.Weapon.ParentType != ObjectType::Player) {
            if (auto wall = level.TryGetWall(tag)) {
                if (wall->Trigger != TriggerID::None)
                    return false;
            }
        }

        auto uv = IntersectFaceUVs(level, point, *seg, tag, tri);

        auto& bitmap = Resources::GetBitmap(Resources::LookupTexID(side.TMap2));
        auto& info = bitmap.Info;
        auto x = uint(uv.x * info.Width) % info.Width;
        auto y = uint(uv.y * info.Height) % info.Height;
        FixOverlayRotation(x, y, info.Width, info.Height, side.OverlayRotation);

        if (!bitmap.Mask.empty() && bitmap.Mask[y * info.Width + x] == Palette::SUPER_MASK)
            return false; // portion hit was supertransparent

        if (bitmap.Data[y * info.Width + x].a == 0)
            return false; // portion hit was transparent

        // Hit opaque overlay!
        //Inferno::SubtractLight(level, tag, *seg);

        fmt::print("tmap2: {}\n", side.TMap2);
        bool usedEClip = false;

        if (eclip.DestroyedEClip != EClipID::None) {
            // Hack storing exploding side state into the global effect.
            // The original game did this, but should be replaced with a more robust system.
            if (Seq::inRange(Resources::GameData.Effects, (int)eclip.DestroyedEClip)) {
                auto& destroyed = Resources::GameData.Effects[(int)eclip.DestroyedEClip];
                if (!destroyed.OneShotTag) {
                    side.TMap2 = Resources::LookupLevelTexID(destroyed.VClip.Frames[0]);
                    destroyed.TimeLeft = destroyed.VClip.PlayTime;
                    destroyed.OneShotTag = tag;
                    destroyed.DestroyedTexture = eclip.DestroyedTexture;
                    usedEClip = true;
                    Render::LoadTextureDynamic(eclip.DestroyedTexture);
                    Render::LoadTextureDynamic(side.TMap2);
                }
            }
        }

        if (!usedEClip) {
            side.TMap2 = hasEClip ? eclip.DestroyedTexture : tmi.DestroyedTexture;
            Render::LoadTextureDynamic(side.TMap2);
        }

        Editor::Events::LevelChanged();

        //Render::ExplosionInfo ei;
        //ei.Clip = hasEClip ? eclip.DestroyedVClip : VClipID::LightExplosion;
        //ei.MinRadius = ei.MaxRadius = hasEClip ? eclip.ExplosionSize : 20.0f;
        //ei.FadeTime = 0.25f;
        //ei.Position = point;
        //ei.Segment = tag.Segment;
        //Render::CreateExplosion(ei);
        Render::SparkEmitter e;
        e.Position = point + side.AverageNormal * 0.1f;
        e.Life = 5;
        e.Segment = tag.Segment;
        e.Direction = side.AverageNormal;
        e.Up = side.Tangents[0];
        e.ConeRadius = 1;
        e.Duration = { 0.75f, 2.4f };
        e.Restitution = 0.6f;
        e.Velocity = { 50, 65 };
        e.Count = { 80, 100 };
        Render::AddSparkEmitter(e);

        auto& vclip = Resources::GetVideoClip(eclip.DestroyedVClip);
        auto soundId = vclip.Sound != SoundID::None ? vclip.Sound : SoundID::LightDestroyed;
        Sound3D sound(point, tag.Segment);
        sound.Resource = Resources::GetSoundResource(soundId);
        Sound::Play(sound);

        if (auto trigger = level.TryGetTrigger(side.Wall)) {
            SPDLOG_INFO("Activating switch {}:{}\n", tag.Segment, tag.Side);
            //fmt::print("Activating switch {}:{}\n", tag.Segment, tag.Side);
            ActivateTrigger(level, *trigger);
        }

        return true; // was destroyed!
    }

    // Rolls the object when turning
    void TurnRoll(PhysicsData& pd, float rollScale, float rollRate, float dt) {
        const auto desiredBank = pd.AngularVelocity.y * rollScale;
        const auto theta = desiredBank - pd.TurnRoll;

        auto roll = rollRate;

        if (std::abs(theta) < roll) {
            roll = theta; // clamp roll to theta
        }
        else {
            if (theta < 0)
                roll = -roll;
        }

        pd.TurnRoll = pd.BankState.Update(roll, dt);
    }

    // Applies angular physics to the player
    void AngularPhysics(Object& obj, float dt) {
        auto& pd = obj.Physics;

        if (IsZero(pd.AngularVelocity) && IsZero(pd.AngularThrust))
            return;

        auto pdDrag = pd.Drag > 0 ? pd.Drag : 1;
        const auto drag = pdDrag * 5 / 2;

        if (HasFlag(pd.Flags, PhysicsFlag::UseThrust) && pd.Mass > 0)
            pd.AngularVelocity += pd.AngularThrust / pd.Mass; // acceleration

        if (!HasFlag(pd.Flags, PhysicsFlag::FixedAngVel))
            pd.AngularVelocity *= 1 - drag;

        Debug::R = pd.AngularVelocity.y;

        // unrotate object for bank caused by turn
        if (HasFlag(pd.Flags, PhysicsFlag::TurnRoll))
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(pd.TurnRoll) * obj.Rotation);

        obj.Rotation = Matrix3x3(Matrix::CreateFromYawPitchRoll(-pd.AngularVelocity * dt * XM_2PI) * obj.Rotation);

        if (HasFlag(pd.Flags, PhysicsFlag::TurnRoll)) {
            TurnRoll(obj.Physics, PlayerTurnRollScale, PlayerTurnRollRate, dt);

            // re-rotate object for bank caused by turn
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(-pd.TurnRoll) * obj.Rotation);
        }
    }

    void LinearPhysics(Object& obj) {
        auto& pd = obj.Physics;

        if (pd.Velocity == Vector3::Zero && pd.Thrust == Vector3::Zero)
            return;

        if (pd.Drag > 0) {
            if (pd.Thrust != Vector3::Zero /*pd.HasFlag(PhysicsFlag::UseThrust)*/ && pd.Mass > 0)
                pd.Velocity += pd.Thrust / pd.Mass; // acceleration

            pd.Velocity *= 1 - pd.Drag;
        }
    }

    void PlotPhysics(double t, const PhysicsData& pd) {
        static int index = 0;
        static double refresh_time = 0.0;

        if (refresh_time == 0.0)
            refresh_time = t;

        if (Input::IsKeyDown(DirectX::Keyboard::Keys::NumPad8)) {
            if (index < Debug::ShipVelocities.size() && t >= refresh_time) {
                //while (refresh_time < Game::ElapsedTime) {
                Debug::ShipVelocities[index] = pd.Velocity.Length();
                //std::cout << t << "," << physics.Velocity.Length() << "\n";
                refresh_time = t + 1.0f / 60.0f;
                index++;
            }
        }
        else {
            index = 1;
        }
    }

    // Applies wiggle to an object
    void WiggleObject(Object& obj, double t, float dt, float amplitude, float rate) {
        auto angle = std::sinf((float)t * XM_2PI * rate) * 20; // multiplier tweaked to cause 0.5 units of movement at a 1/64 tick rate
        auto wiggle = obj.Rotation.Up() * angle * amplitude * dt;
        obj.Physics.Velocity += wiggle;
    }

    // Moves a projectile in a sine pattern
    void SineWeapon(Object& obj, float dt, float speed, float amplitude) {
        if (obj.Control.Type != ControlType::Weapon || !obj.Control.Weapon.SineMovement) return;
        auto offset = std::sin(obj.Control.Weapon.AliveTime * XM_2PI * speed + dt) - std::sin(obj.Control.Weapon.AliveTime * XM_2PI * speed);
        obj.Position += obj.Rotation.Up() * offset * amplitude;
    }

    void FixedPhysics(Object& obj, float dt) {
        auto& physics = obj.Physics;

        if (obj.Type == ObjectType::Player) {
            //const auto& ship = Resources::GameData.PlayerShip;

            //physics.Thrust *= ship.MaxThrust / dt;
            //physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.Thrust;
            Debug::ShipAcceleration = Vector3::Zero;
        }

        AngularPhysics(obj, dt);
        LinearPhysics(obj);
    }

    struct Triangle {
        Array<Vector3, 3> Points;
        Vector3& operator[](int i) { return Points[i]; }
        const Vector3& operator[](int i) const { return Points[i]; }

        Plane GetPlane() const { return Plane(Points[0], Points[1], Points[2]); }
    };

    struct HitResult {
        Vector3 Intersect, IntersectVec, Normal; // where the hit occurred
        float Dot; // Dot product of face normal and object velocity
    };

    struct HitResult2 {
        Vector3 Intersect; // where the hit occurred
        float Distance; // How far along the trajectory
    };

    // Closest point on line
    Vector3 ClosestPointOnLine(const Vector3& a, const Vector3& b, const Vector3& p) {
        // Project p onto ab, computing the paramaterized position d(t) = a + t * (b - a)
        auto ab = b - a;
        auto t = (p - a).Dot(ab) / ab.Dot(ab);

        // Clamp T to a 0-1 range. If t was < 0 or > 1 then the closest point was outside the line!
        t = std::clamp(t, 0.0f, 1.0f);

        // Compute the projected position from the clamped t
        return a + t * ab;
    }

    struct ClosestResult {
        float distSq, s, t;
        Vector3 c1, c2;
    };

    // Computes closest points between two lines. 
    // C1 and C2 of S1(s)=P1+s*(Q1-P1) and S2(t)=P2+t*(Q2-P2), returning s and t. 
    // Function result is squared distance between between S1(s) and S2(t)
    ClosestResult ClosestPointBetweenLines(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2) {
        auto d1 = q1 - p1; // Direction vector of segment S1
        auto d2 = q2 - p2; // Direction vector of segment S2
        auto r = p1 - p2;
        auto a = d1.Dot(d1); // Squared length of segment S1, always nonnegative
        auto e = d2.Dot(d2); // Squared length of segment S2, always nonnegative
        auto f = d2.Dot(r);

        constexpr float EPSILON = 0.001f;
        float s{}, t{};
        Vector3 c1, c2;

        // Check if either or both segments degenerate into points
        if (a <= EPSILON && e <= EPSILON) {
            // Both segments degenerate into points
            s = t = 0.0f;
            c1 = p1;
            c2 = p2;
            auto distSq = (c1 - c2).Dot(c1 - c2);
            return { distSq, s, t, c1, c2 };
        }

        if (a <= EPSILON) {
            // First segment degenerates into a point
            s = 0.0f;
            t = f / e; // s = 0 => t = (b*s + f) / e = f / e
            t = std::clamp(t, 0.0f, 1.0f);
        }
        else {
            float c = d1.Dot(r);
            if (e <= EPSILON) {
                // Second segment degenerates into a point
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f); // t = 0 => s = (b*t - c) / a = -c / a
            }
            else {
                // The general nondegenerate case starts here
                float b = d1.Dot(d2);
                float denom = a * e - b * b; // Always nonnegative
                // If segments not parallel, compute closest point on L1 to L2 and
                // clamp to segment S1. Else pick arbitrary s (here 0)
                s = denom == 0 ? 0 : std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
                // Compute point on L2 closest to S1(s) using
                // t = Dot((P1 + D1*s) - P2,D2) / Dot(D2,D2) = (b*s + f) / e
                t = (b * s + f) / e;
                // If t in [0,1] done. Else clamp t, recompute s for the new value
                // of t using s = Dot((P2 + D2*t) - P1,D1) / Dot(D1,D1)= (t*b - c) / a
                // and clamp s to [0, 1]
                if (t < 0.0f) {
                    t = 0.0f;
                    s = std::clamp(-c / a, 0.0f, 1.0f);
                }
                else if (t > 1.0f) {
                    t = 1.0f;
                    s = std::clamp((b - c) / a, 0.0f, 1.0f);
                }
            }
        }

        c1 = p1 + d1 * s;
        c2 = p2 + d2 * t;
        auto distSq = (c1 - c2).Dot(c1 - c2);
        return { distSq, s, t, c1, c2 };
    }

    // Returns true if a point lies within a triangle
    bool PointInTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point) {
        // Move the triangle so that the point becomes the triangle's origin
        auto a = p0 - point;
        auto b = p1 - point;
        auto c = p2 - point;

        // Compute the normal vectors for triangles:
        Vector3 u = b.Cross(c), v = c.Cross(a), w = a.Cross(b);

        // Test if the normals are facing the same direction
        return u.Dot(v) >= 0.0f && u.Dot(w) >= 0.0f;
    }

    // Returns true if a point lies within a triangle
    bool PointInTriangle(const Triangle& t, Vector3 point) {
        // Move the triangle so that the point becomes the triangle's origin
        auto a = t[0] - point;
        auto b = t[1] - point;
        auto c = t[2] - point;

        // Compute the normal vectors for triangles:
        Vector3 u = b.Cross(c), v = c.Cross(a), w = a.Cross(b);

        // Test if the normals are facing the same direction
        return u.Dot(v) >= 0.0f && u.Dot(w) >= 0.0f;
    }

    // Returns the closest point on a triangle to a point
    Vector3 ClosestPointOnTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point) {
        Plane plane(p0, p1, p2);
        point = ProjectPointOntoPlane(point, plane);

        if (PointInTriangle(p0, p1, p2, point))
            return point; // point is on the surface of the triangle

        // check the points and edges
        auto c1 = ClosestPointOnLine(p0, p1, point);
        auto c2 = ClosestPointOnLine(p1, p2, point);
        auto c3 = ClosestPointOnLine(p2, p0, point);

        auto mag1 = (point - c1).LengthSquared();
        auto mag2 = (point - c2).LengthSquared();
        auto mag3 = (point - c3).LengthSquared();

        float min = std::min(std::min(mag1, mag2), mag3);

        if (min == mag1)
            return c1;
        else if (min == mag2)
            return c2;
        return c3;
    }

    // Returns the nearest distance to the face edge and a point. Skips the internal split.
    float FaceEdgeDistance(const Segment& seg, SideID side, const Face& face, const Vector3& point) {
        // Check the four outside edges of the face
        float mag1, mag2, mag3, mag4;
        mag1 = mag2 = mag3 = mag4 = FLT_MAX;

        // If the edge doesn't have a connection it's safe to put a decal on it
        if (seg.SideHasConnection(GetAdjacentSide(side, 0))) {
            auto c = ClosestPointOnLine(face[0], face[1], point);
            mag1 = (point - c).Length();
        }
        if (seg.SideHasConnection(GetAdjacentSide(side, 1))) {
            auto c = ClosestPointOnLine(face[1], face[2], point);
            mag2 = (point - c).Length();
        }
        if (seg.SideHasConnection(GetAdjacentSide(side, 2))) {
            auto c = ClosestPointOnLine(face[2], face[3], point);
            mag3 = (point - c).Length();
        }
        if (seg.SideHasConnection(GetAdjacentSide(side, 3))) {
            auto c = ClosestPointOnLine(face[3], face[0], point);
            mag4 = (point - c).Length();
        }

        return std::min(std::min(std::min(mag1, mag2), mag3), mag4);
    }

    Vector3 GetTriangleNormal(const Vector3& a, const Vector3& b, const Vector3& c) {
        auto v1 = b - a;
        auto v2 = c - a;
        auto normal = v1.Cross(v2);
        normal.Normalize();
        return normal;
    }

    // Untested
    /*
    Option<Vector3> NearestPointOnTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, const BoundingSphere& sphere) {
        auto N = GetTriangleNormal(p0, p1, p2);
        float dist = (sphere.Center - p0).Dot(N); // signed distance between sphere and plane
        //if (!mesh.is_double_sided() && dist > 0)
            //return false; // can pass through back side of triangle (optional)
        if (dist < -sphere.Radius || dist > sphere.Radius)
            return {}; // no intersection

        auto point0 = (sphere.Center - N) * dist; // projected sphere center on triangle plane

        float radiussq = sphere.Radius * sphere.Radius;

        auto point1 = ClosestPointOnLine(p0, p1, sphere.Center);
        auto v1 = sphere.Center - point1;
        float distsq1 = v1.Dot(v1);
        bool intersects = distsq1 < radiussq;

        auto point2 = ClosestPointOnLine(p1, p2, sphere.Center);
        auto v2 = sphere.Center - point2;
        float distsq2 = v2.Dot(v2);
        intersects |= distsq2 < radiussq;

        auto point3 = ClosestPointOnLine(p2, p0, sphere.Center);
        auto v3 = sphere.Center - point3;
        float distsq3 = v3.Dot(v3);
        intersects |= distsq3 < radiussq;

        bool inside = PointInTriangle(p0, p1, p2, sphere.Center);

        if (inside || intersects) {
            auto best_point = point0;
            Vector3 intersection_vec;

            if (inside) {
                intersection_vec = sphere.Center - point0;
            }
            else {
                auto d = sphere.Center - point1;
                float best_distsq = d.Dot(d);
                best_point = point1;
                intersection_vec = d;

                d = sphere.Center - point2;
                float distsq = d.Dot(d);
                if (distsq < best_distsq) {
                    distsq = best_distsq;
                    best_point = point2;
                    intersection_vec = d;
                }

                d = sphere.Center - point3;
                distsq = d.Dot(d);
                if (distsq < best_distsq) {
                    distsq = best_distsq;
                    best_point = point3;
                    intersection_vec = d;
                }
            }

            auto len = intersection_vec.Length();  // vector3 length calculation:
            auto penetration_normal = intersection_vec / len;  // normalize
            float penetration_depth = sphere.Radius - len; //
            return sphere.Center + penetration_normal * penetration_depth; // intersection success
        }

        return {};
    }
    */

    // Intersects sphere a with b. Surface normal points towards a.
    HitInfo IntersectSphereSphere(const BoundingSphere& a, const BoundingSphere& b) {
        HitInfo hit;
        Vector3 c0(a.Center), c1(b.Center);
        auto v = c1 - c0;
        float depth = b.Radius + a.Radius - v.Length();
        if (depth > 0) {
            v.Normalize();
            auto e0 = c0 + v * a.Radius;
            auto e1 = c1 - v * b.Radius;
            hit.Point = (e1 + e0) / 2;
            hit.Distance = Vector3::Distance(hit.Point, c0);
            hit.Normal = -v;
        }

        return hit;
    }

    //// Returns the closest point on a triangle to a point
    //Vector3 ClosestPoint(const Triangle& t, Vector3 point) {
    //    point = ProjectPointOntoPlane(point, t.GetPlane());

    //    if (PointInTriangle(t, point))
    //        return point; // point is on the surface of the triangle

    //    // check the points and edges
    //    auto c1 = ClosestPoint(t[0], t[1], point);
    //    auto c2 = ClosestPoint(t[1], t[2], point);
    //    auto c3 = ClosestPoint(t[2], t[0], point);

    //    auto mag1 = (point - c1).LengthSquared();
    //    auto mag2 = (point - c2).LengthSquared();
    //    auto mag3 = (point - c3).LengthSquared();

    //    float min = std::min(std::min(mag1, mag2), mag3);

    //    if (min == mag1)
    //        return c1;
    //    else if (min == mag2)
    //        return c2;
    //    return c3;
    //}

    // Returns the nearest intersection point on a face
    HitInfo IntersectFaceSphere(const Face& face, const BoundingSphere& sphere) {
        HitInfo hit;
        auto i = face.Side.GetRenderIndices();

        if (sphere.Intersects(face[i[0]], face[i[1]], face[i[2]])) {
            auto p = ClosestPointOnTriangle(face[i[0]], face[i[1]], face[i[2]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < hit.Distance) {
                hit.Point = p;
                hit.Distance = dist;
            }
        }

        if (sphere.Intersects(face[i[3]], face[i[4]], face[i[5]])) {
            auto p = ClosestPointOnTriangle(face[i[3]], face[i[4]], face[i[5]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < hit.Distance) {
                hit.Point = p;
                hit.Distance = dist;
            }
        }

        if (hit.Distance > sphere.Radius)
            hit.Distance = FLT_MAX;
        else
            (hit.Point - sphere.Center).Normalize(hit.Normal);

        return hit;
    }


    Tuple<Vector3, float> IntersectTriangleSphere(const Vector3& p0, const Vector3& p1, const Vector3& p2, const BoundingSphere& sphere) {
        if (sphere.Intersects(p0, p1, p2)) {
            auto p = ClosestPointOnTriangle(p0, p1, p2, sphere.Center);
            auto dist = (p - sphere.Center).Length();
            return { p, dist };
        }

        return { {}, FLT_MAX };
    }


    HitInfo BoundingCapsule::Intersects(const DirectX::BoundingSphere& sphere) const {
        auto p = ClosestPointOnLine(B, A, sphere.Center);
        DirectX::BoundingSphere cap(p, Radius);
        return IntersectSphereSphere(cap, sphere);
    }

    bool BoundingCapsule::Intersects(const BoundingCapsule& other) const {
        auto p = ClosestPointBetweenLines(A, B, other.A, other.B);
        float r = Radius + other.Radius;
        return p.distSq <= r * r;
    }

    bool BoundingCapsule::Intersects(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& faceNormal, Vector3& refPoint, Vector3& normal, float& dist) const {
        if (p0 == p1 || p1 == p2 || p2 == p0) return false; // Degenerate check
        // Compute capsule line endpoints A, B like before in capsule-capsule case:
        auto capsuleNormal = B - A;
        capsuleNormal.Normalize();

        if (capsuleNormal.Dot(faceNormal) < 0) {
            // only do projections if triangle faces towards the capsule

            //auto offset = capsuleNormal * Radius; // line end offset
            //auto a = base + offset; // base
            //auto b = tip - offset; // tip

            //Render::Debug::DrawLine(a, b, { 1, 0, 0 });

            // Project the line onto plane
            Ray r(A, capsuleNormal);
            Plane p(p0, p1, p2);
            auto linePlaneIntersect = ProjectRayOntoPlane(r, p0, p.Normal());
            auto inside = PointInTriangle(p0, p1, p2, linePlaneIntersect);

            if (inside) {
                refPoint = linePlaneIntersect;
                //Render::Debug::DrawPoint(refPoint, { 0, 1, 0 });
            }
            else {
                refPoint = ClosestPointOnTriangle(p0, p1, p2, linePlaneIntersect);
                //Render::Debug::DrawPoint(refPoint, { 0, 1, 1 });
            }

            auto center = ClosestPointOnLine(A, B, refPoint);
            DirectX::BoundingSphere sphere(center, Radius);

            auto [point, idist] = IntersectTriangleSphere(p0, p1, p2, sphere);

            if (idist != FLT_MAX) {
                refPoint = point;

                normal = idist == 0 ? faceNormal : center - point;
                normal.Normalize();
                dist = idist;
                return idist < Radius;
            }
        }

        // projection didn't intersect triangle, check if end does
        DirectX::BoundingSphere sphere{ B, Radius };
        auto [point, idist] = IntersectTriangleSphere(p0, p1, p2, sphere);
        return idist < Radius;
    }

    bool ObjectCanHitTarget(const Object& src, const Object& target) {
        if (!target.IsAlive() && target.Type != ObjectType::Reactor) return false;
        if (src.Signature == target.Signature) return false; // don't hit yourself!
        //if (src.Parent == target.Parent && src.Parent != ObjID::None) return false; // don't hit your siblings!

        //if ((src.Parent != ObjID::None && target.Parent != ObjID::None) && src.Parent == target.Parent)
        //    return false; // Don't hit your siblings!

        switch (src.Type) {
            case ObjectType::Robot:
                switch (target.Type) {
                    case ObjectType::Wall:
                    //case ObjectType::Robot:
                    case ObjectType::Player:
                    case ObjectType::Coop:
                    //case ObjectType::Weapon:
                    case ObjectType::Clutter:
                        return true;
                    default:
                        return false;
                }

            case ObjectType::Coop:
            case ObjectType::Player:
                switch (target.Type) {
                    case ObjectType::Weapon:
                    {
                        // Player can't hit their own mines until they arm
                        if ((target.ID == (int)WeaponID::ProxMine || target.ID == (int)WeaponID::SmartMine)
                            && target.Control.Weapon.AliveTime < Game::MINE_ARM_TIME)
                            return false;

                        return target.ID == (int)WeaponID::LevelMine;
                    }

                    case ObjectType::Wall:
                    case ObjectType::Robot:
                    case ObjectType::Powerup:
                    case ObjectType::Reactor:
                    case ObjectType::Clutter:
                    case ObjectType::Hostage:
                    //case ObjectType::Player: // player can hit other players, but not in singleplayer
                    //case ObjectType::Coop:
                    case ObjectType::Marker:
                        return true;
                    default:
                        return false;
                }

            case ObjectType::Weapon:
                if (Seq::contains(src.Control.Weapon.RecentHits, target.Signature))
                    return false; // Don't hit objects recently hit by this weapon (for piercing)

                switch (target.Type) {
                    case ObjectType::Wall:
                    case ObjectType::Robot:
                    {
                        auto& ri = Resources::GetRobotInfo(target.ID);
                        if (ri.IsCompanion)
                            return false; // weapons can't directly hit guidebots

                        return true;
                    }
                    case ObjectType::Player:
                    {
                        if (target.ID > 0) return false; // Only hit player 0 in singleplayer
                        if (src.Parent == ObjID(0)) return false; // Don't hit the player with their own shots
                        if (WeaponIsMine((WeaponID)src.ID) && src.Control.Weapon.AliveTime < Game::MINE_ARM_TIME)
                            return false; // Mines can't hit the player until they arm

                        return true;
                    }

                    //case ObjectType::Coop:
                    case ObjectType::Weapon:
                        if (WeaponIsMine((WeaponID)src.ID))
                            return false; // mines can't hit other mines

                        return WeaponIsMine((WeaponID)target.ID);

                    case ObjectType::Reactor:
                    case ObjectType::Clutter:
                        return true;
                    default:
                        return false;
                }

            case ObjectType::Reactor:
                switch (target.Type) {
                    case ObjectType::Wall:
                    //case ObjectType::Robot:
                    case ObjectType::Player:
                    case ObjectType::Clutter:
                    case ObjectType::Coop:
                        return true;
                    default:
                        return false;
                }

            case ObjectType::Clutter:
                return false; // not implemented

            default:
                return false;
        }
    }

    // Finds the nearest sphere-level intersection
    bool IntersectLevel(Level& level, const BoundingSphere& sphere, SegID segId, ObjID oid, LevelHit& hit) {
        auto& seg = level.GetSegment(segId);
        hit.Visited.insert(segId);

        auto& obj = level.Objects[(int)oid];

        // Did we hit any objects in this segment?
        for (int i = 0; i < level.Objects.size(); i++) {
            auto& other = level.Objects[i];
            if (other.Segment != segId) continue;
            if (oid == (ObjID)i) continue; // don't hit yourself!
            if (oid == other.Parent) continue; // Don't hit your children!

            if (!ObjectCanHitTarget(obj, other)) continue;

            BoundingSphere objSphere(other.Position, other.Radius);
            if (auto info = IntersectSphereSphere(sphere, objSphere)) {
                hit.Update(info, &other);
            }
        }

        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, segId, side);

            if (auto h = IntersectFaceSphere(face, sphere)) {
                if (h.Normal.Dot(face.AverageNormal()) > 0)
                    continue; // passed through back of face

                if (seg.SideIsSolid(side, level)) {
                    hit.Update(h, { segId, side }); // hit a solid wall
                }
                else {
                    // intersected with a connected side, must check faces in it too
                    auto conn = seg.GetConnection(side);
                    if (conn > SegID::None && !hit.Visited.contains(conn))
                        IntersectLevel(level, sphere, conn, oid, hit); // Recursive
                }
            }
        }

        return hit;
    }

    // Finds the nearest sphere-level intersection for debris
    // Debris only collide with robots, players and walls
    bool IntersectLevelDebris(Level& level, const BoundingCapsule& capsule, SegID segId, LevelHit& hit) {
        auto& seg = level.GetSegment(segId);
        hit.Visited.insert(segId);

        // Did we hit any objects in this segment?
        for (int i = 0; i < level.Objects.size(); i++) {
            auto& other = level.Objects[i];
            if (!other.IsAlive() || other.Segment != segId) continue;
            if (other.Type != ObjectType::Player && other.Type != ObjectType::Robot && other.Type != ObjectType::Reactor)
                continue;
            //if (!ObjectCanHitTarget(obj.Type, other.Type)) continue;

            BoundingSphere sphere(other.Position, other.Radius);
            if (auto info = capsule.Intersects(sphere)) {
                hit.Update(info, &other);
            }
        }

        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, seg, side);
            auto i = face.Side.GetRenderIndices();

            Vector3 refPoint, normal;
            float dist{};
            if (capsule.Intersects(face[i[0]], face[i[1]], face[i[2]], face.Side.Normals[0], refPoint, normal, dist)) {
                if (seg.SideIsSolid(side, level) && dist < hit.Distance) {
                    hit.Normal = normal;
                    hit.Point = refPoint;
                    hit.Distance = dist;
                    hit.Tag = { segId, side };
                }
                else {
                    // scan touching seg
                    auto conn = seg.GetConnection(side);
                    if (conn > SegID::None && !hit.Visited.contains(conn))
                        IntersectLevelDebris(level, capsule, conn, hit);
                }
            }

            if (capsule.Intersects(face[i[3]], face[i[4]], face[i[5]], face.Side.Normals[1], refPoint, normal, dist)) {
                if (seg.SideIsSolid(side, level)) {
                    if (dist < hit.Distance) {
                        hit.Normal = normal;
                        hit.Point = refPoint;
                        hit.Distance = dist;
                        hit.Tag = { segId, side };
                    }
                }
                else {
                    // scan touching seg
                    auto conn = seg.GetConnection(side);
                    if (conn > SegID::None && !hit.Visited.contains(conn))
                        IntersectLevelDebris(level, capsule, conn, hit);
                }
            }
        }

        return hit;
    }

    // intersects a ray with the level, returning hit information
    bool IntersectLevel(Level& level, const Ray& ray, SegID start, float maxDist, bool passTransparent, bool hitTestTextures, LevelHit& hit) {
        if (start == SegID::None) return false;
        if (maxDist <= 0.01f) return false;
        SegID next = start;

        while (next > SegID::None) {
            SegID segId = next;
            hit.Visited.insert(segId); // must track visited segs to prevent circular logic
            next = SegID::None;
            auto& seg = level.GetSegment(segId);

            for (auto& side : SideIDs) {
                auto face = Face::FromSide(level, seg, side);

                float dist{};
                auto tri = face.Intersects(ray, dist);
                if (tri != -1 && dist < hit.Distance) {
                    if (dist > maxDist) return {}; // hit is too far

                    auto intersect = hit.Point = ray.position + ray.direction * dist;
                    Tag tag{ segId, side };

                    bool isSolid = false;
                    if (seg.SideIsWall(side) && WallIsTransparent(level, tag)) {
                        if (passTransparent)
                            isSolid = false;
                        else if (hitTestTextures)
                            isSolid = !WallPointIsTransparent(level, intersect, seg, tag, tri);
                    }
                    else {
                        isSolid = seg.SideIsSolid(side, level);
                    }

                    if (isSolid) {
                        hit.Tag = tag;
                        hit.Distance = dist;
                        hit.Normal = face.AverageNormal();
                        hit.Tangent = face.Side.Tangents[tri];
                        hit.WallPoint = hit.Point = ray.position + ray.direction * dist;
                        hit.EdgeDistance = FaceEdgeDistance(seg, side, face, hit.Point);
                        return true;
                    }
                    else {
                        auto conn = seg.GetConnection(side);
                        if (!hit.Visited.contains(conn))
                            next = conn;
                        break; // go to next segment
                    }
                }
            }
        }

        return false;
    }

    // Intersects a capsule with the level
    bool IntersectLevel(Level& level, const BoundingCapsule& capsule, SegID segId, const Object& object, LevelHit& hit) {
        auto& seg = level.GetSegment(segId);
        hit.Visited.insert(segId);

        // Did we hit any objects in this segment?
        for (int i = 0; i < level.Objects.size(); i++) {
            auto& target = level.Objects[i];
            if (target.Segment != segId) continue;
            if (object.Parent == (ObjID)i) continue; // don't hit your parent!
            if (!ObjectCanHitTarget(object, target)) continue;

            BoundingSphere sphere(target.Position, target.Radius);
            if (auto info = capsule.Intersects(sphere)) {
                hit.Update(info, &target);
            }
        }

        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, seg, side);
            auto i = face.Side.GetRenderIndices();

            Vector3 refPoint, normal;
            float dist{};

            for (int tri = 0; tri < 2; tri++) {
                if (capsule.Intersects(face[i[tri * 3]], face[i[tri * 3 + 1]], face[i[tri * 3 + 2]],
                                       face.Side.Normals[tri], refPoint, normal, dist)) {
                    if (seg.SideIsSolid(side, level) && dist < hit.Distance) {
                        Tag tag(segId, side);
                        if (object.Type == ObjectType::Weapon) {
                            if (WallPointIsTransparent(level, refPoint, seg, tag, tri))
                                continue; // skip projectiles that hit transparent part of a wall
                        }

                        auto dir = object.Physics.Velocity;
                        dir.Normalize();
                        Ray ray(object.LastPosition, dir);
                        float wallDistance;
                        face.Intersects(ray, wallDistance);
                        hit.WallPoint = object.LastPosition + dir * wallDistance;

                        hit.Normal = face.Side.Normals[tri];
                        hit.Point = refPoint;
                        hit.Distance = dist;
                        hit.Tangent = face.Side.Tangents[tri];
                        hit.EdgeDistance = FaceEdgeDistance(seg, side, face, hit.Point);
                        hit.Tag = { segId, side };
                        hit.Tri = tri;
                    }
                    else {
                        // scan touching seg
                        auto conn = seg.GetConnection(side);
                        if (conn > SegID::None && !hit.Visited.contains(conn))
                            IntersectLevel(level, capsule, conn, object, hit);
                    }
                }
            }
        }

        return hit;
    }

    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent) {
        auto dir = b.Position - a.Position;
        auto dist = dir.Length();
        dir.Normalize();
        Ray ray(a.Position, dir);
        LevelHit hit;
        return IntersectLevel(Game::Level, ray, a.Segment, dist, passTransparent, true, hit);
    }

    //void Intersect(Level& level, SegID segId, const Triangle& t, Object& obj, float dt, int pass) {
    //    //if (obj.Type == ObjectType::Player) return;

    //    Plane plane(t.Points[0], t.Points[1], t.Points[2]);
    //    auto& pd = obj.Physics;

    //    if (pd.Velocity.Dot(plane.Normal()) > 0) return; // ignore faces pointing away from velocity
    //    auto delta = obj.Position - obj.LastPosition;
    //    auto expectedDistance = delta.Length();
    //    if (expectedDistance < 0.001f) return;
    //    Vector3 dir;
    //    delta.Normalize(dir);
    //    //auto expectedTravel = (obj.Position() - obj.PrevPosition()).Length();

    //    float hitDistance{};
    //    Ray ray(obj.LastPosition, dir);
    //    //bool hit = false;

    //    //if (ray.Intersects(t.Points[0], t.Points[1], t.Points[2], hitDistance)) {
    //    //    hit = hitDistance < expectedDistance - obj.Radius;
    //    //    //if (hit && hitDistance < expectedDistance.Length() + obj.Radius) // did the object pass all the way through the wall in one frame?
    //    //    if (hit)
    //    //        obj.Position = obj.LastPosition + dir * (hitDistance - obj.Radius);
    //    //}

    //    bool isHit = false;

    //    LevelHit hit;
    //    IntersectLevel(level, ray, segId, expectedDistance, false, hit);
    //    if (hit.HitObj) {
    //        // hit an object
    //        obj.Position = obj.LastPosition + dir * (hit.Distance - obj.Radius);
    //        hitDistance = hit.Distance;
    //        isHit = true;
    //    }
    //    else if (hit.Tag) {
    //        // hit a wall
    //        obj.Position = obj.LastPosition + dir * (hit.Distance - obj.Radius);
    //        hitDistance = hit.Distance;
    //        isHit = true;
    //    }
    //    else {
    //        // ray cast didn't hit anything, try the sphere test
    //        // note that this is not a sweep and will miss points between the begin and end.
    //        // Fortunately, most fast-moving objects are projectiles and have small radii.
    //        BoundingSphere sphere(obj.Position, obj.Radius);
    //        isHit = sphere.Intersects(t.Points[0], t.Points[1], t.Points[2]);

    //        float planeDist;
    //        ray.Intersects(plane, planeDist);
    //        if (!isHit && planeDist <= expectedDistance) {
    //            // Last, test if the object sphere collides with the intersection of the triangle's plane
    //            sphere = BoundingSphere(obj.LastPosition + dir * planeDist, obj.Radius * 1.1f);
    //            //BoundingSphere sphere(obj.Position(), obj.Radius);
    //            isHit = sphere.Intersects(t.Points[0], t.Points[1], t.Points[2]);
    //        }
    //    }

    //    if (!isHit) return;

    //    bool tryAgain = false;

    //    auto closestPoint = ClosestPointOnTriangle(t[0], t[1], t[2], obj.Position);
    //    Debug::ClosestPoints.push_back(closestPoint);
    //    auto closestNormal = obj.Position - closestPoint;
    //    closestNormal.Normalize();

    //    // Adjust velocity
    //    if (pd.HasFlag(PhysicsFlag::Stick)) {

    //    }
    //    else {
    //        // We're constrained by wall, so subtract wall part from velocity
    //        auto wallPart = closestNormal.Dot(pd.Velocity);

    //        if (pd.HasFlag(PhysicsFlag::Bounce))
    //            wallPart *= 2; //Subtract out wall part twice to achieve bounce

    //        pd.Velocity -= closestNormal * wallPart;
    //        tryAgain = true;

    //        //pd.Velocity = Vector3::Reflect(pd.Velocity, plane.Normal()) / pd.Mass;
    //    }

    //    // Check if the wall is penetrating the object, and if it is apply some extra force to get it out
    //    if (pass > 0 && !pd.HasFlag(PhysicsFlag::Bounce)) {
    //        auto depth = obj.Radius - (obj.Position - closestPoint).Length();
    //        if (depth > 0.075f) {
    //            auto strength = depth / 0.15f;
    //            pd.Velocity += closestNormal * pd.Velocity.Length() * strength;
    //            //SPDLOG_WARN("Object inside wall. depth: {} strength: {}", depth, strength);

    //            // Counter the input velocity
    //            pd.Velocity -= obj.Physics.InputVelocity * strength * dt;
    //        }
    //    }

    //    // Move the object to the surface of the triangle
    //    obj.Position = closestPoint + closestNormal * obj.Radius;
    //}


    void UpdateGame(Level& level, float dt) {
        for (auto& obj : level.Objects) {
            obj.Lifespan -= dt;
        }

        UpdateDoors(level, dt);
    }


    //void TurnTowardsVector(const Vector3& target, Object& obj, float rate) {
    //    if (target == Vector3::Zero) return;

    //}

    void ApplyForce(Object& obj, const Vector3& force) {
        if (obj.Movement != MovementType::Physics) return;
        if (obj.Physics.Mass == 0) return;
        obj.Physics.Velocity += force * 1.0 / obj.Physics.Mass;
    }

    // Creates an explosion that can cause damage or knockback
    void CreateExplosion(Level& level, const Object* source, const GameExplosion& explosion) {
        for (auto& obj : level.Objects) {
            if (&obj == source) continue;
            if (!obj.IsAlive()) continue;

            if (obj.Type == ObjectType::Weapon && (obj.ID != (int)WeaponID::ProxMine && obj.ID != (int)WeaponID::SmartMine && obj.ID != (int)WeaponID::LevelMine))
                continue; // only allow explosions to affect weapons that are mines

            // ((obj0p->type==OBJ_ROBOT) && ((Objects[parent].type != OBJ_ROBOT) || (Objects[parent].id != obj0p->id)))
            //if (&level.GetObject(obj.Parent) == &source) continue; // don't hit your parent

            if (obj.Type != ObjectType::Player && obj.Type != ObjectType::Robot && obj.Type != ObjectType::Weapon && obj.Type != ObjectType::Reactor)
                continue;

            auto dist = Vector3::Distance(obj.Position, explosion.Position);

            // subtract object radius so large enemies don't take less splash damage, this increases the effectiveness of explosives in general
            // however don't apply it to players due to dramatically increasing the amount of damage taken
            if (obj.Type != ObjectType::Player && obj.Type != ObjectType::Coop)
                dist -= obj.Radius;

            if (dist >= explosion.Radius) continue;
            dist = std::max(dist, 0.0f);

            Vector3 dir = obj.Position - explosion.Position;
            dir.Normalize();
            Ray ray(explosion.Position, dir);
            LevelHit hit;
            if (IntersectLevel(level, ray, explosion.Segment, dist, true, true, hit))
                continue;

            // linear damage falloff
            float damage = explosion.Damage - (dist * explosion.Damage) / explosion.Radius;
            float force = explosion.Force - (dist * explosion.Force) / explosion.Radius;

            Vector3 forceVec = dir * force;
            //auto hitPos = (source.Position - obj.Position) * obj.Radius / (obj.Radius + dist);

            // Find where the point of impact is... ( pos_hit )
            //vm_vec_scale(vm_vec_sub(&pos_hit, &obj->pos, &obj0p->pos), fixdiv(obj0p->size, obj0p->size + dist));

            switch (obj.Type) {
                case ObjectType::Weapon:
                {
                    ApplyForce(obj, forceVec);
                    // Mines can blow up under enough force
                    //if (obj.ID == (int)WeaponID::ProxMine || obj.ID == (int)WeaponID::SmartMine) {
                    //    if (dist * force > 0.122f) {
                    //        obj.Lifespan = 0;
                    //        // explode()?
                    //    }
                    //}
                    break;
                }

                case ObjectType::Robot:
                {
                    ApplyForce(obj, forceVec);
                    obj.ApplyDamage(damage);
                    obj.LastHitForce += forceVec;
                    fmt::print("applied {} splash damage at dist {}\n", damage, dist);

                    // stun robot if not boss

                    // Boss invuln stuff

                    // guidebot ouchies
                    break;
                }

                case ObjectType::Reactor:
                {
                    // apply damage if source is player
                    break;
                }

                case ObjectType::Player:
                {
                    ApplyForce(obj, forceVec);
                    // also apply rotational

                    // shields, flash, physics
                    // divide damage by 4 on trainee

                    break;
                }

                default:
                    throw Exception("Invalid object type in CreateExplosion()");
            }
        }
    }

    void WeaponHitObject(const LevelHit& hit, Object& obj, Level& level) {
        auto& weapon = Resources::GameData.Weapons[obj.ID];
        float damage = weapon.Damage[Game::Difficulty];

        auto& target = *hit.HitObj;
        //auto p = src.Mass * src.InputVelocity;

        auto& targetPhys = target.Physics;
        auto srcMass = obj.Physics.Mass == 0 ? 0.01f : obj.Physics.Mass;
        auto targetMass = targetPhys.Mass == 0 ? 0.01f : targetPhys.Mass;

        // apply forces from projectile to object
        auto force = obj.Physics.Velocity * srcMass / targetMass;
        targetPhys.Velocity += hit.Normal * hit.Normal.Dot(force);
        target.LastHitForce += force;

        Matrix basis(target.Rotation);
        basis = basis.Invert();
        force = Vector3::Transform(force, basis); // transform forces to basis of object
        auto arm = Vector3::Transform(hit.Point - target.Position, basis);
        auto torque = force.Cross(arm);
        auto inertia = (2.0f / 5.0f) * targetMass * target.Radius * target.Radius;
        auto accel = torque / inertia;
        targetPhys.AngularVelocity += accel; // should we multiply by dt here?

        if (target.Type == ObjectType::Weapon) {
            target.Lifespan = -1; // Cause the target weapon to detonate by expiring
            if (weapon.SplashRadius == 0)
                return; // non-explosive weapons keep going
        }
        else {
            if (target.Type != ObjectType::Player) // player shields are handled differently
                target.ApplyDamage(damage);

            //fmt::print("applied {} damage\n", damage);
            VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : VClipID::SmallExplosion;

            Render::ExplosionInfo expl;
            expl.Sound = weapon.RobotHitSound;
            expl.Segment = hit.HitObj->Segment;
            expl.Position = hit.Point;
            expl.Parent = obj.Parent;

            expl.Clip = vclip;
            expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
            expl.Color = Color{ 1.15f, 1.15f, 1.15f };
            expl.FadeTime = 0.1f;

            if (obj.ID == (int)WeaponID::Concussion) {
                // todo: and all other missiles
                expl.Instances = 2;
                expl.Delay = { 0, 0 };
                expl.Clip = weapon.RobotHitVClip;
                expl.Color = Color{ 1, 1, 1 };
            }

            Render::CreateExplosion(expl);
        }

        obj.Control.Weapon.AddRecentHit(target.Signature);

        if (!weapon.Piercing)
            obj.Flags |= ObjectFlag::Dead; // remove weapon after hitting an enemy

        if (weapon.SplashRadius > 0) {
            GameExplosion ge{};
            ge.Segment = hit.Tag.Segment;
            ge.Position = hit.Point;
            ge.Damage = damage;
            ge.Force = damage; // force = damage, really?
            ge.Radius = weapon.SplashRadius;

            CreateExplosion(level, &obj, ge);
        }
    }

    void WeaponHitWall(const LevelHit& hit, Object& obj, Level& level, ObjID objId) {
        CheckDestroyableOverlay(level, hit.Point, hit.Tag, hit.Tri, obj);

        auto& weapon = Resources::GameData.Weapons[obj.ID];
        float damage = weapon.Damage[Game::Difficulty];
        float splashRadius = weapon.SplashRadius;
        float force = damage;
        float impactSize = weapon.ImpactSize;

        // don't use volatile hits on large explosions like megas
        constexpr float VOLATILE_DAMAGE_RADIUS = 30;
        bool isLargeExplosion = splashRadius >= VOLATILE_DAMAGE_RADIUS / 2;

        // weapons with splash damage (explosions) always use robot hit effects
        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;

        bool addDecal = !weapon.Extended.Decal.empty();
        bool hitLiquid = false;
        bool hitForcefield = false;

        auto& side = level.GetSide(hit.Tag);
        auto& ti = Resources::GetLevelTextureInfo(side.TMap);

        hitForcefield = ti.HasFlag(TextureFlag::ForceField);
        if (hitForcefield) {
            addDecal = false;

            if (!weapon.IsMatter) {
                // Bounce energy weapons
                obj.Physics.Bounces++;
                obj.Parent = ObjID::None; // Make hostile to owner!

                Sound3D sound(hit.Point, hit.Tag.Segment);
                sound.Resource = Resources::GetSoundResource(SoundID::WeaponHitForcefield);
                Sound::Play(sound);
            }
        }

        if (ti.HasFlag(TextureFlag::Volatile)) {
            if (!isLargeExplosion) {
                // add volatile size and damage bonuses to smaller explosions
                vclip = VClipID::HitLava;
                constexpr float VOLATILE_DAMAGE = 10;
                constexpr float VOLATILE_FORCE = 5;

                damage = damage / 4 + VOLATILE_DAMAGE;
                splashRadius += VOLATILE_DAMAGE_RADIUS;
                force = force / 2 + VOLATILE_FORCE;
                impactSize += 1;
            }

            soundId = SoundID::HitLava;
            addDecal = false;
            hitLiquid = true;
        }
        else if (ti.HasFlag(TextureFlag::Water)) {
            if (weapon.IsMatter)
                soundId = SoundID::MissileHitWater;
            else
                soundId = SoundID::HitWater;

            if (isLargeExplosion) {
                // reduce strength of megas and shakers in water, but don't cancel them
                splashRadius *= 0.5f;
                damage *= 0.25f;
                force *= 0.5f;
                impactSize *= 0.5f;
            }
            else {
                vclip = VClipID::HitWater;
                splashRadius = 0; // Cancel explosions when hitting water
            }

            addDecal = false;
            hitLiquid = true;
        }

        if (addDecal) {
            auto decalSize = weapon.Extended.DecalRadius ? weapon.Extended.DecalRadius : weapon.ImpactSize / 3;

            Render::DecalInfo decal{};
            auto rotation = Matrix::CreateFromAxisAngle(hit.Normal, Random() * XM_2PI);
            decal.Normal = hit.Normal;
            decal.Tangent = Vector3::Transform(hit.Tangent, rotation);
            decal.Bitangent = decal.Tangent.Cross(hit.Normal);
            decal.Radius = decalSize;
            decal.Position = hit.Point;
            decal.Segment = hit.Tag.Segment;
            decal.Side = hit.Tag.Side;
            decal.Texture = weapon.Extended.Decal;

            // check that decal isn't too close to edge due to lack of clipping
            if (hit.EdgeDistance >= decalSize * 0.75f) {
                if (auto wall = Game::Level.TryGetWall(hit.Tag)) {
                    if (Game::Player.CanOpenDoor(*wall))
                        addDecal = false; // don't add decals to unlocked doors, as they will disappear on the next frame
                    else if (wall->Type != WallType::WallTrigger)
                        addDecal = wall->State == WallState::Closed; // Only allow decals on closed walls
                }

                if (addDecal)
                    Render::AddDecal(decal);
            }

            if (!weapon.Extended.ExplosionTexture.empty() && !obj.Physics.CanBounce()) {
                // Add the planar explosion effect
                decal.Texture = weapon.Extended.ExplosionTexture;
                decal.Radius = weapon.Extended.ExplosionSize;
                decal.Life = decal.FadeTime = weapon.Extended.ExplosionTime;
                decal.FadeRadius = decalSize * 2.4f;
                decal.Additive = true;
                decal.Color = Color{ 1.5f, 1.5f, 1.5f };

                if (splashRadius == 0) {
                    // Don't create light on the decal of explosive weapons, instead use their explosion
                    decal.LightColor = weapon.Extended.LightColor;
                    decal.LightRadius = weapon.Extended.LightRadius;
                }
                
                Render::AddDecal(decal);
                vclip = VClipID::None;
            }
        }

        if (HasFlag(obj.Physics.Flags, PhysicsFlag::Stick) && !hitLiquid && !hitForcefield) {
            // sticky flare behavior

            Vector3 vec;
            obj.Physics.Velocity.Normalize(vec);
            obj.Position += vec * hit.Distance;
            obj.Physics.Velocity = Vector3::Zero;
            //obj.Movement = MovementType::None;
            //obj.LastPosition = obj.Position;
            StuckObjects.Add(hit.Tag, objId);
            obj.Flags |= ObjectFlag::Attached;
            return;
        }

        if (obj.Physics.CanBounce() && !hitLiquid) {
            return; // don't create explosions when bouncing
        }

        obj.Flags |= ObjectFlag::Dead; // remove weapon after hitting a wall

        auto dir = obj.Physics.Velocity;
        dir.Normalize();

        if (soundId != SoundID::None) {
            auto soundRes = Resources::GetSoundResource(soundId);
            if (!hitLiquid)
                soundRes.D3 = weapon.Extended.ExplosionSound;

            Sound3D sound(hit.WallPoint, hit.Tag.Segment);
            sound.Resource = soundRes;
            sound.Source = obj.Parent;
            Sound::Play(sound);
        }

        if (vclip != VClipID::None) {
            Render::ExplosionInfo e;
            e.Radius = { impactSize * 0.9f, impactSize * 1.1f };
            e.Clip = vclip;
            e.Segment = hit.Tag.Segment;
            e.Parent = obj.Parent;

            // move explosions out of wall
            if (impactSize < 5)
                e.Position = hit.WallPoint - dir * impactSize * 0.5f;
            else
                e.Position = hit.WallPoint - dir * 2.5;

            e.Color = Color{ 1, 1, 1 };
            e.FadeTime = 0.1f;

            if (obj.ID == (int)WeaponID::Concussion) {
                e.Instances = 3;
                e.Delay = { 0, 0 };
            }

            Render::CreateExplosion(e);
        }

        if (splashRadius > 0) {
            GameExplosion ge{};
            ge.Segment = hit.Tag.Segment;
            ge.Position = hit.Point + hit.Normal * obj.Radius; // shift explosion out of wall
            ge.Damage = damage;
            ge.Force = force;
            ge.Radius = splashRadius;

            CreateExplosion(level, &obj, ge);
        }
    }

    // Updates the segment the object is in an activates triggers
    void UpdateObjectSegment(Level& level, ObjID objId) {
        auto& obj = level.Objects[(int)objId];
        auto prevSegId = obj.Segment;

        if (Editor::PointInSegment(level, obj.Segment, obj.Position))
            return; // already in the right segment

        if (obj.Segment == SegID::None)
            return; // Object was outside of world

        // fast moving objects can cross multiple segments in one update
        // in practice this tends to affect gauss the most
        bool foundTouchingSeg = false;

        // Check if the new position is in a touching segment
        auto& seg = level.GetSegment(obj.Segment);
        for (auto& cid : seg.Connections) {
            if (Editor::PointInSegment(level, cid, obj.Position)) {
                obj.Segment = cid;
                // update the segment object lists
                Seq::remove(seg.Objects, objId);
                auto& cseg = level.GetSegment(cid);
                cseg.Objects.push_back(objId);
                foundTouchingSeg = true;
                break;
            }
        }

        if (foundTouchingSeg) {
            // Activate any triggers on the side passed through
            if (obj.Segment != prevSegId && obj.Type == ObjectType::Player) {
                auto sideId = level.GetConnectedSide(obj.Segment, prevSegId);
                if (auto wall = level.TryGetWall({ prevSegId, sideId })) {
                    if (auto trigger = level.TryGetTrigger(wall->Trigger)) {
                        fmt::print("Activating fly through trigger {}:{}\n", obj.Segment, prevSegId);
                        ActivateTrigger(level, *trigger);
                    }
                }
            }
        }
        else {
            // object crossed multiple segments in a single update.
            // usually caused by fast moving projectiles, but can also happen if object is outside world.
            auto prevSeg = obj.Segment;
            Editor::UpdateObjectSegment(level, obj);
            if (obj.Type == ObjectType::Player && prevSeg != obj.Segment) {
                SPDLOG_WARN("Player {} warped from {} to segment {}. Any fly-through triggers did not activate!", objId, prevSeg, obj.Segment);
            }

            // Update object pointers
            Seq::remove(seg.Objects, objId);
            auto& cseg = level.GetSegment(obj.Segment);
            cseg.Objects.push_back(objId);
        }
    }

    void UpdatePhysics(Level& level, double /*t*/, float dt) {
        Debug::Steps = 0;
        Debug::ClosestPoints.clear();

        UpdateGame(level, dt);

        for (int id = 0; id < level.Objects.size(); id++) {
            auto& obj = level.Objects[id];
            if (!obj.IsAlive() && obj.Type != ObjectType::Reactor) continue;
            if (obj.Type == ObjectType::Player && obj.ID > 0) continue; // singleplayer only
            if (obj.Movement != MovementType::Physics) continue;

            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;

            FixedPhysics(obj, dt);

            if (obj.Physics.Wiggle > 0) {
                //auto offset = (float)obj.Signature * 0.8191f; // random offset to keep objects from wiggling at same time
                //WiggleObject(obj, t + offset, dt, obj.Physics.Wiggle, obj.Physics.WiggleRate);
            }

            obj.Physics.InputVelocity = obj.Physics.Velocity;
            obj.Position += obj.Physics.Velocity * dt;

            if (HasFlag(obj.Flags, ObjectFlag::Attached))
                continue; // don't test collision of attached objects

            auto delta = obj.Position - obj.LastPosition;
            auto maxDistance = delta.Length();
            LevelHit hit{ .Source = &obj };

            if (maxDistance < 0.001f) {
                // no travel, but need to check for being inside of wall (maybe this isn't necessary)
                BoundingSphere sphere(obj.Position, obj.Radius);

                if (IntersectLevel(level, sphere, obj.Segment, (ObjID)id, hit)) {
                    Debug::ClosestPoints.push_back(hit.Point);
                    Render::Debug::DrawLine(hit.Point, hit.Point + hit.Normal, { 1, 0, 0 });
                }
            }
            else {
                Vector3 dir;
                delta.Normalize(dir);

                if (obj.Radius < 0.1) {
                    Ray ray(obj.LastPosition, dir);
                    auto maxDist = (dir * obj.Physics.Velocity).Length();
                    if (IntersectLevel(level, ray, obj.Segment, maxDist, false, true, hit)) {
                        //Render::Debug::DrawPoint(hit.Point, { 1, 1, 0 });
                        Debug::ClosestPoints.push_back(hit.Point);
                        Render::Debug::DrawLine(hit.Point, hit.Point + hit.Normal, { 1, 0, 0 });
                    }
                }
                else {
                    BoundingCapsule capsule{ .A = obj.LastPosition, .B = obj.Position, .Radius = obj.Radius };

                    if (IntersectLevel(level, capsule, obj.Segment, obj, hit)) {
                        //Render::Debug::DrawPoint(hit.Point, { 1, 1, 0 });
                        Debug::ClosestPoints.push_back(hit.Point);
                        Render::Debug::DrawLine(hit.Point, hit.Point + hit.Normal, { 1, 0, 0 });
                    }
                }
            }

            if (hit) {
                if (obj.Type == ObjectType::Weapon) {
                    if (hit.HitObj)
                        WeaponHitObject(hit, obj, level);
                    else
                        WeaponHitWall(hit, obj, level, ObjID(id));
                }

                if (auto wall = level.TryGetWall(hit.Tag)) {
                    HitWall(level, hit.Point, obj, *wall);
                }

                if (obj.Type == ObjectType::Player && hit.HitObj) {
                    Game::Player.TouchObject(*hit.HitObj);
                }

                if (HasFlag(obj.Physics.Flags, PhysicsFlag::Bounce) || obj.Physics.Bounces > 0) {
                    obj.Physics.Velocity = Vector3::Reflect(obj.Physics.Velocity, hit.Normal);
                    if (obj.Type == ObjectType::Weapon)
                        obj.Rotation = Matrix3x3(obj.Physics.Velocity, obj.Rotation.Up());

                    obj.Physics.Bounces--;
                }

                //CollideTriangles(level, obj, dt, 0);
                //CollideTriangles(level, obj, dt, 1); // Doing two passes makes the result more stable
                //CollideTriangles(level, obj, dt, 2); // Doing two passes makes the result more stable

                //auto frameVec = obj.Position() - obj.PrevTransform.Translation();
                //obj.Movement.Physics.Velocity = frameVec / dt;
                obj.LastHitForce *= 0.80f; // decay every update

                // don't update the seg if weapon hit something, as this causes problems with weapon forcefield bounces
                if (obj.Type != ObjectType::Weapon) {
                    UpdateObjectSegment(level, (ObjID)id);
                }
            }
            else {
                UpdateObjectSegment(level, (ObjID)id);
            }

            //if (obj.LastPosition != obj.Position)
            //    Render::Debug::DrawLine(obj.LastPosition, obj.Position, { 0, 1.0f, 0.2f });

            if (id == 0) {
                Debug::ShipVelocity = obj.Physics.Velocity;
                Debug::ShipPosition = obj.Position;
            }
        }
    }
}
