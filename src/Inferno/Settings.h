#pragma once

#include "Types.h"

// Global settings that should be serialized
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

        enum class MiddleMouseMode {
            Mouselook, Orbit
        };
    }

    enum class RenderMode {
        None,
        Flat, // Untextured
        Textured, // Unlit texturing
        Shaded // Shaded textures
    };

    enum class TextureFilterMode {
        Point, EnhancedPoint, Smooth
    };

    enum class WiggleMode {
        Normal, Reduced, Off
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
        bool Multithread = true;

        // Retired settings
        bool CheckCoplanar = true;
    };

    constexpr int PALETTE_SIZE = 36;

    Array<Color, PALETTE_SIZE> GetDefaultPalette();

    struct EditorSettings {
        bool ShowLevelTitle = true;
        Editor::InsertMode InsertMode = {};
        Editor::SelectionMode SelectionMode = {};
        float TranslationSnap = 5, RotationSnap = 0;
        Editor::CoordinateSystem CoordinateSystem{};
        Editor::TexturePreviewSize TexturePreviewSize = Editor::TexturePreviewSize::Medium;

        Array<Color, PALETTE_SIZE> Palette; // User color palette
        LightSettings Lighting;
        float MouselookSensitivity = 0.005f; // Editor mouselook
        float MoveSpeed = 120.0f; // Editor move speed
        bool EditBothWallSides = true;
        bool ReopenLastLevel = true;

        bool EnableWallMode = false;
        bool EnableTextureMode = false;
        bool SelectMarkedSegment = false;
        bool ResetUVsOnAlign = true;
        bool EnablePhysics = false;
        bool PasteSegmentObjects = true;
        bool PasteSegmentWalls = true;
        bool PasteSegmentSpecial = true;
        bool OutlineBossTeleportSegments = false;

        float ObjectRenderDistance = 300.0f;

        float GizmoSize = 5.0f;
        float GizmoThickness = 0.3f;
        float CrosshairSize = 0.5f;
        float WeldTolerance = 1.0f;
        float CleanupTolerance = 0.1f;

        int ResetUVsAngle = 0; // Additional angle to apply when resetting UVs. 0-3 for 0, 90, 180, 270

        int UndoLevels = 50;
        int FontSize = 24;

        int AutosaveMinutes = 5;

        struct SelectionSettings {
            float PlanarTolerance = 15.0f;
            bool StopAtWalls = false;
            bool UseTMap1 = true;
            bool UseTMap2 = true;
        } Selection;

        bool ShowObjects = true;
        bool ShowWalls = false;
        bool ShowTriggers = false;
        bool ShowFlickeringLights = false;
        bool ShowAnimation = true;
        bool ShowLighting = true;
        bool ShowMatcenEffects = false;
        bool ShowPortals = false;
        bool ShowLights = false; // Show dynamic light outlines
        float WireframeOpacity = 0.5f;

        bool InvertY = false;
        bool InvertOrbitY = false;
        float FieldOfView = 80;
        Editor::MiddleMouseMode MiddleMouseMode = Editor::MiddleMouseMode::Mouselook;

        struct OpenWindows {
            bool Lighting = false;
            bool Properties = true;
            bool Textures = true;
            bool Reactor = false;
            bool Noise = false;
            bool TunnelBuilder = false;
            bool Sound = false;
            bool Diagnostics = false;
            bool BriefingEditor = false;
            bool TextureEditor = false;
            bool Scale = false;
            bool MaterialEditor = false;
            bool Debug = false;
        } Windows;

        bool ShowWireframe = false;
        Inferno::RenderMode RenderMode = Inferno::RenderMode::Shaded;
        std::deque<std::filesystem::path> RecentFiles;

        int MaxRecentFiles = 8;
        void AddRecentFile(std::filesystem::path path);
    };

    struct GraphicsSettings {
        bool HighRes = false; // Enables high res textures and filtering
        bool EnableBloom = true; // Enables bloom post-processing and tone mapping
        bool EnableProcedurals = true;
        int MsaaSamples = 1;
        int ForegroundFpsLimit = -1, BackgroundFpsLimit = 20;
        bool UseVsync = true;
        bool NewLightMode = true;
        int ToneMapper = 1;
        TextureFilterMode FilterMode = TextureFilterMode::EnhancedPoint;

        // Debugging

        bool OutlineVisibleRooms = false;
    };

    struct InfernoSettings {
        List<filesystem::path> DataPaths;
        filesystem::path Descent1Path, Descent2Path;
        bool InvertY = false;
        float MouseSensitivity = 1/64.0f;
        bool HalvePitchSpeed = true; // Halves the maximum pitch speed
        bool ScreenshotMode = false; // game setting?
        float MasterVolume = 0.1f;
        float MusicVolume = 0.5f;
        bool GenerateMaps = true; // Generate specular and normal maps if missing
        bool Descent3Enhanced = false;
        WiggleMode ShipWiggle = WiggleMode::Reduced;
    };

    struct CheatSettings {
        bool DisableWeaponDamage = false;
        bool DisableWallCollision = false;
        bool DisableAI = false;
        bool ShowPathing = false;
        bool FullyLoaded = false; // Max weapons on spawn
        bool Invulnerable = false;
        bool Cloaked = false;
        bool LowShields = false;
    };

    namespace Settings {
        inline InfernoSettings Inferno;
        inline EditorSettings Editor;
        inline GraphicsSettings Graphics;
        inline CheatSettings Cheats;

        void Save(const filesystem::path& path = "inferno.cfg");
        void Load(const filesystem::path& path = "inferno.cfg");
    }
}
