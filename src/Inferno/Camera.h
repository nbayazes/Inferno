#pragma once

#include "Types.h"

namespace Inferno {
    using DirectX::SimpleMath::Viewport;
    using DirectX::SimpleMath::Quaternion;

    // Returns a frustum for the perspective view
    inline DirectX::BoundingFrustum GetFrustum(const Vector3& position, const Matrix& view, const Matrix& projection) {
        DirectX::BoundingFrustum frustum;
        DirectX::BoundingFrustum::CreateFromMatrix(frustum, projection);
        DirectX::XMVECTOR s, r, t;
        DirectX::XMMatrixDecompose(&s, &r, &t, view);
        r = DirectX::XMQuaternionInverse(r);
        frustum.Transform(frustum, 1.0f, r, position);
        return frustum;
    }

    // Descent uses LH coordinate system
    class Camera {
        Vector3 _lerpStart, _lerpEnd;
        float _lerpTime{}, _lerpDuration{};
        float _shake = 0;
        float _pendingShake = 0;
        float _fovDeg = 60; // FOV in degrees
        bool _changed = false;
        Viewport _viewport = { 0, 0, 1024, 768, 1, 3000 };

    public:
        Vector3 Position = { 40, 0, 0 };
        Matrix View;
        Matrix Projection;
        Matrix InverseProjection;

        Vector3 Target = Vector3::Zero;
        Vector3 Up = Vector3::UnitY;

        float MinimumZoom = 5; // closest the camera can get to the target

        DirectX::BoundingFrustum Frustum;
        Matrix ViewProjection;

        Vector2 GetViewportSize() const {
            return { _viewport.width, _viewport.height };
        }

        void SetViewport(Vector2 size) {
            if (size == GetViewportSize()) return;
            _viewport.width = size.x;
            _viewport.height = size.y;
            _changed = true;
        }

        void SetClipPlanes(float nearClip, float farClip) {
            if (_viewport.minDepth == nearClip && _viewport.maxDepth == farClip) return;
            _viewport.minDepth = nearClip;
            _viewport.maxDepth = farClip;
            _changed = true;
        }

        //const Viewport& Viewport() const { return _viewport; }

        void SetFov(float fovDeg) {
            if (fovDeg == _fovDeg) return;
            _fovDeg = fovDeg;
            _changed = true;
        }

        void SetPosition(const Vector3& position) {
            if (Position == position) return;
            Position = position;
            _changed = true;
        }

        float GetNearClip() const { return _viewport.minDepth; }
        float GetFarClip() const { return _viewport.maxDepth; }

        //const Vector3& Position() const { return _position; }

        //void LookAtPerspective(float fovDeg) {
        //    View = DirectX::XMMatrixLookAtLH(_position, Target, Up);
        //    Projection = DirectX::XMMatrixPerspectiveFovLH(fovDeg * DegToRad, _viewport.AspectRatio(), _viewport.minDepth, _viewport.maxDepth);
        //    InverseProjection = Projection.Invert();
        //}

        //void LookAtOrthographic() {
        //    //View = Matrix::CreateLookAt(Position, Target, Up);
        //    View = DirectX::XMMatrixLookAtLH(_position, Target, Up);
        //    Projection = Matrix::CreateOrthographicOffCenter(0, _viewport.width, _viewport.height, 0, _viewport.minDepth, _viewport.maxDepth);
        //    InverseProjection = Projection.Invert();
        //}

        Matrix3x3 GetOrientation() const { return Matrix3x3(GetForward(), Up); }

        void MoveTo(const Vector3& position, const Vector3& target, const Vector3& up) {
            if(position == Position && target == Target && up == Up) return;
            Position = position;
            Target = target;
            Up = up;
            _changed = true;
        }

        void Rotate(float yaw, float pitch) {
            _changed = true;
            auto qyaw = Quaternion::CreateFromAxisAngle(Up, yaw);
            auto qpitch = Quaternion::CreateFromAxisAngle(GetRight(), pitch);

            Vector3 offset = Target - Position;
            Target = Vector3::Transform(offset, qyaw * qpitch) + Position;
            Up = Vector3::Transform(Up, qpitch);
            Up.Normalize();
        }

        void Roll(float roll) {
            _changed = true;
            auto qroll = Quaternion::CreateFromAxisAngle(GetForward(), roll * 2);
            Up = Vector3::Transform(Up, qroll);
            Up.Normalize();
        }

        // Orbits around the target point
        void Orbit(float yaw, float pitch) {
            _changed = true;
            Vector3 offset = Position - Target;
            auto qyaw = Quaternion::CreateFromAxisAngle(Up, yaw);
            auto qpitch = Quaternion::CreateFromAxisAngle(Up.Cross(offset), -pitch);

            Position = Vector3::Transform(offset, qyaw * qpitch) + Target;
            Up = Vector3::Transform(Up, qpitch);
            Up.Normalize();
        }

        Vector3 GetForward() const {
            auto forward = Target - Position;
            forward.Normalize();
            return forward;
        }

        Vector3 GetRight() const {
            auto right = GetForward().Cross(Up);
            right.Normalize();
            return right;
        }

        void Pan(float horizontal, float vertical) {
            _changed = true;
            auto right = GetRight();
            auto translation = right * horizontal + Up * vertical;
            Target += translation;
            Position += translation;
        }

        void MoveForward(float frameTime) {
            _changed = true;
            auto step = GetForward() * frameTime;
            Position += step;
            Target += step;
        }

        void MoveBack(float frameTime) {
            _changed = true;
            auto step = -GetForward() * frameTime;
            Position += step;
            Target += step;
        }

        void MoveLeft(float frameTime) {
            _changed = true;
            auto step = GetRight() * frameTime;
            Position += step;
            Target += step;
        }

        void MoveRight(float frameTime) {
            _changed = true;
            auto step = -GetRight() * frameTime;
            Position += step;
            Target += step;
        }

        void MoveUp(float frameTime) {
            _changed = true;
            auto step = Up * frameTime;
            Position += step;
            Target += step;
        }

        void MoveDown(float frameTime) {
            _changed = true;
            auto step = -Up * frameTime;
            Position += step;
            Target += step;
        }

        void Zoom(const float& value) {
            _changed = true;
            Vector3 delta = Target - Position;
            delta.Normalize();
            // add the value along the direction of the vector
            Vector3 pos = Position + delta * value;

            if (Vector3::Distance(pos, Target) > MinimumZoom)
                Position = pos;
        }

        void ZoomIn() {
            _changed = true;
            auto delta = Target - Position;
            auto direction = delta;
            direction.Normalize();

            // scale zoom amount based on distance from target
            auto value = std::clamp(delta.Length() / 6, MinimumZoom, 100.0f);
            Vector3 pos = Position + direction * value;

            if (Vector3::Distance(pos, Target) > MinimumZoom)
                Position = pos;
        }

        void ZoomOut() {
            _changed = true;
            auto delta = Target - Position;
            auto direction = delta;
            direction.Normalize();

            // scale zoom amount based on distance from target
            auto value = std::clamp(delta.Length() / 6, 10.0f, 100.0f);
            Vector3 pos = Position - direction * value;
            Position = pos;
        }

        // Unprojects a screen coordinate into world space along the near plane
        Vector3 Unproject(Vector2 screen, const Matrix& world = Matrix::Identity) const {
            return _viewport.Unproject({ screen.x, screen.y, 0 }, Projection, View, world);
        }

        Ray UnprojectRay(Vector2 screen, const Matrix& world = Matrix::Identity) const {
            auto direction = Unproject(screen, world) - Position;
            direction.Normalize();
            return { Position, direction };
        }

        // Projects a world coordinate into screen space
        Vector3 Project(Vector3 p, const Matrix& world = Matrix::Identity) const {
            return _viewport.Project(p, Projection, View, world);
        }

        void MoveTo(const Vector3& target) {
            auto translation = target - Target;
            Position += translation;
            Target += translation;
            _changed = true;
        }

        void LerpTo(const Vector3& target, float duration) {
            _lerpDuration = duration;
            _lerpTime = 0;
            _lerpEnd = target;
            _lerpStart = Target;
            _changed = true;
        }

        void Update(float dt) {
            float shake = _pendingShake * dt * 4;
            _pendingShake -= shake;
            if (_pendingShake < 0) _pendingShake = 0;

            constexpr float DECAY_SPEED = 5;
            _shake += shake - DECAY_SPEED * dt;
            if (_shake < 0) _shake = 0;

            if (_lerpTime < _lerpDuration) {
                _lerpTime += dt;
                auto t = _lerpTime / _lerpDuration;
                auto lerp = Vector3::Lerp(_lerpStart, _lerpEnd, t);
                MoveTo(lerp);
            }
        }

        void UpdatePerspectiveMatrices() {
            if (!_changed) return;
            View = DirectX::XMMatrixLookAtLH(Position, Target, Up);
            Projection = DirectX::XMMatrixPerspectiveFovLH(_fovDeg * DegToRad, _viewport.AspectRatio(), _viewport.minDepth, _viewport.maxDepth);
            ViewProjection = View * Projection;
            InverseProjection = Projection.Invert();
            Frustum = GetFrustum(Position, View, Projection);
            _changed = false;
        }

        void Shake(float amount) {
            _pendingShake += amount;

            //const float maxShakeAngle = DirectX::XM_PI / 128 * _shake;

            //constexpr float period = 15;

            //Vector3 shakeOffset(
            //    _shake * OpenSimplex2::Noise2(0, t * period, 0.0),
            //    _shake * OpenSimplex2::Noise2(0, t * period, t),
            //    _shake * OpenSimplex2::Noise2(0, t * period, -t)
            //);

            //Vector3 rot(
            //    maxShakeAngle * OpenSimplex2::Noise2(0, t * period, 0.0),
            //    maxShakeAngle * OpenSimplex2::Noise2(0, t * period, t),
            //    maxShakeAngle * OpenSimplex2::Noise2(0, t * period, -t)
            //);

            //auto up = Up + rot;
            //up.Normalize();

            //View = DirectX::XMMatrixLookAtLH(Position + shakeOffset, Target + shakeOffset, up);
        }
    };
}
