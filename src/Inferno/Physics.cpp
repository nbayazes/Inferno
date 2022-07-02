#include "pch.h"
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "Input.h"
#include <iostream>

using namespace DirectX;


namespace Inferno {
    void TurnRoll(Object& obj, float dt) {
        constexpr auto turnRollScale = FixToFloat(0x4ec4 / 2) * XM_2PI;
        auto& pd = obj.Movement.Physics;
        const auto desiredBank = pd.AngularVelocity.y * turnRollScale;

        if (std::abs(pd.TurnRoll - desiredBank) > 0.001f) {
            constexpr auto rollRate = FixToFloat(0x2000) * XM_2PI;
            auto max_roll = rollRate * dt;
            const auto delta_ang = desiredBank - pd.TurnRoll;

            if (std::abs(delta_ang) < max_roll) {
                max_roll = delta_ang;
            }
            else {
                if (delta_ang < 0)
                    max_roll = -max_roll;
            }

            pd.TurnRoll += max_roll;
        }

        Debug::R = pd.TurnRoll;
    }

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
            obj.Transform = Matrix::CreateRotationZ(pd.TurnRoll) * obj.Transform;

        obj.Transform = Matrix::CreateFromYawPitchRoll(-pd.AngularVelocity * dt * XM_2PI) * obj.Transform;

        if (pd.HasFlag(PhysicsFlag::TurnRoll))
            TurnRoll(obj, dt);

        if (pd.TurnRoll) // re-rotate object for bank caused by turn
            obj.Transform = Matrix::CreateRotationZ(-pd.TurnRoll) * obj.Transform;
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
            physics.Thrust += obj.Transform.Forward() * dt;

        if (Input::IsKeyDown(Keys::Subtract))
            physics.Thrust += obj.Transform.Backward() * dt;

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
            physics.Thrust += obj.Transform.Left() * dt;

        if (Input::IsKeyDown(Keys::NumPad3))
            physics.Thrust += obj.Transform.Right() * dt;
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
        auto wiggle = obj.Transform.Up() * angle * amplitude * dt;
        obj.Movement.Physics.Velocity += wiggle;
    }

    void FixedPhysics(Object& obj, double t, float dt) {
        auto& physics = obj.Movement.Physics;

        if (obj.Type == ObjectType::Player) {
            const auto& ship = Resources::GameData.PlayerShip;

            physics.Thrust *= ship.MaxThrust / dt;
            physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.AngularThrust;
            Debug::ShipAcceleration = Vector3::Zero;

            AngularPhysics(obj, dt);
            LinearPhysics(obj);
        }
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
    Vector3 ClosestPoint(const Vector3& a, const Vector3& b, const Vector3& p) {
        // Project p onto ab, computing the paramaterized position d(t) = a + t * (b - a)
        auto ab = b - a;
        auto t = (p - a).Dot(ab) / ab.Dot(ab);

        // Clamp T to a 0-1 range. If t was < 0 or > 1 then the closest point was outside the line!
        t = std::clamp(t, 0.0f, 1.0f);

        // Compute the projected position from the clamped t
        return a + t * ab;
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
    Vector3 ClosestPoint(const Triangle& t, Vector3 point) {
        point = ProjectPointOntoPlane(point, t.GetPlane());

        if (PointInTriangle(t, point))
            return point; // point is on the surface of the triangle

        // check the points and edges
        auto c1 = ClosestPoint(t[0], t[1], point);
        auto c2 = ClosestPoint(t[1], t[2], point);
        auto c3 = ClosestPoint(t[2], t[0], point);

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

    HitResult2 Intersect2(const Triangle& t, Object& obj, float dt) {
        Plane plane(t.Points[0], t.Points[1], t.Points[2]);
        auto& pi = obj.Movement.Physics;

        Vector3 movement = obj.Position() - obj.PrevPosition();
        Vector3 direction;
        movement.Normalize(direction);

        // offset the ray start position by the object radius
        auto offset = obj.Position() + direction * obj.Radius;
        Ray ray(offset, direction);

        float hitDist{};
        if (!ray.Intersects(t.Points[0], t.Points[1], t.Points[2], hitDist)) {
            // could be behind or parallel
        }

        auto intersect = offset + direction * hitDist; // P
        ProjectPointOntoPlane(intersect, plane);

        // intersect an offsetted ray with the plane of the triangle to find the point P of intersection at which the sphere first touches the plane(when moving towards it).
        // Then you find the closest point Q on the triangle to this point P.
        // From Q you fire a ray back at the sphere, which will give you the time of collision, if any.



        //auto signedDistance = N * p + Cp;
        auto signedDistance = plane.DotNormal(obj.Position()) + plane.D();

        auto t0 = (1 - signedDistance) / plane.DotNormal(pi.Velocity); // time of intersection for a unit sphere


        auto planeIntersect = obj.PrevPosition() - plane.Normal() * obj.Radius + t0 * pi.Velocity;


        // special case: object is embedded in wall when velocity = 0 and intersecting

        return {};
    }

    Option<HitResult> Intersect(const Triangle& t, Object& obj, float dt) {
        Plane plane(t.Points[0], t.Points[1], t.Points[2]);
        //auto newPos = obj.PrevTransform.Translation();
        BoundingSphere sphere(obj.PrevTransform.Translation(), obj.Radius);
        auto& pd = obj.Movement.Physics;

        //auto direction = obj.Transform.Translation() - obj.PrevTransform.Translation();
        if (pd.Velocity.Dot(plane.Normal()) > 0) return {}; // ignore faces pointing away from velocity

        //auto frameVec = pd.Velocity; // velocity change due to intersections in this frame (should be aggregate of all sources)
        //auto newPos = pd.Velocity * dt;
        Vector3 direction;
        pd.Velocity.Normalize(direction);

        //if (sphere.Intersects(plane)) { // check that dest sphere intersects with triangle plane (or is behind it)
        if (sphere.Intersects(t.Points[0], t.Points[1], t.Points[2])) {
            //auto direction = obj.Transform.Translation() - obj.PrevTransform.Translation();
            direction.Normalize();
            Ray ray(obj.PrevTransform.Translation(), direction);
            //ray.Intersects(sphere, dist);

            // where does the velocity intersect the triangle?
            float intersectionDist{};
            if (!ray.Intersects(plane, intersectionDist)) return {};
            auto intersection = obj.PrevTransform.Translation() + direction * intersectionDist;

            // Adjust velocity
            if (pd.HasFlag(PhysicsFlag::Stick)) {

            }
            else {
                // We're constrained by wall, so subtract wall part from velocity
                auto wallPart = plane.Normal().Dot(pd.Velocity);

                if (pd.HasFlag(PhysicsFlag::Bounce))
                    wallPart *= 2; //Subtract out wall part twice to achieve bounce

                pd.Velocity -= plane.Normal() * wallPart;

                //pd.Velocity = Vector3::Reflect(pd.Velocity, plane.Normal()) / pd.Mass;
            }

            Debug::ClosestPoint = ClosestPoint(t, obj.Position());

            auto projected = ProjectPointOntoPlane(obj.Transform.Translation(), plane); // where was the closest point on plane?
            Vector3 framePos = projected + plane.Normal() * obj.Radius;
            //obj.Transform.Translation(framePos);


            // Snap object position to intersection point
            // todo: only apply snap using triangle that is most aligned to velocity


            //obj.Transform.Translation(obj.PrevTransform.Translation());


            // Does the projected point lie inside the polygon?
            // if it does, reposition using old code, otherwise use corner code...
            // 
            //auto projected = ProjectPointOntoPlane(dest, plane); // where was the closest point on plane?

            //float projDist{};
            //if (Ray(projected, -plane.Normal()).Intersects(t.Points[0], t.Points[1], t.Points[2], projDist)) {
            //    Debug::Steps = 1;

            ////    obj.Transform.Translation(projected + plane.Normal() * obj.Radius);
            //}
            //else {
            //    Debug::Steps = 2;
            //}

            //Set velocity from actual movement
            //pd.Velocity = moved / dt;

            //vms_vector moved_vec;
            //vm_vec_sub(&moved_vec, &obj->pos, &start_pos);
            //vm_vec_copy_scale(&obj->mtype.phys_info.velocity, &moved_vec, fixdiv(f1_0, FrameTime));

            //if (dist <= 0.01f || dist >= obj.Radius) return {}; // was the intersection inside of the object radius?
            //auto intersect = obj.PrevTransform.Translation() + direction * dist;

            //auto intersect = ProjectPointOntoPlane(dest, plane); // where was the closest point on plane?
            //auto intersectVec = intersect - dest;
            //auto len = intersectVec.Length();
            //if (len <= 0.01f || len >= obj.Radius) return {}; // was the intersection inside of the object radius?
            //intersectVec.Normalize();


            // Does the intersection point lie inside the polygon?
            // if it does, reposition using old code, otherwise use corner code...

            //float dist{};
            //if (Ray(dest, intersectVec).Intersects(t.Points[0], t.Points[1], t.Points[2], dist)) {
                //auto wallPart = intersectVec.Dot(plane.Normal());
                //auto hitSpeed = wallPart / dt; // these are used for wall scrape damage / lava hits


                //obj.Transform.Translation(intersect - intersectVec * obj.Radius);

                // shift the object position off of the wall. note that this isn't accurate at sharp angles
                //obj.Transform.Translation(intersect - direction * obj.Radius);

                //pd.Velocity = Vector3::Reflect(pd.Velocity, plane.Normal()) / pd.Mass;

                //wall_part = vm_vec_dot(&moved_v, &hit_info.hit_wallnorm);
                //if (wall_part != 0 && moved_time > 0 && (hit_speed = -fixdiv(wall_part, moved_time)) > 0)
                //    collide_object_with_wall(obj, hit_speed, WallHitSeg, WallHitSide, &hit_info.hit_pnt);


                // bouncing
                // obj.Movement.Physics.Velocity = Vector3::Reflect(obj.Movement.Physics.Velocity, plane.Normal())

                //obj.Movement.Physics.Velocity += /*obj.Movement.Physics.Velocity.Length() **/ plane.Normal() / obj.Movement.Physics.Mass;

                //vm_vec_scale_add2(&obj->mtype.phys_info.velocity, force_vec, fixdiv(f1_0, obj->mtype.phys_info.mass));

                // cancel velocity in the axis of the plane normal
                //obj.Movement.Physics.Velocity = Vector3::Reflect(obj.Movement.Physics.Velocity, plane.Normal());

                //auto velVec = pd.Velocity;
                //velVec.Normalize();
                //auto dot = plane.DotNormal(velVec);
                //auto dot = AngleBetweenVectors(velVec, plane.Normal());
                //return { { intersect, intersectVec, plane.Normal(), dot } };
            //}

            //auto intersect = ProjectPointOntoPlane(dest, p0);
            //auto vec = intersect - dest;
            //auto len = vec.Length();
            //if (len <= 0.01f || len >= obj.Radius) return;
            //vec.Normalize();
            //Ray ray(dest, vec);
            //float dist{};
            //if (ray.Intersects(face[indices[0]], face[indices[1]], face[indices[2]], dist)) {
            //    obj.Transform.Translation(intersect - vec * obj.Radius);
            //}
        }

        return {};
    }

    //List<Triangle> GatherTriangles(Level& level) {
    //    List<Triangle> triangles;

    //    for (auto& seg : level.Segments) {
    //        for (auto& side : SideIDs) {
    //            auto face = Face::FromSide(level, seg, side);
    //            triangles.push_back(face
    //        }
    //    }
    //}

    void CollideTriangles(Level& level, Object& obj, float dt) {
        //List<Triangle> triangles; // todo: don't reallocate this every frame
        List<HitResult> hits; // todo: don't reallocate this every frame
        // gather all nearby triangles

        for (auto& seg : level.Segments) {
            //auto& seg = level.Segments[0];
            for (auto& side : SideIDs) {
                auto face = Face::FromSide(level, seg, side);
                if (seg.SideIsSolid(side, level)) {
                    if (auto result = Intersect({ face.VerticesForPoly0() }, obj, dt)) hits.push_back(*result);
                    if (auto result = Intersect({ face.VerticesForPoly1() }, obj, dt)) hits.push_back(*result);
                }

                //triangles.push_back({ face.VerticesForPoly0() });
                //triangles.push_back({ face.VerticesForPoly1() });
            }
        }

        //for (auto& hit : hits) {
        //    //obj.Transform.Translation(hit.Intersect + hit.Normal * obj.Radius);
        //    obj.Transform.Translation(hit.Intersect - hit.IntersectVec * obj.Radius);
        //    obj.Movement.Physics.Velocity = Vector3::Reflect(obj.Movement.Physics.Velocity, hit.Normal);
        //    Debug::ShipVelocity = obj.Movement.Physics.Velocity;
        //    Debug::K = hit.Dot;
        //}

        //Seq::sortBy(hits, [](HitResult& a, HitResult& b) { return a.Dot > b.Dot; });

        //if (!hits.empty()) {
        //    auto& hit = hits[0];
        //    obj.Transform.Translation(hit.Intersect - hit.IntersectVec * obj.Radius);
        //    //obj.Movement.Physics.Velocity = Vector3::Reflect(obj.Movement.Physics.Velocity, hit.Normal);
        //    Debug::ShipVelocity = obj.Movement.Physics.Velocity;
        //    Debug::K = hit.Dot;
        //}
    }


    void UpdatePhysics(Level& level, double t, float dt) {
        Debug::Steps = 0;

        int i = 0;
        for (auto& obj : level.Objects) {
            obj.PrevTransform = obj.Transform;

            if (i++ > 0) continue; // debugging, only do physics on player
            HandleInput(obj, dt); // player only

            if (obj.Movement.Type == MovementType::Physics) {
                FixedPhysics(obj, t, dt);

                if (obj.Movement.Physics.HasFlag(PhysicsFlag::Wiggle))
                    WiggleObject(obj, t, dt, Resources::GameData.PlayerShip.Wiggle); // rather hacky, assumes the ship is the only thing that wiggles

                CollideTriangles(level, obj, dt);

                obj.Transform.Translation(obj.Position() + obj.Movement.Physics.Velocity * dt);
                //auto frameVec = framePos - obj.PrevTransform.Translation();
                auto frameVec = obj.Position() - obj.PrevTransform.Translation();
                obj.Movement.Physics.Velocity = frameVec / dt;

                //ApplyPhysics(obj, obj.Movement.Physics, Render::FrameTime);
            }

            Debug::ShipVelocity = obj.Movement.Physics.Velocity;
            Debug::ShipPosition = obj.Position();
        }
    }
}