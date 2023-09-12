#pragma once
#include "Game.Navigation.h"
#include "Level.h"
#include "HogFile.h"
#include "Mission.h"
#include "Game.Player.h"
#include "SystemClock.h"

namespace Inferno {
    struct LevelHit;

    enum class GameState {
        Game, // In first person and running game logic
        Paused, // In game but paused or in a menu
        ExitSequence, // exit tunnel sequence
        ScoreScreen,
        Briefing,
        MainMenu,
        Editor
    };
}

namespace Inferno::Game {
    constexpr float TICK_RATE = 1.0f / 64; // 64 ticks per second (homing missiles use 32 ticks per second)

    inline int Difficulty = 2; // 0 to 4 for trainee to insane

    constexpr int DEFAULT_GRAVITY = 30;
    inline Vector3 Gravity = { 0, -DEFAULT_GRAVITY, 0 }; // u/s acceleration

    // The loaded level. Only one level can be active at a time.
    inline Inferno::Level Level;

    // The loaded mission. Not always present.
    inline Option<HogFile> Mission;

    // Only single player for now
    inline struct Player Player = {};

    // is the game level loading?
    inline std::atomic IsLoading = false;

    void AttachLight(Object& obj, ObjRef ref);

    void LoadLevel(Inferno::Level&&);

    void LoadMission(const filesystem::path& file);

    inline void UnloadMission() {
        Mission = {};
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo();

    inline double Time = 0; // Elapsed game time since level start in seconds. Stops when paused.
    inline float DeltaTime = 0; // Elapsed fixed-step game time since last update. 0 when paused.
    inline float LerpAmount = 1; // How much to lerp between the previous and next object states

    void WeaponHitObject(const LevelHit& hit, Object& src);
    void WeaponHitWall(const LevelHit& hit, Object& obj, Inferno::Level& level, ObjID objId);

    void FireWeapon(ObjRef, WeaponID, uint8 gun, Vector3* customDir = nullptr, float damageMultiplier = 1, bool showFlash = true, bool playSound = true);
    Vector3 GetSpreadDirection(ObjID objId, const Vector2& spread);

    // Detonates a weapon with a splash radius
    void ExplodeWeapon(struct Level& level, const Object&);

    void Update(float dt);

    // Finds the nearest object ID to an object
    Tuple<ObjID, float> FindNearestObject(const Vector3& position, float maxDist, ObjectMask mask = ObjectMask::Any);
    Tuple<ObjID, float> FindNearestVisibleObject(const Vector3& position, SegID, float maxDist, ObjectMask, span<ObjID> objFilter);

    void UpdateWeapon(Object&, float dt);

    inline bool ShowDebugOverlay = false;

    inline bool SecretLevelDestroyed = false;

    // Schedules an object to be added at end of update
    void AddObject(const Object&);

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
    constexpr float MINE_ARM_TIME = 2.0f; // How long before player can shoot or be hit by their own mines
    constexpr int EXTRA_LIFE_POINTS = 50'000;
    constexpr uint HOSTAGE_SCORE = 1000;
    constexpr uint REACTOR_SCORE = 5000;
    // 255 marks where weapons aren't considered for autoselection

    constexpr Array<uint8, 11> DefaultPrimaryPriority{ 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 255 };
    constexpr Array<uint8, 11> DefaultSecondaryPriority{ 9, 8, 4, 3, 1, 5, 0, 255, 7, 6, 2 };

    inline Array<uint8, 11> PrimaryPriority = DefaultPrimaryPriority;
    inline Array<uint8, 11> SecondaryPriority = DefaultSecondaryPriority;

    inline bool Cheater = false;

    void AddPointsToScore(int points);

    inline Color ScreenFlash = { 0, 0, 0 }; // Used when picking up an item or taking damage
    constexpr float MAX_FLASH = 0.1f;
    constexpr float FLASH_DECAY_RATE = MAX_FLASH * 0.75f;

    void AddScreenFlash(const Color&);

    inline bool ControlCenterDestroyed = false;
    inline float CountdownTimer = -1.0f; // time before reactor goes critical
    inline int CountdownSeconds = -1; // seconds before the reactor goes critical
    inline int TotalCountdown = -1; // the starting countdown time

    void SetState(GameState);
    GameState GetState();

    inline bool InGame() { return GetState() == GameState::Game; }
    inline NavigationNetwork Navigation;

    inline Object& GetPlayer() {
        return Level.Objects[(int)Player.Reference.Id];
    }

    bool ObjectIsInFOV(const Ray& ray, const Object& other, float fov);

    // Returns the object ID based on its address
    inline ObjID GetObjectID(const Object& obj) {
        auto id = ObjID(&obj - Level.Objects.data());
        assert((int)id < Level.Objects.size() && (int)id >= 0); // Object wasn't in the level
        return id;
    }

    // Returns an object reference based on its address
    inline ObjRef GetObjectRef(const Object& obj) {
        auto id = ObjID(&obj - Level.Objects.data());
        assert((int)id < Level.Objects.size() && (int)id >= 0); // Object wasn't in the level
        return { id, obj.Signature };
    }

    // Returns an object reference based on its ID
    inline ObjRef GetObjectRef(ObjID id) {
        if(auto obj = Level.TryGetObject(id))
            return { id, obj->Signature };
        else
            return {}; // null handle
    }
}
