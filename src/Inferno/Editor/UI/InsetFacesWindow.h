#pragma once
#include "Editor/Editor.h"
#include "WindowBase.h"
#include "Settings.h"
#include "Shell.h"

namespace Inferno::Editor {
    inline List<Vector3> InsetFacesPreview;

    class InsetFacesWindow final : public WindowBase {
        float _inset = 2;
        float _depth = 0;
        bool _individual = false;
        bool _showPreview = true;
        int _depthInt = 20;

    public:
        //static bool Preview;

        InsetFacesWindow() : WindowBase("Inset Faces", &Settings::Editor.Windows.InsetFaces) {
            // There isn't much in this window, so make it a little shorter
            DefaultHeight = 200 * Shell::DpiScale;
            Editor::Events::MarkedFacesChanged += [this] { UpdatePreview(); };
        }

        void OnUpdate() override {
            if (ImGui::SliderFloat("Inset", &_inset, -20, 20, "%.1f")) UpdatePreview();
            if (ImGui::SliderFloat("Depth", &_depth, -20, 20, "%.1f")) UpdatePreview();

            if (ImGui::Checkbox("Individual", &_individual)) {
                UpdatePreview();
            }

            if (ImGui::Checkbox("Preview", &_showPreview)) {
                UpdatePreview();
            }

            if (ImGui::Button("Insert"))
                InsertSegments();
        }

        void InsertSegments() {
            auto tags = Editor::Marked.GetMarkedFaces();
            auto& level = Game::Level;

            if (_individual) {
                for (auto& tag : tags) {
                    auto face = Face::FromSide(level, tag);
                    auto normal = face.AverageNormal();

                    auto newSeg = InsertSegment(level, tag, Selection.Point, InsertMode::Extrude, &Vector3::Zero);
                    if (newSeg == SegID::None) continue;

                    auto conn = GetOppositeSide(level.GetConnectedSide(tag));
                    auto newFace = Face::FromSide(level, conn);
                    Array<Vector3, 4> points = { newFace.P0, newFace.P1, newFace.P2, newFace.P3 };

                    for (int i = 0; i < 4; i++) {
                        // https://stackoverflow.com/questions/54033808/how-to-offset-polygon-edges
                        auto na = normal.Cross(points[ModSafe(i - 1, 4)] - points[i]);
                        auto nb = normal.Cross(points[i] - points[ModSafe(i + 1, 4)]);
                        na.Normalize();
                        nb.Normalize();

                        auto bis = na + nb;
                        bis.Normalize();

                        auto l = _inset / sqrt((1.0f + na.Dot(nb)) / 2.0f);
                        newFace[i] = points[i] - l * bis - normal * _depth;
                    }

                    auto nearby = GetNearbySegments(level, newSeg);
                    JoinTouchingSegments(level, newSeg, nearby, Settings::Editor.CleanupTolerance);
                    ResetUVs(level, newSeg);
                }
            }
            else {}

            level.UpdateAllGeometricProps();
            Marked.Clear();
            Events::LevelChanged();
            Editor::History.SnapshotLevel("Inset faces");
            UpdatePreview();
        }

        Vector3 Inset(const Vector3& normal, const Vector3& p, const Vector3& left, const Vector3& right) {
            auto na = normal.Cross(left - p);
            auto nb = normal.Cross(p - right);
            na.Normalize();
            nb.Normalize();

            auto bis = na + nb;
            bis.Normalize();

            auto l = _inset / sqrt((1.0f + na.Dot(nb)) / 2.0f);
            return p - l * bis - normal * _depth;
        }

        void UpdatePreview() {
            // Gather faces, inset towards center, move along face normal by depth
            InsetFacesPreview.clear();
            if (!_showPreview) return;

            auto tags = Editor::Marked.GetMarkedFaces();
            auto& level = Game::Level;

            if (_individual) {
                for (auto& tag : tags) {
                    auto face = Face::FromSide(level, tag);

                    Array<Vector3, 4> verts{};

                    auto normal = face.AverageNormal();

                    for (int i = 0; i < 4; i++) {
                        auto na = normal.Cross(face[i - 1] - face[i]);
                        auto nb = normal.Cross(face[i] - face[i + 1]);
                        na.Normalize();
                        nb.Normalize();

                        auto bis = na + nb;
                        bis.Normalize();

                        auto l = _inset / sqrt((1.0f + na.Dot(nb)) / 2.0f);
                        verts[i] = face[i] - l * bis - normal * _depth;
                    }

                    for (int i = 0; i < 4; i++) {
                        InsetFacesPreview.push_back(verts[i]);
                        InsetFacesPreview.push_back(verts[(i + 1) % 4]);
                    }
                }
            }
            else {
                // find the outer loop of indices. If a face is completely surrounded by marked faces extrude with no inset.
                struct Edge {
                    uint16 A, B;
                    Tag Tag;
                    bool IsUnique = true;

                    int32 GetHash() const {
                        if (B > A) return (int32)B << 16 | A;
                        return (int32)A << 16 | B;
                    }
                };

                //List<Edge> edges;
                Dictionary<int32, Edge> edges;


                for (auto& tag : tags) {
                    if (!level.SegmentExists(tag)) continue;
                    auto& seg = level.GetSegment(tag);
                    auto indices = seg.GetVertexIndices(tag.Side);

                    for (int i = 0; i < 4; i++) {
                        Edge edge = { indices[i], indices[(i + 1) % 4], tag };
                        auto hash = edge.GetHash();
                        if (edges.contains(hash)) {
                            edges[hash].IsUnique = false;
                        }
                        else {
                            edges[hash] = edge;
                        }
                    }

                    /*auto& side = level.GetSide(tag);
                    
                    
                    auto face = Face::FromSide(level, tag);
                    auto verts = face.Inset(_inset, -_depth);
                    */
                }

                List<Edge> outsideEdges;

                for (auto& edge : edges | views::values) {
                    if (edge.IsUnique)
                        outsideEdges.push_back(edge);
                }

                struct Edge2 {
                    uint16 Index;
                    Vector3 Normal;
                };

                List<Edge> edgeLoop; // find connected edges
                List<Edge2> edgeLoop2; // find connected edges


                for (auto& edge : outsideEdges) {
                    for (auto& other : outsideEdges) {
                        if (edge.GetHash() != other.GetHash()) {
                            if (edge.A == other.B) {
                                edgeLoop.push_back(edge);
                                //edgeLoop2.push_back({ edge.A, normal });
                                //edgeLoop.push_back(other);
                            }
                        }
                    }
                }

                // Sort the edge loop
                for (size_t i = 0; i + 1 < edgeLoop.size(); i++) {
                    for (size_t j = 0; j < edgeLoop.size(); j++) {
                        if (i == j) continue;
                        if (edgeLoop[i].B == edgeLoop[j].A) {
                            std::swap(edgeLoop[j], edgeLoop[i + 1]);
                            break;
                        }
                    }
                }

                for (int i = 0; i < edgeLoop.size(); i++) {
                    {
                        auto& edge = edgeLoop[i];
                        auto& side = level.GetSide(edge.Tag);
                        auto& normal = side.AverageNormal;
                        auto& point = level.Vertices[edge.A];
                        auto& prevEdge = edgeLoop[ModSafe(i - 1, (int)edgeLoop.size())];
                        auto& p0 = level.Vertices[prevEdge.A];
                        auto& p1 = level.Vertices[edge.B];

                        auto na = normal.Cross(p0 - point);
                        auto nb = normal.Cross(point - p1);
                        na.Normalize();
                        nb.Normalize();

                        auto bis = na + nb;
                        bis.Normalize();

                        auto l = _inset / sqrt((1.0f + na.Dot(nb)) / 2.0f);
                        auto vert = point - l * bis - normal * _depth;

                        InsetFacesPreview.push_back(vert);
                    }

                    {
                        auto& edge = edgeLoop[i];
                        auto& side = level.GetSide(edge.Tag);
                        auto& normal = side.AverageNormal;
                        auto& point = level.Vertices[edge.B];
                        auto& nextEdge = edgeLoop[ModSafe(i + 1, (int)edgeLoop.size())];
                        auto& p0 = level.Vertices[edge.A];
                        auto& p1 = level.Vertices[nextEdge.B];

                        auto na = normal.Cross(p0 - point);
                        auto nb = normal.Cross(point - p1);
                        na.Normalize();
                        nb.Normalize();

                        auto bis = na + nb;
                        bis.Normalize();

                        auto l = _inset / sqrt((1.0f + na.Dot(nb)) / 2.0f);
                        auto vert = point - l * bis - normal * _depth;

                        InsetFacesPreview.push_back(vert);
                    }

                    // todo: average previous and next points
                }

                for (int i = 1; i < InsetFacesPreview.size(); i += 2) {
                    auto& v = InsetFacesPreview[i];
                    auto& next = InsetFacesPreview[ModSafe(i + 1, (int)InsetFacesPreview.size())];

                    v = next = (v + next) / 2.0f;
                }
            }
        }
    };
}
