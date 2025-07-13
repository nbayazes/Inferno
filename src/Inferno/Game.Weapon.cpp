#include "pch.h"
#include "Game.Weapon.h"
#include "VisualEffects.h"
#include "Game.AI.h"
#include "Object.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Segment.h"
#include "Game.Wall.h"
#include "Graphics.h"
#include "Physics.h"
#include "SoundSystem.h"
#include "Resources.h"
#include "Settings.h"

namespace Inferno::Game {
    void CreateWeaponExplosion(const Object& obj, const Weapon& weapon, float scale = 1) {
        ExplosionEffectInfo e;
        e.Radius = { weapon.ImpactSize * 0.9f * scale, weapon.ImpactSize * 1.1f * scale };
        e.Clip = weapon.SplashRadius > 0 ? weapon.RobotHitVClip : weapon.WallHitVClip;
        e.FadeTime = weapon.Extended.ExplosionTime;
        e.LightColor = weapon.Extended.ExplosionColor;
        CreateExplosion(e, obj.Segment, obj.Position);
    }

    void ExplodeWeapon(struct Level& level, const Object& obj) {
        if (!obj.IsWeapon()) return;
        const Weapon& weapon = Resources::GetWeapon(obj);

        // Create sparks
        if (auto sparks = EffectLibrary.GetSparks(weapon.Extended.DeathSparks)) {
            auto position = Vector3::Transform(sparks->Offset, obj.GetTransform(Game::LerpAmount));
            AddSparkEmitter(*sparks, obj.Segment, position);
        }

        if (weapon.SplashRadius > 0) {
            // Create explosion
            float damage = GetDamage(weapon);

            // Mine was hit before it armed, do no splash damage
            if (ObjectIsMine(obj)) {
                float scale = 1;

                if (obj.Control.Weapon.AliveTime < Game::MINE_ARM_TIME) {
                    damage = 0;
                    scale = 0.5f;
                }

                // Create visual effect and sound here, as mines do not directly hit enemies or walls
                CreateWeaponExplosion(obj, weapon, scale);

                SoundResource resource = { weapon.RobotHitSound };
                resource.D3 = weapon.Extended.ExplosionSound;
                Sound3D sound(resource);
                sound.Volume = Game::WEAPON_HIT_WALL_VOLUME;
                sound.Radius = weapon.Extended.ExplosionSoundRadius;
                Sound::Play(sound, obj.Position, obj.Segment);
            }

            GameExplosion ge{};
            ge.Damage = damage;
            ge.Force = damage * weapon.Extended.StunMult;
            ge.Radius = weapon.SplashRadius;
            ge.Segment = obj.Segment;
            ge.Position = obj.Position;
            ge.Room = level.GetRoomID(obj);
            CreateExplosion(level, &obj, ge);
        }

        if (weapon.Spawn != WeaponID::None && weapon.SpawnCount > 0)
            CreateMissileSpawn(obj, weapon.SpawnCount);

        // Alert enemies when a player weapon is destroyed
        //if (obj.Parent == Game::Player.Reference)
        //AlertEnemiesOfNoise(obj, weapon.Extended.SoundRadius, 1, AI_AWARENESS_INVESTIGATE);
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
        float damage = GetDamage(weapon); // Damage used when hitting lava
        float splashRadius = weapon.SplashRadius;
        float force = damage;
        float impactSize = weapon.ImpactSize;

        // don't use volatile hits on large explosions like megas
        constexpr float VOLATILE_DAMAGE_RADIUS = 30;
        bool isLargeExplosion = splashRadius >= VOLATILE_DAMAGE_RADIUS / 2;

        SoundID soundId = weapon.WallHitSound;
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
            //obj.Position -= vec * obj.Radius; // move out of wall
            obj.Physics.Velocity = Vector3::Zero;
            StuckObjects.Add(hit.Tag, objId);
            obj.Flags |= ObjectFlag::Attached;
            return;
        }

        auto bounce = hit.Bounce != BounceType::None;
        if (hitLava && weapon.SplashRadius > 0)
            bounce = false; // Explode bouncing explosive weapons (mines) when touching lava

        if (!bounce) {
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
                Sound::Play({ SoundID::WeaponHitForcefield }, hit.Point, hit.Tag.Segment);
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

            ExplosionEffectInfo e;
            e.Radius = { weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f };
            e.Clip = vclip;
            e.FadeTime = weapon.Extended.ExplosionTime;
            e.Color = Color{ 1, .7f, .7f, 2 };
            e.LightColor = Color{ 1.0f, 0.6f, 0.05f, .5 };
            e.LightRadius = splashRadius;
            CreateExplosion(e, obj.Segment, obj.Position);

            Sound::Play({ SoundID::HitLava }, hit.Point, hit.Tag.Segment);
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

            ParticleInfo e;
            e.Radius = NumericRange(weapon.ImpactSize * 0.9f, weapon.ImpactSize * 1.1f).GetRandom();
            e.Clip = vclip;
            e.FadeTime = weapon.Extended.ExplosionTime;
            e.Color = Color(1, 1, 1);
            AddParticle(e, obj.Segment, obj.Position);

            auto splashId = weapon.IsMatter ? SoundID::MissileHitWater : SoundID::HitWater;
            Sound::Play({ splashId }, hit.Point, hit.Tag.Segment);
        }
        else {
            // Hit normal wall
            Game::AddWeaponDecal(hit, weapon);

            // Explosive weapons create their effects on death in ExplodeWeapon() instead of here
            if (!bounce /*&& splashRadius <= 0*/) {
                if (vclip != VClipID::None)
                    Game::CreateWeaponExplosion(obj, weapon);
            }

            // Don't play hit sound if door is locked. Door will play a different sound.
            bool locked = false;
            if (auto wall = level.TryGetWall(hit.Tag))
                locked = wall->Type == WallType::Door && wall->HasFlag(WallFlag::DoorLocked);

            if (!locked && !bounce) {
                SoundResource resource = { soundId };
                resource.D3 = weapon.Extended.ExplosionSound; // Will take priority if D3 is loaded
                Sound3D sound(resource);
                sound.Volume = Game::WEAPON_HIT_WALL_VOLUME;
                sound.Radius = weapon.Extended.ExplosionSoundRadius;
                Sound::Play(sound, hit.Point, hit.Tag.Segment);
            }
        }

        if (!bounce)
            obj.Lifespan = 0; // remove weapon after hitting a wall
    }

    void ProxMineBehavior(Object& mine) {
        constexpr auto PROX_ACTIVATE_RANGE = 40; // Starts tracking at this range
        constexpr auto PROX_DETONATE_RANGE = 15; // Explodes at this distance to target
        constexpr auto PROX_DETONATE_TIME = 0.3f; // Explode timer when 'close' to the target
        auto& cw = mine.Control.Weapon;

        if (TimeHasElapsed(mine.NextThinkTime)) {
            mine.Parent = {}; // Clear parent so player can hit it
            mine.NextThinkTime = Game::Time + 0.25f;

            // Try to find a nearby target
            if (!cw.TrackingTarget) {
                auto filter = HasFlag(mine.Faction, Faction::Robot) ? ObjectMask::Player : ObjectMask::Robot;
                Array srcRef = { Game::GetObjectRef(mine) };

                auto [ref, dist] = Game::FindNearestVisibleObject({ mine.Segment, mine.Position }, PROX_ACTIVATE_RANGE, filter, srcRef);
                if (ref && dist <= PROX_ACTIVATE_RANGE) {
                    cw.TrackingTarget = ref; // New target!
                }
            }
        }

        if (!mine.Control.Weapon.Flags && mine.Control.Weapon.AliveTime > Game::MINE_ARM_TIME) {
            Sound3D sound(SoundID(155));
            sound.Radius = 120;
            sound.Volume = 1.0f;
            sound.Pitch = 0.275f;
            Sound::PlayFrom(sound, mine);

            //sound.Delay = 0.15f;
            //Sound::PlayFrom(sound, mine);
            mine.Control.Weapon.Flags = true;
        }

        if (!cw.TrackingTarget)
            return; // Still no target

        auto target = Game::Level.TryGetObject(cw.TrackingTarget);
        auto dist = target ? mine.Distance(*target) : FLT_MAX;

        // Close to the target, explode soon!
        if (dist <= PROX_DETONATE_RANGE && mine.Lifespan > PROX_DETONATE_TIME) {
            mine.Lifespan = PROX_DETONATE_TIME;
            return;
        }

        if (dist <= PROX_ACTIVATE_RANGE) {
            if (target && target->IsPlayer()) {
                // Play lock warning for player
                if (Game::Player.HomingObjectDist < 0 || dist < Game::Player.HomingObjectDist)
                    Game::Player.HomingObjectDist = dist;
            }

            if (!cw.DetonateMine) {
                // Commit to the target
                cw.DetonateMine = true;
                mine.Lifespan = 2; // detonate in 2 seconds
                ClearFlag(mine.Physics.Flags, PhysicsFlag::Bounce); // explode on contacting walls

                if (target) {
                    auto& weapon = Resources::GetWeapon(WeaponID::ProxMine);
                    auto delta = target->Position - mine.Position;
                    delta.Normalize();
                    mine.Physics.Thrust = delta * weapon.Thrust; // fire and forget thrust
                }
            }
        }
    }

    void AddPlanarExplosion(const Weapon& weapon, const LevelHit& hit) {
        auto rotation = Matrix::CreateFromAxisAngle(hit.Normal, Random() * DirectX::XM_2PI);

        // Add the planar explosion effect
        Decal decal{};
        decal.Texture = weapon.Extended.ExplosionTexture;
        decal.Radius = weapon.Extended.ExplosionSize;
        decal.FadeTime = weapon.Extended.ExplosionTime;
        decal.FadeRadius = weapon.GetDecalSize() * 2.4f;
        decal.Additive = true;
        decal.Color = Color{ 1.5f, 1.5f, 1.5f };
        auto tangent = Vector3::Transform(hit.Tangent, rotation);
        AddDecal(decal, hit.Tag, hit.Point, hit.Normal, tangent, weapon.Extended.ExplosionTime);

        //Render::DynamicLight light{};
        //light.LightColor = weapon.Extended.ExplosionColor;
        //light.Radius = weapon.Extended.LightRadius;
        //light.Position = hit.Point + hit.Normal * weapon.Extended.ExplosionSize;
        //light.Duration = light.FadeTime = weapon.Extended.ExplosionTime;
        //light.Segment = hit.Tag.Segment;
        //Render::AddDynamicLight(light);
    }

    void WeaponHitObject(const LevelHit& hit, Object& src) {
        ASSERT(hit.HitObj);
        ASSERT(src.IsWeapon());
        const auto& weapon = Resources::GetWeapon(src);
        float damage = GetDamage(weapon) * src.Control.Weapon.Multiplier;

        auto& target = *hit.HitObj;
        src.LastHitObject = target.Signature;

        if (target.Type == ObjectType::Weapon) {
            // a bomb or other weapon was shot. cause it to explode by expiring.
            target.Lifespan = -1;
            if (weapon.SplashRadius == 0)
                return; // non-explosive weapons keep going
        }

        if (target.IsPlayer()) {
            // Players don't take direct damage from explosive weapons for balance reasons
            // The secondary explosion will still inflict damage.
            // However we still apply damage so the correct sound effect plays.
            if (weapon.IsExplosive() || !weapon.Extended.DirectDamage)
                damage = 0;

            Game::Player.ApplyDamage(damage * weapon.PlayerDamageScale, true);

            if (auto parent = Game::GetObject(src.Parent)) {
                if (parent->IsRobot()) {
                    auto& ai = GetAI(*parent);
                    ai.Awareness = 1; // Make fully aware if a robot hits the player. This is so hitting a cloaked player keeps them awake.
                    ai.Target = { target.Segment, target.Position };
                }
            }
        }
        else if (target.IsRobot()) {
            Vector3 srcDir;
            src.Physics.Velocity.Normalize(srcDir);
            // Explosive weapons stun more due to their damage being split
            NavPoint srcPos = { target.Segment, target.Position - srcDir * 10 };

            if (weapon.Extended.DirectDamage) {
                auto parent = Game::Level.TryGetObject(src.Parent);
                DamageRobot(srcPos, target, damage, weapon.Extended.StunMult, parent);
            }
        }
        else if (weapon.Extended.DirectDamage) {
            target.ApplyDamage(damage);
        }

        //fmt::print("applied {} damage\n", damage);

        if (!target.IsPlayer()) {
            ExplosionEffectInfo expl;
            expl.Sound = weapon.RobotHitSound;
            expl.Volume = WEAPON_HIT_OBJECT_VOLUME;
            //expl.Parent = src.Parent;
            expl.Clip = VClipID::SmallExplosion;
            expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
            //expl.Color = Color{ 1.15f, 1.15f, 1.15f };
            expl.FadeTime = 0.1f;
            CreateExplosion(expl, target.Segment, hit.Point);
        }

        //AddPlanarExplosion(weapon, hit);

        // More damage creates more sparks (missiles)
        constexpr float HEAVY_HIT = 25;
        float damageMult = damage < HEAVY_HIT ? 1.0f : 2.0f;

        if (auto sparks = EffectLibrary.GetSparks("weapon_hit_obj")) {
            // Mass weapons set explosion color, energy weapons set light color
            if (weapon.Extended.ExplosionColor != LIGHT_UNSET)
                sparks->Color += weapon.Extended.ExplosionColor * 60;
            else
                sparks->Color += weapon.Extended.LightColor * 60;

            sparks->Color.w = 1;
            sparks->Count.Min = int(sparks->Count.Min * damageMult);
            sparks->Count.Max = int(sparks->Count.Max * damageMult);
            constexpr float duration = 1;
            AddSparkEmitter(*sparks, target.Segment, hit.Point);

            if (!weapon.IsExplosive()) {
                LightEffectInfo light{};
                light.LightColor = weapon.Extended.ExplosionColor;
                light.Radius = weapon.Extended.LightRadius;
                light.FadeTime = sparks->FadeTime / 2;
                AddLight(light, hit.Point, duration, target.Segment);
            }
        }

        // Tearing metal on heavy hit
        //if (damage > HEAVY_HIT && target.IsRobot()) {
        //    Sound3D sound(SoundID::TearD1_02);
        //    sound.Volume = 0.75f;
        //    Sound::Play3D(sound, target);
        //}

        if (weapon.RobotHitSound != SoundID::None || !weapon.Extended.ExplosionSound.empty()) {
            SoundResource resource = { weapon.RobotHitSound };
            resource.D3 = weapon.Extended.ExplosionSound; // Will take priority if D3 is loaded
            Sound3D sound(resource);
            sound.Volume = Game::WEAPON_HIT_OBJECT_VOLUME;
            sound.Radius = weapon.Extended.ExplosionSoundRadius;
            Sound::Play(sound, hit.Point, hit.Tag.Segment);
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

        Decal decal{};
        auto rotation = Matrix::CreateFromAxisAngle(hit.Normal, Random() * DirectX::XM_2PI);
        auto tangent = Vector3::Transform(hit.Tangent, rotation);
        decal.Radius = decalSize;
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
                AddDecal(decal, hit.Tag, hit.Point, hit.Normal, tangent, weapon.Extended.ExplosionTime);
        }

        if (!weapon.Extended.ExplosionTexture.empty() /*&& !obj.Physics.CanBounce()*/) {
            AddPlanarExplosion(weapon, hit);
            //vclip = VClipID::None;
        }
    }

    Vector3 GetSpreadDirection(const Object& obj, const Vector2& spread) {
        auto direction = obj.Rotation.Forward();

        if (spread != Vector2::Zero) {
            direction += obj.Rotation.Right() * spread.x;
            direction += obj.Rotation.Up() * spread.y;
        }

        direction.Normalize();
        return direction;
    }

    void FireSpreadWeapon(Object& obj, FireWeaponInfo& info, const Vector2& spread = Vector2::Zero) {
        auto direction = GetSpreadDirection(obj, spread);
        info.customDir = &direction;
        FireWeapon(obj, info);
    }

    Sound3D InitWeaponSound(WeaponID id, float volume) {
        auto& weapon = Resources::GetWeapon(id);
        auto resource = weapon.Extended.FireSound.empty() ? SoundResource{ weapon.FlashSound } : SoundResource{ weapon.Extended.FireSound };

        Sound3D sound(resource);
        sound.Volume = volume;
        sound.Radius = weapon.Extended.SoundRadius;

        if (id == WeaponID::Vulcan) {
            sound.Merge = false;
            sound.Pitch -= Random() * 0.05f;
        }

        return sound;
    }

    //void PlayWeaponSound(WeaponID id, float volume, SegID segment, const Vector3& position) {
    //    if (volume <= 0)
    //        return;

    //    auto sound = InitWeaponSound(id, volume);
    //    Sound::Play(sound, position, segment);
    //}

    void PlayWeaponSound(WeaponID id, float volume, const Object& parent, uint8 gun) {
        if (volume <= 0)
            return;

        auto sound = InitWeaponSound(id, volume);

        if (gun != 255) {
            auto gunSubmodel = GetGunpointSubmodelOffset(parent, gun);
            sound.AttachOffset = GetSubmodelOffset(parent, gunSubmodel);
        }

        Sound::PlayFrom(sound, parent);
    }

    Object CreateWeaponProjectile(WeaponID id, const Vector3& position, const Vector3& direction,
                                  SegID segment, ObjRef parentRef, float damageMultiplier = 1) {
        auto parent = Game::Level.TryGetObject(parentRef);

        auto& weapon = Resources::GetWeapon(id);
        Object bullet{};
        bullet.Position = bullet.PrevPosition = position;
        auto rotation = VectorToObjectRotation(direction);
        bullet.Rotation = bullet.PrevRotation = rotation;
        bullet.Segment = TraceSegment(Game::Level, segment, position); // handle gunpoints positioning the projectile into an adjacent seg
        //if (bullet.Segment != segment)
        //    SPDLOG_INFO("Gun shifted projectile segment");

        bullet.Movement = MovementType::Physics;
        // todo: speedvar
        //auto speedvar = weapon.SpeedVariance != 1 ? 1 - weapon.SpeedVariance * Random() : 1;
        float speed = 0;

        if (weapon.Extended.InitialSpeed[(int)Game::Difficulty] != 0)
            speed = weapon.Extended.InitialSpeed[(int)Game::Difficulty];
        else
            speed = GetSpeed(weapon);

        bullet.Physics.Velocity = direction * speed;

        //bullet.Physics.Velocity *= 0.5f;
        if (weapon.Extended.InheritParentVelocity && parent) {
            if (WeaponIsMine(id) && parent->IsRobot()) {
                // Randomize the drop direction a bit when a robot drops a mine
                auto veldir = parent->Rotation.Backward() * 3 + RandomVector();
                veldir.Normalize();
                bullet.Physics.Velocity += 20 * veldir;
            }
            else {
                bullet.Physics.Velocity += parent->Physics.Velocity;
            }
        }

        SetFlag(bullet.Physics.Flags, PhysicsFlag::PointCollideWalls, weapon.Extended.PointCollideWalls);

        if (weapon.Extended.UseThrust)
            SetFlag(bullet.Physics.Flags, PhysicsFlag::UseThrust);

        bullet.Physics.Flags |= weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
        if (weapon.Extended.RicochetChance > 0)
            bullet.Physics.Flags |= PhysicsFlag::Ricochet;
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
            Graphics::LoadTexture(weapon.BlobBitmap);
        }
        else if (weapon.RenderType == WeaponRenderType::VClip) {
            bullet.Render.Type = RenderType::WeaponVClip;
            bullet.Render.VClip.ID = weapon.WeaponVClip;
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : weapon.BlobSize;
            Graphics::LoadTexture(weapon.WeaponVClip);
        }
        else if (weapon.RenderType == WeaponRenderType::Model) {
            bullet.Render.Type = RenderType::Model;

            auto& model = Resources::GetModel(weapon.Model);
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : model.Radius / weapon.ModelSizeRatio;
            if (bullet.Radius < 0) bullet.Radius = 1;

            auto d3Model = weapon.Extended.ModelName.empty() ? ModelID::None : Graphics::LoadOutrageModel(weapon.Extended.ModelName);

            if (Settings::Inferno.Descent3Enhanced && d3Model != ModelID::None) {
                bullet.Render.Model.ID = Graphics::LoadOutrageModel(weapon.Extended.ModelName);
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
            Graphics::LoadModel(weapon.Model);
            Graphics::LoadModel(weapon.ModelInner);

            if (bullet.Render.Model.ID == ModelID::None)
                bullet.Render.Type = RenderType::None;
        }
        else if (weapon.RenderType == WeaponRenderType::None) {
            bullet.Radius = weapon.Extended.Size >= 0 ? weapon.Extended.Size : 1;
        }

        // Mines look weird when rotated randomly
        if (id != WeaponID::ProxMine && id != WeaponID::SmartMine)
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

        bullet.Rotation.Normalize();
        bullet.PrevRotation = bullet.Rotation;

        // If a weapon creates children, they should bounce for a short duration so they aren't immediately destroyed
        if (parent && parent->IsWeapon())
            bullet.Physics.Bounces = 1;

        return bullet;
    }

    ObjRef FireWeapon(Object& obj, const FireWeaponInfo& info) {
        obj.Effects.CloakFlickerTimer = CLOAK_FIRING_FLICKER;

        auto ref = Game::GetObjectRef(obj);
        auto gunSubmodel = GetGunpointSubmodelOffset(obj, info.gun);
        auto objOffset = GetSubmodelOffset(obj, gunSubmodel);
        auto position = Vector3::Transform(objOffset, obj.GetTransform());
        Vector3 direction = info.customDir ? *info.customDir : obj.Rotation.Forward();
        auto projectile = CreateWeaponProjectile(info.id, position, direction, obj.Segment, ref, info.damageMultiplier);
        auto& weapon = Resources::GetWeapon(info.id);
        projectile.Faction = obj.Faction;
        bool showFlash = info.showFlash;

        if (weapon.Extended.Recoil)
            ApplyForce(obj, obj.Rotation.Backward() * weapon.Extended.Recoil);

        auto renderFlag = RenderFlag::None;

        if (Game::GetState() == GameState::Game && obj.IsPlayer()) {
            if (info.gun == 6)
                renderFlag = RenderFlag::ThirdPerson; // Hide center gun flash in first person (gun is under the ship, player can't see it!)

            if (!Settings::Inferno.ShowWeaponFlash)
                renderFlag = RenderFlag::ThirdPerson; // Hide first-person weapon flash if setting is disabled
        }

        if (showFlash) {
            ParticleInfo p{};
            p.Clip = weapon.FlashVClip;
            p.Radius = weapon.FlashSize;
            p.FadeTime = 0.175f;
            p.Color = weapon.Extended.FlashColor * 10; // Flash sprites look better when overexposed
            p.Flags = renderFlag;
            AttachParticle(p, ref, gunSubmodel);

            // Muzzle flash. Important for mass weapons that don't emit lights on their own.
            LightEffectInfo light;
            light.LightColor = weapon.Extended.FlashColor;
            light.Radius = weapon.FlashSize * 4;
            light.FadeTime = 0.25f;
            light.SpriteMult = 0;
            light.Flags = renderFlag;
            AddLight(light, position, light.FadeTime, obj.Segment);
        }

        auto objRef = AddObject(projectile);

        if (auto tracer = EffectLibrary.GetTracer(weapon.Extended.Tracer))
            AddTracer(*tracer, objRef);

        if (auto sparks = EffectLibrary.GetSparks(weapon.Extended.Sparks))
            AttachSparkEmitter(*sparks, objRef);

        return objRef;
    }

    void SpreadfireBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        //constexpr float SPREAD_ANGLE = 1 / 16.0f * RadToDeg;
        auto spread = Resources::GetWeapon(wid).Extended.Spread * DegToRad;
        auto& obj = Game::GetPlayerObject();

        auto& weapon = Resources::GetWeapon(wid);
        FireWeaponInfo info = { .id = wid, .gun = gun, .volume = weapon.Extended.FireVolume };

        if (player.SpreadfireToggle) {
            // Vertical
            FireSpreadWeapon(obj, info);
            info.showFlash = false;
            info.volume = 0;
            FireSpreadWeapon(obj, info, { 0, -spread });
            FireSpreadWeapon(obj, info, { 0, spread });
        }
        else {
            // Horizontal
            FireSpreadWeapon(obj, info);
            info.showFlash = false;
            info.volume = 0;
            FireSpreadWeapon(obj, info, { -spread, 0 });
            FireSpreadWeapon(obj, info, { spread, 0 });
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
        auto& obj = Game::GetPlayerObject();
        player.HelixOrientation = (player.HelixOrientation + 1) % 8;
        auto offset = GetHelixOffset(player.HelixOrientation);
        auto& weapon = Resources::GetWeapon(wid);
        FireWeaponInfo info = { .id = wid, .gun = gun, .volume = weapon.Extended.FireVolume };

        FireSpreadWeapon(obj, info);
        info.showFlash = false;
        info.volume = 0;
        FireSpreadWeapon(obj, info, offset);
        FireSpreadWeapon(obj, info, offset * 2);
        FireSpreadWeapon(obj, info, -offset);
        FireSpreadWeapon(obj, info, -offset * 2);
    }

    void VulcanBehavior(const Inferno::Player& /*player*/, uint8 gun, WeaponID wid) {
        //constexpr float SPREAD_ANGLE = 1 / 32.0f * RadToDeg; // -0.03125 to 0.03125 spread
        auto& weapon = Resources::GetWeapon(wid);
        auto spread = weapon.Extended.Spread * DegToRad;
        auto point = RandomPointInCircle(spread);
        FireWeaponInfo info = { .id = wid, .gun = gun, .volume = weapon.Extended.FireVolume };
        FireSpreadWeapon(Game::GetPlayerObject(), info, { point.x, point.y });
    }

    void ShotgunBehavior(const Inferno::Player& /*player*/, uint8 gun, WeaponID wid) {
        auto& weapon = Resources::GetWeapon(wid);
        auto spread = weapon.Extended.Spread * DegToRad;

        bool flash = true;
        for (uint i = 0; i < weapon.FireCount; i++) {
            auto point = RandomPointInCircle(spread);
            FireWeaponInfo info = { .id = wid, .gun = gun, .volume = weapon.Extended.FireVolume, .showFlash = flash };
            FireSpreadWeapon(Game::GetPlayerObject(), info, { point.x, point.y });
            flash = false;
        }
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
        RayQuery query{ .MaxDistance = dist, .Start = obj.Segment, .Mode = RayQueryMode::Precise };

        bool inFov = PointIsInFOV(obj.Rotation.Forward(), dir, fov);
        return inFov && !Intersect.RayLevel(targetRay, query, hit);
    }

    // Used for omega and homing weapons. Fov is expressed in cos(theta), use ConvertFov() if needed.
    ObjRef GetClosestObjectInFOV(const Object& src, float fov, float maxDist, ObjectMask mask, Faction faction) {
        ObjRef target;
        float bestDotFov = -1;
        auto forward = src.Rotation.Forward();
        // this could be changed to only scan segments in front of the missile, but the check might be more expensive than just iterating

        IterateNearbySegments(Game::Level, src, maxDist, TraversalFlag::PassTransparent, [&](const Segment& seg, bool&) {
            for (auto& objId : seg.Objects) {
                if (auto obj = Game::Level.TryGetObject(objId)) {
                    if (!obj->IsAlive() || !obj->PassesMask(mask) || !obj->IsInFaction(faction) || HasFlag(obj->Flags, ObjectFlag::Destroyed)) continue;

                    auto [odir, odist] = GetDirectionAndDistance(obj->Position, src.Position);
                    auto dot = odir.Dot(forward);
                    if (target && dot < bestDotFov /*&& !isClose*/)
                        continue; // Already found a target and this one is further from center FOV

                    if (CanTrackTarget(src, *obj, fov, maxDist)) {
                        bestDotFov = dot;
                        target = { objId, obj->Signature };
                    }
                }
            }
        });

        return target;
    }

    void OmegaBehavior(Inferno::Player& player, uint8 gun, WeaponID wid) {
        constexpr auto MAX_DIST = 60;
        constexpr auto MAX_TARGETS = 3;
        constexpr auto MAX_CHAIN_DIST = 30;

        const auto& weapon = Resources::GetWeapon(wid);

        auto& battery = player.Ship.Weapons[(int)PrimaryWeaponIndex::Omega];

        player.OmegaCharge -= battery.EnergyUsage;
        player.OmegaCharge = std::max(0.0f, player.OmegaCharge);

        auto pObj = Game::Level.TryGetObject(player.Reference);
        if (!pObj) return;
        auto& playerObj = *pObj;

        auto gunSubmodel = GetGunpointSubmodelOffset(playerObj, gun);
        auto objOffset = GetSubmodelOffset(playerObj, gunSubmodel);
        auto start = Vector3::Transform(objOffset, playerObj.GetTransform());
        auto targetMask = ObjectMask::Robot | ObjectMask::Mine;
        auto targetFactions = Faction::Robot | Faction::Neutral;
        auto initialTarget = GetClosestObjectInFOV(playerObj, weapon.Extended.HomingFov, MAX_DIST, targetMask, targetFactions);
        auto spark = EffectLibrary.GetSparks("omega hit");

        LightEffectInfo light{};
        light.LightColor = weapon.Extended.LightColor;
        light.Radius = weapon.Extended.LightRadius;
        light.FadeTime = weapon.Extended.LightFadeTime;
        AddLight(light, playerObj.Position, weapon.Lifetime, playerObj.Segment);

        if (initialTarget) {
            // found a target! try chaining to others
            std::array<ObjRef, MAX_TARGETS> targets{};
            //targets.fill(ObjID::None);
            targets[0] = initialTarget;

            for (int i = 0; i < MAX_TARGETS - 1; i++) {
                if (!targets[i]) break;

                if (auto src = Game::Level.TryGetObject(targets[i])) {
                    auto [id, dist] = Game::FindNearestVisibleObject({ src->Segment, src->Position }, MAX_CHAIN_DIST, targetMask, targets, targetFactions);
                    if (id)
                        targets[i + 1] = id;
                }
            }

            //auto prevPosition = start;
            ObjRef prevRef = player.Reference;
            int objGunpoint = gun;

            auto beam = EffectLibrary.GetBeamInfo("omega beam");
            auto beam2 = EffectLibrary.GetBeamInfo("omega beam2");
            auto tracer = EffectLibrary.GetBeamInfo("omega tracer");

            auto damage = GetDamage(weapon);

            // Apply damage and visuals to each target
            for (auto& targetRef : targets) {
                if (!targetRef) continue;
                auto target = Game::Level.TryGetObject(targetRef);
                if (!target) continue;
                if (!Settings::Cheats.DisableWeaponDamage) {
                    if (target->IsPlayer())
                        Player.ApplyDamage(damage, true);
                    else if (target->IsRobot())
                        DamageRobot(playerObj, *target, damage, weapon.Extended.StunMult, pObj);
                    if (target->IsWeapon())
                        // a bomb or other weapon was shot. cause it to explode by expiring.
                        target->Lifespan = -1;
                    else
                        target->ApplyDamage(damage);
                }

                // Beams between previous and next target
                if (beam) AttachBeam(*beam, 0, prevRef, targetRef, objGunpoint);
                if (beam2) {
                    AttachBeam(*beam2, 0, prevRef, targetRef, objGunpoint);
                    AttachBeam(*beam2, 0, prevRef, targetRef, objGunpoint);
                }

                prevRef = targetRef;
                objGunpoint = -1;

                if (tracer) {
                    AttachBeam(*tracer, 0, targetRef);
                    AttachBeam(*tracer, 0, targetRef);
                }

                // Sparks and explosion
                if (spark) AddSparkEmitter(*spark, target->Segment, target->Position);

                ExplosionEffectInfo expl;
                //expl.Sound = weapon.RobotHitSound;
                expl.Clip = VClipID::SmallExplosion;
                expl.Radius = { weapon.ImpactSize * 0.85f, weapon.ImpactSize * 1.15f };
                expl.Variance = target->Radius * 0.45f;
                //expl.Color = Color{ 1.15f, 1.15f, 1.15f };
                expl.FadeTime = 0.1f;
                CreateExplosion(expl, target->Segment, target->Position);
            }

            // Hit sound
            constexpr std::array hitSounds = { "EnvElectricA", "EnvElectricB", "EnvElectricC", "EnvElectricD", "EnvElectricE", "EnvElectricF" };
            if (auto initialTargetObj = Game::Level.TryGetObject(initialTarget)) {
                auto name = hitSounds[RandomInt((int)hitSounds.size() - 1)];
                Sound3D hitSound({ name });
                hitSound.Volume = 2.00f;
                hitSound.Radius = 200;
                Sound::Play(hitSound, initialTargetObj->Position, initialTargetObj->Segment);
            }
        }
        else {
            // no target: pick a random point within FOV
            auto offset = RandomPointInCircle(acos(weapon.Extended.HomingFov) * 0.5f);
            auto dir = playerObj.Rotation.Forward();
            dir += playerObj.Rotation.Right() * offset.x;
            dir += playerObj.Rotation.Up() * offset.y;
            dir.Normalize();

            Vector3 tracerEnd;
            LevelHit hit;
            RayQuery query{ .MaxDistance = MAX_DIST, .Start = playerObj.Segment, .Mode = RayQueryMode::Precise };

            if (Game::Intersect.RayLevel({ playerObj.Position, dir }, query, hit)) {
                //tracer.End = beam.End = hit.Point;
                tracerEnd = hit.Point;

                if (spark) AddSparkEmitter(*spark, hit.Tag.Segment, hit.Point);

                // Do wall hit stuff
                Object dummy{};
                dummy.Position = hit.Point;
                dummy.Parent = player.Reference;
                dummy.ID = (int)WeaponID::Omega;
                dummy.Type = ObjectType::Weapon;
                dummy.Control.Weapon.ParentType = ObjectType::Player; // needed for wall triggers to work correctly
                dummy.Segment = hit.Tag.Segment;
                WeaponHitWall(hit, dummy, Game::Level, ObjID::None);

                if (auto wall = Game::Level.TryGetWall(hit.Tag))
                    HitWall(Game::Level, hit.Point, dummy, *wall);
            }
            else {
                //SetFlag(tracer.Flags, BeamFlag::FadeEnd);
                tracerEnd = start + dir * MAX_DIST;
            }

            if (auto miss = EffectLibrary.GetBeamInfo("omega miss"))
                AddBeam(*miss, player.Reference, tracerEnd, gun);
        }

        // Fire sound
        Sound3D sound(weapon.FlashSound);
        sound.Volume = 0.70f;
        sound.AttachOffset = gunSubmodel.offset;
        Sound::PlayFrom(sound, playerObj);

        auto renderFlag = RenderFlag::None;

        if (Game::GetState() == GameState::Game && !Settings::Inferno.ShowWeaponFlash)
            renderFlag = RenderFlag::ThirdPerson; // Hide first-person weapon flash if setting is disabled

        ParticleInfo p{};
        p.Clip = weapon.FlashVClip;
        p.Radius = weapon.FlashSize;
        p.FadeTime = 0.175f;
        p.Color = weapon.Extended.FlashColor;
        p.Flags = renderFlag;
        AttachParticle(p, player.Reference, gunSubmodel);
    }

    void FusionBehavior(const Inferno::Player& player, uint8 gun, WeaponID wid) {
        // Fixes original behavior of fusion jumping from 2.9x to 4x damage at 4 seconds charge.
        // This is believed to be a logic error.
        // Self-damage starts after two seconds, at which the original damage multiplier is 2x.
        // This funciton results in a 2.5x multiplier at 2 seconds, a small buff to charging.
        constexpr auto MAX_FUSION_CHARGE_TIME = 4.0f; // Time in seconds for full charge
        constexpr auto MAX_FUSION_CHARGE_MULT = 3.0f; // Bonus damage multiplier for full charge
        float multiplier = MAX_FUSION_CHARGE_MULT * player.WeaponCharge / MAX_FUSION_CHARGE_TIME;
        if (multiplier > MAX_FUSION_CHARGE_MULT) multiplier = MAX_FUSION_CHARGE_MULT;

        auto& weapon = Resources::GetWeapon(wid);

        FireWeaponInfo info = {
            .id = wid,
            .gun = gun,
            .volume = weapon.Extended.FireVolume,
            .damageMultiplier = 1 + multiplier
        };

        FireWeapon(Game::GetPlayerObject(), info);
    }

    // default weapon firing behavior
    void DefaultBehavior(const Inferno::Player& /*player*/, uint8 gun, WeaponID wid) {
        FireWeaponInfo info = { .id = wid, .gun = gun };
        FireWeapon(Game::GetPlayerObject(), info);
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

        auto search = [&](const Segment& seg, bool& stop) {
            for (auto& objId : seg.Objects) {
                if (auto obj = Game::Level.TryGetObject(objId)) {
                    if (!obj->PassesMask(mask)) continue;

                    if (!obj->IsAlive() || HasFlag(obj->Flags, ObjectFlag::Destroyed)) continue;
                    if (obj->IsCloaked() || obj->IsPhasing()) continue; // cloaked objects aren't visible
                    auto [dir, dist] = GetDirectionAndDistance(obj->Position, object.Position);

                    if (dist < maxDist) {
                        Ray ray(object.Position, dir);
                        RayQuery query;
                        query.Start = object.Segment;
                        query.MaxDistance = dist;
                        query.Mode = RayQueryMode::Precise;
                        LevelHit hit;
                        if (!Intersect.RayLevel(ray, query, hit)) {
                            targets[count] = { objId, obj->Signature };
                            count++;
                            if (count >= targets.size()) {
                                SPDLOG_WARN("Max nearby targets reached");
                                stop = true;
                                return;
                            }
                        }
                    }
                }
            }
        };

        IterateNearbySegments(Game::Level, object, maxDist, TraversalFlag::PassTransparent, search);

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

        //SPDLOG_INFO("Tracking: {} dir: {}", targetId, dir);
        auto parentRef = Game::GetObjectRef(parent);
        auto blob = CreateWeaponProjectile(type, parent.Position, dir, parent.Segment, parentRef);

        blob.Control.Weapon.TrackingTarget = targetId;
        blob.Faction = parent.Faction;
        AddObject(blob);
    }

    void CreateMissileSpawn(const Object& missile, uint blobs) {
        auto mask = missile.Control.Weapon.ParentType == ObjectType::Player ? ObjectMask::Robot : ObjectMask::Player;
        int targetCount = 0;

        const Weapon& weapon = Resources::GetWeapon(missile);

        auto spawn = weapon.Spawn;
        if (missile.Control.Weapon.ParentType != ObjectType::Player && spawn == WeaponID::PlayerSmartBlob)
            spawn = WeaponID::RobotSmartBlob; // HACK: Override blobs for robot smart missiles

        const Weapon& spawnWeapon = Resources::GetWeapon(spawn);
        auto targets = GetNearbyLockTargets(missile, spawnWeapon.Extended.HomingDistance, targetCount, mask);
        Sound3D sound(spawnWeapon.FlashSound);
        sound.Volume = spawnWeapon.Extended.FireVolume;
        sound.Radius = spawnWeapon.Extended.SoundRadius;
        Sound::Play(sound, missile.Position, missile.Segment);

        if (targetCount > 0) {
            //SPDLOG_INFO("Found blob targets");
            // if found targets, pick random target from array
            for (size_t i = 0; i < blobs; i++)
                CreateHomingBlob(spawn, missile, targets[RandomInt(targetCount - 1)]);
        }
        else {
            //SPDLOG_INFO("No blob targets");
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
        obj.Rotation = VectorToObjectRotation(fvec);
    }

    Vector3 ClampTargetToFov(const Vector3& direction, const Vector3& origin, const Vector3& target, float rads) {
        // project target to centerline of gunpoint
        auto projTarget = direction * direction.Dot(target - origin) + origin;
        auto projDist = Vector3::Distance(origin, projTarget);
        auto projDir = target - projTarget;
        projDir.Normalize();
        auto maxLeadDist = tanf(rads) * projDist;
        return projTarget + maxLeadDist * projDir;
    }

    void UpdateHomingWeapon(Object& weapon, const Weapon& weaponInfo, float dt) {
        if (!weaponInfo.IsHoming) return;

        //if (!TimeHasElapsed(weapon.NextThinkTime))
        //    return; // Not ready to think

        // Homing weapons update slower to match the original behavior
        //weapon.NextThinkTime = Game::Time + HOMING_TICK_RATE;

        if (weapon.Control.Weapon.AliveTime < WEAPON_HOMING_DELAY)
            return; // Not ready to start homing yet

        weapon.Physics.Bounces = 0; // Hack for smart missile blob bounces
        auto& target = weapon.Control.Weapon.TrackingTarget;
        auto fov = weaponInfo.Extended.HomingFov;
        auto distance = weaponInfo.Extended.HomingDistance;

        bool targetingMine = false;

        // Check if the target is still trackable
        if (target) {
            auto targetObj = Game::GetObject(target);
            if (targetObj)
                targetingMine = ObjectIsMine(*targetObj);

            if (!targetObj || !CanTrackTarget(weapon, *targetObj, fov, distance)) {
                //SPDLOG_INFO("Lost tracking target");
                target = {}; // target destroyed or out of view
            }
        }


        // Check if a mine came into view
        if (!targetingMine) {
            Faction targetFaction = HasFlag(weapon.Faction, Faction::Player) ? Faction::Robot | Faction::Neutral : Faction::Player | Faction::Neutral;

            if (auto mine = GetClosestObjectInFOV(weapon, fov / 2, distance / 2, ObjectMask::Mine, targetFaction)) {
                target = mine;
                //SPDLOG_INFO("Redirecting missile to mine {}", mine);
            }
        }

        if (!target) {
            // Find a new target
            auto mask = ObjectMask::Robot | ObjectMask::Mine;
            if (auto parent = Game::GetObject(weapon.Parent))
                if (parent->IsRobot())
                    mask = ObjectMask::Player;

            target = GetClosestObjectInFOV(weapon, fov, distance, mask, FlipFlags(weapon.Faction));
            //if (target)
            //    SPDLOG_INFO("Locking onto {}", target);
        }
        else if (auto targetObj = Game::GetObject(target)) {
            // turn towards target
            auto [targetDir, targetDist] = GetDirectionAndDistance(targetObj->Position, weapon.Position);

            // Update player lock warning
            if (targetObj->IsPlayer()) {
                if (Game::Player.HomingObjectDist < 0 || targetDist < Game::Player.HomingObjectDist)
                    Game::Player.HomingObjectDist = targetDist;
            }


            auto forward = weapon.Rotation.Forward();
            auto targetAngle = AngleBetweenVectors(forward, targetDir);
            auto turnRate = weaponInfo.Extended.HomingTurnRate * DegToRad * dt;
            Vector3 dir = weapon.Physics.Velocity;
            auto speed = dir.Length();

            // limit turn rate
            if (targetAngle > turnRate) {
                auto targetPosition = ClampTargetToFov(forward, weapon.Position, targetObj->Position, turnRate);
                dir = targetPosition - weapon.Position;
            }

            dir.Normalize();
            weapon.Physics.Velocity = dir * speed;

            //Render::Debug::DrawLine(weapon.Position, target->Position, Color(1, 0, 0));
            TurnTowardsNormal(weapon, dir, dt);
        }
    }

    void UpdateWeapon(Object& weapon, float dt) {
        weapon.Control.Weapon.AliveTime += dt;
        auto& weaponInfo = Resources::GetWeapon(weapon);

        if (weaponInfo.Extended.Behavior == "proxmine")
            ProxMineBehavior(weapon);

        UpdateHomingWeapon(weapon, weaponInfo, dt);
    }
}
