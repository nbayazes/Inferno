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

    // User defined coordinate system
    inline Matrix UserCSys;
    void AlignUserCSysToGizmo();
    void AlignUserCSysToSide();
    void AlignUserCSysToMarked();

    void SetMode(SelectionMode);

    inline void ToggleWallMode() {
        Settings::Editor.EnableWallMode = !Settings::Editor.EnableWallMode;
    }

    inline void ToggleTextureMode() {
        Settings::Editor.EnableTextureMode = !Settings::Editor.EnableTextureMode;
        Editor::Gizmo.UpdateAxisVisiblity(Settings::Editor.SelectionMode);
        Editor::Gizmo.UpdatePosition();
    }

    // Text to show in status bar. Limited to string due to imgui.
    inline string StatusText = "Ready";

    template<class...TArgs>
    void SetStatusMessage(const string_view format, TArgs&&...args) {
        StatusText = fmt::vformat(format, fmt::make_format_args(args...));
        SPDLOG_INFO("{}", StatusText);
    }

    // Sets the status message along with a ding sound
    template<class...TArgs>
    void SetStatusMessageWarn(const string_view format, TArgs&&...args) {
        SetStatusMessage(format, std::forward<TArgs>(args)...);
        PlaySound(L"SystemAsterisk", nullptr, SND_ASYNC);
    }

    template<class...TArgs>
    void SetStatusMessage(const wstring_view format, TArgs&&...args) {
        auto str = fmt::vformat(fmt::wstring_view(format), fmt::make_wformat_args(std::forward<TArgs>(args)...));
        SPDLOG_INFO(L"{}", str);
        StatusText = Convert::ToString(str);
    }

    void Initialize();

    // Origin is used by rotation and scale transforms
    void SetOrigin(Vector3);

    void Update();

    void ExportBitmap(LevelTexID);

    void UpdateWindowTitle();

    void OnLevelLoad(bool reload);

    void LoadFile(filesystem::path path);

    //void LoadLevel(std::filesystem::path) noexcept;
    void LoadLevelFromHOG(string name);
    void LoadMission(std::filesystem::path);

    void OnSelectTexture(LevelTexID tmap1, LevelTexID tmap2);

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
            if (Settings::Editor.SelectionMode == SelectionMode::Segment) {
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

    void CleanLevel(Level&);
    void ResetFlickeringLightTimers(Level& level);

    namespace Commands {
        void CleanLevel();
        void GoToReactor();
        void GoToBoss();

        void GoToPlayerStart();
        void GoToExit();
        void GoToSecretExit();
        void GoToSecretExitReturn();

        void PlaytestLevel();
        void StartGame();

        void Exit();

        inline Command Undo{ .Action = [] { History.Undo(); }, .CanExecute = [] { return History.CanUndo(); }, .Name = "Undo" };
        inline Command Redo{ .Action = [] { History.Redo(); }, .CanExecute = [] { return History.CanRedo(); }, .Name = "Redo" };
        extern Command Insert, Delete;
        inline Command DisableFlickeringLights{ .Action = [] { Editor::DisableFlickeringLights(Game::Level); } };
        extern Command AlignViewToFace, ZoomExtents;
        extern Command FocusSegment, FocusObject, FocusSelection;
        extern Command CycleRenderMode, ToggleWireframe;
    }
}
