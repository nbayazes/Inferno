#pragma once

#include "Types.h"
#include "Level.h"
#include "Utility.h"
#include "Events.h"
#include "Gizmo.h"
#include "Resources.h"
#include "WindowsDialogs.h"
#include "Face.h"
#include "Command.h"
#include "Bindings.h"

#include "Editor.Selection.h"
#include "Editor.Geometry.h"
#include "Editor.Undo.h"
#include "Editor.Clipboard.h"
#include "Editor.Wall.h"
#include "Editor.IO.h"
#include "Editor.Segment.h"
#include "Editor.Object.h"
#include "Editor.Texture.h"
#include "Editor.Lighting.h"

namespace Inferno::Editor {
    void UpdateCamera(Camera&);

    // The mouse cursor projected into the scene
    inline Ray MouseRay;

    // The behavior of the cursor when dragged
    enum class CursorDragMode { Select, Extrude, Transform };
    inline CursorDragMode DragMode = CursorDragMode::Select;

    // Coordinate system used for 'reference' 
    inline Matrix GlobalOrientation;
    void AlignGlobalOrientation();
    void AlignGlobalOrientationToSide();

    void SetMode(SelectionMode);

    // Text to show in status bar. Limited to string due to imgui.
    inline string StatusText = "Ready";

    template<class...TArgs>
    void SetStatusMessage(const string_view format, TArgs&&...args) {
        SPDLOG_INFO(format, args...);
        StatusText = fmt::format(format, std::forward<TArgs>(args)...);
    }

    // Sets the status message along with a ding sound
    template<class...TArgs>
    void SetStatusMessageWarn(const string_view format, TArgs&&...args) {
        SPDLOG_WARN(format, args...);
        StatusText = fmt::format(format, std::forward<TArgs>(args)...);
        PlaySound(L"SystemAsterisk", nullptr, SND_ASYNC);
    }

    template<class...TArgs>
    void SetStatusMessage(const wstring_view format, TArgs&&...args) {
        SPDLOG_INFO(format, args...);
        auto str = fmt::format(format, std::forward<TArgs>(args)...);
        StatusText = Convert::ToString(str);
    }

    void Initialize();

    // Origin is used by rotation and scale transforms
    void SetOrigin(Vector3);

    void Update();

    void ExportBitmap(LevelTexID);

    void UpdateWindowTitle();

    void OnLevelLoad(bool resetCamera);

    void LoadFile(filesystem::path path);

    //void LoadLevel(std::filesystem::path) noexcept;
    void LoadLevelFromHOG(string name);
    void LoadMission(std::filesystem::path);

    void OnSelectTexture(LevelTexID tmap, LevelTexID tmap2);

    // Checks if the current file is dirty. Returns true if the file can be closed.
    bool CanCloseCurrentFile();

    void DisableFlickeringLights(Level& level);

    // Returns vertices based on marks or the selection
    inline List<PointID> GetSelectedVertices() {
        auto verts = Editor::Marked.GetVertexHandles(Game::Level);
        if (verts.empty())
            verts = Editor::Selection.GetVertexHandles(Game::Level);

        return verts;
    }

    // Returns faces based on marks or the selection
    inline List<Tag> GetSelectedFaces() {
        auto faces = Editor::Marked.GetMarkedFaces();
        if (faces.empty()) {
            if (Settings::SelectionMode == SelectionMode::Segment) {
                for (auto& side : SideIDs)
                    faces.push_back({ Editor::Selection.Segment, side });
            }
            else {
                faces.push_back(Editor::Selection.Tag());
            }
        }

        return faces;
    }

    inline List<WallID> GetSelectedWalls() {
        List<WallID> walls;
        for (auto& id : GetSelectedFaces()) {
            auto wall = Game::Level.TryGetWallID(id);
            if (wall != WallID::None)
                walls.push_back(wall);
        }

        return walls;
    }

    // Returns objects based on marks or the selection
    inline List<ObjID> GetSelectedObjects() {
        auto objects = Seq::ofSet(Editor::Marked.Objects);
        if (objects.empty())
            objects.push_back(Editor::Selection.Object);

        return objects;
    }

    // Returns segments based on marks or the selection
    inline List<SegID> GetSelectedSegments() {
        auto segs = Seq::ofSet(Marked.Segments);
        if (segs.empty())
            segs.push_back(Editor::Selection.Segment);

        return segs;
    }

    // Tries to get the currently selected segment in the editor
    inline Segment* GetSelectedSegment() {
        return Game::Level.TryGetSegment(Editor::Selection.Segment);
    }

    // Tries to get the currently selected side in the editor
    inline SegmentSide* GetSelectedSide() {
        if (auto seg = GetSelectedSegment())
            return &seg->GetSide(Editor::Selection.Side);

        return nullptr;
    }

    // Returns a random value between 0 and 1
    inline float Random() {
        return (float)rand() / RAND_MAX;
    }

    void CleanLevel(Level&);
    void ResetFlickeringLightTimers(Level& level);

    namespace Commands {
        void CleanLevel();

        // Camera
        void AlignViewToFace();
        void ZoomExtents();
        void FocusSegment();
        void FocusObject();

        void GoToReactor();
        void GoToBoss();

        void GoToPlayerStart();
        void GoToExit();
        void GoToSecretExit();
        void GoToSecretExitReturn();

        void PlaytestLevel();
        void StartGame();

        void Exit();

        inline void CycleRenderMode() {
            int i = (int)Settings::RenderMode + 1;
            if (i > (int)RenderMode::Shaded) {
                i = 0;
                Settings::ShowWireframe = true;
            }
            else {
                Settings::ShowWireframe = false;
            }
            Settings::RenderMode = (RenderMode)i;
        }

        inline Command ToggleWireframe{
            .Action = [] {
                Settings::ShowWireframe = !Settings::ShowWireframe;

                // Always keep something visible
                if (!Settings::ShowWireframe && Settings::RenderMode == RenderMode::None)
                    Settings::RenderMode = RenderMode::Textured;
            },
            .Name = "Toggle Wireframe"
        };

        inline Command Undo{ .Action = [] { History.Undo(); }, .CanExecute = [] { return History.CanUndo(); } };
        inline Command Redo{ .Action = [] { History.Redo(); }, .CanExecute = [] { return History.CanRedo(); } };
        extern Command Insert, Delete;
        inline Command DisableFlickeringLights{ .Action = [] { Editor::DisableFlickeringLights(Game::Level); } };
    }
}
