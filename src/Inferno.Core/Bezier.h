#pragma once

#include "Types.h"

namespace Inferno {
    inline Vector3 DeCasteljausAlgorithm(float t, const Array<Vector3, 4>& points) {
        auto q = Vector3::Lerp(points[0], points[1], t);
        auto r = Vector3::Lerp(points[1], points[2], t);
        auto s = Vector3::Lerp(points[2], points[3], t);

        auto p2 = Vector3::Lerp(q, r, t);
        auto t2 = Vector3::Lerp(r, s, t);

        return Vector3::Lerp(p2, t2, t);
    }

    struct BezierCurve {
        Array<Vector3, 4> Points{};

        // Evaluates the curve at position t
        Vector3 Evaluate(float t) const {
            return DeCasteljausAlgorithm(t, Points);
        }

        // Estimate the curve length by summing many segment lengths
        float EstimateLength(int steps) const {
            float delta = 1 / (float)steps;
            Vector3 prevPos = Points[0];
            float length = 0;

            // Move along the curve
            for (int i = 1; i <= steps; i++) {
                float t = delta * (float)i;
                auto pos = Evaluate(t);
                length += Vector3::Distance(pos, prevPos);
                prevPos = pos;
            }

            return length;
        }
    };
}