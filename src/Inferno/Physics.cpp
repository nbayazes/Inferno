#include "pch.h"
#define NOMINMAX
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Game.Object.h"
#include "Graphics/Render.h"
#include "Input.h"
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
    Vector2 IntersectFaceUVs(const Vector3& point, const Face& face, int tri) {
        auto& indices = face.Side.GetRenderIndices();
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
    bool WallPointIsTransparent(const Vector3& pnt, const Face& face, int tri) {
        auto& side = face.Side;
        auto tmap = side.TMap2 > LevelTexID::Unset ? side.TMap2 : side.TMap;
        auto& bitmap = Resources::GetBitmap(Resources::LookupTexID(tmap));
        if (!bitmap.Info.Transparent) return false; // Must be flagged transparent

        auto uv = IntersectFaceUVs(pnt, face, tri);
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
    bool CheckDestroyableOverlay(Level& level, const Vector3& point, Tag tag, int tri, bool isPlayer) {
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
        if (!isPlayer) {
            if (auto wall = level.TryGetWall(tag)) {
                if (wall->Trigger != TriggerID::None)
                    return false;
            }
        }

        auto face = Face::FromSide(level, *seg, tag.Side);
        auto uv = IntersectFaceUVs(point, face, tri);

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
        if (auto e = Render::EffectLibrary.GetSparks("overlay_destroyed")) {
            e->Direction = side.AverageNormal;
            e->Up = side.Tangents[0];
            auto position = point + side.AverageNormal * 0.1f;
            Render::AddSparkEmitter(*e, tag.Segment, position);
        }

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

        if (IsZero(pd.AngularVelocity) && IsZero(pd.AngularThrust) && IsZero(pd.AngularAcceleration))
            return;

        auto pdDrag = pd.Drag > 0 ? pd.Drag : 1;
        const auto drag = pdDrag * 5 / 2;
        const auto stepScale = dt / Game::TICK_RATE;

        if (HasFlag(pd.Flags, PhysicsFlag::UseThrust) && pd.Mass > 0)
            pd.AngularVelocity += pd.AngularThrust / pd.Mass * stepScale; // acceleration

        if (!HasFlag(pd.Flags, PhysicsFlag::FixedAngVel)) {
            pd.AngularVelocity += pd.AngularAcceleration * dt;
            pd.AngularAcceleration *= 1 - drag * stepScale;
            pd.AngularVelocity *= 1 - drag * stepScale;
        }

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

    void LinearPhysics(Object& obj, float dt) {
        auto& pd = obj.Physics;
        const auto stepScale = dt / Game::TICK_RATE;

        if (pd.Velocity == Vector3::Zero && pd.Thrust == Vector3::Zero)
            return;

        if (pd.Drag > 0) {
            if (pd.Thrust != Vector3::Zero && pd.Mass > 0)
                pd.Velocity += pd.Thrust / pd.Mass * stepScale; // acceleration

            pd.Velocity *= 1 - pd.Drag * stepScale;
        }

        obj.Position += pd.Velocity * dt;

        if (obj.Physics.Wiggle > 0) {
            //auto offset = (float)obj.Signature * 0.8191f; // random offset to keep objects from wiggling at same time
            //WiggleObject(obj, t + offset, dt, obj.Physics.Wiggle, obj.Physics.WiggleRate);
        }
    }

    void PlotPhysics(double t, const PhysicsData& pd) {
        static int index = 0;
        static double refresh_time = 0.0;

        if (refresh_time == 0.0)
            refresh_time = t;

        if (Input::IsKeyDown(DirectX::Keyboard::Keys::Add)) {
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

    void PlayerPhysics(const Object& obj, float /*dt*/) {
        auto& physics = obj.Physics;

        if (obj.Type == ObjectType::Player) {
            //const auto& ship = Resources::GameData.PlayerShip;

            //physics.Thrust *= ship.MaxThrust / dt;
            //physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.Thrust;
            Debug::ShipAcceleration = Vector3::Zero;
        }
    }

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
        return u.Dot(v) >= 0.0f && u.Dot(w) >= 0.0f && v.Dot(w) >= 0.0f;
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
        float mag1 = FLT_MAX, mag2 = FLT_MAX, mag3 = FLT_MAX, mag4 = FLT_MAX;

        // todo: this isn't true for inverted segments
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

    // intersects a with b, with hit normal pointing towards a
    HitInfo IntersectSphereSphere(const BoundingSphere& a, const BoundingSphere& b) {
        HitInfo hit;
        Vector3 c0(a.Center), c1(b.Center);
        auto v = c0 - c1;
        auto distance = v.Length();
        if (distance < a.Radius + b.Radius) {
            v.Normalize();
            hit.Point = b.Center + v * b.Radius;
            hit.Distance = Vector3::Distance(hit.Point, c0);
            hit.Normal = v;
        }

        return hit;
    }

    // Intersects a sphere with a point. Surface normal points towards point.
    HitInfo IntersectPointSphere(const Vector3 point, const BoundingSphere& sphere) {
        HitInfo hit;
        auto dir = point - sphere.Center;
        float depth = sphere.Radius - dir.Length();
        if (depth > 0) {
            dir.Normalize();
            hit.Point = sphere.Center + dir * sphere.Radius;
            hit.Distance = Vector3::Distance(hit.Point, point);
            hit.Normal = -dir;
        }

        return hit;
    }

    // Returns the nearest intersection point on a face
    HitInfo IntersectFaceSphere(const Face& face, const BoundingSphere& sphere) {
        HitInfo hit;
        auto& i = face.Side.GetRenderIndices();

        if (sphere.Intersects(face[i[0]], face[i[1]], face[i[2]])) {
            auto p = ClosestPointOnTriangle(face[i[0]], face[i[1]], face[i[2]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < hit.Distance) {
                hit.Point = p;
                hit.Distance = dist;
                hit.Tri = 0;
            }
        }

        if (sphere.Intersects(face[i[3]], face[i[4]], face[i[5]])) {
            auto p = ClosestPointOnTriangle(face[i[3]], face[i[4]], face[i[5]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < hit.Distance) {
                hit.Point = p;
                hit.Distance = dist;
                hit.Tri = 1;
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

    Set<SegID> g_VisitedSegs; // global visited segments buffer
    Queue<SegID> g_VisitedStack;

    Set<SegID>& GetPotentialSegments(Level& level, SegID start, const Vector3& point, float radius) {
        g_VisitedSegs.clear();
        g_VisitedStack.push(start);
        int depth = 0; // Always add segments touching the start segment, otherwise overlapping objects might be missed

        while (!g_VisitedStack.empty()) {
            auto segId = g_VisitedStack.front();
            g_VisitedStack.pop();
            g_VisitedSegs.insert(segId);
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);

                Plane p(side.Center + side.AverageNormal * radius, side.AverageNormal);
                if (depth == 0 || p.DotCoordinate(point) <= 0) {
                    // Point was behind the plane or this was the starting segment
                    auto conn = seg.GetConnection(sideId);
                    if (conn != SegID::None && !g_VisitedSegs.contains(conn))
                        g_VisitedStack.push(conn);
                }
            }

            depth++;
            // todo: detail segments
        }

        return g_VisitedSegs;
    }

    bool ObjectCanHitTarget(const Object& src, const Object& target) {
        if (!target.IsAlive() && target.Type != ObjectType::Reactor) return false;
        //if (!HasFlag(target.Movement, MovementType::Physics)) return false;
        if (src.Signature == target.Signature) return false; // don't hit yourself!
        //if (src.Parent == target.Parent && src.Parent != ObjID::None) return false; // don't hit your siblings!

        //if ((src.Parent != ObjID::None && target.Parent != ObjID::None) && src.Parent == target.Parent)
        //    return false; // Don't hit your siblings!

        switch (src.Type) {
            case ObjectType::Robot:
                switch (target.Type) {
                    case ObjectType::Wall:
                    case ObjectType::Robot:
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
                        if (target.ID > 0) return false;          // Only hit player 0 in singleplayer
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

    // Finds the nearest sphere-level intersection for debris
    // Debris only collide with robots, players and walls
    bool IntersectLevelDebris(Level& level, const BoundingCapsule& capsule, SegID segId, LevelHit& hit) {
        auto& pvs = GetPotentialSegments(level, segId, capsule.A, capsule.Radius);
        auto dir = capsule.B - capsule.A;
        dir.Normalize();
        Ray ray(capsule.A, dir);

        // Did we hit any objects?
        for (auto& segment : pvs) {
            auto& seg = level.GetSegment(segment);

            for (int i = 0; i < seg.Objects.size(); i++) {
                auto other = level.TryGetObject(seg.Objects[i]);

                if (!other->IsAlive() || other->Segment != segment) continue;
                if (other->Type != ObjectType::Player && other->Type != ObjectType::Robot && other->Type != ObjectType::Reactor)
                    continue;

                BoundingSphere sphere(other->Position, other->Radius);
                float dist;
                if (ray.Intersects(sphere, dist) && dist < other->Radius) {
                    hit.Distance = dist;
                    hit.Normal = -dir;
                    hit.Point = capsule.A + dir * dist;
                    return true;
                }
            }
        }

        // todo: add debris level hit testing. need to prevent duplicating triangle hit testing
        //for (auto& side : SideIDs) {
        //    auto face = Face::FromSide(level, seg, side);
        //    auto& i = face.Side.GetRenderIndices();

        //    Vector3 refPoint, normal;
        //    float dist{};
        //    if (capsule.Intersects(face[i[0]], face[i[1]], face[i[2]], face.Side.Normals[0], refPoint, normal, dist)) {
        //        if (seg.SideIsSolid(side, level) && dist < hit.Distance) {
        //            hit.Normal = normal;
        //            hit.Point = refPoint;
        //            hit.Distance = dist;
        //            hit.Tag = { segId, side };
        //        }
        //        else {
        //            // scan touching seg
        //            auto conn = seg.GetConnection(side);
        //            if (conn > SegID::None && !visitedSegs.contains(conn))
        //                IntersectLevelDebris(level, capsule, conn, hit);
        //        }
        //    }

        //    if (capsule.Intersects(face[i[3]], face[i[4]], face[i[5]], face.Side.Normals[1], refPoint, normal, dist)) {
        //        if (seg.SideIsSolid(side, level)) {
        //            if (dist < hit.Distance) {
        //                hit.Normal = normal;
        //                hit.Point = refPoint;
        //                hit.Distance = dist;
        //                hit.Tag = { segId, side };
        //            }
        //        }
        //        else {
        //            // scan touching seg
        //            auto conn = seg.GetConnection(side);
        //            if (conn > SegID::None && !visitedSegs.contains(conn))
        //                IntersectLevelDebris(level, capsule, conn, hit);
        //        }
        //    }
        //}

        return hit;
    }

    // intersects a ray with the level, returning hit information
    bool IntersectLevel(Level& level, const Ray& ray, SegID start, float maxDist, bool passTransparent, bool hitTestTextures, LevelHit& hit) {
        if (start == SegID::None) return false;
        if (maxDist <= 0.01f) return false;
        SegID next = start;
        Set<SegID> visitedSegs;

        while (next > SegID::None) {
            SegID segId = next;
            visitedSegs.insert(segId); // must track visited segs to prevent circular logic
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
                            isSolid = !WallPointIsTransparent(intersect, face, tri);
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
                        if (!visitedSegs.contains(conn))
                            next = conn;
                        break; // go to next segment
                    }
                }
            }
        }

        return false;
    }

    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent) {
        auto dir = b.Position - a.Position;
        auto dist = dir.Length();
        dir.Normalize();
        Ray ray(a.Position, dir);
        LevelHit hit;
        return IntersectLevel(Game::Level, ray, a.Segment, dist, passTransparent, true, hit);
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
                    if (!Settings::Cheats.DisableWeaponDamage)
                        obj.ApplyDamage(damage);

                    obj.LastHitForce = forceVec;
                    fmt::print("applied {} splash damage at dist {}\n", damage, dist);

                    // stun robot if not boss

                    // Boss invuln stuff

                    // guidebot ouchies
                    // todo: turn object to face away from explosion
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
                    // todo: turn object to face away from explosion

                    break;
                }

                default:
                    throw Exception("Invalid object type in CreateExplosion()");
            }
        }
    }


    void IntersectBoundingBoxes(const Object& obj) {
        auto rotation = obj.Rotation;
        rotation.Forward(-rotation.Forward());
        auto orientation = Quaternion::CreateFromRotationMatrix(rotation);

        if (obj.Render.Type == RenderType::Model) {
            auto& model = Resources::GetModel(obj.Render.Model.ID);
            int smIndex = 0;
            for (auto& sm : model.Submodels) {
                auto offset = model.GetSubmodelOffset(smIndex++);
                auto transform = obj.GetTransform();
                transform.Translation(transform.Translation() + offset);
                transform = obj.Rotation * Matrix::CreateTranslation(obj.Position);

                auto bounds = sm.Bounds;
                bounds.Center.z *= -1;
                bounds.Center = Vector3::Transform(bounds.Center, transform);
                // todo: animation
                bounds.Orientation = orientation;
                Render::Debug::DrawBoundingBox(bounds, Color(0, 1, 0));
            }
        }
    }

    void CollideObjects(const LevelHit& hit, Object& a, Object& b, float /*dt*/) {
        if (hit.Speed <= 0.1f) return;

        SPDLOG_INFO("{}-{} impact speed: {}", a.Signature, b.Signature, hit.Speed);

        if (b.Type == ObjectType::Powerup || b.Type == ObjectType::Marker)
            return;

        if (a.Type != ObjectType::Weapon && b.Type != ObjectType::Weapon) { }

        //auto v1 = a.Physics.PrevVelocity.Dot(hit.Normal);
        //auto v2 = b.Physics.PrevVelocity.Dot(hit.Normal);
        //Vector3 v1{}, v2{};
        //v1 = v2 = hit.Normal * hit.Speed;

        // Player ramming a robot should impart less force than a weapon
        //float restitution = a.Type == ObjectType::Player ? 0.6f : 1.0f;

        // These equations are valid as long as one mass is not zero
        auto m1 = a.Physics.Mass == 0.0f ? 1.0f : a.Physics.Mass;
        auto m2 = b.Physics.Mass == 0.0f ? 1.0f : b.Physics.Mass;
        //auto newV1 = (m1 * v1 + m2 * v2 - m2 * (v1 - v2) * restitution) / (m1 + m2);
        //auto newV2 = (m1 * v1 + m2 * v2 - m1 * (v2 - v1) * restitution) / (m1 + m2);

        //auto bDeltaVel = hit.Normal * (newV2 - v2);
        //if (!HasFlag(a.Physics.Flags, PhysicsFlag::Piercing)) // piercing weapons shouldn't bounce
        //    a.Physics.Velocity += hit.Normal * (newV1 - v1);

        //if (b.Movement == MovementType::Physics)
        //    b.Physics.Velocity += hit.Normal * (newV2 - v2);

        //if (a.Type == ObjectType::Weapon && !HasFlag(a.Physics.Flags, PhysicsFlag::Bounce))
        //    a.Physics.Velocity = Vector3::Zero; // stop weapons when hitting an object

        //auto actualVel = (a.Position - a.LastPosition) / dt;

        constexpr float RESITUTION = 0.5f;

        auto force = -hit.Normal * hit.Speed * m1 / m2;
        b.Physics.Velocity += force * RESITUTION;

        a.LastHitForce = b.LastHitForce = force * RESITUTION;
        //a.Position += hit.Normal * 0.1f;
        //b.Position -= hit.Normal * hit.Speed * dt;


        // Only apply rotational velocity when something hits a robot. Feels bad if a player being hit loses aim.
        if (/*a.Type == ObjectType::Weapon &&*/ b.Type == ObjectType::Robot) {
            Matrix basis(b.Rotation);
            basis = basis.Invert();
            force = Vector3::Transform(force, basis); // transform forces to basis of object
            auto arm = Vector3::Transform(hit.Point - b.Position, basis);
            const auto torque = force.Cross(arm);
            const auto inertia = (2.0f / 5.0f) * m2 * b.Radius * b.Radius; // moment of inertia of a solid sphere I = 2/5 MR^2
            const auto accel = torque / inertia;
            b.Physics.AngularAcceleration += accel;

            //targetPhys.Velocity += hit.Normal * hit.Normal.Dot(force);
            //target.LastHitForce = force;

            //Matrix basis(target.Rotation);
            //basis = basis.Invert();
            //force = Vector3::Transform(force, basis); // transform forces to basis of object
            //const auto arm = Vector3::Transform(hit.Point - target.Position, basis);
            //const auto torque = force.Cross(arm);
            //const auto inertia = (2.0f / 5.0f) * targetMass * target.Radius * target.Radius;
            //const auto accel = torque / inertia;
            //targetPhys.AngularVelocity += accel; // should we multiply by dt here?
        }

        // todo: player hitting a robot should cause it to rotate away slightly
        // however, using the correct physics causes robots to spin erratically when sliding against them
    }

    // Returns the closest point and distance on a triangle to a point
    Tuple<Vector3, float> ClosestPointOnTriangle2(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& point, int* edgeIndex = nullptr) {
        Vector3 points[3] = {
            ClosestPointOnLine(p0, p1, point),
            ClosestPointOnLine(p1, p2, point),
            ClosestPointOnLine(p2, p0, point)
        };

        float distances[3]{};
        for (int j = 0; j < std::size(points); j++) {
            distances[j] = Vector3::Distance(point, points[j]);
        }

        int minIndex = 0;
        for (int j = 0; j < std::size(points); j++) {
            if (distances[j] < distances[minIndex])
                minIndex = j;
        }

        if (edgeIndex) *edgeIndex = minIndex;

        return { points[minIndex], distances[minIndex] };
    }

    // Performs polygon accurate intersection of an object and a model
    // Object is repositioned based on the intersections
    HitInfo IntersectMesh(Object& obj, Object& target, float dt) {
        if (target.Render.Type != RenderType::Model) return {};
        auto& model = Resources::GetModel(target.Render.Model.ID);

        float travelDist = obj.Physics.Velocity.Length() * dt;
        bool needsRaycast = travelDist > obj.Radius * 1.5f;

        if (!needsRaycast && Vector3::Distance(obj.Position, target.Position) > obj.Radius + target.Radius)
            return {}; // Objects too far apart

        Vector3 direction;
        obj.Physics.Velocity.Normalize(direction);

        // transform ray to model space of the target object
        auto transform = target.GetTransform();
        auto invTransform = transform.Invert();
        auto invRotation = Matrix(target.Rotation).Invert();
        auto localPos = Vector3::Transform(obj.Position, invTransform);
        auto localDir = Vector3::TransformNormal(direction, invRotation);
        localDir.Normalize();
        Ray ray = { localPos, localDir }; // update the input ray

        HitInfo hit;

        Vector3 averagePosition;
        Vector3 maxPosition;
        float maxCenterDist = 0;
        int hits = 0;

        int texNormalIndex = 0, flatNormalIndex = 0;

        for (int smIndex = 0; smIndex < model.Submodels.size(); smIndex++) {
            auto submodelOffset = model.GetSubmodelOffset(smIndex);
            auto& submodel = model.Submodels[smIndex];

            auto hitTestIndices = [&](span<const uint16> indices, span<const Vector3> normals, int& normalIndex) {
                for (int i = 0; i < indices.size(); i += 3) {
                    // todo: account for animation
                    Vector3 p0 = model.Vertices[indices[i + 0]] + submodelOffset;
                    Vector3 p1 = model.Vertices[indices[i + 1]] + submodelOffset;
                    Vector3 p2 = model.Vertices[indices[i + 2]] + submodelOffset;
                    p0.z *= -1; // flip z due to lh/rh differences
                    p1.z *= -1;
                    p2.z *= -1;
                    Vector3 normal = normals[normalIndex++];
                    //auto normal2 = -(p1 - p0).Cross(p2 - p0);
                    //normal2.Normalize();
                    //assert(normal == normal2);

                    bool triFacesObj = localDir.Dot(normal) <= 0;

                    if (needsRaycast) {
                        float dist;
                        if (triFacesObj && ray.Intersects(p0, p1, p2, dist) && dist < travelDist) {
                            // Move object to intersection of face then proceed as usual
                            localPos += localDir * (dist - obj.Radius);
                        }
                    }

                    auto offset = normal * obj.Radius; // offset triangle by radius to account for object size
                    Plane plane(p0 + offset, p1 + offset, p2 + offset);
                    auto planeDist = -plane.DotCoordinate(localPos); // flipped winding
                    if (planeDist > 0 || planeDist < -obj.Radius)
                        continue; // Object isn't close enough to the triangle plane

#ifdef DEBUG_OUTLINE
                    auto drawTriangleEdge = [&transform](const Vector3& a, const Vector3& b) {
                        auto dbgStart = Vector3::Transform(a, transform);
                        auto dbgEnd = Vector3::Transform(b, transform);
                        Render::Debug::DrawLine(dbgStart, dbgEnd, { 0, 1, 0 });
                    };

                    drawTriangleEdge(p0, p1);
                    drawTriangleEdge(p1, p2);
                    drawTriangleEdge(p2, p0);
                    {
                        auto center = (p0 + p1 + p2) / 3;
                        auto dbgStart = Vector3::Transform(center, transform);
                        auto dbgEnd = Vector3::Transform(center + normal, transform);
                        Render::Debug::DrawLine(dbgStart, dbgEnd, { 0, 1, 0 });
                    }
#endif

                    auto point = ProjectPointOntoPlane(localPos, plane);
                    float hitDistance = FLT_MAX;
                    Vector3 hitPoint, hitNormal = normal;

                    if (triFacesObj && PointInTriangle(p0 + offset, p1 + offset, p2 + offset, point)) {
                        // point was inside the triangle and behind the plane
                        hitPoint = point - offset;
                        hitNormal = normal;
                        hitDistance = planeDist;
                        //edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                    }
                    else {
                        // Point wasn't inside the triangle, check the edges
                        auto [triPoint, triDist] = ClosestPointOnTriangle2(p0, p1, p2, localPos);

                        if (triDist <= obj.Radius) {
                            auto edgeNormal = localPos - triPoint;
                            edgeNormal.Normalize(hitNormal);

                            if (ray.direction.Dot(edgeNormal) > 0)
                                continue; // velocity going away from edge

                            // Object hit a triangle edge
                            hitDistance = triDist;
                            hitPoint = triPoint;
                        }
                    }

                    if (hitDistance < obj.Radius) {
                        // Transform from local back to world space
                        hit.Point = Vector3::Transform(hitPoint, transform);
                        hit.Normal = Vector3::TransformNormal(hitNormal, target.Rotation);
                        hit.Distance = hitDistance;
                        //Debug::ClosestPoints.push_back(hitPoint);
                        //Render::Debug::DrawLine(hitPoint, hitPoint + hitNormal * 2, { 0, 1, 0 });

                        if (!HasFlag(obj.Physics.Flags, PhysicsFlag::Piercing)) {
                            auto wallPart = hit.Normal.Dot(obj.Physics.Velocity);
                            hit.Speed = std::max(std::abs(wallPart), hit.Speed);
                            obj.Physics.Velocity -= hit.Normal * wallPart; // slide along wall

                            if (obj.Type != ObjectType::Weapon && obj.Type != ObjectType::Reactor) {
                                auto pos = hit.Point + hit.Normal * obj.Radius;
                                auto centerDist = Vector3::Distance(pos, target.Position);
                                if (centerDist > maxCenterDist) {
                                    maxPosition = pos;
                                    //obj.Position = hit.Point + hit.Normal * obj.Radius;
                                }
                                averagePosition += pos;
                            }
                            // todo: averaging position works better, but causes object to get placed inside slightly. causing jitter during physics
                            // but not taking average allows player to phase through objects
                            // instead, take the position farthest from the object center?
                            hits++;
                        }
                    }
                }
            };

            hitTestIndices(submodel.Indices, model.Normals, texNormalIndex);
            hitTestIndices(submodel.FlatIndices, model.FlatNormals, flatNormalIndex);
        }

        if (hits > 0 && obj.Type != ObjectType::Weapon && obj.Type != ObjectType::Reactor) {
            // Don't move weapons or reactors
            // Move objects to the average position of all hits. This fixes jitter against more complex geometry and when nudging between walls.
            obj.Position = averagePosition / (float)hits;
            //obj.Position = maxPosition;
        }

        return hit;
    }

    constexpr float MIN_TRAVEL_DISTANCE = 0.001f; // Min distance an object must move to test collision

    bool IntersectLevelNew(Level& level, Object& obj, ObjID oid, LevelHit& hit, float dt) {
        Vector3 direction;
        float travelDistance = obj.Physics.Velocity.Length() * dt;
        // Don't hit test objects that haven't moved unless they are the player
        // This is so moving powerups are tested against the player
        //if (travelDistance <= MIN_TRAVEL_DISTANCE && obj.Type != ObjectType::Player) return false;
        obj.Physics.Velocity.Normalize(direction);
        Ray pathRay(obj.PrevPosition, direction);

        // Use a larger radius for the object so the large objects in adjacent segments are found.
        // Needs testing against boss robots
        auto& pvs = GetPotentialSegments(level, obj.Segment, obj.Position, obj.Radius * 2);

        // Did we hit any objects?
        for (auto& segId : pvs) {
            auto& seg = level.GetSegment(segId);

            for (int i = 0; i < seg.Objects.size(); i++) {
                if (oid == seg.Objects[i]) continue; // don't hit yourself!
                auto other = level.TryGetObject(seg.Objects[i]);
                if (!other) continue;
                if (oid == other->Parent) continue; // Don't hit your children!
                if (!ObjectCanHitTarget(obj, *other)) continue;

                // sphere collisions between all objects is stable
                // polygon collisions between all objects is mostly stable
                // polygon collisions between only player and robots isn't stable

                // todo: option to disable polygon accurate weapon hits?
                bool useMeshTests =
                    obj.Type == ObjectType::Weapon || 
                    other->Type == ObjectType::Reactor ||
                    other->Type == ObjectType::Robot;
                    //(obj.Type == ObjectType::Player && other->Type == ObjectType::Robot) ||
                    //(obj.Type == ObjectType::Robot && other->Type == ObjectType::Player);

                //useMeshTests = false;

                if (useMeshTests && other->Render.Type == RenderType::Model && IsNormalized(pathRay.direction)) {
                    // sphere-poly -> a is moved when touching b
                    // poly-sphere -> a is moved when touching b (using a's mesh)
                    if (auto info = IntersectMesh(obj, *other, dt)) {
                        hit.Update(info, other);
                        CollideObjects(hit, obj, *other, dt);
                    }
                }
                else {
                    BoundingSphere sphereA(obj.Position, obj.Radius);
                    BoundingSphere sphereB(other->Position, other->Radius);

                    if (auto info = IntersectSphereSphere(sphereA, sphereB)) {
                        if (other->Type == ObjectType::Robot || other->Type == ObjectType::Reactor) {
                            // todo: unify this math with intersect mesh and level hits
                            auto hitSpeed = info.Normal.Dot(obj.Physics.Velocity);
                            info.Speed = std::abs(hitSpeed);
                            obj.Position = info.Point + info.Normal * obj.Radius;
                            obj.Physics.Velocity -= info.Normal * hitSpeed;
                        }

                        hit.Update(info, other);
                        CollideObjects(hit, obj, *other, dt);
                    }
                }
            }
        }

        Vector3 averagePosition;
        int hits = 0;

        for (auto& segId : pvs) {
            Debug::SegmentsChecked++;
            auto& seg = level.Segments[(int)segId];

            for (auto& sideId : SideIDs) {
                if (!seg.SideIsSolid(sideId, level)) continue;
                auto& side = seg.GetSide(sideId);
                auto face = Face::FromSide(level, seg, sideId);
                auto& indices = side.GetRenderIndices();
                float edgeDistance = 0; // 0 for edge tests

                // Check the position against each triangle
                for (int tri = 0; tri < 2; tri++) {
                    Vector3 tangent = face.Side.Tangents[tri];
                    // Offset the triangle by the object radius and then do a point-triangle intersection.
                    // This leaves space at the edges to do capsule intersection checks.
                    const auto offset = side.Normals[tri] * obj.Radius;
                    const Vector3 p0 = face[indices[tri * 3 + 0]];
                    const Vector3 p1 = face[indices[tri * 3 + 1]];
                    const Vector3 p2 = face[indices[tri * 3 + 2]];

                    bool triFacesObj = pathRay.direction.Dot(side.Normals[tri]) <= 0;
                    float hitDistance = FLT_MAX;
                    Vector3 hitPoint, hitNormal;

                    // a size 4 object would need a velocity > 250 to clip through walls
                    if (obj.Type == ObjectType::Weapon) {
                        // Use raycasting for weapons because they are typically small and have high velocities
                        float dist;
                        if (triFacesObj &&
                            pathRay.Intersects(p0, p1, p2, dist) &&
                            dist < travelDistance) {
                            // move the object to the surface and proceed as normal
                            hitPoint = obj.PrevPosition + pathRay.direction * dist;
                            if (WallPointIsTransparent(hitPoint, face, tri))
                                continue; // skip projectiles that hit transparent part of a wall

                            //obj.Position = hitPoint - pathRay.direction * obj.Radius;
                            averagePosition += hitPoint - pathRay.direction * obj.Radius;
                            hits++;
                            hitNormal = side.Normals[tri];
                            hitDistance = dist;
                            edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                        }
                    }
                    else {
                        // Use point-triangle intersections for everything else.
                        // Note that fast moving objects could clip through walls!

                        Plane plane(p0 + offset, p1 + offset, p2 + offset);
                        auto planeDist = plane.DotCoordinate(obj.Position);
                        if (planeDist >= 0 || planeDist < -obj.Radius)
                            continue; // Object isn't close enough to the triangle plane

                        auto point = ProjectPointOntoPlane(obj.Position, plane);

                        if (triFacesObj && PointInTriangle(p0 + offset, p1 + offset, p2 + offset, point)) {
                            // point was inside the triangle and behind the plane
                            hitPoint = point - offset;
                            hitNormal = side.Normals[tri];
                            hitDistance = planeDist;
                            edgeDistance = FaceEdgeDistance(seg, sideId, face, hitPoint);
                        }
                        else {
                            // Point wasn't inside the triangle, check the edges
                            int edgeIndex;
                            auto [triPoint, triDist] = ClosestPointOnTriangle2(p0, p1, p2, obj.Position, &edgeIndex);

                            if (triDist <= obj.Radius) {
                                auto normal = obj.Position - triPoint;
                                normal.Normalize(hitNormal);

                                if (pathRay.direction.Dot(hitNormal) > 0)
                                    continue; // velocity going away from surface

                                // Object hit a triangle edge
                                hitDistance = triDist;
                                hitPoint = triPoint;

                                Vector3 tanVec;
                                if (edgeIndex == 0)
                                    tanVec = p1 - p0;
                                else if (edgeIndex == 1)
                                    tanVec = p2 - p1;
                                else
                                    tanVec = p0 - p2;

                                tanVec.Normalize(tangent);
                            }
                        }
                    }

                    float hitSpeed = 0;

                    if (hitDistance < obj.Radius) {
                        // Check if hit is transparent (duplicate check due to triangle edges)
                        if (obj.Type == ObjectType::Weapon && WallPointIsTransparent(hitPoint, face, tri))
                            continue; // skip projectiles that hit transparent part of a wall

                        // Object hit a wall, apply physics
                        hitSpeed = hitNormal.Dot(obj.Physics.Velocity);

                        //if (obj.Physics.CanBounce()) {
                        //    wallPart *= 2; // Subtract wall part twice to achieve bounce
                        //    pathRay.direction = Vector3::Reflect(pathRay.direction, hitNormal);
                        //    pathRay.position = obj.Position;

                        //    //obj.Physics.Velocity = Vector3::Reflect(obj.Physics.Velocity, hit.Normal);
                        //    if (obj.Type == ObjectType::Weapon)
                        //        obj.Rotation = Matrix3x3(obj.Physics.Velocity, obj.Rotation.Up());

                        //    // subtracting number of bounces here makes sense, in case multiple hits occur in a single tick.
                        //    // however then bounces needs to be 1 higher than stated in the config
                        //    // so that bounce effects work correctly in WeaponHitWall()
                        //    //obj.Physics.Bounces--;
                        //}

                        if (!HasFlag(obj.Physics.Flags, PhysicsFlag::Piercing)) {
                            obj.Physics.Velocity -= hitNormal * hitSpeed; // slide along wall (or bounce)
                            averagePosition += hitPoint + hitNormal * obj.Radius;
                            hits++;
                        }

                        // apply friction so robots pinned against the wall don't spin in place
                        if (obj.Type == ObjectType::Robot) {
                            obj.Physics.AngularAcceleration *= 0.5f;
                            //obj.Physics.Velocity *= 0.125f;
                        }
                        //Debug::ClosestPoints.push_back(hitPoint);
                        //Render::Debug::DrawLine(hitPoint, hitPoint + hitNormal, { 1, 0, 0 });
                    }

                    if (hitDistance < hit.Distance) {
                        // Store the closest overall hit as the final hit
                        hit.Distance = hitDistance;
                        hit.Normal = hitNormal;
                        hit.Point = hitPoint;
                        hit.Tag = { (SegID)segId, sideId };
                        hit.Tangent = tangent;
                        hit.EdgeDistance = edgeDistance;
                        hit.Tri = tri;
                        hit.WallPoint = hitPoint;
                        hit.Speed = abs(hitSpeed);
                    }
                }
            }
        }

        if (hits > 0) obj.Position = averagePosition / (float)hits;

        return hit;
    }

    void UpdatePhysics(Level& level, double /*t*/, float dt) {
        Debug::Steps = 0;
        Debug::ClosestPoints.clear();
        Debug::SegmentsChecked = 0;

        // At least two steps are necessary to prevent jitter in sharp corners (including against objects)
        constexpr int STEPS = 2;
        dt /= STEPS;

        for (int id = 0; id < level.Objects.size(); id++) {
            auto& obj = level.Objects[id];
            if (!obj.IsAlive() && obj.Type != ObjectType::Reactor) continue;
            if (obj.Type == ObjectType::Player && obj.ID > 0) continue; // singleplayer only
            if (obj.Movement != MovementType::Physics) continue;

            for (int i = 0; i < STEPS; i++) {
                obj.PrevPosition = obj.Position;
                obj.PrevRotation = obj.Rotation;
                obj.Physics.PrevVelocity = obj.Physics.Velocity;

                PlayerPhysics(obj, dt);
                AngularPhysics(obj, dt);
                LinearPhysics(obj, dt);

                if (HasFlag(obj.Flags, ObjectFlag::Attached))
                    continue; // don't test collision of attached objects

                //if (id != 0) continue; // player only testing
                LevelHit hit{ .Source = &obj };

                if (IntersectLevelNew(level, obj, (ObjID)id, hit, dt)) {
                    if (obj.Type == ObjectType::Weapon) {
                        if (hit.HitObj) {
                            Game::WeaponHitObject(hit, obj, level);
                        }
                        else {
                            Game::WeaponHitWall(hit, obj, level, ObjID(id));
                        }
                    }

                    if (auto wall = level.TryGetWall(hit.Tag)) {
                        HitWall(level, hit.Point, obj, *wall);
                    }

                    if (obj.Type == ObjectType::Player && hit.HitObj) {
                        Game::Player.TouchObject(*hit.HitObj);
                    }

                    if (obj.Physics.CanBounce()) {
                        // this doesn't work because the object velocity is already modified
                        obj.Physics.Velocity = Vector3::Reflect(obj.Physics.PrevVelocity, hit.Normal);
                        if (obj.Type == ObjectType::Weapon)
                            obj.Rotation = Matrix3x3(obj.Physics.Velocity, obj.Rotation.Up());

                        obj.Physics.Bounces--;
                    }

                    // don't update the seg if weapon hit something, as this causes problems with weapon forcefield bounces
                    /*        if (obj.Type != ObjectType::Weapon) {
                                MoveObject(level, (ObjID)id);
                            }*/

                    // Play a wall hit sound if the object hits something head-on
                    if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Robot) {
                        //vm_vec_sub(&moved_v, &obj->pos, &save_pos);
                        //wall_part = vm_vec_dot(&moved_v, &hit_info.hit_wallnorm);

                        auto deltaVel = (obj.Physics.Velocity - obj.Physics.PrevVelocity).Length();
                        //auto deltaVel = obj.Physics.Velocity - obj.Physics.LastVelocity;
                        //auto actualVel = (obj.Position - obj.LastPosition) / dt;
                        //auto velDotNorm = deltaVel.Dot(hit.Normal);

                        // sudden change in velocity means we hit something
                        if (deltaVel > 35) {
                            Sound3D sound(hit.Point, hit.Tag.Segment);
                            sound.Resource = Resources::GetSoundResource(SoundID::PlayerHitWall);
                            Sound::Play(sound);
                        }
                    }
                }
            }

            if (obj.Physics.Velocity.Length() * dt > MIN_TRAVEL_DISTANCE)
                MoveObject(level, (ObjID)id); // todo: refer to move object above w/ forcefields

            //if (obj.LastPosition != obj.Position)
            //    Render::Debug::DrawLine(obj.LastPosition, obj.Position, { 0, 1.0f, 0.2f });

            if (id == 0) {
                Debug::ShipVelocity = obj.Physics.Velocity;
                Debug::ShipPosition = obj.Position;
                PlotPhysics(Clock.GetTotalTimeSeconds(), obj.Physics);
            }
        }
    }
}
