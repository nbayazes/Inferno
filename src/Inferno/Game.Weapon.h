#pragma once
#include "Game.Object.h"
#include "SoundTypes.h"

namespace Inferno {
    struct LevelHit;
}

namespace Inferno::Game {
    struct FireWeaponInfo {
        WeaponID id;
        uint8 gun;
        Vector3* customDir = nullptr;
        float volume = 1;
        float damageMultiplier = 1;
        bool showFlash = true;
    };

    void WeaponHitObject(const LevelHit& hit, Object& src);
    void AddWeaponDecal(const LevelHit& hit, const Weapon& weapon);

    // Plays a weapon sound attached to an object. If gun = 255 the object center is used.
    void PlayWeaponSound(WeaponID id, float volume, const Object& parent, uint8 gun = 255);

    Sound3D InitWeaponSound(WeaponID id, float volume);

    //void PlayWeaponSound(WeaponID id, float volume, SegID segment, const Vector3& position);

    // Fires a weapon from a model gunpoint
    ObjRef FireWeapon(Object& obj, const FireWeaponInfo& info);

    // Detonates a weapon with a splash radius
    void ExplodeWeapon(struct Level& level, const Object&);

    void WeaponHitWall(const LevelHit& hit, Object& obj, Inferno::Level& level, ObjID objId);

    void CreateMissileSpawn(const Object& missile, uint blobs);

    void UpdateWeapon(Object&, float dt);

    // Spread is x/y units relative to the object's forward direction
    Vector3 GetSpreadDirection(const Object& obj, const Vector2& spread);

}
