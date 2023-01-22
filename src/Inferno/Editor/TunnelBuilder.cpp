#include "pch.h"
#include "TunnelBuilder.h"
#include "Face.h"
#include "Events.h"
#include "Resources.h"
#include "Gizmo.h"
#include "Editor.Texture.h"
#include "Editor.Undo.h"
#include "Game.h"

namespace Inferno::Editor {
    constexpr long Factorial(const int n) {
        long f = 1;
        for (int i = 1; i <= n; ++i)
            f *= i;
        return f;
    }

    // returns n! / (i! * (n - i)!)
    constexpr float Coeff(int n, int i) {
        return (float)Factorial(n) / ((float)Factorial(i) * (float)Factorial(n - i));
    }

    // returns a weighted coefficient for each point
    float Blend(int i, int n, float u) {
        return Coeff(n, i) * powf(u, float(i)) * powf(1 - u, float(n - i));
    }

    Vector3 BezierCurve::Compute(float u) const {
        Vector3 v;

        for (int i = 0; i < 4; i++) {
            auto b = Blend(i, 3, u);
            v += Points[i] * b;
        }

        return v;
    }

    void BezierCurve::Transform(const Matrix& m) const {
        for (int i = 0; i < 4; i++)
            Vector3::Transform(Points[i], m);
    }

    Vector3 DeCasteljausAlgorithm(float t, Array<Vector3, 4> points) {
        auto q = Vector3::Lerp(points[0], points[1], t);
        auto r = Vector3::Lerp(points[1], points[2], t);
        auto s = Vector3::Lerp(points[2], points[3], t);

        auto p2 = Vector3::Lerp(q, r, t);
        auto t2 = Vector3::Lerp(r, s, t);

        return Vector3::Lerp(p2, t2, t);
    }

    TunnelNode CreateNode(Level& level, PointTag source, float sign) {
        TunnelNode node;

        //node.Tag = source;
        auto face = Face::FromSide(level, source);
        node.Normal = face.AverageNormal() * sign;
        node.Vertices = face.CopyPoints();
        node.Point = face.Center();

        // get the opposite vertex of the side?
        //pSegment->CreateOppVertexIndex(m_sideKey.m_nSide, m_oppVertexIndex);
        //auto m_point = pSegment->ComputeCenter(m_sideKey.m_nSide);

        //auto forward = node.Normal;
        //auto r = face.VectorForEdge(source.Point) * sign;
        node.Up = face[(source.Point + 1) % 4] - face[source.Point];
        node.Up.Normalize();

        // As far as I can tell we do have to do this to allow users to pick matching lines at each end
        // (in some cases there can be no good "opposite" line)
        //right *= -sign;

        //auto up = node.Normal.Cross(right);
        //up.Normalize();
        //node.Up = up;

        //DebugTunnelPoints.push_back(face.GetEdgeMidpoint(source.Point));

        node.Rotation = Matrix::CreateWorld(Vector3::Zero, node.Normal, node.Up);

        //node.Rotation.Forward(node.Normal);
        //node.Rotation.Right(right);
        //node.Rotation.Up(up);
        return node;
    }

    // Summed length to a given step
    float PathLength(span<PathNode> nodes, int steps) {
        float length = 0.0;

        for (int i = 1; i <= steps; i++)
            length += (nodes[i].Position - nodes[i - 1].Position).Length();

        return length;
    }

    // Used to modify the end rotation...
    float TotalTwist(const TunnelNode& start, const TunnelNode& end) {
        // revert the end orientation's z rotation in regard to the start orientation by 
        // determining the angle of the two matrices' z axii (forward vectors) and rotating
        // the end matrix around the perpendicular of the two matrices' z axii.

        // Angle between vectors?
        auto dot = end.Rotation.Forward().Dot(start.Rotation.Forward());
        dot = std::clamp(dot, -1.0f, 1.0f);
        auto bendAngle = acos(dot);

        Vector3 rotAxis;
        if (bendAngle > M_PI - 1e-6) {
            // Angle is close to 180 degrees, which means the rotation axis could be anything
            // perpendicular to the forward vector. We'll pick an axis also perpendicular to the
            // displacement between the two ends of the corridor.
            Vector3 displacement = end.Point - start.Point;
            if (displacement.Length() > 1e-3) {
                rotAxis = displacement.Cross(-start.Rotation.Forward());
            }
            else {
                // No or small displacement - the tunnel maker probably shouldn't be started
                // but just in case - we could pick anything so we'll just pick the start
                // end's up vector
                rotAxis = start.Rotation.Up();
            }
        }
        else if (bendAngle > 1e-6) {
            rotAxis = end.Rotation.Forward().Cross(-start.Rotation.Forward());
        }

        auto endRotation = end.Rotation.Right();

        if (bendAngle > 1e-6 && rotAxis.Length() > 1e-6) {
            // dot >= 0.999999 ~ parallel
            // Construct quaternion from the axis and angle, and "undo" the end orientation's
            // bend so it is parallel with the start face. We only need the R vector to
            // determine rotation.
            auto q = Quaternion::CreateFromAxisAngle(rotAxis, -bendAngle);
            endRotation = Vector3::Transform(endRotation, q);
            endRotation.Normalize();
        }

        // Calculate rotation using atan2 (so we can get the direction at the same time).
        // y = projection of transformed R vector on start U vector
        // x = projection of transformed R vector on start R vector
        auto y = end.Rotation.Right().Dot(start.Rotation.Up());
        auto x = end.Rotation.Right().Dot(start.Rotation.Right());
        return atan2(y, x);
    }

    void Bend(const PathNode& n0, PathNode& n1) {
        // rotate the previous matrix around the perpendicular of the previous and the current forward vector
        // to orient it properly for the current path node
        auto dot = n0.Rotation.Forward().Dot(n1.Rotation.Forward()); // angle of current and previous forward vectors
        if (dot >= 0.999999f) {
            // dot >= 1e-6 ~ parallel
            n1.Rotation.Right(n0.Rotation.Right()); // rotate right and up vectors accordingly
            n1.Rotation.Up(n0.Rotation.Up());
        }
        else if (dot <= -0.999999f) {
            // dot >= 1e-6 ~ parallel
            n1.Rotation.Right(-n0.Rotation.Right()); // rotate right and up vectors accordingly
            n1.Rotation.Up(n0.Rotation.Up());
        }
        else {
            //#ifdef _DEBUG
            //            CDoubleVector v0(n1->m_rotation.F());
            //            CDoubleVector v1(/*(dot < 0.0) ? n0->m_rotation.F () :*/ -n0->m_rotation.F());
            //            double a = acos(dot);
            //            CDoubleVector axis = CrossProduct(v0, v1);
            //            axis.Normalize();
            //            CDoubleVector vi;
            //            axis = Perpendicular(vi, v0, v1);
            //            axis.Normalize();
            //#endif

            auto bendAngle = acos(dot);

            n1.Axis = n1.Rotation.Forward().Cross(-n0.Rotation.Forward());
            n1.Axis.Normalize();
            if (n1.Axis.Length() > 0) {
                auto q = Quaternion::CreateFromAxisAngle(n1.Axis, bendAngle);
                auto fVec = Vector3::Transform(n0.Rotation.Forward(), q);
                dot = fVec.Dot(n1.Rotation.Forward());
                if (dot < 0.999f)
                    bendAngle += acos(dot);

                q = Quaternion::CreateFromAxisAngle(n1.Axis, bendAngle);

                // rotate right and up vectors accordingly
                auto right = Vector3::Transform(n0.Rotation.Right(), q);
                auto up = Vector3::Transform(n0.Rotation.Up(), q);
                right.Normalize();
                up.Normalize();
                n1.Rotation.Right(right);
                n1.Rotation.Up(up);
            }
        }
    }

    void Twist(const PathNode& n0, PathNode& n1, float pathDeltaAngle, float scale) {
        if (!EnableTunnelTwist) return;

        // twist the current matrix around the forward vector 
        n1.Angle = pathDeltaAngle * scale;
        auto axis = n1.Rotation.Backward();

        if (axis.Length() == 0)
            return;

        if (fabs(n1.Angle - n0.Angle) > 1e-6) {
            auto q = Quaternion::CreateFromAxisAngle(axis, n1.Angle - n0.Angle);
            auto right = Vector3::Transform(n1.Rotation.Right(), q);
            auto up = Vector3::Transform(n1.Rotation.Up(), q);
            right.Normalize();
            up.Normalize();
            n1.Rotation.Right(right);
            n1.Rotation.Up(up);
        }
    }

    // Estimate the length of a curve by taking segment lengths
    float EstimateCurveLength(const BezierCurve2& curve, int steps) {
        float delta = 1 / (float)steps;
        Vector3 lastPos = curve.Points[0];
        float length = 0;

        //Move along the curve
        for (int i = 1; i <= steps; i++) {
            float t = delta * (float)i;

            auto pos = DeCasteljausAlgorithm(t, curve.Points);
            length += Vector3::Distance(pos, lastPos);
            lastPos = pos;
        }

        return length;
    }

    float EstimateCurveLength(const BezierCurve2& curve, float tStart, float tEnd, int steps) {
        //Divide the curve into sections
        float delta = (tEnd - tStart) / (float)steps;

        //The start position of the curve
        Vector3 lastPos = DeCasteljausAlgorithm(tStart, curve.Points);
        float length = 0;

        //Move along the curve
        for (int i = 1; i <= steps; i++) {
            //Calculate the t value at this section
            float t = tStart + delta * (float)i;
            Vector3 pos = DeCasteljausAlgorithm(t, curve.Points);
            length += Vector3::Distance(pos, lastPos);
            lastPos = pos;
        }

        return length;
    }

    Vector3 DeCasteljausDerivative(const Array<Vector3, 4>& curve, float t) {
        Vector3 dU = t * t * (-3.0f * (curve[0] - 3.0f * (curve[1] - curve[2]) - curve[3]));
        dU += t * (6.0f * (curve[0] - 2.0f * curve[1] + curve[2]));
        dU += -3.0f * (curve[0] - curve[1]);
        return dU;
    }

    // Get an infinitely small length from the derivative of the curve at position t
    float GetArcLengthIntegrand(const Array<Vector3, 4>& curve, float t) {
        return DeCasteljausDerivative(curve, t).Length();
    }

    float GetLengthSimpsons(const Array<Vector3, 4>& curve, float tStart, float tEnd) {
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
    float FindTValue(const Array<Vector3, 4>& curve, float dist, float totalLength) {
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

    List<Vector3> DivideCurveIntoSteps(const Array<Vector3, 4>& curve, int steps) {
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

    TunnelPath CreatePath(const TunnelNode& start, const TunnelNode& end, int steps, float startLength, float endLength) {
        BezierCurve2 curve2;
        curve2.Points[0] = start.Point;
        curve2.Points[1] = start.Point + start.Normal * startLength;
        curve2.Points[2] = end.Point - end.Normal * endLength;
        curve2.Points[3] = end.Point;
        auto bezierPoints = DivideCurveIntoSteps(curve2.Points, steps);

        TunnelBuilderHandles = curve2;

        TunnelPath path{};

        auto& nodes = path.Nodes;
        nodes.resize(steps + 1);

        for (int i = 0; i < nodes.size(); i++)
            nodes[i].Position = bezierPoints[i] /*- start.Point*/;

        nodes[0].Rotation = start.Rotation;
        nodes[0].Axis = start.Rotation.Right();
        nodes[0].Vertices = start.Vertices;
        nodes[steps].Rotation = end.Rotation;

        //auto deltaAngle = TotalTwist(start, end);
        // change of basis
        auto offset = Matrix::CreateTranslation(start.Point);
        //auto startTransform = Matrix::CreateWorld(start.Point, start.Normal, start.Up) - offset;
        //auto endTransform = Matrix::CreateWorld(end.Point, end.Normal, end.Up) - offset;
        //endTransform = endTransform.Invert();

        auto startTransform = Matrix::CreateWorld(Vector3::Zero, start.Normal, start.Up);
        auto endTransform = Matrix::CreateWorld(Vector3::Zero, end.Normal, end.Up) * Matrix::CreateFromAxisAngle(end.Normal, DirectX::XM_PI);
        auto rotation = endTransform.Invert() * startTransform;
        auto euler = rotation.ToEuler();

        Matrix transform = Matrix::CreateTranslation(-end.Point) * rotation * Matrix::CreateTranslation(start.Point);

        auto totalLength = PathLength(nodes, steps);

        std::array<Vector3, 4> deltaShift{}; // amount of vertex change between each frame
        std::array<Vector3, 4> baseFrame{}; // start frame shifted to origin

        for (int i = 0; i < 4; i++) {
            //shiftedEnd[i] = Vector3::Transform(end.Vertices[i], transform);
            baseFrame[i] = start.Vertices[i] - start.Point;
            deltaShift[i] = Vector3::Transform(end.Vertices[3 - i], transform) - start.Vertices[i];
            DebugTunnelLines.push_back(Vector3::Transform(end.Vertices[i], transform) + start.Normal * totalLength);
            DebugTunnelLines.push_back(Vector3::Transform(end.Vertices[(i + 1) % 4], transform) + start.Normal * totalLength);
        }

        //auto upBasis = Vector3::Transform(end.Up, rotation);
        //auto angle = AngleBetweenVectors(start.Up, upBasis);

        //auto angleStep = deltaAngle / steps;

        // DEBUG
        //for (int i = 1; i < nodes.size(); i++)
        //    nodes[i].Position = curve2.Points[0] + start.Normal * (totalLength / steps) * i;

        //auto positionStep = start.Normal * (totalLength / steps);

        for (int i = 1; i <= steps; i++) {
            auto& n0 = nodes[i - 1];
            auto& n1 = nodes[i];


            //n1.Rotation = n0.Rotation;
            //auto m = Matrix::CreateFromAxisAngle(start.Normal, angleStep);
            //n1.Rotation *= Matrix::CreateFromAxisAngle(start.Normal, angleStep);
            //n1.Rotation *= m;

            // Set verts from rotation and position
            for (int j = 0; j < 4; j++) {
                // use the base cross section shifted to origin
                // 1. apply cross section morph
                // 2. rotate in place towards the next point on curve
                // 3. move to the node position

                n1.Vertices[j] = n1.Position + baseFrame[j] /*+ n0.Vertices[j]*/ + deltaShift[j] * i / steps /*+ positionStep*/;

                // n1.Position
            }

            if (i < steps) {
                // prev matrix is the end side's matrix - use its forward vector
                auto forward = n1.Position - n0.Position;
                forward.Normalize();
                n1.Rotation.Forward(forward);
            }
            //Bend(n0, n1);
            //Twist(n0, n1, deltaAngle, PathLength(nodes, i) / totalLength);
        }

        // Rotating the r and u vectors can cause an error because a x and y rotation may be applied. It would certainly
        // be possible to fix that, but I have tormented my brain enough. Computing the error and rotating the vectors 
        // accordingly works well enough.
        //float error[2] = { 0.0, 0.0 };
        //auto direction = -Sign(end.Rotation.Up().Dot(nodes[steps].Rotation.Right()));

        //int maxIter = 50;

        //while (fabs(error[1] = acos(end.Rotation.Right().Dot(nodes[steps].Rotation.Right())) * direction) > 0.01) {
        //    if ((error[0] != 0.0) && (fabs(error[1]) > error[0]))
        //        direction = -direction;
        //    error[0] = fabs(error[1]);

        //    for (int i = steps; i > 0; i--) {
        //        auto& node = nodes[i];
        //        auto angle = error[1] * PathLength(nodes, i) / totalLength;
        //        auto q = Quaternion::CreateFromAxisAngle(node.Rotation.Forward(), angle);

        //        node.Rotation.Right(Vector3::Transform(node.Rotation.Right(), q));
        //        node.Rotation.Up(Vector3::Transform(node.Rotation.Up(), q));
        //    }

        //    if (maxIter-- <= 0) {
        //        SPDLOG_WARN("Reached max iterations in CreatePath()");
        //        break;
        //    }
        //}

        return path;
    }

    void CreateTunnelSegments(Level& level, const TunnelPath& path, PointTag start, PointTag end) {
        // todo: check that tunnel is valid
        Tag last = start;
        auto startIndices = level.GetSegment(start).GetVertexIndices(start.Side);
        //auto endIndices = level.GetSegment(end).GetVertexIndices(end.Side);

        Marked.Segments.clear();

        auto vertIndex = (uint16)level.Vertices.size(); // take index before adding new points

        // copy vertices, skip start side
        level.Vertices.insert(level.Vertices.end(),
                              TunnelBuilderPoints.begin() + startIndices.size(),
                              TunnelBuilderPoints.begin() + startIndices.size() * path.Nodes.size());

        for (size_t nNode = 1; nNode < path.Nodes.size(); nNode++) {
            auto& lastSeg = level.GetSegment(last);

            Segment seg = {};
            auto id = (SegID)level.Segments.size();

            auto oppositeSide = (int)GetOppositeSide(last.Side);
            seg.Connections[oppositeSide] = last.Segment;
            lastSeg.Connections[(int)last.Side] = id;

            auto srcIndices = lastSeg.GetVertexIndices(last.Side);
            auto& srcVertIndices = SIDE_INDICES[oppositeSide];
            auto& newVertIndices = SIDE_INDICES[(int)last.Side];
            seg.Indices[srcVertIndices[3]] = srcIndices[0];
            seg.Indices[srcVertIndices[2]] = srcIndices[1];
            seg.Indices[srcVertIndices[1]] = srcIndices[2];
            seg.Indices[srcVertIndices[0]] = srcIndices[3];

            seg.Indices[newVertIndices[0]] = vertIndex + 0;
            seg.Indices[newVertIndices[1]] = vertIndex + 1;
            seg.Indices[newVertIndices[2]] = vertIndex + 2;
            seg.Indices[newVertIndices[3]] = vertIndex + 3;
            vertIndex += (uint16)startIndices.size();

            // copy textures
            for (int i = 0; i < 6; i++) {
                auto& side = seg.Sides[i];
                side.TMap = lastSeg.Sides[i].TMap;
                side.TMap2 = lastSeg.Sides[i].TMap2;
                side.OverlayRotation = lastSeg.Sides[i].OverlayRotation;
                //side.UVs = lastSeg.Sides[i].UVs;

                // Clear door textures
                if (Resources::GetWallClipID(side.TMap) != WClipID::None)
                    side.TMap = LevelTexID::Unset;

                if (Resources::GetWallClipID(side.TMap2) != WClipID::None)
                    side.TMap2 = LevelTexID::Unset;
            }

            seg.UpdateGeometricProps(level);

            level.Segments.push_back(seg);
            last.Segment = id;
            ResetUVs(level, id);
            Marked.Segments.insert(id);
        }

        Editor::History.SnapshotLevel("Create Tunnel");
        Events::SegmentsChanged();
        Events::LevelChanged();
    }

    void CreateDebugPath(TunnelPath& path) {
        TunnelBuilderPath.clear();

        for (auto& node : path.Nodes) {
            TunnelBuilderPath.push_back(node.Position);
        }
    }

    void ClearTunnel() {
        TunnelBuilderPath.clear();
        TunnelBuilderPoints.clear();
        DebugTunnelPoints.clear();
        DebugTunnel.Nodes.clear();
        DebugTunnelLines.clear();
    }

    void CreateTunnel(Level& level, PointTag start, PointTag end, int steps, float startLength, float endLength) {
        if (!level.SegmentExists(start) || !level.SegmentExists(end))
            return;

        ClearTunnel();
        TunnelStart = start;
        TunnelEnd = end;

        // clamp inputs
        if (steps < 1) steps = 1;
        startLength = std::clamp(startLength, MinTunnelLength, MaxTunnelLength);
        endLength = std::clamp(endLength, MinTunnelLength, MaxTunnelLength);

        auto startNode = CreateNode(level, start, -1);
        auto endNode = CreateNode(level, end, 1);
        auto path = CreatePath(startNode, endNode, steps, startLength, endLength);
        //path.StartVertices = startNode.Vertices; 
        auto startVertices = startNode.Vertices; // should be verts of all selected faces, not just the first

        // Pre-calculate morph vectors if necessary since we'll need to use them a lot
        List<Vector3> vMorph;
        List<ushort> nVertexMap;
        nVertexMap.resize(4);

        bool morph = startVertices.size() == 4; // morph only works with single selected face
        morph = false;
        //auto startIndices = level.GetSegment(start).GetVertexIndices(start.Side);

        if (morph) {
            //Matrix startRotation = startNode.Rotation.Invert();
            Matrix startRotation = startNode.Rotation;
            Matrix endRotation = endNode.Rotation;
            //endRotation.Right(-endRotation.Right());
            //endRotation.Up(-endRotation.Up());

            for (ushort i = 0; i < 4; i++) {
                ushort startIndex = (start.Point + i) % 4;
                Vector3 vStart = startNode.Vertices[startIndex];
                //DebugTunnelPoints.push_back(vStart);
                vStart -= startNode.Point; // shift to tunnel start
                vStart = Vector3::Transform(vStart, startRotation.Invert()); // un-rotate

                ushort endIndex = (end.Point + 5 - i) % 4;
                Vector3 vEnd = endNode.Vertices[endIndex];
                //DebugTunnelPoints.push_back(vEnd);
                vEnd -= endNode.Point; // shift to tunnel end
                vEnd = Vector3::Transform(vEnd, endRotation.Invert() /** startRotation*/); // un-rotate

                vMorph.push_back(vEnd - vStart);
                //DebugTunnelPoints.push_back(vStart);
                //DebugTunnelPoints.push_back(vEnd + Vector3::UnitZ * 10);

                nVertexMap[startIndex] = endIndex;

                // map the vertex back to the starting vertex
                //for (ushort nStartVertex = 0; nStartVertex < 4; nStartVertex++)
                //    if (startIndex == startIndices[nStartVertex]) {
                //        nVertexMap[nStartVertex] = i;
                //        break;
                //    }
            }
        }

        // Compute all tunnel vertices by rotating the base vertices using each path node's orientation (== rotation matrix)
        // The rotation is relative to the base coordinate system (identity matrix), but the vertices are relative to the 
        // start point and start rotation, so each vertex has to be un-translated and un-rotated before rotating and translating
        // it with the current path node's orientation matrix and position.
        //List<Vector3> vertices;

        for (int nSegment = 0; nSegment <= steps; nSegment++) {
            //Matrix rotation = path.Nodes[nSegment].Rotation.Invert();
            //auto& translation = path.Nodes[nSegment].Vertex;

            //for (uint nVertex = 0; nVertex < startVertices.size(); nVertex++) {
            //    Vector3 v = startVertices[nVertex];
            //    v -= startNode.Point; // un-translate (make relative to tunnel start)
            //    v = Vector3::Transform(v, startNode.Rotation.Transpose()); // un-rotate

            //    if (morph) {
            //        float amount = (float)nSegment / (float)steps;
            //        //v += vMorph[nVertexMap[nVertex]] * amount;
            //        v += vMorph[nVertex] * amount;
            //    }

            //    v = Vector3::Transform(v, rotation.Transpose());
            //    v += translation;
            //    //vertices.push_back(v);
            //    TunnelBuilderPoints.push_back(v);
            //}

            for (int i = 0; i < 4; i++) {
                TunnelBuilderPoints.push_back(path.Nodes[nSegment].Vertices[i]);
            }

        }

        CreateDebugPath(path);
        DebugTunnel = path;
    }
}
