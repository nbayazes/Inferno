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

    inline Vector3 DeCasteljausDerivative(const Array<Vector3, 4>& curve, float t) {
        Vector3 dU = t * t * (-3.0f * (curve[0] - 3.0f * (curve[1] - curve[2]) - curve[3]));
        dU += t * (6.0f * (curve[0] - 2.0f * curve[1] + curve[2]));
        dU += -3.0f * (curve[0] - curve[1]);
        return dU;
    }

    // Get an infinitely small length from the derivative of the curve at position t
    inline float GetArcLengthIntegrand(const Array<Vector3, 4>& curve, float t) {
        return DeCasteljausDerivative(curve, t).Length();
    }

    inline float GetLengthSimpsons(const Array<Vector3, 4>& curve, float tStart, float tEnd) {
        //This is the resolution and has to be even
        constexpr int n = 20;

        //Now we need to divide the curve into sections
        float delta = (tEnd - tStart) / (float)n;

        float endPoints = GetArcLengthIntegrand(curve, tStart) + GetArcLengthIntegrand(curve, tEnd);

        //Everything multiplied by 4
        float x4 = 0;
        for (int i = 1; i < n; i += 2) {
            float t = tStart + delta * i;
            x4 += GetArcLengthIntegrand(curve, t);
        }

        //Everything multiplied by 2
        float x2 = 0;
        for (int i = 2; i < n; i += 2) {
            float t = tStart + delta * i;
            x2 += GetArcLengthIntegrand(curve, t);
        }

        float length = (delta / 3.0f) * (endPoints + 4.0f * x4 + 2.0f * x2);
        return length;
    }

    //Use Newton–Raphsons method to find the t value at the end of this distance d
    inline float FindTValue(const Array<Vector3, 4>& curve, float dist, float totalLength) {
        float t = dist / totalLength;

        //Need an error so we know when to stop the iteration
        constexpr float error = 0.001f;
        int iterations = 0;

        while (true) {
            //Newton's method
            float tNext = t - (GetLengthSimpsons(curve, 0, t) - dist) / GetArcLengthIntegrand(curve, t);

            //Have we reached the desired accuracy?
            if (std::abs(tNext - t) < error)
                break;

            t = tNext;
            iterations += 1;

            if (iterations > 1000)
                break;
        }

        return t;
    }

    // Finds equally divided points along a Bezier curve regardless of handle positions
    inline List<Vector3> DivideCurveIntoSteps(const Array<Vector3, 4>& curve, int steps) {
        List<Vector3> result;
        float totalLength = GetLengthSimpsons(curve, 0, 1);

        float sectionLength = totalLength / (float)steps;
        float currentDistance = sectionLength;
        result.push_back(curve[0]); // start point

        for (int i = 1; i < steps; i++) {
            //Use Newton–Raphsons method to find the t value from the start of the curve 
            //to the end of the distance we have
            float t = FindTValue(curve, currentDistance, totalLength);

            //Get the coordinate on the Bezier curve at this t value
            Vector3 pos = DeCasteljausAlgorithm(t, curve);
            result.push_back(pos);

            //Add to the distance traveled on the line so far
            currentDistance += sectionLength;
        }

        result.push_back(curve[3]); // end point
        return result;
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