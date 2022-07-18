#include "pch.h"
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "Input.h"
#include "Editor/Editor.Object.h"
#include "Graphics/Render.Debug.h"
#include <iostream>

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

    void Intersect(const Triangle& t, Object& obj, float dt, int pass) {
        //if (obj.Type == ObjectType::Player) return;

        Plane plane(t.Points[0], t.Points[1], t.Points[2]);
        auto& pd = obj.Movement.Physics;

        if (pd.Velocity.Dot(plane.Normal()) > 0) return; // ignore faces pointing away from velocity
        auto expectedDistance = obj.Position - obj.LastPosition;
        if (expectedDistance.Length() < 0.001f) return;
        Vector3 dir;
        expectedDistance.Normalize(dir);
        //auto expectedTravel = (obj.Position() - obj.PrevPosition()).Length();

        float hitDistance{};
        Ray ray(obj.LastPosition, dir);
        bool hit = false;

        if (ray.Intersects(t.Points[0], t.Points[1], t.Points[2], hitDistance)) {
            hit = hitDistance < expectedDistance.Length() - obj.Radius;
            //if (hit && hitDistance < expectedDistance.Length() + obj.Radius) // did the object pass all the way through the wall in one frame?
            if (hit)
                obj.Position = obj.LastPosition + dir * (hitDistance - obj.Radius);
        }

        if (!hit) {
            // ray cast didn't hit anything, try the sphere test
            // note that this is not a sweep and will miss points between the begin and end.
            // Fortunately, most fast-moving objects are projectiles and have small radii.
            BoundingSphere sphere(obj.Position, obj.Radius);
            hit = sphere.Intersects(t.Points[0], t.Points[1], t.Points[2]);

            float planeDist;
            ray.Intersects(plane, planeDist);
            if (!hit && planeDist <= expectedDistance.Length()) {
                // Last, test if the object sphere collides with the intersection of the triangle's plane
                sphere = BoundingSphere(obj.LastPosition + dir * planeDist, obj.Radius * 1.1f);
                //BoundingSphere sphere(obj.Position(), obj.Radius);
                hit = sphere.Intersects(t.Points[0], t.Points[1], t.Points[2]);
            }
        }

        if (!hit) return;

        bool tryAgain = false;

        auto closestPoint = ClosestPoint(t, obj.Position);
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

    class SegmentSearch {
        Set<Segment*> _results;
        Stack<Segment*> _stack;
    public:
        const Set<Segment*>& GetNearby(Level& level, const Object& obj, float range) {
            auto root = level.TryGetSegment(obj.Segment);
            _stack.push(root);
            _results.clear();

            while (!_stack.empty()) {
                auto seg = _stack.top();
                _stack.pop();
                if (!seg) continue;

                _results.insert(seg);
                for (auto& side : SideIDs) {
                    if (auto conn = level.TryGetSegment(seg->GetConnection(side))) {
                        if (_results.contains(conn)) continue;
                        if (seg != root && !SideVertsInRange(level, *seg, side, obj.Position, range)) continue;
                        _stack.push(conn);
                    }
                }
            }

            return _results;
        }

    private:
        bool SideVertsInRange(Level& level, const Segment& seg, SideID side, const Vector3 point, float range) {
            for (auto& i : seg.GetVertexIndices(side)) {
                if (Vector3::Distance(level.Vertices[i], point) < range)
                    return true;
            }

            return false;
        }
    } SegmentSearch;

    void CollideTriangles(Level& level, Object& obj, float dt, int pass) {
        // gather all nearby triangles

        //int tries = 0;
        //int totalTries = 0;
        //do {

        auto searchRange = obj.Radius * 2 + (obj.Movement.Physics.Velocity * dt).Length();
        Debug::R = searchRange;
        auto& nearby = SegmentSearch.GetNearby(level, obj, searchRange);
        Debug::Steps = (float)nearby.size();

        for (auto& seg : nearby) {
            for (auto& side : SideIDs) {
                auto face = Face::FromSide(level, *seg, side);
                if (seg->SideIsSolid(side, level)) {
                    Intersect({ face.VerticesForPoly0() }, obj, dt, pass);
                    Intersect({ face.VerticesForPoly1() }, obj, dt, pass);
                }
            }
        }

        //for (auto& seg : level.Segments) {
        //    for (auto& side : SideIDs) {
        //        auto face = Face::FromSide(level, seg, side);
        //        if (seg.SideIsSolid(side, level)) {
        //            Intersect({ face.VerticesForPoly0() }, obj, dt, pass);
        //            Intersect({ face.VerticesForPoly1() }, obj, dt, pass);
        //        }
        //    }
        //}


        //    totalTries++;
        //    if (totalTries > 8) {
        //        SPDLOG_WARN("max total tries reached");
        //        break;
        //    }
        //} while (tries);
    }


    void UpdatePhysics(Level& level, double t, float dt) {
        Debug::Steps = 0;
        Debug::ClosestPoints.clear();

        HandleInput(level.Objects[0], dt);
        Matrix m;
        Quaternion q;

        for (auto& obj : level.Objects) {
            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;

            if (obj.Movement.Type == MovementType::Physics) {
                FixedPhysics(obj, dt);

                if (obj.Movement.Physics.HasFlag(PhysicsFlag::Wiggle))
                    WiggleObject(obj, t, dt, Resources::GameData.PlayerShip.Wiggle); // rather hacky, assumes the ship is the only thing that wiggles

                obj.Movement.Physics.InputVelocity = obj.Movement.Physics.Velocity;
                obj.Position += obj.Movement.Physics.Velocity * dt;

                CollideTriangles(level, obj, dt, 0);
                CollideTriangles(level, obj, dt, 1); // Doing two passes makes the result more stable with odd geometry
                //CollideTriangles(level, obj, dt, 2); // Doing two passes makes the result more stable with odd geometry

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