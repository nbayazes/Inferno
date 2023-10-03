#include "pch.h"

#include "Game.AI.h"
#include "Object.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Wall.h"
#include "Physics.h"
#include "SoundSystem.h"
#include "Graphics/Render.Debug.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"

namespace Inferno::Game {
    void DrawWeaponExplosion(const Object& obj, const Weapon& weapon) {
        Render::ExplosionInfo e;
        e.Radius = { weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f };
        e.Clip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;
        e.FadeTime = weapon.Extended.ExplosionTime;
        e.LightColor = weapon.Extended.ExplosionColor;
        Render::CreateExplosion(e, obj.Segment, obj.Position);
    }

    void ExplodeWeapon(struct Level& level, const Object& obj) {
        if (!obj.IsWeapon()) return;
        const Weapon& weapon = Resources::GetWeapon(obj);

        // Create sparks
        if (auto sparks = Render::EffectLibrary.GetSparks(weapon.Extended.DeathSparks)) {
            Render::AddSparkEmitter(*sparks, obj.Segment, obj.Position);
        }

        if (weapon.SplashRadius > 0) {
            // Create explosion
            float damage = weapon.Damage[Game::Difficulty];
            DrawWeaponExplosion(obj, weapon);

            Sound3D sound({ weapon.RobotHitSound }, obj.Position, obj.Segment);
            Sound::Play(sound);

            GameExplosion ge{};
            ge.Damage = damage;
            ge.Force = damage;
            ge.Radius = weapon.SplashRadius;
            ge.Segment = obj.Segment;
            ge.Position = obj.Position;
            ge.Room = level.GetRoomID(obj);
            CreateExplosion(level, &obj, ge);
        }

        if (weapon.Spawn != WeaponID::None && weapon.SpawnCount > 0)
            CreateMissileSpawn(obj, 6);
    }

    void ProxMineBehavior(Object& mine) {
        constexpr auto PROX_WAKE_RANGE = 60;
        constexpr auto PROX_ACTIVATE_RANGE = 30;

        auto& cw = mine.Control.Weapon;

        if (TimeHasElapsed(mine.NextThinkTime)) {
            mine.Parent = {}; // Clear parent so player can hit it
            mine.NextThinkTime = Game::Time + 0.25f;

            // Try to find a nearby target
            if (!cw.TrackingTarget) {
                // todo: filter targets based on if mine owner is a player
                auto [ref, dist] = Game::FindNearestObject(mine.Position, PROX_WAKE_RANGE, ObjectMask::Enemy);
                if (ref && dist <= PROX_WAKE_RANGE)
                    cw.TrackingTarget = ref; // New target!
            }
        }

        if (!cw.TrackingTarget)
            return; // Still no target

        auto target = Game::Level.TryGetObject(cw.TrackingTarget);
        auto dist = target ? mine.Distance(*target) : FLT_MAX;

        if (dist > PROX_WAKE_RANGE) {
            cw.TrackingTarget = {}; // Went out of range
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
                mine.Lifespan = 2;
                ClearFlag(mine.Physics.Flags, PhysicsFlag::Bounce); // explode on contacting walls

                if (target) {
                    auto delta = target->Position - mine.Position;
                    delta.Normalize();
                    mine.Physics.Thrust = delta * 0.9; // fire and forget thrust
                }
            }
        }
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
        Render::AddDecal(planar);

        //Render::DynamicLight light{};
        //light.LightColor = weapon.Extended.ExplosionColor;
        //light.Radius = weapon.Extended.LightRadius;
        //light.Position = hit.Point + hit.Normal * weapon.Extended.ExplosionSize;
        //light.Duration = light.FadeTime = weapon.Extended.ExplosionTime;
        //light.Segment = hit.Tag.Segment;
        //Render::AddDynamicLight(light);
    }

    void WeaponHitObject(const LevelHit& hit, Object& src) {
        assert(hit.HitObj);
        assert(src.IsWeapon());
        const auto& weapon = Resources::GetWeapon(src);
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
                    Game::Player.ApplyDamage(damage, true);
            }
            else if (target.IsRobot()) {
                Vector3 srcDir;
                src.Physics.Velocity.Normalize(srcDir);
                auto parent = Game::Level.TryGetObject(src.Parent);
                bool srcIsPlayer = parent ? parent->IsPlayer() : false;
                DamageRobot(target.Position - srcDir * 5, srcIsPlayer, target, damage, weapon.Extended.StunMult);
            }
            else {
                target.ApplyDamage(damage);
            }

            //fmt::print("applied {} damage\n", damage);

            if (!target.IsPlayer() && !weapon.IsExplosive()) {
                // Missiles create their explosion effects when expiring
                Render::ExplosionInfo expl;
                expl.Sound = weapon.RobotHitSound;
                //expl.Parent = src.Parent;
                expl.Clip = VClipID::SmallExplosion;
                expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
                //expl.Color = Color{ 1.15f, 1.15f, 1.15f };
                expl.FadeTime = 0.1f;
                Render::CreateExplosion(expl, target.Segment, hit.Point);
            }

            //AddPlanarExplosion(weapon, hit);

            // More damage creates more sparks
            float damageMult = std::clamp(damage / 20.0f, 1.0f, 2.0f);
            if (auto sparks = Render::EffectLibrary.GetSparks("weapon_hit_obj")) {
                // Mass weapons set explosion color, energy weapons set light color
                if (weapon.Extended.ExplosionColor != LIGHT_UNSET)
                    sparks->Color += weapon.Extended.ExplosionColor * 60;
                else
                    sparks->Color += weapon.Extended.LightColor * 60;

                sparks->Color.w = 1;
                sparks->Count.Min = int(sparks->Count.Min * damageMult);
                sparks->Count.Max = int(sparks->Count.Max * damageMult);
                Render::AddSparkEmitter(*sparks, target.Segment, hit.Point);

                Render::DynamicLight light{};
                light.LightColor = weapon.Extended.ExplosionColor;
                light.Radius = weapon.Extended.LightRadius;
                light.Position = hit.Point;
                light.Duration = light.FadeTime = weapon.Extended.ExplosionTime;
                light.Segment = target.Segment;
                Render::AddDynamicLight(light);
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

    void AddWeaponDecal(const LevelHit& hit, const Weapon& weapon) {
        if (!Settings::Inferno.Descent3Enhanced) return; // todo: remove later, might want decals in non-descent3 mode
        auto decalSize = weapon.Extended.DecalRadius ? weapon.Extended.DecalRadius : weapon.ImpactSize / 3;
        bool addDecal = !weapon.Extended.Decal.empty();
        if (!addDecal) return;

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

        if (!weapon.Extended.ExplosionTexture.empty() /*&& !obj.Physics.CanBounce()*/) {
            AddPlanarExplosion(weapon, hit);
            //vclip = VClipID::None;
        }
    }

    // There are four possible outcomes when hitting a wall:
    // 1. Hit a normal wall
    // 2. Hit water. Reduces damage of explosion and changes sound effect
    // 3. Hit lava. Creates explosion for all weapons and changes sound effect
    // 4. Hit forcefield. Bounces non-matter weapons.
    void WeaponHitWall(const LevelHit& hit, Object& obj, Inferno::Level& level, ObjID objId) {
        if (!hit.Tag) return;
        if (obj.Lifespan <= 0) return; // Already dead
        bool isPlayer = obj.Control.Weapon.ParentType == ObjectType::Player;
        CheckDestroyableOverlay(level, hit.Point, hit.Tag, hit.Tri, isPlayer);

        auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
        float damage = weapon.Damage[Game::Difficulty]; // Damage used when hitting lava
        float splashRadius = weapon.SplashRadius;
        float force = damage;
        float impactSize = weapon.ImpactSize;

        // don't use volatile hits on large explosions like megas
        constexpr float VOLATILE_DAMAGE_RADIUS = 30;
        bool isLargeExplosion = splashRadius >= VOLATILE_DAMAGE_RADIUS / 2;

        // weapons with splash damage (explosions) always use robot hit effects
        SoundID soundId = weapon.SplashRadius > 0 ? weapon.RobotHitSound : weapon.WallHitSound;
        VClipID vclip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;

        auto& side = level.GetSide(hit.Tag);
        auto& ti = Resources::GetLevelTextureInfo(side.TMap);
        bool hitForcefield = ti.HasFlag(TextureFlag::ForceField);
        bool hitLava = ti.HasFlag(TextureFlag::Volatile);
        bool hitWater = ti.HasFlag(TextureFlag::Water);

        // Special case for flares
        if (HasFlag(obj.Physics.Flags, PhysicsFlag::Stick) && !hitLava && !hitWater && !hitForcefield) {
            // sticky flare behavior
            Vector3 vec;
            obj.Physics.Velocity.Normalize(vec);
            obj.Position -= vec * obj.Radius; // move out of wall
            obj.Physics.Velocity = Vector3::Zero;
            StuckObjects.Add(hit.Tag, objId);
            obj.Flags |= ObjectFlag::Attached;
            return;
        }

        if (!hit.Bounced) {
            // Move object to the desired explosion location
            auto dir = obj.Physics.PrevVelocity;
            dir.Normalize();

            if (impactSize < 5)
                obj.Position = hit.Point - dir * impactSize * 0.25f;
            else
                obj.Position = hit.Point - dir * 2.5;
        }

        if (hitForcefield) {
            if (!weapon.IsMatter) {
                // Bounce energy weapons
                obj.Physics.Bounces++;
                obj.Parent = {}; // Make hostile to owner!

                Sound3D sound({ SoundID::WeaponHitForcefield }, hit.Point, hit.Tag.Segment);
                Sound::Play(sound);
            }
        }
        else if (hitLava) {
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

            // Create a damaging and visual explosion
            GameExplosion ge{};
            ge.Segment = hit.Tag.Segment;
            ge.Position = obj.Position;
            ge.Damage = damage;
            ge.Force = force;
            ge.Radius = splashRadius;
            ge.Room = level.GetRoomID(obj);
            CreateExplosion(level, &obj, ge);

            Render::ExplosionInfo e;
            e.Radius = { weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f };
            e.Clip = vclip;
            e.FadeTime = weapon.Extended.ExplosionTime;
            e.Color = Color{ 1, .7, .7, 2 };
            e.LightColor = Color{ 1.0f, 0.05f, 0.05f, 4 };
            e.LightRadius = splashRadius;
            Render::CreateExplosion(e, obj.Segment, obj.Position);

            Sound3D sound({ SoundID::HitLava }, hit.Point, hit.Tag.Segment);
            Sound::Play(sound);
        }
        else if (hitWater) {
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

            if (splashRadius > 0) {
                // Create damage for large explosions
                GameExplosion ge{};
                ge.Segment = hit.Tag.Segment;
                ge.Position = obj.Position;
                ge.Damage = damage;
                ge.Force = force;
                ge.Radius = splashRadius;
                CreateExplosion(level, &obj, ge);
            }

            Render::Particle e;
            e.Radius = NumericRange(weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f).GetRandom();
            e.Clip = vclip;
            e.FadeTime = weapon.Extended.ExplosionTime;
            e.Color = Color(1, 1, 1);
            Render::AddParticle(e, obj.Segment, obj.Position);

            auto splashId = weapon.IsMatter ? SoundID::MissileHitWater : SoundID::HitWater;
            Sound3D sound({ splashId }, hit.Point, hit.Tag.Segment);
            Sound::Play(sound);
        }
        else {
            // Hit normal wall
            AddWeaponDecal(hit, weapon);

            // Explosive weapons play their effects on death instead of here
            if (!hit.Bounced && splashRadius <= 0) {
                if (vclip != VClipID::None)
                    DrawWeaponExplosion(obj, weapon);

                SoundResource resource = { soundId };
                resource.D3 = weapon.Extended.ExplosionSound; // Will take priority if D3 is loaded
                Sound3D sound(resource, hit.Point, hit.Tag.Segment);
                Sound::Play(sound);
            }
        }

        if (!hit.Bounced)
            obj.Lifespan = 0; // remove weapon after hitting a wall
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

    void FireSpreadWeapon(ObjRef ref, uint8 gun, WeaponID id, bool showFlash = true, const Vector2& spread = Vector2::Zero) {
        auto direction = GetSpreadDirection(ref.Id, spread);
        FireWeapon(ref, id, gun, &direction, showFlash);
    }

    Object CreateWeaponProjectile(WeaponID id, const Vector3& position, const Vector3& direction,
                                  SegID segment, ObjRef parentRef,
                                  float damageMultiplier = 1, float volume = DEFAULT_WEAPON_VOLUME) {
        auto parent = Game::Level.TryGetObject(parentRef);

        auto& weapon = Resources::GetWeapon(id);
        Object bullet{};
        bullet.Position = bullet.PrevPosition = position;
        auto rotation = VectorToRotation(direction);
        rotation.Forward(-rotation.Forward());
        bullet.Rotation = bullet.PrevRotation = rotation;

        bullet.Movement = MovementType::Physics;
        // todo: speedvar
        //auto speedvar = weapon.SpeedVariance != 1 ? 1 - weapon.SpeedVariance * Random() : 1;
        float speed = 0;

        if (weapon.Extended.InitialSpeed[Game::Difficulty] != 0)
            speed = weapon.Extended.InitialSpeed[Game::Difficulty];
        else
            speed = weapon.Speed[Game::Difficulty];

        bullet.Physics.Velocity = direction * speed;

        //bullet.Physics.Velocity *= 0.5f;
        if (weapon.Extended.InheritParentVelocity && parent)
            bullet.Physics.Velocity += parent->Physics.Velocity;

        if (!weapon.Extended.PointCollideWalls)
            ClearFlag(bullet.Physics.Flags, PhysicsFlag::PointCollideWalls);

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
        bullet.Control.Weapon.ParentType = parent ? parent->Type : ObjectType::None;
        bullet.Control.Weapon.Multiplier = damageMultiplier;

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
            auto randomRotation = Matrix::CreateFromAxisAngle(bullet.Rotation.Forward(), Random() * DirectX::XM_2PI);
            bullet.Rotation *= randomRotation;
            bullet.PrevRotation = bullet.Rotation;

            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
            Render::LoadModelDynamic(weapon.ModelInner);

            if (bullet.Render.Model.ID == ModelID::None)
                bullet.Render.Type = RenderType::None;
        }
        else if (weapon.RenderType == WeaponRenderType::None) {
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : 1;
        }

        bullet.Render.Rotation = Random() * DirectX::XM_2PI;

        bullet.Lifespan = weapon.Lifetime;
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = parent && parent->IsWeapon() ? parent->Parent : parentRef; // If the parent is a weapon, hopefully its parent is a robot or player
        bullet.Segment = segment;
        bullet.Render.Emissive = weapon.Extended.Glow;

        if (id == WeaponID::ProxMine || id == WeaponID::SmartMine)
            bullet.NextThinkTime = Game::Time + MINE_ARM_TIME;
        else
            bullet.NextThinkTime = 0;

        if (volume > 0) {
            Sound3D sound({ weapon.FlashSound }, parentRef);
            sound.Volume = volume;
            sound.Radius = weapon.Extended.SoundRadius;

            if (parent) {
                sound.AttachToSource = true;
                sound.AttachOffset = parent->Position - position;
                sound.FromPlayer = parent->IsPlayer();
            }

            if (id == WeaponID::Vulcan) {
                sound.Merge = false;
                sound.Pitch -= Random() * 0.05f;
            }

            Sound::Play(sound);
        }

        bullet.Rotation.Normalize();
        bullet.PrevRotation = bullet.Rotation;

        // If a weapon creates children, they should bounce for a short duration so they aren't immediately destroyed
        if (parent && parent->IsWeapon())
            bullet.Physics.Bounces = 1;

        return bullet;
    }

    void FireWeapon(ObjRef ref, WeaponID id, uint8 gun, Vector3* customDir, float damageMultiplier, bool showFlash, float volume) {
        auto& level = Game::Level;
        auto pObj = level.TryGetObject(ref);
        if (!pObj) {
            __debugbreak(); // tried to fire weapon from unknown object
            return;
        }
        auto& obj = *pObj;

        if (obj.IsPlayer() && gun == 6 && Game::GetState() == GameState::Game)
            showFlash = false; // Hide flash in first person

        auto gunSubmodel = GetLocalGunpointOffset(obj, gun);
        auto objOffset = GetSubmodelOffset(obj, gunSubmodel);
        auto position = Vector3::Transform(objOffset, obj.GetTransform());
        Vector3 direction = customDir ? *customDir : obj.Rotation.Forward();
        auto projectile = CreateWeaponProjectile(id, position, direction, obj.Segment, ref, damageMultiplier, volume);
        auto& weapon = Resources::GetWeapon(id);

        if (weapon.Extended.Recoil)
            obj.Physics.Thrust += obj.Rotation.Backward() * weapon.Extended.Recoil;

        if (showFlash) {
            Render::Particle p{};
            p.Clip = weapon.FlashVClip;
            p.Radius = weapon.FlashSize;
            p.Parent = ref;
            p.ParentSubmodel = gunSubmodel;
            p.FadeTime = 0.175f;
            p.Color = weapon.Extended.FlashColor;
            Render::AddParticle(p, obj.Segment, position);

            // Muzzle flash. Important for mass weapons that don't emit lights on their own.
            Render::DynamicLight light;
            light.LightColor = weapon.Extended.FlashColor;
            light.Radius = weapon.FlashSize * 4;
            light.FadeTime = light.Duration = 0.25f;
            light.Segment = obj.Segment;
            light.Position = position;
            Render::AddDynamicLight(light);
        }

        AddObject(projectile);
    }

    void SpreadfireBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        //constexpr float SPREAD_ANGLE = 1 / 16.0f * RadToDeg;
        auto spread = Resources::GetWeapon(wid).Extended.Spread * DegToRad;

        if (player.SpreadfireToggle) {
            // Vertical
            FireSpreadWeapon(player.Reference, gun, wid);
            FireSpreadWeapon(player.Reference, gun, wid, false, { 0, -spread });
            FireSpreadWeapon(player.Reference, gun, wid, false, { 0, spread });
        }
        else {
            // Horizontal
            FireSpreadWeapon(player.Reference, gun, wid);
            FireSpreadWeapon(player.Reference, gun, wid, false, { -spread, 0 });
            FireSpreadWeapon(player.Reference, gun, wid, false, { spread, 0 });
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
        FireSpreadWeapon(player.Reference, gun, wid);
        FireSpreadWeapon(player.Reference, gun, wid, false, offset);
        FireSpreadWeapon(player.Reference, gun, wid, false, offset * 2);
        FireSpreadWeapon(player.Reference, gun, wid, false, -offset);
        FireSpreadWeapon(player.Reference, gun, wid, false, -offset * 2);
    }

    void VulcanBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        //constexpr float SPREAD_ANGLE = 1 / 32.0f * RadToDeg; // -0.03125 to 0.03125 spread
        auto spread = Resources::GetWeapon(wid).Extended.Spread * DegToRad;
        auto point = RandomPointInCircle(spread);
        FireSpreadWeapon(player.Reference, gun, wid, true, { point.x, point.y });
    }

    void ShotgunBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        auto& weapon = Resources::GetWeapon(wid);
        auto spread = weapon.Extended.Spread * DegToRad;

        bool flash = true;
        for (size_t i = 0; i < weapon.FireCount; i++) {
            auto point = RandomPointInCircle(spread);
            FireSpreadWeapon(player.Reference, gun, wid, flash, { point.x, point.y });
            flash = false;
        }
    }

    // FOV in 0 to PI
    bool ObjectIsInFOV(const Ray& ray, const Object& obj, float fov) {
        auto vec = obj.Position - ray.position;
        vec.Normalize();
        auto angle = AngleBetweenVectors(ray.direction, vec);
        return angle <= fov;
    }

    bool CanTrackTarget(const Object& obj, const Object& target, float fov, float maxDistance) {
        if (!target.IsAlive()) return false;
        if (target.IsCloaked() || target.IsPhasing()) return false;
        auto [dir, dist] = GetDirectionAndDistance(target.Position, obj.Position);
        if (dist > maxDistance) return false;

        //auto vec = obj.Position - src.Position;
        //vec.Normalize();
        Ray targetRay(obj.Position, dir);
        LevelHit hit;
        RayQuery query{ .MaxDistance = dist, .Start = obj.Segment, .TestTextures = true };

        bool inFov = ObjectIsInFOV(Ray(obj.Position, obj.Rotation.Forward()), target, fov);
        return inFov && !Intersect.RayLevel(targetRay, query, hit);
    }

    // Used for omega and homing weapons
    ObjRef GetClosestObjectInFOV(const Object& src, float fov, float maxDist, ObjectMask mask) {
        ObjRef target;
        float dotFov = -1;
        float dist = FLT_MAX;
        auto forward = src.Rotation.Forward();

        auto action = [&](const Room& room) {
            for (auto& segId : room.Segments) {
                auto& seg = Game::Level.GetSegment(segId);

                for (auto& objId : seg.Objects) {
                    auto pobj = Game::Level.TryGetObject(objId);
                    if (!pobj) continue;
                    auto obj = *pobj;
                    if (!obj.IsAlive()) continue;
                    if (!obj.PassesMask(mask)) continue;

                    auto [odir, odist] = GetDirectionAndDistance(obj.Position, src.Position);
                    auto dot = odir.Dot(forward);
                    bool isClose = target ? dot > dotFov - 0.1f : true; // Is the new target closer to center of FOV?

                    if (target && dot < dotFov && !isClose)
                        continue; // Already found a target and this one is further from center FOV

                    if (CanTrackTarget(src, obj, fov, maxDist)) {
                        // new target is closer to center FOV 
                        if (isClose && odist < dist) {
                            dotFov = dot;
                            dist = odist;
                            target = { objId, obj.Signature };
                        }
                    }
                }
            }
        };

        auto room = Game::Level.GetRoomID(src);
        TraverseRoomsByDistance(Game::Level, room, src.Position, maxDist, false, action);
        return target;
    }

    void OmegaBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        constexpr auto FOV = 12.5f * DegToRad;
        constexpr auto MAX_DIST = 60;
        constexpr auto MAX_TARGETS = 3;
        constexpr auto MAX_CHAIN_DIST = 30;

        player.OmegaCharge -= OMEGA_CHARGE_COST;
        player.OmegaCharge = std::max(0.0f, player.OmegaCharge);

        const auto& weapon = Resources::GetWeapon(wid);

        auto pObj = Game::Level.TryGetObject(player.Reference);
        if (!pObj) return;
        auto& playerObj = *pObj;

        auto gunSubmodel = GetLocalGunpointOffset(playerObj, gun);
        auto objOffset = GetSubmodelOffset(playerObj, gunSubmodel);
        auto start = Vector3::Transform(objOffset, playerObj.GetTransform());
        auto initialTarget = GetClosestObjectInFOV(playerObj, FOV, MAX_DIST, ObjectMask::Enemy);

        auto spark = Render::EffectLibrary.GetSparks("omega_hit");

        if (initialTarget) {
            // found a target! try chaining to others
            std::array<ObjRef, MAX_TARGETS> targets{};
            //targets.fill(ObjID::None);
            targets[0] = initialTarget;

            for (int i = 0; i < MAX_TARGETS - 1; i++) {
                if (!targets[i]) break;

                if (auto src = Game::Level.TryGetObject(targets[i])) {
                    auto [id, dist] = Game::FindNearestVisibleObject(src->Position, src->Segment, MAX_CHAIN_DIST, ObjectMask::Enemy, targets);
                    if (id)
                        targets[i + 1] = id;
                }
            }

            //auto prevPosition = start;
            ObjRef prevRef = player.Reference;
            int objGunpoint = gun;

            auto beam = Render::EffectLibrary.GetBeamInfo("omega_beam");
            auto beam2 = Render::EffectLibrary.GetBeamInfo("omega_beam2");
            auto tracer = Render::EffectLibrary.GetBeamInfo("omega_tracer");

            // Apply damage and visuals to each target
            for (auto& targetRef : targets) {
                if (!targetRef) continue;
                auto target = Game::Level.TryGetObject(targetRef);
                if (!target) continue;
                if (!Settings::Cheats.DisableWeaponDamage)
                    target->ApplyDamage(weapon.Damage[Difficulty]);

                // Beams between previous and next target
                if (beam) Render::AddBeam(*beam, weapon.FireDelay, prevRef, targetRef, objGunpoint);
                if (beam2) {
                    Render::AddBeam(*beam2, weapon.FireDelay, prevRef, targetRef, objGunpoint);
                    Render::AddBeam(*beam2, weapon.FireDelay, prevRef, targetRef, objGunpoint);
                }

                prevRef = targetRef;
                objGunpoint = -1;

                if (tracer) {
                    Render::AddBeam(*tracer, weapon.FireDelay, targetRef);
                    Render::AddBeam(*tracer, weapon.FireDelay, targetRef);
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
                auto name = hitSounds[RandomInt((int)hitSounds.size() - 1)];
                Sound3D hitSound({ name }, initialTargetObj->Position, initialTargetObj->Segment);
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
            RayQuery query{ .MaxDistance = MAX_DIST, .Start = playerObj.Segment, .TestTextures = true };

            if (Game::Intersect.RayLevel({ playerObj.Position, dir }, query, hit)) {
                //tracer.End = beam.End = hit.Point;
                tracerEnd = hit.Point;

                if (spark) Render::AddSparkEmitter(*spark, hit.Tag.Segment, hit.Point);

                // Do wall hit stuff
                Object dummy{};
                dummy.Position = hit.Point;
                dummy.Parent = player.Reference;
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
                Render::AddBeam(*miss, weapon.FireDelay, player.Reference, tracerEnd, gun);
        }

        // Fire sound
        Sound3D sound({ weapon.FlashSound }, player.Reference);
        sound.Volume = 0.40f;
        sound.AttachToSource = true;
        sound.AttachOffset = gunSubmodel.Offset;
        sound.FromPlayer = true;
        Sound::Play(sound);

        Render::Particle p{};
        p.Clip = weapon.FlashVClip;
        p.Radius = weapon.FlashSize;
        p.Parent = player.Reference;
        p.ParentSubmodel = gunSubmodel;
        p.FadeTime = 0.175f;
        p.Color = weapon.Extended.FlashColor;
        Render::AddParticle(p, playerObj.Segment, start);
    }

    void FusionBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        // Fixes original behavior of fusion jumping from 2.9x to 4x damage at 4 seconds charge.
        // This is believed to be a logic error. One could argue the charge multiplier should
        // be 4 and not 3, but that would make fusion stronger under normal usage.
        constexpr auto MAX_FUSION_CHARGE_TIME = 4.0f; // Time in seconds for full charge
        constexpr auto MAX_FUSION_CHARGE_MULT = 3.0f; // Bonus damage multiplier for full charge
        float multiplier = MAX_FUSION_CHARGE_MULT * player.WeaponCharge / MAX_FUSION_CHARGE_TIME;
        if (multiplier > MAX_FUSION_CHARGE_MULT) multiplier = MAX_FUSION_CHARGE_MULT;
        FireWeapon(player.Reference, wid, gun, nullptr, 1 + multiplier);
    }

    // default weapon firing behavior
    void DefaultBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        FireWeapon(player.Reference, wid, gun);
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

    template <uint32 TResults = 30>
    Array<ObjRef, TResults> GetNearbyLockTargets(const Object& object, float maxDist, int& count, ObjectMask mask) {
        Array<ObjRef, TResults> targets{};
        count = 0;

        auto startRoom = Game::Level.GetRoom(object);
        if (!startRoom) return targets;

        for (auto& segId : startRoom->VisibleSegments) {
            if (auto seg = Game::Level.TryGetSegment(segId)) {
                for (auto& objId : seg->Objects) {
                    if (auto obj = Game::Level.TryGetObject(objId)) {
                        if (!obj->PassesMask(mask)) continue;
                        //if (Game::ObjectCanSeeObject(object, *obj, maxDist)) {
                        //    targets[count] = { objId, obj->Signature };
                        //    count++;
                        //    if (count >= targets.size()) {
                        //        SPDLOG_WARN("Max nearby targets reached");
                        //        return targets;
                        //    }
                        //}

                        if (!obj->IsAlive()) continue;
                        if (obj->IsCloaked() || obj->IsPhasing()) continue; // cloaked objects aren't visible
                        auto [dir, dist] = GetDirectionAndDistance(obj->Position, object.Position);

                        if (dist < maxDist) {
                            Ray ray(object.Position, dir);
                            RayQuery query;
                            query.Start = object.Segment;
                            query.MaxDistance = dist;
                            query.TestTextures = true;
                            LevelHit hit;
                            if (!Intersect.RayLevel(ray, query, hit)) {
                                targets[count] = { objId, obj->Signature };
                                count++;
                                if (count >= targets.size()) {
                                    SPDLOG_WARN("Max nearby targets reached");
                                    return targets;
                                }
                            }
                        }
                    }
                }
            }
        }

        return targets;
    }

    // For smart missiles, energy retaliation
    void CreateHomingBlob(WeaponID type, const Object& parent, ObjRef targetId = {}) {
        Vector3 dir;

        if (auto target = Game::Level.TryGetObject(targetId)) {
            dir = target->Position - parent.Position;
            dir.Normalize();
            dir += RandomVector(0.25f); // Slightly randomize direction so the blobs don't stack
            dir.Normalize();
        }
        else {
            dir = RandomVector();
        }

        SPDLOG_INFO("Tracking: {} dir: {}", targetId, dir);
        auto parentRef = Game::GetObjectRef(parent);
        auto blob = CreateWeaponProjectile(type, parent.Position, dir, parent.Segment, parentRef, 1, 0);
        blob.Control.Weapon.TrackingTarget = targetId;
        AddObject(blob);
    }

    void CreateMissileSpawn(const Object& missile, uint blobs) {
        auto mask = missile.Control.Weapon.ParentType == ObjectType::Player ? ObjectMask::Enemy : ObjectMask::Player;
        int targetCount;

        const Weapon& weapon = Resources::GetWeapon(missile);

        auto spawn = weapon.Spawn;
        if (missile.Control.Weapon.ParentType != ObjectType::Player && spawn == WeaponID::PlayerSmartBlob)
            spawn = WeaponID::RobotSmartBlob; // HACK: Override blobs for robot smart missiles

        const Weapon& spawnWeapon = Resources::GetWeapon(spawn);
        auto targets = GetNearbyLockTargets(missile, spawnWeapon.Extended.HomingDistance, targetCount, mask);
        Sound3D sound({ spawnWeapon.FlashSound }, missile.Position, missile.Segment);
        sound.Volume = DEFAULT_WEAPON_VOLUME * 1.5f;
        sound.Radius = spawnWeapon.Extended.SoundRadius;
        Sound::Play(sound);

        if (targetCount > 0) {
            SPDLOG_INFO("Found blob targets");
            // if found targets, pick random target from array
            for (size_t i = 0; i < blobs; i++)
                CreateHomingBlob(spawn, missile, targets[RandomInt(targetCount - 1)]);
        }
        else {
            SPDLOG_INFO("No blob targets");
            // Otherwise random points
            for (size_t i = 0; i < blobs; i++)
                CreateHomingBlob(spawn, missile);
        }
    }

    void TurnTowardsNormal(Object& obj, const Vector3& normal, float /*dt*/) {
        Vector3 fvec = normal;
        //constexpr float HOMING_MISSILE_SCALE = 8;
        //fvec *= dt * HOMING_MISSILE_SCALE;
        fvec += obj.Rotation.Forward();
        fvec.Normalize();
        obj.Rotation = Matrix3x3(VectorToRotation(fvec));
        obj.Rotation.Forward(-obj.Rotation.Forward()); // correct for lh model
    }

    void UpdateHomingWeapon(Object& weapon, const Weapon& weaponInfo, float dt) {
        if (!weaponInfo.IsHoming) return;

        if (!TimeHasElapsed(weapon.NextThinkTime))
            return; // Not ready to think

        // Homing weapons update slower to match the original behavior
        weapon.NextThinkTime = Game::Time + HOMING_TICK_RATE;

        if (weapon.Control.Weapon.AliveTime < WEAPON_HOMING_DELAY)
            return; // Not ready to start homing yet

        weapon.Physics.Bounces = 0; // Hack for smart missile blob bounces
        auto& targetRef = weapon.Control.Weapon.TrackingTarget;

        // Check if the target is still trackable
        if (targetRef) {
            auto target = Game::Level.TryGetObject(targetRef);
            if (!target || !CanTrackTarget(weapon, *target, weaponInfo.Extended.HomingFov, weaponInfo.Extended.HomingDistance)) {
                SPDLOG_INFO("Lost tracking target");
                targetRef = {}; // target destroyed or out of view
            }
        }

        if (!targetRef) {
            // Find a new target
            auto mask = ObjectMask::Enemy;
            if (auto parent = Game::Level.TryGetObject(weapon.Parent))
                if (parent->IsRobot())
                    mask = ObjectMask::Player;

            targetRef = GetClosestObjectInFOV(weapon, weaponInfo.Extended.HomingFov, weaponInfo.Extended.HomingDistance, mask);
            if (targetRef)
                SPDLOG_INFO("Locking onto {}", targetRef);
        }
        else if (auto target = Game::Level.TryGetObject(targetRef)) {
            // turn towards target
            auto [targetDir, targetDist] = GetDirectionAndDistance(target->Position, weapon.Position);

            if (target->IsPlayer()) {
                if (Game::Player.HomingObjectDist < 0 || targetDist < Game::Player.HomingObjectDist)
                    Game::Player.HomingObjectDist = targetDist;
            }

            Vector3 dir = weapon.Physics.Velocity;
            auto speed = dir.Length();
            dir.Normalize();

            // Hack for smart missile blobs to speed up over time
            /*auto maxSpeed = weaponInfo.Speed[Game::Difficulty];
            if (speed + 1 < maxSpeed) {
                speed += maxSpeed * HOMING_TICK_RATE / 2;
                if (speed > maxSpeed) speed = maxSpeed;
            }*/

            dir *= 4; // NEW: Increase weighting of existing direction to smooth turn radius. This does slightly reduce turn speed.
            dir += targetDir;

            // make smart blobs track better (hacky, add homing speed to weapon info)
            //if (weapon.Render.Type != RenderType::Model)
            //    dir += targetDir;

            dir.Normalize();
            weapon.Physics.Velocity = dir * speed;

            Render::Debug::DrawLine(weapon.Position, target->Position, Color(1, 0, 0));

            // Remove life based on amount turned ... ?
            //auto dot = tempVel.Dot(targetDir);
            TurnTowardsNormal(weapon, dir, dt);
        }
    }

    void UpdateWeapon(Object& weapon, float dt) {
        weapon.Control.Weapon.AliveTime += dt;
        if (weapon.ID == (int)WeaponID::ProxMine)
            ProxMineBehavior(weapon);

        auto& weaponInfo = Resources::GetWeapon(weapon);
        UpdateHomingWeapon(weapon, weaponInfo, dt);
    }
}
