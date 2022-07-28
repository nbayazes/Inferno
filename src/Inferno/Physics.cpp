#include "pch.h"
#include <iostream>
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "Input.h"
#include "Editor/Editor.Object.h"
#include "Graphics/Render.Debug.h"
#include "SoundSystem.h"
#include "Editor/Events.h"

using namespace DirectX;

namespace Inferno {
    constexpr auto PlayerTurnRollScale = FixToFloat(0x4ec4 / 2) * XM_2PI;
    constexpr auto PlayerTurnRollRate = FixToFloat(0x2000) * XM_2PI;

    // Rolls the object when turning
    void TurnRoll(Object& obj, float rollScale, float rollRate, float dt) {
        auto& pd = obj.Movement.Physics;
        const auto desiredBank = pd.AngularVelocity.y * rollScale;

        if (std::abs(pd.TurnRoll - desiredBank) > 0.001f) {
            auto roll = rollRate * dt;
            const auto theta = desiredBank - pd.TurnRoll;

            if (std::abs(theta) < roll) {
                roll = theta;
            }
            else {
                if (theta < 0)
                    roll = -roll;
            }

            pd.TurnRoll += roll;
        }

        //Debug::R = pd.TurnRoll;
    }

    // Applies angular physics to the player
    void AngularPhysics(Object& obj, float dt) {
        auto& pd = obj.Movement.Physics;

        if (pd.AngularVelocity == Vector3::Zero && pd.AngularThrust == Vector3::Zero)
            return;

        if (pd.Drag > 0) {
            const auto drag = pd.Drag * 5 / 2;

            if (pd.HasFlag(PhysicsFlag::UseThrust) && pd.Mass > 0)
                pd.AngularVelocity += pd.AngularThrust / pd.Mass; // acceleration

            if (!pd.HasFlag(PhysicsFlag::FreeSpinning))
                pd.AngularVelocity *= 1 - drag;
        }

        if (pd.TurnRoll) // unrotate object for bank caused by turn
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(pd.TurnRoll) * obj.Rotation);

        obj.Rotation = Matrix3x3(Matrix::CreateFromYawPitchRoll(-pd.AngularVelocity * dt * XM_2PI) * obj.Rotation);

        if (pd.HasFlag(PhysicsFlag::TurnRoll))
            TurnRoll(obj, PlayerTurnRollScale, PlayerTurnRollRate, dt);

        if (pd.TurnRoll) // re-rotate object for bank caused by turn
            obj.Rotation = Matrix3x3(Matrix::CreateRotationZ(-pd.TurnRoll) * obj.Rotation);
    }

    void LinearPhysics(Object& obj) {
        auto& pd = obj.Movement.Physics;

        if (pd.Velocity == Vector3::Zero && pd.Thrust == Vector3::Zero)
            return;

        if (pd.Drag > 0) {
            if (pd.HasFlag(PhysicsFlag::UseThrust) && pd.Mass > 0)
                pd.Velocity += pd.Thrust / pd.Mass; // acceleration

            pd.Velocity *= 1 - pd.Drag;
        }
    }

    // This should be done elsewhere but is useful for testing
    // todo: keyboard ramping
    void HandleInput(Object& obj, float dt) {
        auto& physics = obj.Movement.Physics;
        using Keys = DirectX::Keyboard::Keys;

        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;

        if (Input::IsKeyDown(Keys::Add))
            physics.Thrust += obj.Rotation.Forward() * dt;

        if (Input::IsKeyDown(Keys::Subtract))
            physics.Thrust += obj.Rotation.Backward() * dt;

        // yaw
        if (Input::IsKeyDown(Keys::NumPad4))
            physics.AngularThrust.y = -dt;
        if (Input::IsKeyDown(Keys::NumPad6))
            physics.AngularThrust.y = dt;

        // pitch
        if (Input::IsKeyDown(Keys::NumPad5))
            physics.AngularThrust.x = -dt;
        if (Input::IsKeyDown(Keys::NumPad8))
            physics.AngularThrust.x = dt;


        // roll
        if (Input::IsKeyDown(Keys::NumPad7))
            physics.AngularThrust.z = -dt;
        if (Input::IsKeyDown(Keys::NumPad9))
            physics.AngularThrust.z = dt;


        if (Input::IsKeyDown(Keys::NumPad1))
            physics.Thrust += obj.Rotation.Left() * dt;

        if (Input::IsKeyDown(Keys::NumPad3))
            physics.Thrust += obj.Rotation.Right() * dt;
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
    void WiggleObject(Object& obj, double t, float dt, float amplitude) {
        auto angle = std::sinf((float)t * XM_2PI) * 20; // multiplier tweaked to cause 0.5 units of movement at a 1/64 tick rate
        auto wiggle = obj.Rotation.Up() * angle * amplitude * dt;
        obj.Movement.Physics.Velocity += wiggle;
    }

    void FixedPhysics(Object& obj, float dt) {
        auto& physics = obj.Movement.Physics;

        if (obj.Type == ObjectType::Player) {
            const auto& ship = Resources::GameData.PlayerShip;

            physics.Thrust *= ship.MaxThrust / dt;
            physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.AngularThrust;
            Debug::ShipAcceleration = Vector3::Zero;

        }

        AngularPhysics(obj, dt);
        LinearPhysics(obj);
    }

    struct Triangle {
        Array<Vector3, 3> Points;
        Vector3& operator[] (int i) { return Points[i]; }
        const Vector3& operator[] (int i) const { return Points[i]; }

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

    struct ClosestResult { float distSq, s, t; Vector3 c1, c2; };

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


    Vector3 GetTriangleNormal(const Vector3& a, const Vector3& b, const Vector3& c) {
        auto v1 = b - a;
        auto v2 = c - a;
        auto normal = v1.Cross(v2);
        normal.Normalize();
        return normal;
    }

    // Untested
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
    bool IntersectFaceSphere(const Face& face, const BoundingSphere& sphere, Vector3& point, float& distance) {
        auto i = face.Side.GetRenderIndices();

        distance = FLT_MAX;

        if (sphere.Intersects(face[i[0]], face[i[1]], face[i[2]])) {
            auto p = ClosestPointOnTriangle(face[i[0]], face[i[1]], face[i[2]], sphere.Center);
            auto vec = p - sphere.Center;
            auto dist = (p - sphere.Center).Length();
            if (dist < distance) {
                point = p;
                distance = dist;
            }
        }

        if (sphere.Intersects(face[i[3]], face[i[4]], face[i[5]])) {
            auto p = ClosestPointOnTriangle(face[i[3]], face[i[4]], face[i[5]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < distance) {
                point = p;
                distance = dist;
            }
        }

        return distance < sphere.Radius;
    }


    Tuple<Vector3, float> IntersectTriangleSphere(const Vector3& p0, const Vector3& p1, const Vector3& p2, const BoundingSphere& sphere) {
        if (sphere.Intersects(p0, p1, p2)) {
            auto p = ClosestPointOnTriangle(p0, p1, p2, sphere.Center);
            auto vec = p - sphere.Center;
            auto dist = (p - sphere.Center).Length();
            return { p, dist };
        }

        return { {}, FLT_MAX };
    }

    struct BoundingCapsule {
        Vector3 A, B;
        float Radius;

        bool Intersect(const BoundingSphere& sphere) const {
            // Compute (squared) distance between sphere center and capsule line segment
            auto p = ClosestPointOnLine(A, B, sphere.Center);
            float dist2 = Vector3::DistanceSquared(sphere.Center, p);
            // If (squared) distance smaller than (squared) sum of radii, they collide
            float r = Radius + sphere.Radius;
            return dist2 <= r * r;
        }

        bool Intersect(const BoundingCapsule& other) const {
            // Compute (squared) distance between the inner structures of the capsules
            auto p = ClosestPointBetweenLines(A, B, other.A, other.B);
            // If (squared) distance smaller than (squared) sum of radii, they collide
            float r = Radius + other.Radius;
            return p.distSq <= r * r;
        }

        bool Intersect(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& faceNormal, Vector3& refPoint, Vector3& normal, float& dist) const {
            if (p0 == p1 || p1 == p2 || p2 == p0) return false; // Degenerate check
            auto base = A;
            auto tip = B;
            // Compute capsule line endpoints A, B like before in capsule-capsule case:
            auto capsuleNormal = tip - base;
            capsuleNormal.Normalize();
            auto offset = capsuleNormal * Radius; // line end offset
            auto a = base + offset; // base
            auto b = tip - offset; // tip

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
            BoundingSphere sphere(center, Radius);

            auto [point, idist] = IntersectTriangleSphere(p0, p1, p2, sphere);
            refPoint = point;

            normal = center - point;
            normal.Normalize();
            dist = idist;
            return idist < Radius;

            //float t;
            //if(!r.Intersects(p, t))
            //    return false;

            //float t = faceNormal.Dot((p0 - base) / std::abs(capsuleNormal.Dot(faceNormal)));
            //auto linePlaneIntersect = base + capsuleNormal * t;


            //Render::Debug::DrawLine(b, linePlaneIntersect, { 1, 1, 1 });

            ///*Vector3*/ refPoint = ClosestPointOnTriangle(p0, p1, p2, linePlaneIntersect);

            //auto c0 = (linePlaneIntersect - p0).Cross(p1 - p0);
            //auto c1 = (linePlaneIntersect - p1).Cross(p2 - p1);
            //auto c2 = (linePlaneIntersect - p2).Cross(p0 - p2);
            //bool inside = c0.Dot(faceNormal) <= 0 && c1.Dot(faceNormal) <= 0 && c2.Dot(faceNormal) <= 0;



            //if (inside) {
            //    Render::Debug::DrawPoint(linePlaneIntersect, { 1, 0, 0 });
            //    refPoint = linePlaneIntersect;
            //}
            //else {
            //    // Edge 1:
            //    auto point1 = ClosestPointOnLine(p0, p1, linePlaneIntersect);
            //    auto v1 = linePlaneIntersect - point1;
            //    auto distsq = v1.Dot(v1);
            //    auto bestDist = distsq;
            //    refPoint = point1;

            //    // Edge 2:
            //    auto point2 = ClosestPointOnLine(p1, p2, linePlaneIntersect);
            //    auto v2 = linePlaneIntersect - point2;
            //    distsq = v2.Dot(v2);
            //    if (distsq < bestDist) {
            //        refPoint = point2;
            //        bestDist = distsq;
            //    }

            //    // Edge 3:
            //    auto point3 = ClosestPointOnLine(p2, p0, linePlaneIntersect);
            //    auto v3 = linePlaneIntersect - point3;
            //    distsq = v3.Dot(v3);
            //    if (distsq < bestDist) {
            //        refPoint = point3;
            //        bestDist = distsq;
            //    }
            //}

            // The center of the best sphere candidate:
            /*Vector3*/
            //Render::Debug::DrawPoint(refPoint, { 1, 1, 0 });

            // Determine whether point is inside all triangle edges:
            //bool inside = PointInTriangle(p0, p1, p2, linePlaneIntersect);


        }
    };

    // Finds the nearest sphere-level intersection
    bool IntersectLevel(Level& level, const BoundingSphere& sphere, SegID segId, LevelHit& hit) {
        auto& seg = level.GetSegment(segId);
        hit.Visited.insert(segId);

        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, segId, side);
            Vector3 point;
            float dist{};
            IntersectFaceSphere(face, sphere, point, dist);

            auto normal = point - sphere.Center;
            normal.Normalize();
            if (normal.Dot(face.AverageNormal()) > 0)
                continue; // passed through back of face

            if (dist < FLT_MAX) {
                if (seg.SideIsSolid(side, level) && dist < hit.Distance) {
                    // hit a solid wall
                    hit.Distance = dist;
                    hit.Point = point;
                    hit.Tag = { segId, side };
                    auto hitNormal = sphere.Center - point;
                    hitNormal.Normalize();
                    hit.Normal = hitNormal;
                }
                else {
                    // intersected with a connected side, must check faces in it too
                    auto conn = seg.GetConnection(side);
                    if (conn > SegID::None && !hit.Visited.contains(conn))
                        IntersectLevel(level, sphere, conn, hit); // Recursive
                }
            }
        }

        return hit;
    }

    // intersects a ray with the level, returning hit information
    bool IntersectLevel(Level& level, const Ray& ray, SegID start, float maxDist, LevelHit& hit) {
        SegID segId = start;

        while (segId != SegID::None) {
            auto& seg = level.GetSegment(segId);

            //// Did we hit any objects in this segment?
            //for (auto& obj : level.Objects) {
            //    if (obj.Segment != segId) continue;

            //    BoundingSphere sphere(obj.Position, obj.Radius);
            //    float dist{};
            //    if (ray.Intersects(sphere, dist) && dist < hit.Distance) {
            //        // object was closer than the previous one
            //        hit.Obj = &obj;
            //        hit.Distance = dist;
            //        auto normal = (ray.direction * dist) - sphere.Center;
            //        normal.Normalize();
            //        hit.Normal = normal;
            //    }
            //}

            if (hit) return hit; // Objects will always be inside of a segment, no need to check walls if we hit something

            for (auto& side : SideIDs) {
                auto face = Face::FromSide(level, seg, side);

                float dist{};
                if (face.Intersects(ray, dist) && dist < hit.Distance) {
                    if (dist > maxDist) return {}; // hit is too far

                    if (seg.SideIsSolid(side, level)) { // todo: this isn't accurate due to door flags
                        hit.Tag = { segId, side };
                        hit.Distance = dist;
                        hit.Normal = {}; // todo: normal
                        return true;
                    }
                    else {
                        segId = seg.GetConnection(side);
                        break; // go to next segment
                    }
                }
            }

            // if the first pass doesn't hit anything it means the ray didn't start inside the seg
            if (segId == start) return false;
        }

        return false;
    }

    // Intersects a capsule with the level
    bool IntersectLevel(Level& level, const BoundingCapsule& capsule, SegID segId, LevelHit& hit) {
        auto& seg = level.GetSegment(segId);
        hit.Visited.insert(segId);

        //// Did we hit any objects in this segment?
        //for (auto& obj : level.Objects) {
        //    if (obj.Segment != segId) continue;

        //    BoundingSphere sphere(obj.Position, obj.Radius);
        //    float dist{};
        //    if (ray.Intersects(sphere, dist) && dist < hit.Distance) {
        //        // object was closer than the previous one
        //        hit.Obj = &obj;
        //        hit.Distance = dist;
        //        auto normal = (ray.direction * dist) - sphere.Center;
        //        normal.Normalize();
        //        hit.Normal = normal;
        //    }
        //}

        //if (hit) return hit; // Objects will always be inside of a segment, no need to check walls if we hit something

        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, seg, side);
            auto i = face.Side.GetRenderIndices();

            Vector3 refPoint, normal;
            float dist{};
            if (capsule.Intersect(face[i[0]], face[i[1]], face[i[2]], face.Side.Normals[0], refPoint, normal, dist)) {
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
                        IntersectLevel(level, capsule, conn, hit);
                }
            }

            if (capsule.Intersect(face[i[3]], face[i[4]], face[i[5]], face.Side.Normals[1], refPoint, normal, dist)) {
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
                        IntersectLevel(level, capsule, conn, hit);
                }
            }
        }

        return hit;
    }

    //void ProjectPath(Level& level, Object& obj, SegID segId, float dt, int pass) {
    //    // determine the type of intersection
    //    // what side was hit? was the side a wall? should a door open?
    //    // does it pass through a transparent wall?

    //    // check traversal
    //    // - project forward, if passes through open side, also check 
    //    auto& pd = obj.Movement.Physics;

    //    auto delta = obj.Position - obj.LastPosition;
    //    auto travel = delta.Length();
    //    if (travel < 0.001f) return;
    //    Vector3 dir;
    //    delta.Normalize(dir);

    //    Ray ray(obj.LastPosition, dir);

    //    // intersect every side in the segment and test if it is in range

    //    // This only casts a single ray, for larger objects this will not be sufficient
    //    Levelhit 
    //    auto hit = IntersectLevel(level, ray, segId, travel);
    //    if (hit.Tag) {
    //        // hit a segment side
    //        Debug::ClosestPoints.push_back(obj.LastPosition + ray.direction * hit.Distance);
    //    }
    //    else if (hit.Obj) {
    //        Debug::ClosestPoints.push_back(obj.LastPosition + ray.direction * hit.Distance);

    //        // hit an object

    //        // most high-speed objects are projectiles with very small radii
    //        // a single ray cast is sufficient
    //    }
    //}

    void Intersect(Level& level, SegID segId, const Triangle& t, Object& obj, float dt, int pass) {
        //if (obj.Type == ObjectType::Player) return;

        Plane plane(t.Points[0], t.Points[1], t.Points[2]);
        auto& pd = obj.Movement.Physics;

        if (pd.Velocity.Dot(plane.Normal()) > 0) return; // ignore faces pointing away from velocity
        auto delta = obj.Position - obj.LastPosition;
        auto expectedDistance = delta.Length();
        if (expectedDistance < 0.001f) return;
        Vector3 dir;
        delta.Normalize(dir);
        //auto expectedTravel = (obj.Position() - obj.PrevPosition()).Length();

        float hitDistance{};
        Ray ray(obj.LastPosition, dir);
        //bool hit = false;

        //if (ray.Intersects(t.Points[0], t.Points[1], t.Points[2], hitDistance)) {
        //    hit = hitDistance < expectedDistance - obj.Radius;
        //    //if (hit && hitDistance < expectedDistance.Length() + obj.Radius) // did the object pass all the way through the wall in one frame?
        //    if (hit)
        //        obj.Position = obj.LastPosition + dir * (hitDistance - obj.Radius);
        //}

        bool isHit = false;

        LevelHit hit;
        IntersectLevel(level, ray, segId, expectedDistance, hit);
        if (hit.Obj) {
            // hit an object
            obj.Position = obj.LastPosition + dir * (hit.Distance - obj.Radius);
            hitDistance = hit.Distance;
            isHit = true;
        }
        else if (hit.Tag) {
            // hit a wall
            obj.Position = obj.LastPosition + dir * (hit.Distance - obj.Radius);
            hitDistance = hit.Distance;
            isHit = true;
        }
        else {
            // ray cast didn't hit anything, try the sphere test
            // note that this is not a sweep and will miss points between the begin and end.
            // Fortunately, most fast-moving objects are projectiles and have small radii.
            BoundingSphere sphere(obj.Position, obj.Radius);
            isHit = sphere.Intersects(t.Points[0], t.Points[1], t.Points[2]);

            float planeDist;
            ray.Intersects(plane, planeDist);
            if (!isHit && planeDist <= expectedDistance) {
                // Last, test if the object sphere collides with the intersection of the triangle's plane
                sphere = BoundingSphere(obj.LastPosition + dir * planeDist, obj.Radius * 1.1f);
                //BoundingSphere sphere(obj.Position(), obj.Radius);
                isHit = sphere.Intersects(t.Points[0], t.Points[1], t.Points[2]);
            }
        }

        if (!isHit) return;

        bool tryAgain = false;

        auto closestPoint = ClosestPointOnTriangle(t[0], t[1], t[2], obj.Position);
        Debug::ClosestPoints.push_back(closestPoint);
        auto closestNormal = obj.Position - closestPoint;
        closestNormal.Normalize();

        // Adjust velocity
        if (pd.HasFlag(PhysicsFlag::Stick)) {

        }
        else {
            // We're constrained by wall, so subtract wall part from velocity
            auto wallPart = closestNormal.Dot(pd.Velocity);

            if (pd.HasFlag(PhysicsFlag::Bounce))
                wallPart *= 2; //Subtract out wall part twice to achieve bounce

            pd.Velocity -= closestNormal * wallPart;
            tryAgain = true;

            //pd.Velocity = Vector3::Reflect(pd.Velocity, plane.Normal()) / pd.Mass;
        }

        // Check if the wall is penetrating the object, and if it is apply some extra force to get it out
        if (pass > 0 && !pd.HasFlag(PhysicsFlag::Bounce)) {
            auto depth = obj.Radius - (obj.Position - closestPoint).Length();
            if (depth > 0.075f) {
                auto strength = depth / 0.15f;
                pd.Velocity += closestNormal * pd.Velocity.Length() * strength;
                //SPDLOG_WARN("Object inside wall. depth: {} strength: {}", depth, strength);

                // Counter the input velocity
                pd.Velocity -= obj.Movement.Physics.InputVelocity * strength * dt;
            }
        }

        // Move the object to the surface of the triangle
        obj.Position = closestPoint + closestNormal * obj.Radius;
    }

    //class SegmentSearch {
    //    Set<Segment*> _results;
    //    Stack<Segment*> _stack;
    //public:
    //    const Set<Segment*>& GetNearby(Level& level, const Object& obj, float range) {
    //        auto root = level.TryGetSegment(obj.Segment);
    //        _stack.push(root);
    //        _results.clear();

    //        while (!_stack.empty()) {
    //            auto seg = _stack.top();
    //            _stack.pop();
    //            if (!seg) continue;

    //            _results.insert(seg);
    //            for (auto& side : SideIDs) {
    //                if (auto conn = level.TryGetSegment(seg->GetConnection(side))) {
    //                    if (_results.contains(conn)) continue;
    //                    if (seg != root && !SideVertsInRange(level, *seg, side, obj.Position, range)) continue;
    //                    _stack.push(conn);
    //                }
    //            }
    //        }

    //        return _results;
    //    }

    //private:
    //    bool SideVertsInRange(Level& level, const Segment& seg, SideID side, const Vector3 point, float range) {
    //        for (auto& i : seg.GetVertexIndices(side)) {
    //            if (Vector3::Distance(level.Vertices[i], point) < range)
    //                return true;
    //        }

    //        return false;
    //    }
    //} SegmentSearch;

    auto GoLess = [](int a, int b) -> bool {
        return a < b;
    };


    template<typename Order>
    struct foo {
        int val;
        bool operator<(const foo& other) {
            return Order(val, other.val);
        }
    };

    typedef foo<decltype(GoLess)> foo_t;

    struct ActiveDoor {
        WallID Front = WallID::None;
        WallID Back = WallID::None;
        //WallID Back;
        float Time = -1;
        int Parts = 0;
        static bool IsAlive(const ActiveDoor& d) { return d.Time >= 0; }
    };

    template<class TData, class TKey = int>
    class SlotMap {
        std::vector<TData> _data;
        std::function<bool(const TData&)> _aliveFn;

    public:
        SlotMap(std::function<bool(const TData&)> aliveFn) : _aliveFn(aliveFn) {}

        TData& Get(TKey key) {
            assert(InRange(key));
            return _data[(int64)key];
        }

        [[nodiscard]] TKey Add(TData&& data) {
            for (size_t i = 0; i < _data.size(); i++) {
                if (!_aliveFn(_data[i])) {
                    _data = data;
                    return (TKey)i;
                }
            }

            _data.push_back(data);
            return TKey(_data.size() - 1);
        }

        [[nodiscard]] TData& Alloc() {
            for (auto& v : _data) {
                if (_aliveFn(v))
                    return v;
            }

            return _data.emplace_back();
        }

        bool InRange(TKey index) const { return index >= (TKey)0 && index < (TKey)_data.size(); }

        [[nodiscard]] auto at(size_t index) { return _data.at(index); }
        [[nodiscard]] auto begin() { return _data.begin(); }
        [[nodiscard]] auto end() { return _data.end(); }
        [[nodiscard]] const auto begin() const { return _data.begin(); }
        [[nodiscard]] const auto end() const { return _data.end(); }
    };

    SlotMap<ActiveDoor> ActiveDoors(ActiveDoor::IsAlive);

    constexpr float DOOR_WAIT_TIME = 5;

    void SetWallTMap(SegmentSide& side1, SegmentSide& side2, const WallClip& clip, int frame) {
        frame = std::clamp(frame, 0, (int)clip.NumFrames);

        auto tmap = clip.Frames[frame];

        bool changed = false;
        if (clip.UsesTMap1()) {
            changed = side1.TMap != tmap || side2.TMap != tmap;
            side1.TMap = side2.TMap = tmap;
        }
        else {
            // assert side.tmap1 && tmap2 != 0
            changed = side1.TMap2 != tmap || side2.TMap2 != tmap;
            side1.TMap2 = side2.TMap2 = tmap;
        }

        if (changed) Editor::Events::LevelChanged();
    }

    void DoOpenDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);
        auto conn = level.GetConnectedSide(wall.Tag);
        auto& side = level.GetSide(wall.Tag);
        auto& cside = level.GetSide(conn);
        auto& cwall = level.GetWall(cside.Wall);

        // todo: remove objects stuck on door

        door.Time += dt;

        auto& clip = Resources::GetWallClip(wall.Clip);
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(door.Time / frameTime);

        if (i < clip.NumFrames) {
            SetWallTMap(side, cside, clip, i);
        }

        if (i > clip.NumFrames / 2) { // half way open
            wall.SetFlag(WallFlag::DoorOpened);
            cwall.SetFlag(WallFlag::DoorOpened);
        }

        if (i >= clip.NumFrames - 1) {
            SetWallTMap(side, cside, clip, i - 1);

            if (!wall.HasFlag(WallFlag::DoorAuto)) {
                door.Time = -1; // free door slot because it won't close
            }
            else {
                fmt::print("Waiting door\n");
                wall.State = WallState::DoorWaiting;
                cwall.State = WallState::DoorWaiting;
                door.Time = 0;
            }
        }
    }

    void DoCloseDoor(Level& level, ActiveDoor& door, float dt) {
        auto& wall = level.GetWall(door.Front);

        auto front = level.TryGetWall(door.Front);
        auto back = level.TryGetWall(door.Back);

        auto conn = level.GetConnectedSide(wall.Tag);
        auto& side = level.GetSide(wall.Tag);
        auto& cside = level.GetSide(conn);

        if (wall.HasFlag(WallFlag::DoorAuto)) {
            for (auto& obj : level.Objects | views::filter(Object::IsAlive) ) {
                if (obj.Segment == wall.Tag.Segment || obj.Segment == conn.Segment) {
                    BoundingSphere sphere(obj.Position, obj.Radius);
                    auto face = Face::FromSide(level, wall.Tag);
                    Vector3 point;
                    float dist{};
                    if (IntersectFaceSphere(face, sphere, point, dist))
                        return; // object blocking doorway!
                }
            }
        }

        auto& clip = Resources::GetWallClip(wall.Clip);

        if (door.Time == 0) // play sound at start of closing
            Sound::Play3D(clip.CloseSound, side.Center, wall.Tag.Segment);

        door.Time += dt;
        auto frameTime = clip.PlayTime / clip.NumFrames;
        auto i = int(clip.NumFrames - door.Time / frameTime - 1);

        if (i < clip.NumFrames / 2) { // Half way closed
            front->ClearFlag(WallFlag::DoorOpened);
            if (back) back->ClearFlag(WallFlag::DoorOpened);
        }

        if (i > 0) {
            SetWallTMap(side, cside, clip, i);
            front->State = WallState::DoorClosing;
            if (back) back->State = WallState::DoorClosing;
            //door.Time = 0;
        }
        else {
            // CloseDoor()
            front->State = WallState::Closed;
            if (back) back->State = WallState::Closed;
            SetWallTMap(side, cside, clip, 0);
        }
    }

    void UpdateGame(Level& level, double t, float dt) {
        for (auto& obj : level.Objects) {
            obj.Lifespan -= dt;
        }

        for (auto& door : ActiveDoors) {
            auto& wall = level.GetWall(door.Front);

            if (wall.State == WallState::DoorOpening) {
                DoOpenDoor(level, door, dt);
            }
            else if (wall.State == WallState::DoorClosing) {
                DoCloseDoor(level, door, dt);
            }
            else if (wall.State == WallState::DoorWaiting) {
                door.Time += dt;
                if (door.Time > DOOR_WAIT_TIME) {
                    fmt::print("Closing door\n");
                    wall.State = WallState::DoorClosing;
                    door.Time = 0;
                }
            }
        }
    }

    ActiveDoor* FindDoor(WallID id) {
        for (auto& door : ActiveDoors) {
            if (door.Front == id || door.Back == id) return &door;
        }

        return nullptr;
    }

    void OpenDoor(Level& level, Tag tag) {
        auto& seg = level.GetSegment(tag);
        auto& side = seg.GetSide(tag.Side);
        auto wall = level.TryGetWall(side.Wall);
        if (!wall) throw Exception("Tried to open door on side that has no wall");

        auto conn = level.GetConnectedSide(tag);
        auto cwallId = level.TryGetWallID(conn);
        auto cwall = level.TryGetWall(cwallId);

        if (wall->State == WallState::DoorOpening ||
            wall->State == WallState::DoorWaiting)
            return;

        fmt::print("Opening door {}:{}\n", tag.Segment, tag.Side);

        ActiveDoor* door = nullptr;
        auto& clip = Resources::GetWallClip(wall->Clip);

        if (wall->State != WallState::Closed) {
            // Reuse door
            if (door = FindDoor(side.Wall)) {
                door->Time = std::max(clip.PlayTime - door->Time, 0.0f);
            }
        }

        if (!door) {
            door = &ActiveDoors.Alloc();
            door->Time = 0;
        }

        wall->State = WallState::DoorOpening;
        door->Front = side.Wall;

        if (cwall) {
            door->Back = cwallId;
            cwall->State = cwall->State = WallState::DoorOpening;
        }


        if (clip.OpenSound != SoundID::None) {
            Sound::Play3D(clip.OpenSound, side.Center, tag.Segment);
        }

        //if (wall->LinkedWall == WallID::None) {
        //    door->Parts = 1;
        //}
        //else {
        //    auto lwall = level.TryGetWall(wall->LinkedWall);
        //    auto& seg2 = level.GetSegment(lwall->Tag);

        //    assert(lwall->LinkedWall == seg.GetSide(tag.Side).Wall);
        //    lwall->State = WallState::DoorOpening;

        //    auto& csegp = level.GetSegment(seg2.GetConnection(lwall->Tag.Side));

        //    door->Parts = 2;

        //}
    }

    void UpdatePhysics(Level& level, double t, float dt) {
        Debug::Steps = 0;
        Debug::ClosestPoints.clear();

        HandleInput(level.Objects[0], dt);

        UpdateGame(level, t, dt);

        for (auto& obj : level.Objects | views::filter(Object::IsAlive)) {
            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;

            if (obj.Movement.Type == MovementType::Physics) {
                FixedPhysics(obj, dt);

                //if (obj.Movement.Physics.HasFlag(PhysicsFlag::Wiggle))
                //    WiggleObject(obj, t, dt, Resources::GameData.PlayerShip.Wiggle); // rather hacky, assumes the ship is the only thing that wiggles

                obj.Movement.Physics.InputVelocity = obj.Movement.Physics.Velocity;
                obj.Position += obj.Movement.Physics.Velocity * dt;


                auto delta = obj.Position - obj.LastPosition;
                auto maxDistance = delta.Length();
                LevelHit hit;

                if (maxDistance < 0.001f) {
                    // no travel, but need to check for being inside of wall (maybe this isn't necessary)
                    BoundingSphere sphere(obj.Position, obj.Radius);

                    if (IntersectLevel(level, sphere, obj.Segment, hit)) {
                        Debug::ClosestPoints.push_back(hit.Point);
                        Render::Debug::DrawLine(hit.Point, hit.Point + hit.Normal, { 1, 0, 0 });
                    }
                }
                else {
                    Vector3 dir;
                    delta.Normalize(dir);
                    //auto expectedTravel = (obj.Position() - obj.PrevPosition()).Length();

                    float hitDistance{};
                    Ray ray(obj.LastPosition, dir);

                    BoundingCapsule capsule{ .A = obj.LastPosition, .B = obj.Position, .Radius = obj.Radius };

                    if (IntersectLevel(level, capsule, obj.Segment, hit)) {

                        //Render::Debug::DrawPoint(hit.Point, { 1, 1, 0 });
                        Debug::ClosestPoints.push_back(hit.Point);
                        Render::Debug::DrawLine(hit.Point, hit.Point + hit.Normal, { 1, 0, 0 });
                    }
                }

                if (hit) {
                    if (obj.Type == ObjectType::Weapon) {
                        obj.Lifespan = -1;
                    }

                    if (auto wall = level.TryGetWall(hit.Tag)) {
                        if (wall->Type == WallType::Door) {
                            if (obj.Type == ObjectType::Weapon && wall->HasFlag(WallFlag::DoorLocked)) {
                                // Can't open door
                                Sound::Play3D(Sound::SOUND_WEAPON_HIT_DOOR, hit.Point, hit.Tag.Segment, obj.Parent);
                            }
                            else if (wall->State != WallState::DoorOpening)
                                OpenDoor(level, hit.Tag);
                        }
                    }
                    else {
                        if (obj.Type == ObjectType::Weapon) {
                            auto& weapon = Resources::GameData.Weapons[obj.ID];
                            Sound::Play3D(weapon.WallHitSound, hit.Point, hit.Tag.Segment, obj.Parent);
                        }
                    }
                }

                //CollideTriangles(level, obj, dt, 0);
                //CollideTriangles(level, obj, dt, 1); // Doing two passes makes the result more stable
                //CollideTriangles(level, obj, dt, 2); // Doing two passes makes the result more stable

                //auto frameVec = obj.Position() - obj.PrevTransform.Translation();
                //obj.Movement.Physics.Velocity = frameVec / dt;
                Editor::UpdateObjectSegment(level, obj);
            }

            Render::Debug::DrawLine(obj.LastPosition, obj.Position, { 0, 1.0f, 0.2f });

            Debug::ShipVelocity = obj.Movement.Physics.Velocity;
            Debug::ShipPosition = obj.Position;
        }
    }
}