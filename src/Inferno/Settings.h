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

    enum class UpscaleFilterMode {
        Point, Smooth
    };

    enum class WiggleMode {
        Normal, Reduced, Off
    };

    enum class ShipRollMode {
        Normal, Reduced
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
        bool ShowTerrain = false;

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
        Editor::MiddleMouseMode MiddleMouseMode = Editor::MiddleMouseMode::Orbit;

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
            bool TerrainEditor = false;
            bool Scale = false;
            bool MaterialEditor = false;
            bool Debug = false;
            bool Bloom = false;
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
        int MsaaSamples = 1; // 1 through 8. 1 is no MSAA
        int ForegroundFpsLimit = 120, BackgroundFpsLimit = 20;
        bool EnableForegroundFpsLimit = false;
        bool UseVsync = true;
        bool NewLightMode = true;
        int ToneMapper = 1;
        TextureFilterMode FilterMode = TextureFilterMode::EnhancedPoint;
        UpscaleFilterMode UpscaleFilter = UpscaleFilterMode::Point;

        float FieldOfView = 70; // Game FOV in degrees. Descent uses 60, but a higher value feels better for input.
        float RenderScale = 1; // Scale of 3D render target

        // Debugging

        bool OutlineVisibleRooms = false;
        bool DrawGunpoints = false;
    };

    enum class WindowMode {
        Fullscreen = 0,
        Maximized = 1,
        Windowed = 2
    };

    struct InfernoSettings {
        List<filesystem::path> DataPaths;
        filesystem::path Descent1Path, Descent2Path;
        bool InvertY = false;
        float MouseSensitivity = 1 / 64.0f;
        float MouseSensitivityX = 1 / 64.0f;
        bool HalvePitchSpeed = true; // Halves the maximum pitch speed. This is the original game behavior.
        bool ScreenshotMode = false; // game setting?
        float MasterVolume = 1.0f;
        float EffectVolume = 0.5f;
        float MusicVolume = 0.5f;
        bool GenerateMaps = true; // Generate specular and normal maps if missing
        bool Descent3Enhanced = false;
        bool ShowWeaponFlash = false; // Are weapon flashes visible in first person?
        WiggleMode ShipWiggle = WiggleMode::Reduced;
        ShipRollMode ShipRoll = ShipRollMode::Normal;  // Scales the amount of roll to apply to the player when turning
        WindowMode WindowMode = WindowMode::Fullscreen;
        bool Fullscreen = false;
        bool Maximized = true; // Maximized or windowed when in windowed mode
        bool ShipAutolevel = false;
        bool NoAutoselectWhileFiring = true;
        bool AutoselectAfterFiring = true;
        bool OnlyCycleAutoselectWeapons = true;
        bool StickyRearview = false;
        bool SlowmoFusion = true;
        bool EnableJoystick = false;
        bool EnableGamepad = true;
        bool EnableMouse = true;
        bool PreferHighResFonts = true;
        uint2 WindowSize;
        uint2 WindowPosition;
        float GamepadSensitivityX = 8;
        float GamepadSensitivityY = 8;
    };

    struct CheatSettings {
        bool DisableWeaponDamage = false;
        bool DisableWallCollision = false;
        bool DisableAI = false;
        bool ShowPathing = false;
        bool FullyLoaded = true; // Max weapons on spawn
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
