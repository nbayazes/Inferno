#include "pch.h"
#include "Game.Cinematics.h"
#include "Game.AI.h"
#include "Game.h"
#include "Game.Input.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "Physics.h"
#include "VisualEffects.h"

namespace Inferno {
    

    void EscapeSequence() {
        //CinematicInfo begin {
        //    //.Letterbox = ,
        //    //.FadeIn = ,
        //    //.FadeOut = ,
        //    //.FadeColor = ,
        //    //.SkipRange = ,
        //    //.MoveObjectToEndOfPathOnSkip = ,
        //    //.TargetObject = ,
        //    //.Target = ,
        //    .CameraPath = ,
        //    .Text = ,
        //    .TextMode = ,
        //    .TextRange = 
        //};
        

        /* -- ESCAPE SEQUENCE --
         * Start cinematic to disable controls
         * Not skippable
         * Move player along path.
         * Create small explosions on walls in front of player
         * NEW: Slightly randomize rotation and shake ship.
         *
         * After 1/3 of tunnel traversed:
         * - move camera outside of ship, offset by 7 units facing backwards
         * - switch to letterbox and disable hud
         * - Create explosions in segments behind player (maybe around path?)
         *
         * After 2/3
         * - prepare to show terrain, switch tunnel model
         *
         * After 3/3
         * - Create big explosion, switch mine model
         * - Detach camera and move to position away from exit, track player?
         * - Do a random roll on the ship
         */
        // 
        // 


        //int stage = 0;


    }


    constexpr float PLAYER_DEATH_EXPLODE_TIME = 2.0f;

    void DrawCutsceneLetterbox() {
        auto& size = Render::Canvas->GetSize();
        auto height = size.y / 8;
        Render::Canvas->DrawRectangle(Vector2(0, 0), Vector2(size.x, height), Color(0, 0, 0));
        Render::Canvas->DrawRectangle(Vector2(0, size.y - height), Vector2(size.x, height), Color(0, 0, 0));
    }

    Vector3 FindDeathCameraPosition(const Vector3& start, SegID startSeg, float preferDist) {
        Vector3 bestDir;
        float bestDist = 0;

        for (int i = 0; i < 10; i++) {
            Ray ray(start, RandomVector());
            RayQuery query{ .MaxDistance = preferDist, .Start = startSeg };
            LevelHit hit{};
            if (!Game::Intersect.RayLevel(ray, query, hit)) {
                // Ray didn't hit anything so use it!
                bestDist = preferDist;
                bestDir = ray.direction;
                break;
            }

            if (hit.Distance > bestDist) {
                bestDist = hit.Distance;
                bestDir = ray.direction;
            }
        }

        return start + bestDir * bestDist * 0.95f;
    }

    void DoDeathSequence(Player& state, float dt) {
        if (!state.IsDead) return;

        state.TimeDead += dt;

        auto& player = Game::GetPlayerObject();

        if (!Game::GetObject(Game::DeathCamera)) {
            Object camera{};
            camera.Type = ObjectType::Camera;
            camera.Segment = player.Segment;
            camera.Position = FindDeathCameraPosition(player.Position, player.Segment, 30);
            Game::DeathCamera = Game::AddObject(camera);
        }

        auto camera = Game::GetObject(Game::DeathCamera);
        if (!camera) {
            SPDLOG_ERROR("Unable to create death camera");
            return;
        }
        ASSERT(camera->Type == ObjectType::Camera);

        auto rollSpeed = std::max(0.0f, PLAYER_DEATH_EXPLODE_TIME - state.TimeDead);
        player.Physics.AngularVelocity.x = rollSpeed / 4;
        player.Physics.AngularVelocity.y = rollSpeed / 2;
        player.Physics.AngularVelocity.z = rollSpeed / 3;

        auto playerPos = player.GetPosition(Game::LerpAmount);

        DrawCutsceneLetterbox();

        if (state.TimeDead > PLAYER_DEATH_EXPLODE_TIME) {
            if (!state.Exploded) {
                state.Exploded = true;
                ResetAITargets();
                state.Lives--;

                GameExplosion explosion;
                explosion.Damage = 50;
                explosion.Force = 150;
                explosion.Radius = 40;
                explosion.Position = player.Position;
                explosion.Room = Game::Level.GetRoomID(player);
                explosion.Segment = player.Segment;
                CreateExplosion(Game::Level, &player, explosion);

                if (auto e = EffectLibrary.GetExplosion("player explosion"))
                    CreateExplosion(*e, player.Segment, playerPos);

                if (auto e = EffectLibrary.GetExplosion("player explosion trail"))
                    CreateExplosion(*e, player.Segment, playerPos);

                CreateObjectDebris(player, player.Render.Model.ID, Vector3::Zero);
                player.Render.Type = RenderType::None; // Hide the player after exploding
                player.Type = ObjectType::Ghost;

                state.DropAllItems();
            }

            string message;
            if (state.HostagesOnShip > 1) {
                message = fmt::format("Ship destroyed, {} hostages lost!", state.HostagesOnShip);
            }
            else if (state.HostagesOnShip == 1) {
                message = "ship destroyed, 1 hostage lost!";
            }
            else {
                message = "ship destroyed!";
            }

            auto height = Render::CANVAS_HEIGHT / 8.0f * Render::Canvas->GetScale();

            Render::DrawTextInfo info;
            info.Position = Vector2(0, 10 + height);
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Top;
            info.Font = FontSize::Small;
            info.Color = Color(0, 1, 0);
            Render::Canvas->DrawGameText(message, info);

            info.VerticalAlign = AlignV::Bottom;
            info.Position = Vector2(0, -10 - height);
            Render::Canvas->DrawGameText("press fire to continue...", info);
        }
        else {
            player.Render.Type = RenderType::Model; // Camera is in third person, show the player

            if (Random() < dt * 4) {
                if (auto e = EffectLibrary.GetExplosion("large fireball")) {
                    CreateExplosion(*e, Game::GetObjectRef(player));
                }
            }

            ASSERT(camera->Type == ObjectType::Camera);
            auto fvec = camera->Position - playerPos;
            //auto cameraDist = fvec.Length();
            fvec.Normalize();
            camera->Rotation = VectorToRotation(fvec);

            //auto goalCameraDist = std::min(TimeDead * 8, 20.0f) + player.Radius;
            //if (cameraDist < 20) {
            //    float delta = dt * 10;
            //    Ray ray(camera->Position, fvec);
            //    RayQuery query{ .MaxDistance = delta };
            //    LevelHit hit{};
            //    if (!Game::Intersect.RayLevel(ray, query, hit)) {
            //        camera->Position += fvec * delta;
            //    }
            //}

            Game::MoveCameraToObject(Game::GameCamera, *camera, Game::LerpAmount);
        }
    }

    void UpdateDeathSequence(float dt) {
        DoDeathSequence(Game::Player, dt);

        if (Game::Player.TimeDead > 2 && Game::Player.Lives == 0) {
            Render::DrawTextInfo info;
            info.Font = FontSize::Big;
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Center;
            Render::Canvas->DrawGameText("game over", info);
        }

        if (Game::Player.TimeDead > 2 && ConfirmedInput()) {
            if (Game::Player.Lives == 0)
                Game::SetState(GameState::Editor); // todo: score screen
            else
                Game::Player.Respawn(true); // todo: EndCutscene() with fades
        }
    }

}
