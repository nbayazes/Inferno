#include "pch.h"
#include "TunnelBuilder.h"
#include "Face.h"
#include "Events.h"
#include "Resources.h"
#include "Gizmo.h"
#include "Editor.Texture.h"

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

    Vector3 BezierCurve::Compute(float u) {
        Vector3 v;

        for (int i = 0; i < 4; i++) {
            auto b = Blend(i, 3, u);
            v += Points[i] * b;
        }

        return v;
    }

    void BezierCurve::Transform(const Matrix& m) {
        for (int i = 0; i < 4; i++)
            Vector3::Transform(Points[i], m);
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
        auto right = face[(source.Point + 1) % 4] - face[source.Point];
        right.Normalize();

        // As far as I can tell we do have to do this to allow users to pick matching lines at each end
        // (in some cases there can be no good "opposite" line)
        right *= -sign;

        auto up = node.Normal.Cross(right);
        up.Normalize();

        node.Rotation.Forward(node.Normal);
        node.Rotation.Right(right);
        node.Rotation.Up(up);
        return node;
    }

    // Summed length to a given step
    float PathLength(span<PathNode> nodes, int steps) {
        float length = 0.0;

        for (int i = 1; i <= steps; i++)
            length += (nodes[i].Vertex - nodes[i - 1].Vertex).Length();

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

        if (bendAngle > 1e-6 && rotAxis.Length() > 1e-6) { // dot >= 0.999999 ~ parallel
            // Construct quaternion from the axis and angle, and "undo" the end orientation's
            // bend so it is parallel with the start face. We only need the R vector to
            // determine rotation.
            auto q = Quaternion::CreateFromAxisAngle(rotAxis, -bendAngle);
            auto right = Vector3::Transform(endRotation, q);
            right.Normalize();
            endRotation = right;
            // previously would modify the end rotation here but that seems suspicious
            //end.Rotation.Right(right);
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
        if (dot >= 0.999999f) { // dot >= 1e-6 ~ parallel
            n1.Rotation.Right(n0.Rotation.Right()); // rotate right and up vectors accordingly
            n1.Rotation.Up(n0.Rotation.Up());
        }
        else if (dot <= -0.999999f) { // dot >= 1e-6 ~ parallel
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
        // if !twist return

        // twist the current matrix around the forward vector 
        n1.Angle = pathDeltaAngle * scale;
        auto axis = n1.Rotation.Forward();

        if (fabs(n1.Angle - n0.Angle) > 1e-6 && axis.Length() > 0) {
            auto q = Quaternion::CreateFromAxisAngle(axis, n1.Angle - n0.Angle);
            auto right = Vector3::Transform(n1.Rotation.Right(), q);
            auto up = Vector3::Transform(n1.Rotation.Up(), q);
            right.Normalize();
            up.Normalize();
            n1.Rotation.Right(right);
            n1.Rotation.Up(up);
        }
    }

    TunnelPath CreatePath(const TunnelNode& start, const TunnelNode& end, int steps, float startLength, float endLength) {
        BezierCurve curve{};
        curve.Length[0] = startLength;
        curve.Length[1] = endLength;

        // setup intermediate points for a cubic bezier curve
        curve.Points = {
            start.Point,
            start.Point + start.Normal * startLength,
            end.Point - end.Normal * endLength,
            end.Point
        };

        TunnelPath path{};
        path.Curve = curve;

        auto& nodes = path.Nodes;
        nodes.resize(steps + 1);

        for (int i = 0; i <= steps; i++)
            nodes[i].Vertex = curve.Compute((float)i / (float)steps);

        nodes[0].Rotation = start.Rotation;
        nodes[steps].Rotation = end.Rotation;
        nodes[0].Axis = start.Rotation.Right();

        auto deltaAngle = TotalTwist(start, end);
        auto totalLength = PathLength(nodes, steps);

        //PathNode* n0 = nullptr;
        //PathNode* n1 = &nodes[0];

        //for (int i = 1; i <= steps; i++) {
        //    n0 = n1;
        //    n1 = &nodes[i];

        //    if (i < steps) { // last matrix is the end side's matrix - use it's forward vector
        //        auto forward = nodes[i + 1].Vertex - nodes[i - 1].Vertex;
        //        forward.Normalize();
        //        n1->Rotation.Forward(forward);
        //    }
        //    Bend(*n0, *n1);
        //    Twist(*n0, *n1, deltaAngle, PathLength(nodes, i) / totalLength);
        //}

        for (int i = 1; i <= steps; i++) {
            auto& n0 = nodes[i - 1];
            auto& n1 = nodes[i];
            if (i < steps) { // last matrix is the end side's matrix - use it's forward vector
                auto forward = nodes[i + 1].Vertex - nodes[i - 1].Vertex;
                forward.Normalize();
                n1.Rotation.Forward(forward);
            }
            Bend(n0, n1);
            Twist(n0, n1, deltaAngle, PathLength(nodes, i) / totalLength);
        }

        // Rotating the r and u vectors can cause an error because a x and y rotation may be applied. It would certainly
        // be possible to fix that, but I have tormented my brain enough. Computing the error and rotating the vectors 
        // accordingly works well enough.
        float error[2] = { 0.0, 0.0 };
        auto direction = -Sign(end.Rotation.Up().Dot(nodes[steps].Rotation.Right()));

        int maxIter = 50;

        while (fabs(error[1] = acos(end.Rotation.Right().Dot(nodes[steps].Rotation.Right())) * direction) > 0.01) {
            if ((error[0] != 0.0) && (fabs(error[1]) > error[0]))
                direction = -direction;
            error[0] = fabs(error[1]);

            for (int i = steps; i > 0; i--) {
                auto& node = nodes[i];
                auto angle = error[1] * PathLength(nodes, i) / totalLength;
                auto q = Quaternion::CreateFromAxisAngle(node.Rotation.Forward(), angle);

                node.Rotation.Right(Vector3::Transform(node.Rotation.Right(), q));
                node.Rotation.Up(Vector3::Transform(node.Rotation.Up(), q));
            }

            if (maxIter-- <= 0) {
                SPDLOG_WARN("Reached max iterations in CreatePath()");
            }
        }

        for (int i = 0; i <= steps; i++)
            nodes[i].Rotation = nodes[i].Rotation.Invert();

        return path;
    }

    // member variables are Tunnel
    //void CreateSegments(List<TunnelSegment>& segments, TunnelPath& path, int steps) {
    //    ushort nVertex = 0;
    //    auto nElements = (short)segments.size();

    //    for (short nSegment = 1; nSegment <= steps; nSegment++) {
    //        auto nStartSide = path.Start.Tag.Side;

    //        for (short iElement = 0; iElement < nElements; iElement++) {
    //            auto nStartSeg = path.StartSides[iElement].Segment;
    //            //CSegment* pStartSeg = segmentManager.Segment(nStartSeg);
    //            //CTunnelElement& e0 = m_segments[nSegment].m_elements[iElement];
    //            //CSegment* pSegment = segmentManager.Segment(e0.m_nSegment);


    //            //CSide* pSide = pSegment->Side(0);
    //            //for (short nSide = 0; nSide < 6; nSide++, pSide++) {
    //            //    pSegment->SetUV(nSide, 0.0, 0.0);
    //            //    pSide->m_info.nBaseTex = pStartSeg->m_sides[nSide].m_info.nBaseTex;
    //            //    pSide->m_info.nOvlTex = pStartSeg->m_sides[nSide].m_info.nOvlTex;
    //            //    pSide->m_nShape = pStartSeg->m_sides[nSide].m_nShape;
    //            //}

    //            //for (int j = 0; j < 6; j++)
    //            //    pSegment->SetChild(j, -1);

    //            //if (nSegment > 1)
    //            //    pSegment->SetChild(oppSideTable[nStartSide], m_segments[nSegment - 1].m_elements[iElement].m_nSegment); // previous tunnel segment
    //            //else if (bFinalize) {
    //            //    pStartSeg->SetChild(nStartSide, e0.m_nSegment);
    //            //    pSegment->SetChild(oppSideTable[nStartSide], nStartSeg);
    //            //}

    //            //if (nSegment < steps)
    //                //pSegment->SetChild(nStartSide, m_segments[nSegment + 1].m_elements[iElement].m_nSegment); // next tunnel segment
    //        }
    //    }
    //}

    void CreateTunnelSegments(Level& level, TunnelPath& path, PointTag start, PointTag end) {
        Tag last = start;
        auto startIndices = level.GetSegment(start).GetVertexIndices(start.Side);
        auto endIndices = level.GetSegment(end).GetVertexIndices(end.Side);

        Marked.Segments.clear();

        auto vertIndex = (uint16)level.Vertices.size(); // take index before adding new points

        // copy vertices, skip start side
        level.Vertices.insert(level.Vertices.end(),
            TunnelBuilderPoints.begin() + startIndices.size(),
            TunnelBuilderPoints.begin() + startIndices.size() * path.Nodes.size());

        for (size_t nNode = 1; nNode < path.Nodes.size(); nNode++) {
            auto& lastSeg = level.GetSegment(last);

            Segment seg = {};
            SegID id = (SegID)level.Segments.size();

            auto oppositeSide = (int)GetOppositeSide(last.Side);
            seg.Connections[oppositeSide] = last.Segment;
            lastSeg.Connections[(int)last.Side] = id;

            auto srcIndices = lastSeg.GetVertexIndices(last.Side);
            auto& srcVertIndices = SideIndices[oppositeSide];
            auto& newVertIndices = SideIndices[(int)last.Side];
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
        Events::SegmentsChanged();
        Events::LevelChanged();
    }

    void CreateDebugPath(TunnelPath& path) {
        TunnelBuilderPath.clear();

        for (auto& node : path.Nodes) {
            TunnelBuilderPath.push_back(node.Vertex);
        }
    }

    void ClearTunnel() {
        TunnelBuilderPath.clear();
        TunnelBuilderPoints.clear();
        DebugTunnelPoints.clear();
        DebugTunnel.Nodes.clear();
    }

    void CreateTunnel(Level& level, PointTag start, PointTag end, int steps, float startLength, float endLength) {
        if (!level.SegmentExists(start) || !level.SegmentExists(end))
            return;

        ClearTunnel();

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
        //morph = false;
        //auto startIndices = level.GetSegment(start).GetVertexIndices(start.Side);

        if (morph) {
            //Matrix startRotation = startNode.Rotation.Invert();
            Matrix startRotation =  startNode.Rotation;
            Matrix endRotation = endNode.Rotation;
            //endRotation.Right(-endRotation.Right());
            //endRotation.Up(-endRotation.Up());

            for (ushort i = 0; i < 4; i++) {
                ushort startIndex = (start.Point + i) % 4;
                Vector3 vStart = startNode.Vertices[startIndex];
                DebugTunnelPoints.push_back(vStart);
                vStart -= startNode.Point; // shift to tunnel start
                vStart = Vector3::Transform(vStart, startRotation.Invert()); // un-rotate

                ushort endIndex = (end.Point + 5 - i) % 4;
                Vector3 vEnd = endNode.Vertices[endIndex];
                DebugTunnelPoints.push_back(vEnd);
                vEnd -= endNode.Point; // shift to tunnel end
                vEnd = Vector3::Transform(vEnd, endRotation.Invert() /** startRotation*/); // un-rotate

                vMorph.push_back(vEnd - vStart);
                DebugTunnelPoints.push_back(vStart);
                DebugTunnelPoints.push_back(vEnd + Vector3::UnitZ * 10);

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
        List<Vector3> vertices;

        for (int nSegment = 0; nSegment <= steps; nSegment++) {
            Matrix rotation = path.Nodes[nSegment].Rotation.Invert();
            auto& translation = path.Nodes[nSegment].Vertex;

            for (uint nVertex = 0; nVertex < startVertices.size(); nVertex++) {
                Vector3 v = startVertices[nVertex];
                v -= startNode.Point; // un-translate (make relative to tunnel start)
                v = Vector3::Transform(v, startNode.Rotation); // un-rotate

                if (morph) {
                    float amount = (float)nSegment / (float)steps;
                    //v += vMorph[nVertexMap[nVertex]] * amount;
                    v += vMorph[nVertex] * amount;
                }

                v = Vector3::Transform(v, rotation);
                v += translation;
                vertices.push_back(v);
                TunnelBuilderPoints.push_back(v);
            }
        }

        CreateDebugPath(path);
        DebugTunnel = path;
    }
}