#include "pch.h"
#include "TunnelBuilder.h"
#include "Face.h"
#include "Events.h"
#include "Resources.h"
#include "Editor.Texture.h"
#include "Editor.Undo.h"
#include "Game.h"
#include "Editor.Geometry.h"

namespace Inferno::Editor {
    TunnelNode CreateNode(Level& level, PointTag source, float sign) {
        TunnelNode node;
        auto face = Face::FromSide(level, (Tag)source);
        node.Normal = face.AverageNormal() * sign;
        node.Vertices = face.CopyPoints();
        node.Point = face.Center();
        auto right = face[(source.Point + 1) % 4] - face[source.Point];
        right.Normalize();

        // As far as I can tell we do have to do this to allow users to pick matching lines at each end
        // (in some cases there can be no good "opposite" line)
        right *= -sign;

        auto up = node.Normal.Cross(right);
        up.Normalize();
        node.Up = up;

        node.Rotation.Forward(node.Normal);
        node.Rotation.Right(right);
        node.Rotation.Up(node.Up);
        return node;
    }

    // Summed length to a given step
    float PathLength(span<PathNode> nodes, int steps) {
        float length = 0.0;

        for (int i = 1; i <= steps; i++)
            length += (nodes[i].Position - nodes[i - 1].Position).Length();

        return length;
    }

    float TotalTwist(const TunnelNode& start, const TunnelNode& end) {
        // revert the end orientation's z rotation in regard to the start orientation by 
        // determining the angle of the two matrices' z axii (forward vectors) and rotating
        // the end matrix around the perpendicular of the two matrices' z axii.

        auto endRot = end.Rotation;
        const auto startRot = start.Rotation;

        auto dot = endRot.Forward().Dot(startRot.Forward());
        auto bendAngle = acos(dot);

        Vector3 rotAxis;
        if (bendAngle > DirectX::XM_PI - 1e-6) {
            // Angle is close to 180 degrees, which means the rotation axis could be anything
            // perpendicular to the forward vector. We'll pick an axis also perpendicular to the
            // displacement between the two ends of the corridor.
            Vector3 displacement = end.Point - start.Point;
            if (displacement.Length() > 1e-3) {
                rotAxis = displacement.Cross(-startRot.Forward());
            }
            else {
                // No or small displacement - the tunnel maker probably shouldn't be started
                // but just in case - we could pick anything so we'll just pick the start up vector
                rotAxis = startRot.Up();
            }
        }
        else if (bendAngle > 1e-6) {
            rotAxis = endRot.Forward().Cross(-startRot.Forward());
        }

        if (bendAngle > 1e-6 && rotAxis.Length() > 1e-6) {
            // Construct quaternion from the axis and angle, and "undo" the end orientation's
            // bend so it is parallel with the start face. We only need the R vector to
            // determine rotation.
            auto q = Quaternion::CreateFromAxisAngle(rotAxis, -bendAngle);
            auto r = Vector3::Transform(endRot.Right(), q);
            r.Normalize();
            endRot.Right(r);
        }

        // Calculate rotation using atan2 (so we can get the direction at the same time).
        // y = projection of transformed R vector on start U vector
        // x = projection of transformed R vector on start R vector
        auto y = endRot.Right().Dot(startRot.Up());
        auto x = endRot.Right().Dot(startRot.Right());
        return atan2(y, x);
    }

    // Rotates n0's matrix around the perpendicular of n0's and n1's vector.
    // Updates n1 right and up vector.
    void Bend(const PathNode& n0, PathNode& n1) {
        // angle between forward vectors
        auto dot = n1.Rotation.Forward().Dot(n0.Rotation.Forward());

        if (dot >= 0.999999f) {
            // Facing same direction, copy
            n1.Rotation.Right(n0.Rotation.Right());
            n1.Rotation.Up(n0.Rotation.Up());
        }
        else if (dot <= -0.999999f) {
            // Facing directly away, copy inverse
            n1.Rotation.Right(-n0.Rotation.Right());
            n1.Rotation.Up(n0.Rotation.Up());
        }
        else {
            // Get the axis of rotation between the two nodes
            n1.Axis = n1.Rotation.Forward().Cross(-n0.Rotation.Forward());
            n1.Axis.Normalize();

            if (n1.Axis.Length() == 0)
                return; // error likely due to points on top of each other

            auto bendAngle = acos(dot);
            auto q = Quaternion::CreateFromAxisAngle(n1.Axis, bendAngle);
            auto fVec = Vector3::Transform(n0.Rotation.Forward(), q);
            auto dot2 = fVec.Dot(n1.Rotation.Forward());
            if (dot2 < 0.999f)
                bendAngle += acos(dot2);

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

    void Twist(const PathNode& n0, PathNode& n1, float pathDeltaAngle, float scale) {
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

    BezierCurve CreateCurve(const TunnelNode& start, const TunnelNode& end, const TunnelArgs& args) {
        BezierCurve curve;
        curve.Points[0] = start.Point;
        curve.Points[1] = start.Point + start.Normal * args.Start.Length;
        curve.Points[2] = end.Point - end.Normal * args.End.Length;
        curve.Points[3] = end.Point;
        return curve;
    }

    TunnelPath CreatePath(const TunnelNode& start, const TunnelNode& end, const TunnelArgs& args) {
        auto steps = args.Steps;
        auto curve = CreateCurve(start, end, args);
        auto bezierPoints = DivideCurveIntoSteps(curve.Points, steps);

        TunnelPath path{};
        path.Curve = curve;

        auto& nodes = path.Nodes;
        nodes.resize(steps + 1);

        for (int i = 0; i < nodes.size(); i++)
            nodes[i].Position = bezierPoints[i];

        nodes[0].Rotation = start.Rotation;
        nodes[0].Axis = start.Rotation.Right();
        nodes[0].Vertices = start.Vertices;
        nodes[steps].Rotation = end.Rotation;

        // change of basis
        auto startTransform = Matrix::CreateWorld(Vector3::Zero, start.Normal, start.Up);
        auto endTransform = Matrix::CreateWorld(Vector3::Zero, end.Normal, end.Up) /** Matrix::CreateFromAxisAngle(end.Normal, DirectX::XM_PI)*/;
        auto rotation = endTransform.Invert() * startTransform;
        Matrix transform = Matrix::CreateTranslation(-end.Point) * rotation * Matrix::CreateTranslation(start.Point);

        auto totalLength = PathLength(nodes, steps);
        auto totalTwist = TotalTwist(start, end);

        std::array<Vector3, 4> deltaShift{}; // amount of vertex change between each frame
        std::array<Vector3, 4> baseFrame{}; // start frame shifted to origin
        std::array<Vector3, 4> startFrame{}; // end points ordered correctly for edge selections

        for (int i = 0; i < 4; i++) {
            auto ia = (3 + i + args.Start.Tag.Point) % 4;
            auto ib = (6 - i + args.End.Tag.Point) % 4; // reverse order to correct for flipped normal

            baseFrame[i] = start.Vertices[ia] - start.Point;
            startFrame[i] = start.Vertices[ia];
            deltaShift[i] = Vector3::Transform(end.Vertices[ib], transform) - start.Vertices[ia];

            //// Connecting lines showing delta
            //DebugTunnelLines.push_back(start.Vertices[ia] + deltaShift[i]);
            //DebugTunnelLines.push_back(start.Vertices[ia]);

            //// Frame used to build delta
            //DebugTunnelLines.push_back(Vector3::Transform(end.Vertices[ib], transform));
            //DebugTunnelLines.push_back(Vector3::Transform(end.Vertices[(ib + 1) % 4], transform));
        }

        nodes[0].Vertices = startFrame;
        //nodes[steps].Vertices = endFrame;

        for (int i = 1; i <= steps; i++) {
            auto& n0 = nodes[i - 1];
            auto& n1 = nodes[i];

            if (i < steps) {
                auto forward = nodes[i + 1].Position - nodes[i - 1].Position;
                forward.Normalize();
                n1.Rotation.Forward(forward);
            }

            Bend(n0, n1);
            if (args.Twist)
                Twist(n0, n1, totalTwist / steps, PathLength(nodes, i) / totalLength);
            //auto twist = Matrix::CreateFromAxisAngle(start.Normal, -totalTwist * (float)i / (float)steps);
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
                //SPDLOG_WARN("Reached max iterations in CreatePath()");
                break;
            }
        }

        for (int i = 1; i <= steps; i++) {
            auto& node = nodes[i];

            // Set verts from rotation and position
            for (int j = 0; j < 4; j++) {
                auto& vert = node.Vertices[j];

                // 1. Morph the section
                vert = baseFrame[j] + deltaShift[j] * (float)i / (float)steps;

                // 2. Rotate section to match node
                vert = Vector3::Transform(vert, start.Rotation.Invert() * node.Rotation);

                // 3. Move section onto node
                vert += node.Position;
            }
        }

        return path;
    }

    void CreateTunnelSegments(Level& level, TunnelArgs& args) {
        auto start = args.Start.Tag;

        if (!level.SegmentExists(args.Start.Tag) || !level.SegmentExists(args.End.Tag) || !args.IsValid())
            return;

        auto path = CreateTunnel(level, args);
        auto prev = start;
        auto startIndices = level.GetSegment((Tag)start).GetVertexIndices(start.Side);

        Marked.Segments.clear();
        auto vertIndex = (uint16)level.Vertices.size(); // take index before adding new points
        auto lastId = SegID::None;

        for (size_t nNode = 1; nNode < path.Nodes.size(); nNode++) {
            auto& lastSeg = level.GetSegment((Tag)prev);

            Segment newSeg = {};
            auto id = (SegID)level.Segments.size();

            auto oppositeSide = (int)GetOppositeSide(prev.Side);
            // attach the previous seg to the new one
            newSeg.Connections[oppositeSide] = prev.Segment;
            lastSeg.Connections[(int)prev.Side] = id;

            auto srcIndices = lastSeg.GetVertexIndices(prev.Side);
            auto& oppIndices = SIDE_INDICES[oppositeSide];
            auto& prevIndices = SIDE_INDICES[(int)prev.Side];

            for (uint16 i = 0; i < 4; i++) {
                // terrible hack to fix winding
                auto offset = start.Point == 1 || start.Point == 3 ? 3 : 1;
                auto v = (offset + i + start.Point) % 4;

                newSeg.Indices[prevIndices[i]] = vertIndex + i;
                newSeg.Indices[oppIndices[3 - i]] = srcIndices[i];
                level.Vertices.push_back(path.Nodes[nNode].Vertices[v]);
            }

            vertIndex += (uint16)startIndices.size();

            // copy textures
            for (int i = 0; i < 6; i++) {
                auto& side = newSeg.Sides[i];
                side.TMap = lastSeg.Sides[i].TMap;
                side.TMap2 = lastSeg.Sides[i].TMap2;
                side.OverlayRotation = lastSeg.Sides[i].OverlayRotation;
                //side.UVs = lastSeg.Sides[i].UVs;

                // Clear door textures
                if (Resources::GetDoorClipID(side.TMap) != DClipID::None)
                    side.TMap = LevelTexID::Unset;

                if (Resources::GetDoorClipID(side.TMap2) != DClipID::None)
                    side.TMap2 = LevelTexID::Unset;
            }

            newSeg.UpdateGeometricProps(level);

            level.Segments.push_back(newSeg);
            prev.Segment = id;
            ResetUVs(level, id);
            Marked.Segments.insert(id);
            lastId = id;
        }

        // join the end segment
        auto nearby = Editor::GetNearbySegments(level, lastId, 100);
        Editor::JoinTouchingSegments(level, lastId, nearby, 0.1f);

        Editor::History.SnapshotLevel("Create Tunnel");
        Events::SegmentsChanged();
        Events::LevelChanged();
    }

    TunnelPath CreateTunnel(Level& level, TunnelArgs& args) {
        if (!level.SegmentExists(args.Start.Tag) || !level.SegmentExists(args.End.Tag) || !args.IsValid())
            return {};

        args.ClampInputs();
        auto startNode = CreateNode(level, args.Start.Tag, -1);
        auto endNode = CreateNode(level, args.End.Tag, 1);
        return CreatePath(startNode, endNode, args);
    }
}
