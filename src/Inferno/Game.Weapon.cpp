#include "pch.h"

#include "Game.AI.h"
#include "Object.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Wall.h"
#include "Physics.h"
#include "SoundSystem.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Game {
    void ExplodeWeapon(const Object& obj) {
        if (obj.Type != ObjectType::Weapon) return;
        const Weapon& weapon = Resources::GetWeapon((WeaponID)obj.ID);
        if (weapon.SplashRadius <= 0) return; // don't explode weapons without a splash radius

        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;
        float damage = weapon.Damage[Game::Difficulty];

        Render::ExplosionInfo e;
        e.Radius = { weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f };
        e.Clip = vclip;
        e.Sound = soundId;
        //e.Color = Color{ 1, 1, 1 };
        e.FadeTime = 0.1f;
        Render::CreateExplosion(e, obj.Segment, obj.Position);

        GameExplosion ge{};
        ge.Damage = damage;
        ge.Force = damage;
        ge.Radius = weapon.SplashRadius;
        ge.Segment = obj.Segment;
        ge.Position = obj.Position;
        CreateExplosion(Game::Level, &obj, ge);
    }

    void ProxMineBehavior(Object& obj) {
        constexpr auto PROX_WAKE_RANGE = 60;
        constexpr auto PROX_ACTIVATE_RANGE = 30;

        auto& cw = obj.Control.Weapon;

        if (TimeHasElapsed(obj.NextThinkTime)) {
            obj.NextThinkTime = (float)Game::Time + 0.25f;

            // Try to find a nearby target
            if (cw.TrackingTarget == ObjID::None) {
                // todo: filter targets based on if mine owner is a player
                auto [id, dist] = Game::FindNearestObject(obj.Position, PROX_WAKE_RANGE, ObjectMask::Enemy);
                if (id != ObjID::None && dist <= PROX_WAKE_RANGE)
                    cw.TrackingTarget = id; // New target!
            }
        }

        if (cw.TrackingTarget == ObjID::None)
            return; // Still no target

        auto target = Game::Level.TryGetObject(cw.TrackingTarget);
        auto dist = target ? obj.Distance(*target) : FLT_MAX;

        if (dist > PROX_WAKE_RANGE) {
            cw.TrackingTarget = ObjID::None; // Went out of range
        }
        else {
            //auto lerp = std::lerp(1.00f, 2.00f, (dist - PROX_ACTIVATE_RANGE) / (PROX_WAKE_RANGE - PROX_ACTIVATE_RANGE));
            //lerp = std::clamp(lerp, 1.0f, 2.0f);

            //if (TimeHasElapsed(cw.SoundDelay)) {
            //    Sound3D sound(obj.Position, obj.Segment);
            //    //sound.Pitch = 0.25f - (lerp - 1.25);
            //    sound.Resource = Resources::GetSoundResource(SoundID::HomingWarning);
            //    Sound::Play(sound);
            //    cw.SoundDelay = (float)Game::Time + lerp;
            //}

            if (dist <= PROX_ACTIVATE_RANGE && !cw.DetonateMine) {
                // Commit to the target
                cw.DetonateMine = true;
                obj.Lifespan = 2;
                ClearFlag(obj.Physics.Flags, PhysicsFlag::Bounce); // explode on contacting walls

                if (target) {
                    auto delta = target->Position - obj.Position;
                    delta.Normalize();
                    obj.Physics.Thrust = delta * 0.9; // fire and forget thrust
                }
            }
        }
    }

    void CreateSmartBlobs(const Object& obj) {
        auto parentType = obj.Control.Weapon.ParentType;

        List<ObjID> targets;
        targets.reserve(30);

        for (int i = 0; i < Game::Level.Objects.size(); i++) {
            auto& other = Game::Level.Objects[i];
            auto validTarget = other.Type == ObjectType::Robot && !other.Control.AI.IsCloaked() || other.Type == ObjectType::Player;

            if (!validTarget || (ObjID)i == other.Control.Weapon.Parent)
                continue; // Don't target cloaked robots (todo: or players?) or the owner

            if (other.Type == ObjectType::Player && parentType == ObjectType::Player)
                continue; // don't track the owning player

            if (other.Type == ObjectType::Robot && parentType == ObjectType::Robot)
                continue; // robot blobs can't track other robots

            auto dist = Vector3::Distance(other.Position, obj.Position);
            constexpr auto MAX_SMART_DISTANCE = 150.0f;
            if (dist <= MAX_SMART_DISTANCE) {
                if (ObjectToObjectVisibility(obj, other, true))
                    targets.push_back((ObjID)i);
            }
        }

        //const int children = 6; // todo: add to extended weapon data
        //auto wid = parentType == ObjectType::Player ? WeaponID::PlayerSmartBlob : WeaponID::RobotSmartBlob;

        //if (targets.empty()) {
        //    for (int i = 0; i < children; i++) {
        //        //CreateHomingWeapon(-1); // random target
        //    }
        //}
        //else {
        //    for (int i = 0; i < children; i++) {
        //        //CreateHomingWeapon(); // use random target in list
        //    }
        //}
    }

    void AddPlanarExplosion(const Weapon& weapon, const LevelHit& hit) {
        auto rotation = Matrix::CreateFromAxisAngle(hit.Normal, Random() * DirectX::XM_2PI);

        // Add the planar explosion effect
        Render::DecalInfo planar{};
        planar.Normal = hit.Normal;
        planar.Tangent = Vector3::Transform(hit.Tangent, rotation);
        planar.Bitangent = planar.Tangent.Cross(hit.Normal);
        planar.Texture = weapon.Extended.ExplosionTexture;
        planar.Radius = weapon.Extended.ExplosionSize;
        planar.Duration = planar.FadeTime = weapon.Extended.ExplosionTime;
        planar.Segment = hit.Tag.Segment;
        planar.Side = hit.Tag.Side;
        planar.Position = hit.Point;
        planar.FadeRadius = weapon.GetDecalSize() * 2.4f;
        planar.Additive = true;
        planar.Color = Color{ 1.5f, 1.5f, 1.5f };
        planar.LightColor = weapon.Extended.ExplosionColor;
        planar.LightRadius = weapon.Extended.LightRadius;

        Render::AddDecal(planar);
    }

    void WeaponHitObject(const LevelHit& hit, Object& src) {
        assert(hit.HitObj);
        assert(src.IsWeapon());
        const auto& weapon = Resources::GameData.Weapons[src.ID];
        const float damage = weapon.Damage[Game::Difficulty] * src.Control.Weapon.Multiplier;

        auto& target = *hit.HitObj;
        src.LastHitObject = target.Signature;

        if (target.Type == ObjectType::Weapon) {
            // a bomb or other weapon was shot. cause it to explode by expiring.
            target.Lifespan = -1;
            if (weapon.SplashRadius == 0)
                return; // non-explosive weapons keep going
        }
        else {
            if (target.IsPlayer()) {
                // Players don't take direct damage from explosive weapons for balance reasons
                // The secondary explosion will still inflict damage
                if (!weapon.IsExplosive())
                    Game::Player.ApplyDamage(damage);
            }
            else if (target.IsRobot()) {
                DamageRobot(target, damage);
            }
            else {
                target.ApplyDamage(damage);
            }

            //fmt::print("applied {} damage\n", damage);

            // Always play hit sound for players. Explosions are separate.
            if (target.IsPlayer()) {
                Sound3D sound(hit.Point, hit.Tag.Segment);
                sound.Resource = Resources::GetSoundResource(SoundID::HitPlayer);
                Sound::Play(sound);
            }
            else if (!weapon.IsExplosive()) {
                // Missiles create their explosion effects when expiring
                Render::ExplosionInfo expl;
                expl.Sound = weapon.RobotHitSound;
                //expl.Parent = src.Parent;
                expl.Clip = VClipID::SmallExplosion;
                expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
                //expl.Color = Color{ 1.15f, 1.15f, 1.15f };
                expl.FadeTime = 0.1f;
                Render::CreateExplosion(expl, hit.HitObj->Segment, hit.Point);
            }

            //AddPlanarExplosion(weapon, hit);

            // More damage creates more sparks
            float damageMult = std::clamp(damage / 20.0f, 1.0f, 2.0f);
            if (auto sparks = Render::EffectLibrary.GetSparks("weapon_hit_obj")) {
                sparks->Color += weapon.Extended.ExplosionColor * 60;
                sparks->Color.w = 1;
                sparks->LightColor = weapon.Extended.ExplosionColor;
                sparks->LightRadius = weapon.Extended.LightRadius;
                sparks->Count.Min = int(sparks->Count.Min * damageMult);
                sparks->Count.Max = int(sparks->Count.Max * damageMult);
                Render::AddSparkEmitter(*sparks, hit.HitObj->Segment, hit.Point);
            }

            //if (weapon.RobotHitSound != SoundID::None || !weapon.Extended.ExplosionSound.empty()) {
            //    auto soundRes = Resources::GetSoundResource(weapon.RobotHitSound);
            //    soundRes.D3 = weapon.Extended.ExplosionSound;

            //    Sound3D sound(hit.Point, hit.HitObj->Segment);
            //    sound.Resource = soundRes;
            //    Sound::Play(sound);
            //}
        }

        src.Control.Weapon.AddRecentHit(target.Signature);

        if (!weapon.Piercing)
            src.Lifespan = -1; // Schedule to explode
    }

    void WeaponHitWall(const LevelHit& hit, Object& obj, Inferno::Level& level, ObjID objId) {
        if (!hit.Tag) return;
        bool isPlayer = obj.Control.Weapon.ParentType == ObjectType::Player;
        CheckDestroyableOverlay(level, hit.Point, hit.Tag, hit.Tri, isPlayer);

        auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
        float damage = weapon.Damage[Game::Difficulty];
        float splashRadius = weapon.SplashRadius;
        float force = damage;
        float impactSize = weapon.ImpactSize;

        // don't use volatile hits on large explosions like megas
        constexpr float VOLATILE_DAMAGE_RADIUS = 30;
        bool isLargeExplosion = splashRadius >= VOLATILE_DAMAGE_RADIUS / 2;

        // weapons with splash damage (explosions) always use robot hit effects
        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;

        bool addDecal = !weapon.Extended.Decal.empty();
        bool hitLiquid = false;
        bool hitForcefield = false;

        auto& side = level.GetSide(hit.Tag);
        auto& ti = Resources::GetLevelTextureInfo(side.TMap);

        hitForcefield = ti.HasFlag(TextureFlag::ForceField);
        if (hitForcefield) {
            addDecal = false;

            if (!weapon.IsMatter) {
                // Bounce energy weapons
                obj.Physics.Bounces++;
                obj.Parent = ObjID::None; // Make hostile to owner!

                Sound3D sound(hit.Point, hit.Tag.Segment);
                sound.Resource = Resources::GetSoundResource(SoundID::WeaponHitForcefield);
                Sound::Play(sound);
            }
        }

        if (ti.HasFlag(TextureFlag::Volatile)) {
            if (!isLargeExplosion) {
                // add volatile size and damage bonuses to smaller explosions
                vclip = VClipID::HitLava;
                constexpr float VOLATILE_DAMAGE = 10;
                constexpr float VOLATILE_FORCE = 5;

                damage = damage / 4 + VOLATILE_DAMAGE;
                splashRadius += VOLATILE_DAMAGE_RADIUS;
                force = force / 2 + VOLATILE_FORCE;
                impactSize += 1;
            }

            soundId = SoundID::HitLava;
            addDecal = false;
            hitLiquid = true;
        }
        else if (ti.HasFlag(TextureFlag::Water)) {
            if (weapon.IsMatter)
                soundId = SoundID::MissileHitWater;
            else
                soundId = SoundID::HitWater;

            if (isLargeExplosion) {
                // reduce strength of megas and shakers in water, but don't cancel them
                splashRadius *= 0.5f;
                damage *= 0.25f;
                force *= 0.5f;
                impactSize *= 0.5f;
            }
            else {
                vclip = VClipID::HitWater;
                splashRadius = 0; // Cancel explosions when hitting water
            }

            addDecal = false;
            hitLiquid = true;
        }

        if (Settings::Inferno.Descent3Enhanced && addDecal) {
            auto decalSize = weapon.Extended.DecalRadius ? weapon.Extended.DecalRadius : weapon.ImpactSize / 3;

            Render::DecalInfo decal{};
            auto rotation = Matrix::CreateFromAxisAngle(hit.Normal, Random() * DirectX::XM_2PI);
            decal.Normal = hit.Normal;
            decal.Tangent = Vector3::Transform(hit.Tangent, rotation);
            decal.Bitangent = decal.Tangent.Cross(hit.Normal);
            decal.Radius = decalSize;
            decal.Position = hit.Point;
            decal.Segment = hit.Tag.Segment;
            decal.Side = hit.Tag.Side;
            decal.Texture = weapon.Extended.Decal;

            // check that decal isn't too close to edge due to lack of clipping
            if (hit.EdgeDistance >= decalSize * 0.75f) {
                if (auto wall = Game::Level.TryGetWall(hit.Tag)) {
                    if (Game::Player.CanOpenDoor(*wall))
                        addDecal = false; // don't add decals to unlocked doors, as they will disappear on the next frame
                    else if (wall->Type != WallType::WallTrigger)
                        addDecal = wall->State == WallState::Closed; // Only allow decals on closed walls
                }

                if (addDecal)
                    Render::AddDecal(decal);
            }

            if (!weapon.Extended.ExplosionTexture.empty() && !obj.Physics.CanBounce()) {
                AddPlanarExplosion(weapon, hit);
                vclip = VClipID::None;
            }
        }

        if (HasFlag(obj.Physics.Flags, PhysicsFlag::Stick) && !hitLiquid && !hitForcefield) {
            // sticky flare behavior
            Vector3 vec;
            obj.Physics.Velocity.Normalize(vec);
            obj.Position += vec * hit.Distance;
            obj.Physics.Velocity = Vector3::Zero;
            //obj.Movement = MovementType::None;
            //obj.LastPosition = obj.Position;
            StuckObjects.Add(hit.Tag, objId);
            obj.Flags |= ObjectFlag::Attached;
            return;
        }

        if (hit.Bounced && !hitLiquid)
            return; // don't create explosions when bouncing

        obj.Flags |= ObjectFlag::Dead; // remove weapon after hitting a wall

        auto dir = obj.Physics.PrevVelocity;
        dir.Normalize();

        if (soundId != SoundID::None || !weapon.Extended.ExplosionSound.empty()) {
            auto soundRes = Resources::GetSoundResource(soundId);
            if (!hitLiquid)
                soundRes.D3 = weapon.Extended.ExplosionSound;

            Sound3D sound(hit.Point, hit.Tag.Segment);
            sound.Resource = soundRes;
            sound.Source = obj.Parent;
            Sound::Play(sound);
        }

        if (vclip != VClipID::None) {
            Render::ExplosionInfo e;
            e.Radius = { impactSize * 0.9f, impactSize * 1.1f };
            e.Clip = vclip;
            e.Parent = obj.Parent;

            Vector3 position;
            // move explosions out of wall
            if (impactSize < 5)
                position = hit.Point - dir * impactSize * 0.25f;
            else
                position = hit.Point - dir * 2.5;

            e.FadeTime = 0.1f;
            if (weapon.Extended.ExplosionColor != Color(-1, -1, -1))
                e.LightColor = weapon.Extended.ExplosionColor;
            e.LightRadius = weapon.Extended.LightRadius;

            if (obj.ID == (int)WeaponID::Concussion) {
                e.Instances = 3;
                e.Delay = { 0, 0 };
            }

            Render::CreateExplosion(e, hit.Tag.Segment, position);
        }

        if (splashRadius > 0) {
            GameExplosion ge{};
            ge.Segment = hit.Tag.Segment;
            ge.Position = hit.Point + hit.Normal * obj.Radius; // shift explosion out of wall
            ge.Damage = damage;
            ge.Force = force;
            ge.Radius = splashRadius;

            CreateExplosion(level, &obj, ge);
        }
    }

    Vector3 GetSpreadDirection(ObjID objId, const Vector2& spread) {
        auto& obj = Game::Level.Objects[(int)objId];
        auto direction = obj.Rotation.Forward();

        if (spread != Vector2::Zero) {
            direction += obj.Rotation.Right() * spread.x;
            direction += obj.Rotation.Up() * spread.y;
        }

        return direction;
    }

    void FireSpreadWeapon(ObjID objId, uint8 gun, WeaponID id, bool showFlash = true, const Vector2& spread = Vector2::Zero) {
        auto direction = GetSpreadDirection(objId, spread);
        FireWeapon(objId, id, gun, &direction, showFlash);
    }

    void FireWeapon(ObjID objId, WeaponID id, uint8 gun, Vector3* customDir, float damageMultiplier, bool showFlash, bool playSound) {
        auto& level = Game::Level;
        auto pObj = level.TryGetObject(objId);
        if (!pObj) {
            __debugbreak(); // tried to fire weapon from unknown object
            return;
        }
        auto& obj = *pObj;

        auto& weapon = Resources::GetWeapon(id);
        if (obj.IsPlayer() && gun == 6 && Game::GetState() == GameState::Game)
            showFlash = false; // Hide flash in first person

        auto gunSubmodel = GetLocalGunpointOffset(obj, gun);
        auto objOffset = GetSubmodelOffset(obj, gunSubmodel);
        auto position = Vector3::Transform(objOffset, obj.GetTransform());
        Vector3 direction = customDir ? *customDir : obj.Rotation.Forward();

        Object bullet{};
        bullet.Position = bullet.PrevPosition = position;

        // Align to direction
        auto forward = -direction;
        auto right = obj.Rotation.Up().Cross(forward);
        auto up = right.Cross(forward);
        right = up.Cross(forward); // ensure orthogonal
        bullet.Rotation = bullet.PrevRotation = Matrix3x3(right, up, forward);

        bullet.Movement = MovementType::Physics;
        bullet.Physics.Velocity = direction * weapon.Speed[Game::Difficulty]/* * 0.01f*/;
        if (weapon.Extended.InheritParentVelocity)
            bullet.Physics.Velocity += obj.Physics.Velocity;

        bullet.Physics.Flags |= weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
        bullet.Physics.AngularVelocity = weapon.Extended.RotationalVelocity;
        bullet.Physics.Flags |= PhysicsFlag::FixedAngVel; // HACK
        if (weapon.Piercing) bullet.Physics.Flags |= PhysicsFlag::Piercing;
        if (weapon.Extended.Sticky) bullet.Physics.Flags |= PhysicsFlag::Stick;
        bullet.Physics.Drag = weapon.Drag;
        bullet.Physics.Mass = weapon.Mass;
        bullet.Physics.Bounces = weapon.Extended.Bounces;
        if (bullet.Physics.Bounces > 0)
            ClearFlag(bullet.Physics.Flags, PhysicsFlag::Bounce); // remove the bounce flag as physics will stop when bounces = 0

        bullet.Control.Type = ControlType::Weapon;
        bullet.Control.Weapon = {};
        bullet.Control.Weapon.ParentType = obj.Type;
        bullet.Control.Weapon.Multiplier = damageMultiplier;

        bullet.LightColor = weapon.Extended.LightColor;
        bullet.LightRadius = weapon.Extended.LightRadius;
        bullet.LightMode = weapon.Extended.LightMode;

        if (weapon.RenderType == WeaponRenderType::Blob) {
            bullet.Render.Type = RenderType::Laser; // Blobs overload the laser render path
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.BlobBitmap);
        }
        else if (weapon.RenderType == WeaponRenderType::VClip) {
            bullet.Render.Type = RenderType::WeaponVClip;
            bullet.Render.VClip.ID = weapon.WeaponVClip;
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.WeaponVClip);
        }
        else if (weapon.RenderType == WeaponRenderType::Model) {
            bullet.Render.Type = RenderType::Model;

            auto& model = Resources::GetModel(weapon.Model);
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : model.Radius / weapon.ModelSizeRatio;
            if (bullet.Radius < 0) bullet.Radius = 1;

            auto d3Model = weapon.Extended.ModelName.empty() ? ModelID::None : Render::LoadOutrageModel(weapon.Extended.ModelName);

            if (Settings::Inferno.Descent3Enhanced && d3Model != ModelID::None) {
                bullet.Render.Model.ID = Render::LoadOutrageModel(weapon.Extended.ModelName);
                bullet.Render.Model.Outrage = true;
                bullet.Scale = weapon.Extended.ModelScale;
            }
            else {
                bullet.Render.Model.ID = weapon.Model;
            }

            // Randomize the rotation of models
            auto rotation = Matrix::CreateFromAxisAngle(bullet.Rotation.Forward(), Random() * DirectX::XM_2PI);
            bullet.Rotation *= rotation;
            bullet.PrevRotation = bullet.Rotation;

            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
            Render::LoadModelDynamic(weapon.ModelInner);

            if (bullet.Render.Model.ID == ModelID::None)
                bullet.Render.Type = RenderType::None;
        }
        else if (weapon.RenderType == WeaponRenderType::None) {
            bullet.Radius = 0.1f; // original game used a value of 1
        }

        bullet.Render.Rotation = Random() * DirectX::XM_2PI;

        bullet.Lifespan = weapon.Lifetime;
        //bullet.Lifespan = 3; // for testing fade-out
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = objId;
        bullet.Segment = obj.Segment;

        bullet.Render.Emissive = weapon.Extended.Glow;

        if (id == WeaponID::ProxMine || id == WeaponID::SmartMine) {
            bullet.NextThinkTime = (float)Game::Time + MINE_ARM_TIME;
        }

        if (playSound) {
            Sound3D sound(objId);
            sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
            sound.Volume = 0.55f;
            sound.AttachToSource = true;
            sound.AttachOffset = obj.Position - position;
            sound.Radius = weapon.Extended.SoundRadius;
            sound.FromPlayer = obj.IsPlayer();
            sound.Source = objId;
            sound.Merge = true;

            if (id == WeaponID::Vulcan) {
                sound.Merge = false;
                sound.Pitch -= Random() * 0.05f;
            }

            Sound::Play(sound);
        }

        if (showFlash) {
            Render::Particle p{};
            p.Clip = weapon.FlashVClip;
            p.Position = position;
            p.Radius = weapon.FlashSize;
            p.Parent = objId;
            p.ParentSubmodel = gunSubmodel;
            p.FadeTime = 0.175f;
            p.Color = weapon.Extended.FlashColor;
            Render::AddParticle(p, obj.Segment);
        }

        bullet.Rotation.Normalize();
        bullet.PrevRotation = bullet.Rotation;
        AddObject(bullet);
    }

    void SpreadfireBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        //constexpr float SPREAD_ANGLE = 1 / 16.0f * RadToDeg;
        auto spread = Resources::GetWeapon(wid).Extended.Spread * DegToRad;

        if (player.SpreadfireToggle) {
            // Vertical
            FireSpreadWeapon(player.ID, gun, wid);
            FireSpreadWeapon(player.ID, gun, wid, false, { 0, -spread });
            FireSpreadWeapon(player.ID, gun, wid, false, { 0, spread });
        }
        else {
            // Horizontal
            FireSpreadWeapon(player.ID, gun, wid);
            FireSpreadWeapon(player.ID, gun, wid, false, { -spread, 0 });
            FireSpreadWeapon(player.ID, gun, wid, false, { spread, 0 });
        }

        player.SpreadfireToggle = !player.SpreadfireToggle;
    }

    constexpr Vector2 GetHelixOffset(int index) {
        switch (index) {
            default:
            case 0: return { 1 / 16.0f, 0 };
            case 1: return { 1 / 17.0f, 1 / 42.0f };
            case 2: return { 1 / 22.0f, 1 / 22.0f };
            case 3: return { 1 / 42.0f, 1 / 17.0f };
            case 4: return { 0, 1 / 16.0f };
            case 5: return { -1 / 42.0f, 1 / 17.0f };
            case 6: return { -1 / 22.0f, 1 / 22.0f };
            case 7: return { -1 / 17.0f, 1 / 42.0f };
        }
    }

    void HelixBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        player.HelixOrientation = (player.HelixOrientation + 1) % 8;
        auto offset = GetHelixOffset(player.HelixOrientation);
        FireSpreadWeapon(player.ID, gun, wid);
        FireSpreadWeapon(player.ID, gun, wid, false, offset);
        FireSpreadWeapon(player.ID, gun, wid, false, offset * 2);
        FireSpreadWeapon(player.ID, gun, wid, false, -offset);
        FireSpreadWeapon(player.ID, gun, wid, false, -offset * 2);
    }

    void VulcanBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        //constexpr float SPREAD_ANGLE = 1 / 32.0f * RadToDeg; // -0.03125 to 0.03125 spread
        auto spread = Resources::GetWeapon(wid).Extended.Spread * DegToRad;
        auto point = RandomPointInCircle(spread);
        FireSpreadWeapon(player.ID, gun, wid, true, { point.x, point.y });
    }

    void ShotgunBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        auto& weapon = Resources::GetWeapon(wid);
        auto spread = weapon.Extended.Spread * DegToRad;

        bool flash = true;
        for (size_t i = 0; i < weapon.FireCount; i++) {
            auto point = RandomPointInCircle(spread);
            FireSpreadWeapon(player.ID, gun, wid, flash, { point.x, point.y });
            flash = false;
        }
    }

    // FOV in 0 to PI
    bool ObjectIsInFOV(const Ray& ray, const Object& other, float fov) {
        auto vec = other.Position - ray.position;
        vec.Normalize();
        auto angle = AngleBetweenVectors(ray.direction, vec);
        return angle <= fov;
    }

    // Used for omega and homing weapons
    ObjID GetClosestObjectInFOV(const Object& src, float fov, float dist, ObjectMask mask) {
        auto result = ObjID::None;
        float minDist = FLT_MAX;

        // todo: don't scan all objects, only nearby ones
        for (int i = 0; i < Game::Level.Objects.size(); i++) {
            auto& obj = Game::Level.Objects[i];
            if (!obj.IsAlive()) continue;
            if (!obj.PassesMask(mask)) continue;

            auto odist = obj.Distance(src);
            if (odist > dist || odist >= minDist) continue;

            auto vec = obj.Position - src.Position;
            vec.Normalize();
            Ray targetRay(src.Position, vec);
            LevelHit hit;

            if (ObjectIsInFOV(Ray(src.Position, src.Rotation.Forward()), obj, fov) &&
                !IntersectRayLevel(Game::Level, targetRay, src.Segment, odist, false, true, hit)) {
                minDist = odist;
                result = (ObjID)i;
            }
        }

        return result;
    }

    void OmegaBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        constexpr auto FOV = 12.5f * DegToRad;
        constexpr auto MAX_DIST = 60;
        constexpr auto MAX_TARGETS = 3;
        constexpr auto MAX_CHAIN_DIST = 30;

        player.OmegaCharge -= OMEGA_CHARGE_COST;
        player.OmegaCharge = std::max(0.0f, player.OmegaCharge);

        const auto& weapon = Resources::GetWeapon(wid);

        auto pObj = Game::Level.TryGetObject(player.ID);
        if (!pObj) return;
        auto& playerObj = *pObj;

        auto gunSubmodel = GetLocalGunpointOffset(playerObj, gun);
        auto objOffset = GetSubmodelOffset(playerObj, gunSubmodel);
        auto start = Vector3::Transform(objOffset, playerObj.GetTransform());
        auto initialTarget = GetClosestObjectInFOV(playerObj, FOV, MAX_DIST, ObjectMask::Enemy);

        auto spark = Render::EffectLibrary.GetSparks("omega_hit");

        if (initialTarget != ObjID::None) {
            // found a target! try chaining to others
            std::array<ObjID, MAX_TARGETS> targets{};
            targets.fill(ObjID::None);
            targets[0] = initialTarget;

            for (int i = 0; i < MAX_TARGETS - 1; i++) {
                if (targets[i] == ObjID::None) break;

                if (auto src = Game::Level.TryGetObject(targets[i])) {
                    auto [id, dist] = Game::FindNearestVisibleObject(src->Position, src->Segment, MAX_CHAIN_DIST, ObjectMask::Enemy, targets);
                    if (id != ObjID::None)
                        targets[i + 1] = id;
                }
            }

            //auto prevPosition = start;
            ObjID prevObj = player.ID;
            int objGunpoint = gun;

            auto beam = Render::EffectLibrary.GetBeamInfo("omega_beam");
            auto beam2 = Render::EffectLibrary.GetBeamInfo("omega_beam2");
            auto tracer = Render::EffectLibrary.GetBeamInfo("omega_tracer");

            // Apply damage and visuals to each target
            for (auto& targetObj : targets) {
                if (targetObj == ObjID::None) continue;
                auto target = Game::Level.TryGetObject(targetObj);
                if (!target) continue;
                if (!Settings::Cheats.DisableWeaponDamage)
                    target->ApplyDamage(weapon.Damage[Difficulty]);

                // Beams between previous and next target
                if (beam) Render::AddBeam(*beam, weapon.FireDelay, prevObj, targetObj, objGunpoint);
                if (beam2) {
                    Render::AddBeam(*beam2, weapon.FireDelay, prevObj, targetObj, objGunpoint);
                    Render::AddBeam(*beam2, weapon.FireDelay, prevObj, targetObj, objGunpoint);
                }

                prevObj = targetObj;
                objGunpoint = -1;

                if (tracer) {
                    Render::AddBeam(*tracer, weapon.FireDelay, targetObj);
                    Render::AddBeam(*tracer, weapon.FireDelay, targetObj);
                }

                // Sparks and explosion
                if (spark) Render::AddSparkEmitter(*spark, target->Segment, target->Position);

                Render::ExplosionInfo expl;
                //expl.Sound = weapon.RobotHitSound;
                expl.Clip = VClipID::SmallExplosion;
                expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
                expl.Variance = target->Radius * 0.45f;
                //expl.Color = Color{ 1.15f, 1.15f, 1.15f };
                expl.FadeTime = 0.1f;
                Render::CreateExplosion(expl, target->Segment, target->Position);
            }

            // Hit sound
            constexpr std::array hitSounds = { "EnvElectricA", "EnvElectricB", "EnvElectricC", "EnvElectricD", "EnvElectricE", "EnvElectricF" };
            if (auto initialTargetObj = Game::Level.TryGetObject(initialTarget)) {
                Sound3D hitSound(initialTargetObj->Position, initialTargetObj->Segment);
                hitSound.Resource = { .D3 = hitSounds[RandomInt((int)hitSounds.size() - 1)] };
                hitSound.Volume = 2.00f;
                hitSound.Radius = 200;
                Sound::Play(hitSound);
            }
        }
        else {
            // no target: pick a random point within FOV
            auto offset = RandomPointInCircle(FOV * 0.75f);
            auto dir = playerObj.Rotation.Forward();
            dir += playerObj.Rotation.Right() * offset.x;
            dir += playerObj.Rotation.Up() * offset.y;
            dir.Normalize();

            Vector3 tracerEnd;

            LevelHit hit;
            if (IntersectRayLevel(Game::Level, { playerObj.Position, dir }, playerObj.Segment, MAX_DIST, false, true, hit)) {
                //tracer.End = beam.End = hit.Point;
                tracerEnd = hit.Point;

                if (spark) Render::AddSparkEmitter(*spark, hit.Tag.Segment, hit.Point);

                // Do wall hit stuff
                Object dummy{};
                dummy.Position = hit.Point;
                dummy.Parent = player.ID;
                dummy.ID = (int)WeaponID::Omega;
                dummy.Type = ObjectType::Weapon;
                dummy.Control.Weapon.ParentType = ObjectType::Player; // needed for wall triggers to work correctly
                WeaponHitWall(hit, dummy, Game::Level, ObjID::None);

                if (auto wall = Game::Level.TryGetWall(hit.Tag))
                    HitWall(Game::Level, hit.Point, dummy, *wall);
            }
            else {
                //SetFlag(tracer.Flags, Render::BeamFlag::FadeEnd);
                tracerEnd = start + dir * MAX_DIST;
            }

            if (auto miss = Render::EffectLibrary.GetBeamInfo("omega_miss"))
                Render::AddBeam(*miss, weapon.FireDelay, player.ID, tracerEnd, gun);
        }

        // Fire sound
        Sound3D sound(player.ID);
        sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
        sound.Volume = 0.40f;
        sound.AttachToSource = true;
        sound.AttachOffset = gunSubmodel.Offset;
        sound.FromPlayer = true;
        Sound::Play(sound);

        Render::Particle p{};
        p.Clip = weapon.FlashVClip;
        p.Position = start;
        p.Radius = weapon.FlashSize;
        p.Parent = player.ID;
        p.ParentSubmodel = gunSubmodel;
        p.FadeTime = 0.175f;
        p.Color = weapon.Extended.FlashColor;
        Render::AddParticle(p, playerObj.Segment);
    }

    void FusionBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        // Fixes original behavior of fusion jumping from 2.9x to 4x damage at 4 seconds charge
        constexpr auto MAX_FUSION_CHARGE_TIME = 4.0f; // Time in seconds for full charge
        constexpr auto MAX_FUSION_CHARGE_MULT = 3.0f; // Bonus damage multiplier for full charge
        float multiplier = MAX_FUSION_CHARGE_MULT * player.WeaponCharge / MAX_FUSION_CHARGE_TIME;
        if (multiplier > MAX_FUSION_CHARGE_MULT) multiplier = MAX_FUSION_CHARGE_MULT;
        FireWeapon(player.ID, wid, gun, nullptr, 1 + multiplier);
    }

    // default weapon firing behavior
    void DefaultBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        FireWeapon(player.ID, wid, gun);
    }

    Dictionary<string, WeaponBehavior> WeaponFireBehaviors = {
        { "default", DefaultBehavior },
        { "vulcan", VulcanBehavior },
        { "helix", HelixBehavior },
        { "spreadfire", SpreadfireBehavior },
        { "omega", OmegaBehavior },
        { "shotgun", ShotgunBehavior },
        { "fusion", FusionBehavior }
    };

    WeaponBehavior& GetWeaponBehavior(const string& name) {
        for (auto& [key, value] : WeaponFireBehaviors) {
            if (name == key) return value;
        }

        return WeaponFireBehaviors["default"];
    }

    void UpdateWeapon(Object& obj, float dt) {
        obj.Control.Weapon.AliveTime += dt;
        if (obj.ID == (int)WeaponID::ProxMine)
            ProxMineBehavior(obj);
    }
}
