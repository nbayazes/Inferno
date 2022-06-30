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
        constexpr auto turnRollScale = FixToFloat(0x4ec4);
        auto& pi = obj.Movement.Physics;
        auto desiredBank = pi.AngularVelocity.y * turnRollScale * 2;

        if (std::abs(pi.TurnRoll - desiredBank) > 0.001f) {
            constexpr auto rollRate = FixToFloat(0x2000);
            auto max_roll = rollRate * dt * 2;
            auto delta_ang = desiredBank - pi.TurnRoll;

            if (std::abs(delta_ang) < max_roll) {
                max_roll = delta_ang;
            }
            else {
                if (delta_ang < 0)
                    max_roll = -max_roll;
            }

            pi.TurnRoll += max_roll;
        }
    }

    void AngularPhysics(Object& obj, float dt) {
        auto& pi = obj.Movement.Physics;

        if (pi.AngularVelocity == Vector3::Zero && pi.AngularThrust == Vector3::Zero)
            return;

        if (pi.Drag > 0) {
            auto drag = pi.Drag * 5 / 2;

            if ((int16)pi.Flags & (int16)PhysicsFlag::UseThrust && pi.Mass > 0) {
                auto accel = pi.AngularThrust / pi.Mass;
                Debug::ShipAcceleration = accel;
                pi.AngularVelocity += accel;
                pi.AngularVelocity *= 1 - drag;
            }
            else if (!(int16)pi.Flags & (int16)PhysicsFlag::FreeSpinning) {
                pi.AngularVelocity *= 1 - drag;
            }
        }

        Debug::ShipVelocity = pi.AngularVelocity;

        //unrotate object for bank caused by turn
        if (pi.TurnRoll) {
            //obj.Transform *= Matrix::CreateFromAxisAngle(obj.Transform.Forward(), -pi.TurnRoll);
            obj.Transform = Matrix::CreateFromAxisAngle(Vector3::Forward, -pi.TurnRoll) * obj.Transform;
        }

        // x: pitch->half of bank 4587
        // y: bank(yaw)->ramping, 9174
        // z: roll->no ramping, 9174


        //auto rotation = Matrix::CreateFromYawPitchRoll(pi.AngularVelocity.x * dt * XM_2PI,
        //                                               pi.AngularVelocity.z * dt * XM_2PI,
        //                                               pi.AngularVelocity.y * dt * XM_2PI);

        //// angular velocities need to be converted from fixed units to radians using 2PI
        //const auto rotation = 
        //    Matrix::CreateFromAxisAngle(obj.Transform.Up(), pi.AngularVelocity.x * dt * XM_2PI) *
        //    Matrix::CreateFromAxisAngle(obj.Transform.Right(), pi.AngularVelocity.y * dt * XM_2PI) *
        //    Matrix::CreateFromAxisAngle(obj.Transform.Forward(), pi.AngularVelocity.z * dt * XM_2PI);


        //obj.Transform *= Matrix::CreateTranslation(-translation) * rotation * Matrix::CreateTranslation(translation);

        //auto rotation = Matrix::CreateFromYawPitchRoll(pi.AngularVelocity.x * dt * XM_2PI,
        //                                               pi.AngularVelocity.y * dt * XM_2PI,
        //                                               pi.AngularVelocity.z * dt * XM_2PI);
        auto rotation = Matrix::CreateFromYawPitchRoll(-pi.AngularVelocity * dt * XM_2PI);
        obj.Transform = rotation * obj.Transform;

        if ((int16)pi.Flags & (int16)PhysicsFlag::TurnRoll)
            TurnRoll(obj, dt);

        //re-rotate object for bank caused by turn
        if (pi.TurnRoll) {
            //obj.Transform *= Matrix::CreateFromAxisAngle(obj.Transform.Forward(), pi.TurnRoll);
            obj.Transform = Matrix::CreateFromAxisAngle(Vector3::Forward, pi.TurnRoll) * obj.Transform;
        }

        //check_and_fix_matrix(&obj->orient);
    }

    void LinearPhysics(Object& obj) {
        auto& pi = obj.Movement.Physics;

        if (pi.Drag > 0 && pi.Mass > 0) {
            if ((int16)pi.Flags & (int16)PhysicsFlag::UseThrust && pi.Mass > 0) {
                auto accel = pi.Thrust / pi.Mass;
                //Debug::ShipAcceleration = accel;

                pi.Velocity += accel;
                pi.Velocity *= 1 - pi.Drag;
            }
            else {
                pi.Velocity *= 1 - pi.Drag;
            }
        }
    }

    void FixedPhysics(Object& obj, double t, float dt) {
        auto& physics = obj.Movement.Physics;

        Vector3 wiggle{};

        if (obj.Type == ObjectType::Player) {
            using Keys = DirectX::Keyboard::Keys;

            //auto ht0 = GetHoldTime(true, 0, frameTime);
            //auto ht1 = GetHoldTime(true, 1, frameTime);

            physics.Thrust = Vector3::Zero;
            physics.AngularThrust = Vector3::Zero;

            if (Input::IsKeyDown(Keys::NumPad8)) {
                physics.Thrust += obj.Transform.Forward() * dt;
            }

            if (Input::IsKeyDown(Keys::NumPad2)) {
                physics.Thrust += obj.Transform.Backward() * dt;
            }

            // yaw
            if (Input::IsKeyDown(Keys::NumPad1))
                physics.AngularThrust.y = -dt;
            if (Input::IsKeyDown(Keys::NumPad3))
                physics.AngularThrust.y = dt;

            // pitch
            if (Input::IsKeyDown(Keys::Add))
                physics.AngularThrust.x = -dt;
            if (Input::IsKeyDown(Keys::Subtract))
                physics.AngularThrust.x = dt;


            // roll
            if (Input::IsKeyDown(Keys::NumPad7))
                physics.AngularThrust.z = -dt;
            if (Input::IsKeyDown(Keys::NumPad9))
                physics.AngularThrust.z = dt;


            if (Input::IsKeyDown(Keys::NumPad4)) {
                physics.Thrust += obj.Transform.Left() * dt; // this should be scaled by time held down up to a maximum
            }

            if (Input::IsKeyDown(Keys::NumPad6)) {
                physics.Thrust += obj.Transform.Right() * dt; // this should be scaled by time held down up to a maximum
            }

            const auto& ship = Resources::GameData.PlayerShip;

            physics.Thrust *= ship.MaxThrust / dt;
            physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.AngularThrust;
            Debug::ShipAcceleration = Vector3::Zero;

            AngularPhysics(obj, dt);
            LinearPhysics(obj);

            if ((int16)physics.Flags & (int16)PhysicsFlag::Wiggle) { // apply wiggle
                auto wamount = Resources::GameData.PlayerShip.Wiggle; // rather hacky, assumes the ship is the only thing that wiggles
                auto wangle = (float)std::sin(t * XM_PI);
                // apply wiggle directly to the position and not as a velocity
                wiggle = obj.Transform.Up() * wangle * wamount * dt;
            }

            //Debug::ShipVelocity = physics.Velocity;

            static int index = 0;
            static double refresh_time = 0.0;

            if (refresh_time == 0.0)
                refresh_time = t;

            if (Input::IsKeyDown(Keys::NumPad8)) {
                if (index < Debug::ShipVelocities.size() && t >= refresh_time) {
                    //while (refresh_time < Game::ElapsedTime) {
                    Debug::ShipVelocities[index] = physics.Velocity.Length();
                    //std::cout << t << "," << physics.Velocity.Length() << "\n";
                    refresh_time = t + 1.0f / 60.0f;
                    index++;
                }
            }
            else {
                index = 1;
            }

            //auto r = 1 - physics.Drag;
            //obj.Transform.Translation(obj.Position() + physics.Velocity * (std::pow(r, dt) / std::log(r)) + wiggle);
        }

        // r: distance traveled
        //position += velocity * (std::pow(r, dt) - 1.0f) / std::log(r);
        //velocity *= std::pow(r, dt);
        //obj.Transform.Translation(obj.Position() + physics.Velocity * (std::pow(r, dt) / std::log(r) + wiggle);

        obj.Transform.Translation(obj.Position() + physics.Velocity * dt + wiggle);
    }


    void UpdatePhysics(Level& level, double t, double dt) {
        for (auto& obj : level.Objects) {
            obj.PrevTransform = obj.Transform;

            if (obj.Movement.Type == MovementType::Physics) {
                FixedPhysics(obj, t, dt);
                //ApplyPhysics(obj, obj.Movement.Physics, Render::FrameTime);
            }
        }
    }
}