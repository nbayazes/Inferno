#pragma once

#include "Types.h"
#include "Yaml.h"

// Global editor settings that should be serialized
namespace Inferno {
    namespace Editor {
        enum class SelectionMode;
        enum class CoordinateSystem;

        enum class InsertMode {
            Normal, Extrude, Mirror
        };

        enum class TexturePreviewSize {
            Small, Medium, Large
        };
    }

    enum class RenderMode {
        None, Flat, Textured, Shaded
    };

    struct LightSettings {
        Color Ambient = { 0.0f, 0.0f, 0.0f };
        float Multiplier = 1.00f;
        float DistanceThreshold = 80.0f;
        float Falloff = 0.1f;
        float Radius = 20.0f;
        float MaxValue = 1.5f;
        bool EnableOcclusion = true;
        bool AccurateVolumes = false;
        int Bounces = 2;
        float Reflectance = 0.225f;
        bool EnableColor = false;
        bool SkipFirstPass = false;
        float LightPlaneTolerance = -0.45f;

        // Retired settings
        bool CheckCoplanar = true;
    };

    namespace Settings {
        constexpr int MaxRecentFiles = 8;

        inline List<filesystem::path> DataPaths;

        inline filesystem::path Descent1Path, Descent2Path;

        inline bool InvertY = false;
        inline bool EnablePhysics = false;

#pragma region Render
        inline bool HighRes = false; // Enables high res textures and filtering
        inline bool EnableBloom = false; // Enables bloom post-processing
        inline int MsaaSamples = 1;
        inline int ForegroundFpsLimit = -1, BackgroundFpsLimit = 20;
        inline float FieldOfView = 80;
#pragma endregion

        inline bool ScreenshotMode = false;

#pragma region Editor
        inline Editor::InsertMode InsertMode = {};
        inline Editor::SelectionMode SelectionMode = {};
        inline float TranslationSnap = 5, RotationSnap = 0;
        inline Editor::CoordinateSystem CoordinateSystem{};
        inline Editor::TexturePreviewSize TexturePreviewSize = Editor::TexturePreviewSize::Medium;

        inline LightSettings Lighting;
        inline float MouselookSensitivity = 0.005f; // Editor mouselook
        inline float MoveSpeed = 120.0f; // Editor move speed
        inline bool EditBothWallSides = true;
        inline bool ReopenLastLevel = true;

        inline bool EnableWallMode = false;
        inline bool EnableTextureMode = false;
        inline bool SelectMarkedSegment = false;
        inline bool ResetUVsOnAlign = true;

        inline float ObjectRenderDistance = 300.0f;

        inline float GizmoSize = 5.0f;
        inline float GizmoThickness = 0.3f;
        inline float CrosshairSize = 0.5f;
        inline float WeldTolerance = 1.0f;
        constexpr float CleanupTolerance = 0.1f;

        inline int ResetUVsAngle = 0; // Additional angle to apply when resetting UVs. 0-3 for 0, 90, 180, 270

        inline int UndoLevels = 50;
        inline int FontSize = 24;

        inline int AutosaveMinutes = 5;

        inline struct SelectionSettings {
            float PlanarTolerance = 15.0f;
            bool StopAtWalls = false;
            bool UseTMap1 = true;
            bool UseTMap2 = true;
        } Selection;

        inline bool ShowObjects = true;
        inline bool ShowWalls = false;
        inline bool ShowTriggers = false;
        inline bool ShowFlickeringLights = false;
        inline bool ShowAnimation = true;
        inline bool ShowLighting = true;
        inline bool ShowMatcenEffects = false;
        inline float WireframeOpacity = 0.5f;

        inline struct OpenWindows {
            bool Lighting = false;
            bool Properties = true;
            bool Textures = true;
            bool Reactor = false;
            bool Noise = false;
            bool TunnelBuilder = false;
            bool Sound = false;
            bool Diagnostics = false;
            bool BriefingEditor = false;
        } Windows;

        inline bool ShowWireframe = false;
        inline Inferno::RenderMode RenderMode = Inferno::RenderMode::Shaded;
        inline std::deque<std::filesystem::path> RecentFiles;

        void AddRecentFile(std::filesystem::path path);
#pragma endregion

        void Save();
        void Load();

        void SaveLightSettings(ryml::NodeRef node);
        LightSettings LoadLightSettings(ryml::NodeRef node);
    }
}
