#pragma once

#include "Command.h"
#include "Editor.Selection.h"
#include "Editor.Undo.h"
#include "Level.h"
#include "Types.h"

namespace Inferno::Editor {
    inline Camera EditorCamera;

    void UpdateCamera(Camera&);

    // The mouse cursor projected into the scene
    inline Ray MouseRay;

    // The behavior of the cursor when dragged
    enum class CursorDragMode { Select, Extrude, Transform };
    inline CursorDragMode DragMode = CursorDragMode::Select;
    inline LightSettings EditorLightSettings;

    // User defined coordinate system
    inline Matrix UserCSys;
    void AlignUserCSysToGizmo();
    void AlignUserCSysToSide();
    void AlignUserCSysToMarked();

    void SetMode(SelectionMode);

    inline void ToggleWallMode() {
        Settings::Editor.EnableWallMode = !Settings::Editor.EnableWallMode;
    }

    void ToggleTextureMode();

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
        PlaySound(Widen("SystemAsterisk").c_str(), nullptr, SND_ASYNC);
    }

    void OpenRecentOrEmpty();

    // Origin is used by rotation and scale transforms
    void SetOrigin(Vector3);

    void Update();

    void ExportBitmap(LevelTexID);

    void OnLevelLoad(bool reload);

    void LoadMission(std::filesystem::path);

    void OnSelectTexture(LevelTexID tmap1, LevelTexID tmap2);

    // Checks if the current file is dirty. Returns true if the file can be closed.
    bool CanCloseCurrentFile();

    void DisableFlickeringLights(Level& level);

    // Returns vertices based on marks or the selection
    List<PointID> GetSelectedVertices();

    // Returns faces based on marks or the selection
    List<Tag> GetSelectedFaces();

    List<WallID> GetSelectedWalls();

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
    Segment* GetSelectedSegment();

    // Tries to get the currently selected side in the editor
    inline SegmentSide* GetSelectedSide() {
        if (auto seg = GetSelectedSegment())
            return &seg->GetSide(Editor::Selection.Side);

        return nullptr;
    }

    void CleanLevel(Level&);
    void InitEditor();
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

        extern Command Undo, Redo;
        extern Command Insert, Delete;
        extern Command DisableFlickeringLights;
        extern Command AlignViewToFace, ZoomExtents;
        extern Command FocusSegment, FocusObject, FocusSelection;
        extern Command CycleRenderMode, ToggleWireframe;
    }
}
