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

    // Returns wiggle as a translation and not a velocity
    Vector3 WiggleObject(const Object& obj, double t, float dt, float amplitude) {
        auto angle = std::sinf(t * XM_PI);
        return obj.Transform.Up() * angle * amplitude * dt;
    }

    void FixedPhysics(Object& obj, double t, float dt) {
        auto& physics = obj.Movement.Physics;

        Vector3 wiggle{};

        if (obj.Type == ObjectType::Player) {
            HandleInput(obj, dt);

            const auto& ship = Resources::GameData.PlayerShip;

            physics.Thrust *= ship.MaxThrust / dt;
            physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.AngularThrust;
            Debug::ShipAcceleration = Vector3::Zero;

            AngularPhysics(obj, dt);
            LinearPhysics(obj);

            if (physics.HasFlag(PhysicsFlag::Wiggle))
                wiggle = WiggleObject(obj, t, dt, Resources::GameData.PlayerShip.Wiggle); // rather hacky, assumes the ship is the only thing that wiggles
            auto angle = std::sinf(t * XM_PI);

            //Debug::ShipVelocity = physics.Velocity;
        }

        obj.Transform.Translation(obj.Position() + physics.Velocity * dt + wiggle);
    }

    struct Triangle {
        Array<Vector3, 3> Points;
    };

    struct HitResult {
        Vector3 Intersect, IntersectVec, Normal; // where the hit occurred
        float Dot; // Dot product of face normal and object velocity
    };

    Option<HitResult> Intersect(const Triangle& t, Object& obj, float dt) {
        Plane plane(t.Points[0], t.Points[1], t.Points[2]);
        auto dest = obj.Transform.Translation();
        BoundingSphere sphere(dest, obj.Radius);
        auto& pd = obj.Movement.Physics;

        if (sphere.Intersects(plane)) { // check that dest sphere intersects with triangle plane (or is behind it)
            auto intersect = ProjectPointOntoPlane(dest, plane); // where was the closest point on plane?
            auto intersectVec = intersect - dest;
            auto len = intersectVec.Length();
            if (len <= 0.01f || len >= obj.Radius) return {}; // was the intersection inside of the object radius?
            intersectVec.Normalize();

            // Does the intersection point lie inside the polygon?
            float dist{};
            if (Ray(dest, intersectVec).Intersects(t.Points[0], t.Points[1], t.Points[2], dist)) {
                //auto wallPart = intersectVec.Dot(plane.Normal());
                //auto hitSpeed = wallPart / dt; // these are used for wall scrape damage / lava hits

                if (pd.HasFlag(PhysicsFlag::Stick)) {

                }
                else {
                    // We're constrained by wall, so subtract wall part from velocity
                    auto wallPart = plane.Normal().Dot(pd.Velocity);

                    if(pd.HasFlag(PhysicsFlag::Bounce))
                       wallPart *= 2; //Subtract out wall part twice to achieve bounce

                    pd.Velocity -= plane.Normal() * wallPart;
                }

                obj.Transform.Translation(intersect - intersectVec * obj.Radius);

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

                auto velVec = pd.Velocity;
                velVec.Normalize();
                //auto dot = plane.DotNormal(velVec);
                auto dot = AngleBetweenVectors(velVec, plane.Normal());
                return { { intersect, intersectVec, plane.Normal(), dot } };
            }

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
            for (auto& side : SideIDs) {
                auto face = Face::FromSide(level, seg, side);

                if (auto result = Intersect({ face.VerticesForPoly0() }, obj, dt)) hits.push_back(*result);
                if (auto result = Intersect({ face.VerticesForPoly1() }, obj, dt)) hits.push_back(*result);

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

    void Intersect(const Face& face, Object& obj) {
        auto indices = face.Side.GetRenderIndices();
        Plane p0(face[indices[0]], face[indices[1]], face[indices[2]]);
        Plane p1(face[indices[3]], face[indices[4]], face[indices[5]]);

        auto dest = obj.Transform.Translation();
        BoundingSphere s(dest, obj.Radius);




        // todo: use surface with sharpest angle to reposition
        //auto velVec = obj.Movement.Physics.Velocity;
        //velVec.Normalize();

        auto p0i = s.Intersects(p0);
        auto p1i = s.Intersects(p1);

        int fi = -1;

        //// Use the face that is sharpest
        //if (p0i && p1i) {
        //    auto dn0 = p0.DotNormal(velVec);
        //    auto dn1 = p1.DotNormal(velVec);
        //    fi = dn1 > dn0 ? 1 : 0;
        //    //if (dn1 > dn0) fi = 1;
        //}

        if (s.Intersects(p0)) { // checks for intersect or behind
            auto intersect = ProjectPointOntoPlane(dest, p0);
            auto vec = intersect - dest;
            auto len = vec.Length();
            if (len <= 0.01f || len >= obj.Radius) return;
            vec.Normalize();
            Ray ray(dest, vec);
            float dist{};
            if (ray.Intersects(face[indices[0]], face[indices[1]], face[indices[2]], dist)) {
                obj.Transform.Translation(intersect - vec * obj.Radius);
            }
        }

        if (s.Intersects(p1)) { // checks for intersect or behind
            auto intersect = ProjectPointOntoPlane(dest, p1);
            auto vec = intersect - dest;
            auto len = vec.Length();
            if (len <= 0.01f || len >= obj.Radius) return;
            vec.Normalize();
            Ray ray(dest, vec);
            float dist{};
            if (ray.Intersects(face[indices[3]], face[indices[4]], face[indices[5]], dist)) {
                obj.Transform.Translation(intersect - vec * obj.Radius);
            }
        }

        //if (s.Intersects(p0) || s.Intersects(p1)) { // checks for intersect or behind
        //    auto intersect = ProjectPointOntoPlane(dest, p0);
        //    auto vec = intersect - dest;
        //    auto len = vec.Length();
        //    if (len <= 0.01f || len >= obj.Radius) return;
        //    vec.Normalize();
        //    Ray ray(dest, vec);
        //    float dist{};
        //    if (ray.Intersects(face[indices[0]], face[indices[1]], face[indices[2]], dist) ||
        //        ray.Intersects(face[indices[3]], face[indices[4]], face[indices[5]], dist)) {
        //        obj.Transform.Translation(intersect - vec * obj.Radius);
        //    }
        //}

        //if (auto hit = face.Intersects(obj)) {
        //    auto vec = obj.Transform.Translation() - obj.PrevTransform.Translation();
        //    vec.Normalize();

        //    obj.Transform.Translation(obj.PrevTransform.Translation() + vec * (hit->Distance - obj.Radius));
        //    //obj.Transform.Translation(obj.Transform.Translation() + hit->Distance)
        //}
    }

    void Collide(Level& level, Object& obj) {
        // compare nearby seg walls with obj pos and adjust it backwards

        for (auto& seg : level.Segments) { // todo: just nearby ones
            for (auto& side : SideIDs) {

                auto face = Face::FromSide(level, seg, side);
                Intersect(face, obj);
            }
        }
    }


    void UpdatePhysics(Level& level, double t, double dt) {
        for (auto& obj : level.Objects) {
            obj.PrevTransform = obj.Transform;

            if (obj.Movement.Type == MovementType::Physics) {
                FixedPhysics(obj, t, dt);
                //Collide(level, obj);
                CollideTriangles(level, obj, dt);
                Debug::ShipVelocity = obj.Movement.Physics.Velocity;
                //ApplyPhysics(obj, obj.Movement.Physics, Render::FrameTime);
            }
        }
    }
}