#pragma once
#include "Bezier.h"
#include "Camera.h"
#include "Editor/Editor.IO.h"
#include "Game.Navigation.h"
#include "Game.Player.h"
#include "Game.Briefing.h"
#include "Game.EscapeSequence.h"
#include "HogFile.h"
#include "Intersect.h"
#include "Level.h"
#include "Mission.h"
#include "SystemClock.h"
#include "Game.State.h"
#include "Difficulty.h"
#include "Game.IO.h"

namespace Inferno::Game {
    constexpr float TICK_RATE = 1.0f / 64; // 64 ticks per second
    constexpr float HOMING_TICK_RATE = 1.0f / 32; // ticks per second for homing weapons
    constexpr float WEAPON_HOMING_DELAY = 1 / 8.0f; // Delay before homing weapons start turning
    constexpr float CLOAK_FIRING_FLICKER = 0.75f; // How long a cloaked object 'flickers' after firing
    constexpr Color MATCEN_PHASING_COLOR = { 8, 0, 8 };
    constexpr float MATCEN_SOUND_RADIUS = 400;
    constexpr float FRIENDLY_FIRE_MULT = 0.5f; // Damage multiplier robots do to each other or themselves
    constexpr float POWERUP_RADIUS = 5.0f; // Make powerups easier to pick up
    constexpr float NEARBY_PORTAL_DEPTH = 150.0f; // How far to search when determining 'nearby' rooms

    constexpr auto FIRST_STRIKE_NAME = "Descent: First Strike"; // title of the main mission

    inline auto Difficulty = DifficultyLevel::Hotshot; // 0 to 4 for trainee to insane
    inline int LevelNumber = 0; // Index of loaded level starting at 1. Secret levels are negative. 0 means no level loaded.
    inline bool NeedsResourceReload = false; // Indicates that resources should be reloaded, typically due to changes in graphics settings
    inline bool LoadSecretLevel = false; // Indicates the next level loaded should be a secret level
    inline bool DemoMode = false; // When true, game started using demo data instead of retail

    constexpr int DEFAULT_GRAVITY = 10;
    inline Vector3 Gravity = { 0, -DEFAULT_GRAVITY, 0 }; // u/s acceleration

    // The loaded level. Only one level can be active at a time.
    inline Inferno::Level Level;
    inline IntersectContext Intersect(Level);

    // The loaded mission. Not always present.
    inline Option<HogFile> Mission;

    // Timestamp the current mission was started
    inline int64 MissionTimestamp = 0;

    // Only single player for now
    inline class Player Player = {};
    inline ObjRef DeathCamera = {};
    inline Camera MainCamera;

    // Sets the primary camera for the main view. Used for sound, rendering and mouse selection.
    void SetActiveCamera(Camera& camera);

    // Gets the primary camera for the main view. Used for sound, rendering and mouse selection.
    Camera& GetActiveCamera();

    // is the game level loading?
    inline std::atomic IsLoading = false;

    void RestartLevel();

    void LoadNextLevel();

    void InitLevel(Inferno::Level&& level);

    void UnloadMission();

    inline bool ResetGameTime = false;
    inline double Time = 0; // Elapsed game time since level start in seconds. Stops when paused.
    inline float FrameTime = 0; // Time of this frame in seconds. 0 when paused.
    inline float TimeScale = 1.0f; // Multiplier applied to elapsed game time

    inline float ScaledTickRate() { return TICK_RATE * TimeScale; }
    void SetTimeScale(float scale, float transitionSpeed = 0);

    inline float LerpAmount = 1; // How much to lerp between the previous and next object states

    inline bool BriefingVisible = false;

    bool EnableAi();

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp);

    void UpdateCameraSegment(Camera& camera);

    void Update(float dt);

    inline bool ShowDebugOverlay = false;

    inline bool SecretLevelDestroyed = false;

    /*inline bool ObjShouldThink(const Object& obj) {
        return obj.NextThinkTime <= Time && obj.NextThinkTime != -1;
    }*/

    // Returns true if the provided time has come to pass
    inline bool TimeHasElapsed(double time) {
        return time <= Time && time != -1;
    }

    using GunIndex = uint8;
    using WeaponBehavior = std::function<void(Inferno::Player&, GunIndex, WeaponID)>;
    WeaponBehavior& GetWeaponBehavior(const string& name);

    constexpr float DOOR_WAIT_TIME = 5; // How long a door stays open before automatically closing
    constexpr float MINE_ARM_TIME = 1.25f; // How long before an object can collide with their own mines. Also disables splash damage for the duration.
    constexpr int EXTRA_LIFE_POINTS = 50'000;
    constexpr uint HOSTAGE_SCORE = 1000;
    constexpr uint REACTOR_SCORE = 5000;
    constexpr float PLAYER_HIT_WALL_NOISE = 1.5f;
    constexpr float PLAYER_HIT_WALL_RADIUS = 120;
    constexpr float PLAYER_FUSION_SOUND_RADIUS = 120;
    constexpr float PLAYER_AFTERBURNER_SOUND_RADIUS = 250; // alert radius for afterburner
    constexpr float FUSION_SHAKE_STRENGTH = 3.5f; // Amount charging fusion shakes the player
    constexpr float CLOAK_TIME = 30.0f;
    constexpr float INVULNERABLE_TIME = 30.0f;
    constexpr float WEAPON_HIT_WALL_VOLUME = 0.9f;
    constexpr float WEAPON_HIT_OBJECT_VOLUME = 1.0f;

    inline bool Cheater = false;
    inline bool PlayingFromEditor = false;
    inline bool FailedEscape = false; // Failed to escape the level in time. Used for scoring and pausing time.

    // Returns the number of extra lives
    uint8 AddPointsToScore(int points);
    void StartMission();

    inline bool ControlCenterDestroyed = false;
    inline float CountdownTimer = -1.0f; // time before reactor goes critical
    inline int CountdownSeconds = -1; // seconds before the reactor goes critical. Used for HUD value and audio.
    inline int TotalCountdown = -1; // the starting countdown time
    inline float GlobalDimming = 1; // Amount of global fade to apply to 'mine' light sources
    inline bool OnTerrain = false; // True when the player or camera is on the terrain

    void SetState(GameState);
    GameState GetState();

    inline NavigationNetwork Navigation;

    inline BriefingState Briefing;

    Object& GetPlayerObject();

    constexpr float DEFAULT_BLOOM = 0.35f;
    inline LerpedColor ScreenGlow = Color(0, 0, 0, 0);
    inline LerpedColor FusionTint = Color(0, 0, 0, 0);
    inline auto DamageTint = Color(0, 0, 0, 0);
    inline LerpedValue Exposure = 1;
    inline LerpedValue BloomStrength = DEFAULT_BLOOM;

    inline Color ScreenFlash = { 0, 0, 0 }; // Used when picking up an item or taking damage
    constexpr float MAX_FLASH = 0.4f;
    constexpr float FLASH_DECAY_RATE = MAX_FLASH * 0.75f;
    constexpr float MAX_DAMAGE_TINT = 0.6f;

    void AddScreenFlash(const Color&);

    void AddDamageTint(const Color& color);

    inline void ResetTints() {
        Game::DamageTint = Color(0, 0, 0);
        Game::FusionTint = Color(0, 0, 0);
        Game::ScreenFlash = Color(0, 0, 0);
    }

    //bool ObjectIsInFOV(const Ray& ray, const Object& obj, float fov);

    // Returns an object reference based on its address
    inline ObjRef GetObjectRef(const Object& obj) {
        auto id = ObjID(&obj - Level.Objects.data());
        ASSERT((int)id < Level.Objects.size() && (int)id >= 0); // Object wasn't in the level data (programming error)
        return { id, obj.Signature };
    }

    // Returns an object reference based on its ID
    inline ObjRef GetObjectRef(ObjID id) {
        if (auto obj = Level.TryGetObject(id))
            return { id, obj->Signature };
        else
            return {}; // null handle
    }

    inline Room* GetCurrentRoom() {
        if (Level.Objects.empty()) return nullptr;
        // should technically get the room the camera is in
        return Level.GetRoom(GetPlayerObject());
    }

    // Stupid intellisense
#undef GetObject

    // Gets an object by reference. Returns null if not found or dead.
    Object* GetObject(ObjRef);
    inline List<RoomID> ActiveRooms; // Rooms that are visible or near the player

    namespace Debug {
        inline uint LiveObjects = 0;
        inline int ActiveRobots = 0;
        inline uint VisibleSegments = 0;
    }

    inline bool IsFinalLevel() {
        if (auto mission = GetCurrentMissionInfo()) {
            return LevelNumber >= (int)mission->Levels.size();
        }

        return false;
    }

    inline Inferno::TerrainInfo Terrain;

    enum class ThreatLevel { None, Minimal, Moderate, High, Extreme };

    void PlayMainMenuMusic();

    void WarpPlayerToExit();
}

namespace Inferno {
    constexpr float GetDamage(const Weapon& weapon) {
        return weapon.Damage[(int)Game::Difficulty];
    }

    constexpr float GetSpeed(const Weapon& weapon) {
        return weapon.Speed[(int)Game::Difficulty];
    }

    inline bool IsCloakEffective(const Object& object) {
        if (object.IsPlayer()) {
            if (Game::Player.GetHeadlightState() != HeadlightState::Off || Game::Player.AfterburnerActive)
                return false;

            //if (Game::Player.WeaponCharge && Game::Player.Primary == PrimaryWeaponIndex::Fusion)
            //    return false; // Charging the fusion is noisy
        }

        return object.IsCloakEffective();
    }

    // Returns true if an object has line of sight to a target. Also checks if the target is cloaked.
    inline bool ObjectCanSeeObject(const Object& obj, const Object& target, float maxDist = -1) {
        if (IsCloakEffective(target) || !target.IsAlive()) return false;
        auto [dir, dist] = GetDirectionAndDistance(target.Position, obj.Position);
        if (maxDist > 0)
            if (dist >= maxDist) return false;

        Ray ray(obj.Position, dir);
        LevelHit hit;
        RayQuery query(dist, obj.Segment, RayQueryMode::Visibility);
        return !Game::Intersect.RayLevel(ray, query, hit);
    }
}
