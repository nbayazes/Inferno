#pragma once

namespace Inferno {
    // Animates a value using second order dynamics
    template <class T>
    class SecondOrderDynamics {
        // https://www.youtube.com/watch?v=KPoeNZZ6H4s
        float _k1, _k2, _k3;
        T _prevValue;
        T _y, _yd = {};

    public:
        // f: Frequency response speed
        // zeta: settling -> 0 is undamped. 0..1 underdamped. 1 > no vibration. 1 is critical dampening
        // r: response ramping. 0..1 input is delayed. 1: immediate response >1: overshoots target  <0 predicts movement
        SecondOrderDynamics(float f = 1, float z = 1, float r = 0, T initialValue = {})
            : _k1(z / (DirectX::XM_PI * f)),
              _k2(1 / std::pow(DirectX::XM_2PI * f, 2.0f)),
              _k3(r * z / (DirectX::XM_2PI * f)) {
            _prevValue = initialValue;
            _y = initialValue;
        }

        // Updates the value
        T Update(T value, T velocity, float dt /*delta time*/) {
            _y += dt * _yd; // integrate by velocity
            _yd += dt * (value + _k3 * velocity - _y - _k1 * _yd) / _k2; // integrate velocity by acceleration
            return _y;
        }

        // Updates the value using an estimated velocity
        T Update(T value, float dt) {
            T velocity = (value - _prevValue) / dt; // estimate velocity from previous state
            _prevValue = value;
            return Update(value, velocity, dt);
        }
    };
}
