#include "pch.h"
#include "Game.Cinematics.h"
#include "Game.AI.h"
#include "Game.Bindings.h"
#include "Game.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "Physics.h"
#include "VisualEffects.h"

namespace Inferno {
    constexpr float PLAYER_DEATH_EXPLODE_TIME = 2.0f;

    void DrawCutsceneLetterbox() {
        auto& size = Render::UICanvas->GetSize();
        auto height = size.y / 8;

        Render::CanvasBitmapInfo cbi;
        cbi.Position = Vector2(0, 0);
        cbi.Size = Vector2(size.x, height);
        cbi.Color = Color(0, 0, 0);
        Render::UICanvas->DrawRectangle(cbi);

        cbi.Position = Vector2(0, size.y - height);
        Render::UICanvas->DrawRectangle(cbi);
    }

    Vector3 FindDeathCameraPosition(const Vector3& start, SegID startSeg, float preferDist) {
        Vector3 bestDir;
        float bestDist = 0;
        bool hitWall = false;

        for (int i = 0; i < 10; i++) {
            Ray ray(start, RandomVector());
            RayQuery query{ .MaxDistance = preferDist, .Start = startSeg };
            LevelHit hit{};
            hitWall = Game::Intersect.RayLevel(ray, query, hit);

            if (!hitWall) {
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

        // Move camera off of wall if possible
        if (hitWall && bestDist > 5) 
            bestDist -= 4.0f;

        SPDLOG_INFO("Hit wall: {}", hitWall);
        return start + bestDir * bestDist;
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
                state.LoseLife();

                if (Game::ControlCenterDestroyed)
                    Game::CountdownTimer = 0.01f; // Start fading out immediately, no respawning

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

                CreateObjectDebris(player, player.Render.Model.ID);

                for (size_t i = 0; i < 16; i++) {
                    auto random = RandomPointOnSphere() * player.Radius * 0.35;
                    CreateDebris(player.Segment, player.Position + random);
                }

                player.Render.Type = RenderType::None; // Hide the player after exploding
                player.Type = ObjectType::Ghost;

                state.DropAllItems();
            }

            string message;
            if (state.stats.hostagesOnboard > 1) {
                message = fmt::format("Ship destroyed, {} hostages lost!", state.stats.hostagesOnboard);
            }
            else if (state.stats.hostagesOnboard == 1) {
                message = "ship destroyed, 1 hostage lost!";
            }
            else {
                message = "ship destroyed!";
            }

            auto height = Render::UICanvas->GetSize().y / 8.0f;

            Render::DrawTextInfo info;
            info.Position = Vector2(0, 10 + height);
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Top;
            info.Font = FontSize::Small;
            info.Color = Color(0, 1, 0);
            Render::UICanvas->DrawRaw(message, info, 1);

            if (!Game::ControlCenterDestroyed) {
                info.VerticalAlign = AlignV::Bottom;
                info.Position = Vector2(0, -10 - height);
                Render::UICanvas->DrawRaw("press fire to continue", info, 1);
            }
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

            Game::MoveCameraToObject(Game::MainCamera, *camera, Game::LerpAmount);
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

        if (Game::Player.TimeDead < 2) return;

        if (Input::OnKeyPressed(Input::Keys::Space) ||
            Game::Bindings.Pressed(GameAction::FirePrimary) ||
            Game::Bindings.Pressed(GameAction::FireSecondary) ||
            Input::MouseButtonPressed(Input::MouseButtons::LeftClick)) {
            if (Game::Player.Lives == 0) {
                Game::SetState(GameState::MainMenu); // todo: final score screen
            }
            else if (!Game::ControlCenterDestroyed) {
                Game::Player.Respawn(true);
            }
        }
    }
}
