#include "pch.h"
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "Input.h"

namespace Inferno {
    Array<float, 8> HoldTime{};

    float GetHoldTime(bool held, int key, float frameTime) {
        if (held) {
            HoldTime[key] += frameTime;
            //HoldTime[key] = std::clamp(HoldTime[key], 0.0f, 1.0f);
        }
        else {
            HoldTime[key] = 0;
        }

        return HoldTime[key];
    };

    void ApplyPhysics(Object& obj, PhysicsData& physics, float frameTime) {
        //physics.Velocity = {};

        auto mass = physics.Mass <= 0 ? 0.01f : physics.Mass; // prevent div 0

        Vector3 wiggle{};

        if (obj.Type == ObjectType::Player) {
            using Keys = DirectX::Keyboard::Keys;

            //auto ht0 = GetHoldTime(true, 0, frameTime);
            //auto ht1 = GetHoldTime(true, 1, frameTime);

            physics.Thrust = Vector3::Zero;

            if (Input::IsKeyDown(Keys::NumPad8)) {
                physics.Thrust += obj.Transform.Forward() * frameTime;
            }

            if (Input::IsKeyDown(Keys::NumPad2)) {
                physics.Thrust += obj.Transform.Backward() * frameTime;
            }

            if (Input::IsKeyDown(Keys::NumPad4)) {
                physics.Thrust += obj.Transform.Left() * frameTime; // this should be scaled by time held down up to a maximum
            }

            if (Input::IsKeyDown(Keys::NumPad6)) {
                physics.Thrust += obj.Transform.Right() * frameTime; // this should be scaled by time held down up to a maximum
            }

            const auto& ship = Resources::GameData.PlayerShip;

            //vm_vec_scale(&obj->mtype.phys_info.thrust, fixdiv(Player_ship->max_thrust, ft));
            physics.Thrust *= ship.MaxThrust / frameTime;
            Debug::ShipThrust = physics.Thrust;

            // maxThrust (u/s) / s -> (u/s2) * thrust (s) -> (u/s)

            // rthrust = controls pitch/heading/bank
            // thrust = forward * thrustTime * afterburner


            // Angular input
            //rotang.p = (fixang)(Controls.pitch_time / ROT_SPEED);
            //rotang.b = (fixang)(Controls.bank_time / ROT_SPEED);
            //rotang.h = (fixang)(Controls.heading_time / ROT_SPEED);

            //physics.Thrust.x = std::clamp(physics.Thrust.x, -ship.MaxThrust, ship.MaxThrust);
            //physics.Thrust.y = std::clamp(physics.Thrust.y, -ship.MaxThrust, ship.MaxThrust);
            //physics.Thrust.z = std::clamp(physics.Thrust.z, -ship.MaxThrust, ship.MaxThrust);
            //physics.Thrust.Clamp({ -ship.MaxThrust, -ship.MaxThrust, -ship.MaxThrust }, { ship.MaxThrust, ship.MaxThrust, ship.MaxThrust });

            Debug::Steps = Debug::R = Debug::K = 0;
            Debug::ShipAcceleration = Vector3::Zero;

            if (physics.Drag > 0 /*&& physics.Thrust.LengthSquared() > 0*/) {
                { // if uses thrust apply angular drag
                    auto accel = physics.AngularThrust / mass * frameTime;

                    { // integrate based on frame time steps...
                        physics.AngularVelocity += accel;
                        physics.AngularVelocity *= 1 - physics.Drag;
                    }

                    // linear scale of remaining
                    //physics.AngularVelocity += accel * k;
                    //physics.AngularVelocity *= 1 - (k * drag);
                }

                constexpr auto FT = 1 / 64.0f; // 64 updates per second
                auto steps = frameTime / FT;
                auto r = std::fmod(frameTime, FT);
                auto k = r / FT;
                Debug::Steps = steps;
                Debug::R = r;
                Debug::K = k;

                auto accel = physics.Thrust / mass;
                Debug::ShipAcceleration = accel;

                //while ((steps -= 1) > 0) { // if uses thrust apply drag to velocity
                // Accumulate number of steps
                while (steps-- > 0) {
                    physics.Velocity += accel;
                    physics.Velocity *= 1 - physics.Drag;
                }

                // max speed = (accel / drag) - accel

                // Apply fractional part of update per second
                physics.Velocity += accel * k;
                physics.Velocity *= 1 - (k * physics.Drag);
            }

            //physics.Velocity += obj.Transform.Forward() * physics.Thrust.z * frameTime;
            //physics.Velocity += obj.Transform.Right() * physics.Thrust.x * frameTime;

            if ((int16)physics.Flags & (int16)PhysicsFlag::Wiggle) { // apply wiggle
                auto wamount = Resources::GameData.PlayerShip.Wiggle; // rather hacky, assumes the ship is the only thing that wiggles
                auto wangle = (float)std::sin(Render::ElapsedTime * DirectX::XM_PI);
                //physics.Velocity += obj.Transform.Up() * swiggle * wiggle * frameTime;
                // apply wiggle directly to the position and not as a velocity
                wiggle = obj.Transform.Up() * wangle * wamount * frameTime;
            }

            Debug::ShipVelocity = physics.Velocity;

            static int index = 0;
            static double refresh_time = 0.0;

            if (refresh_time == 0.0)
                refresh_time = Game::ElapsedTime;

            if (Input::IsKeyDown(Keys::NumPad8)) {
                if (index < Debug::ShipVelocities.size() && Game::ElapsedTime >= refresh_time) {
                    //while (refresh_time < Game::ElapsedTime) {
                    Debug::ShipVelocities[index] = physics.Velocity.Length();
                    refresh_time = Game::ElapsedTime + 1.0f / 60.0f;
                    index++;
                }
            }
            else {
                index = 0;
            }
        }


        obj.Transform.Translation(obj.Position() + physics.Velocity * frameTime + wiggle);
    }

    void UpdatePhysics(Level& level) {
        for (auto& obj : level.Objects) {
            if (obj.Movement.Type == MovementType::Physics) {
                ApplyPhysics(obj, obj.Movement.Physics, Render::FrameTime);
            }
        }
    }
}