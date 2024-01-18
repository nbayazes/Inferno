#pragma once
#include "Camera.h"
#include "Game.Navigation.h"
#include "Game.Player.h"
#include "HogFile.h"
#include "Intersect.h"
#include "Level.h"
#include "Mission.h"
#include "SystemClock.h"

namespace Inferno {
    struct LevelHit;

    enum class GameState {
        Game, // In first person and running game logic
        Paused, // In game but paused or in a menu
        ExitSequence, // exit tunnel sequence
        Cutscene, // In-game cutscene, waits for input to cancel
        ScoreScreen,
        Briefing,
        MainMenu,
        Editor
    };

    enum class DifficultyLevel {
        Trainee,
        Rookie,
        Hotshot,
        Ace,
        Insane,
        Count
    };
}

namespace Inferno::Game {
    constexpr float TICK_RATE = 1.0f / 64; // 64 ticks per second
    constexpr float HOMING_TICK_RATE = 1.0f / 32; // ticks per second for homing weapons
    constexpr float WEAPON_HOMING_DELAY = 1 / 8.0f; // Delay before homing weapons start turning
    constexpr float DEFAULT_WEAPON_VOLUME = 0.55f; // Default volume when firing weapons
    constexpr float CLOAK_FIRING_FLICKER = 0.75f; // How a cloaked object 'flickers' after firing
    constexpr Color MATCEN_PHASING_COLOR = Color(8, 0, 8);
    constexpr float MATCEN_SOUND_RADIUS = 300;
    constexpr float FRIENDLY_FIRE_MULT = 0.5f; // Multiplier on damage robots do to each other or themselves
    constexpr float POWERUP_RADIUS_MULT = 2.00f; // Make powerups easier to pick up
    constexpr float NEARBY_PORTAL_DEPTH = 150.0f; // How far to search when determining 'nearby' rooms

    inline int Difficulty = 2; // 0 to 4 for trainee to insane
    inline int LevelNumber = 0; // Index of loaded level starting at 1. Secret levels are negative. 0 means no level loaded.
    inline bool NeedsResourceReload = false; // Indicates that resources should be reloaded, typically due to changes in graphics settings

    constexpr int DEFAULT_GRAVITY = 30;
    inline Vector3 Gravity = { 0, -DEFAULT_GRAVITY, 0 }; // u/s acceleration

    // The loaded level. Only one level can be active at a time.
    inline Inferno::Level Level;
    inline IntersectContext Intersect(Level);

    // Returns true if an object has line of sight to a target. Also checks if the target is cloaked.
    inline bool ObjectCanSeeObject(const Object& obj, const Object& target, float maxDist = -1) {
        if (target.IsCloakEffective() || !target.IsAlive()) return false;
        auto [dir, dist] = GetDirectionAndDistance(target.Position, obj.Position);
        if (maxDist > 0)
            if (dist >= maxDist) return false;

        Ray ray(obj.Position, dir);
        LevelHit hit;
        RayQuery query(dist, obj.Segment, RayQueryMode::Visibility);
        return !Intersect.RayLevel(ray, query, hit);
    }

    // The loaded mission. Not always present.
    inline Option<HogFile> Mission;

    // Only single player for now
    inline class Player Player = {};
    inline ObjRef DeathCamera = {};

    // is the game level loading?
    inline std::atomic IsLoading = false;

    void LoadLevel(Inferno::Level&&);

    void LoadMission(const filesystem::path& file);

    inline void UnloadMission() {
        Mission = {};
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo();

    inline double Time = 0; // Elapsed game time since level start in seconds. Stops when paused.
    inline float DeltaTime = 0; // Elapsed fixed-step game time in seconds since last update. 0 when paused.
    inline float LerpAmount = 1; // How much to lerp between the previous and next object states

    void WeaponHitObject(const LevelHit& hit, Object& src);
    void WeaponHitWall(const LevelHit& hit, Object& obj, Inferno::Level& level, ObjID objId);

    // Fires a weapon from a model gunpoint
    ObjRef FireWeapon(Object& obj, WeaponID, uint8 gun, Vector3* customDir = nullptr, float damageMultiplier = 1, bool showFlash = true, float volume = DEFAULT_WEAPON_VOLUME);

    // Spread is x/y units relative to the object's forward direction
    Vector3 GetSpreadDirection(const Object& obj, const Vector2& spread);

    // Detonates a weapon with a splash radius
    void ExplodeWeapon(struct Level& level, const Object&);

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp);

    void Update(float dt);

    void CreateMissileSpawn(const Object& missile, uint blobs);

    void UpdateWeapon(Object&, float dt);

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
    constexpr float PLAYER_HIT_WALL_NOISE = 1;
    constexpr float PLAYER_HIT_WALL_RADIUS = 100;
    constexpr float PLAYER_FUSION_SOUND_RADIUS = 120;
    constexpr float FUSION_SHAKE_STRENGTH = 3.5f; // Amount charging fusion shakes the player
    constexpr uint16 VULCAN_AMMO_PICKUP = 2000; // Amount of ammo to give when picking up vulcan ammo
    constexpr float CLOAK_TIME = 30.0f;
    constexpr float INVULNERABLE_TIME = 30.0f;

    // 255 marks where weapons aren't considered for autoselection

    constexpr Array<uint8, 11> DefaultPrimaryPriority{ 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 255 };
    constexpr Array<uint8, 11> DefaultSecondaryPriority{ 9, 8, 4, 3, 1, 5, 0, 255, 7, 6, 2 };

    inline Array<uint8, 11> PrimaryPriority = DefaultPrimaryPriority;
    inline Array<uint8, 11> SecondaryPriority = DefaultSecondaryPriority;

    inline bool Cheater = false;

    void AddPointsToScore(int points);

    inline Color ScreenFlash = { 0, 0, 0 }; // Used when picking up an item or taking damage
    constexpr float MAX_FLASH = 0.4f;
    constexpr float FLASH_DECAY_RATE = MAX_FLASH * 0.75f;

    void AddScreenFlash(const Color&);

    inline bool ControlCenterDestroyed = false;
    inline float CountdownTimer = -1.0f; // time before reactor goes critical
    inline int CountdownSeconds = -1; // seconds before the reactor goes critical. Used for HUD value and audio.
    inline int TotalCountdown = -1; // the starting countdown time
    inline float GlobalDimming = 1; // Amount of global fade to apply to 'mine' light sources

    void SetState(GameState);
    GameState GetState();

    inline bool InGame() { return GetState() == GameState::Game; }
    inline NavigationNetwork Navigation;

    inline Object& GetPlayerObject() {
        return Level.Objects[(int)Player.Reference.Id];
    }

    bool ObjectIsInFOV(const Ray& ray, const Object& obj, float fov);

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

    // Gets an object by reference. Returns null if not found.
    Object* GetObject(ObjRef);
    inline List<RoomID> ActiveRooms; // Rooms that are visible or near the player

    namespace Debug {
        inline uint LiveObjects = 0;
        inline int ActiveRobots = 0;
    }

    inline float FrameTime = 0; // Time of this frame in seconds. 0 when paused.
    inline bool ResetDeltaTime = false;
}
