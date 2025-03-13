#include "pch.h"
#include "Game.Automap.h"
#include "Game.Bindings.h"
#include "Game.Input.h"
#include "Game.Segment.h"
#include "Graphics/Render.h"
#include "Resources.h"
#include "SoundSystem.h"

namespace Inferno {
    Vector2 ApplyOverlayRotation(const SegmentSide& side, Vector2 uv);

    using Render::AutomapType;

    struct AutomapMesh {
        List<LevelVertex> Vertices;
        List<uint32> Indices;

        int32 AddSide(const Level& level, Segment& seg, SideID sideId, bool addOffset = false) {
            auto startIndex = (int32)Vertices.size();
            auto& side = seg.GetSide(sideId);
            auto& uv = side.UVs;

            Indices.push_back(startIndex);
            Indices.push_back(startIndex + 1);
            Indices.push_back(startIndex + 2);

            Indices.push_back(startIndex + 0);
            Indices.push_back(startIndex + 2);
            Indices.push_back(startIndex + 3);

            auto& sideVerts = SIDE_INDICES[(int)sideId];

            auto offset = addOffset ? side.AverageNormal * 0.5f : Vector3::Zero;

            for (int i = 0; i < 4; i++) {
                auto& vert = level.Vertices[seg.Indices[sideVerts[i]]];
                Vector2 uv2 = side.HasOverlay() ? ApplyOverlayRotation(side, uv[i]) : Vector2();
                Vertices.push_back({ vert + offset, uv[i], side.Light[i], uv2, side.AverageNormal });
            }

            return startIndex;
        }
    };

    AutomapType GetAutomapSegmentType(const Segment& seg) {
        if (seg.Type == SegmentType::Energy)
            return AutomapType::Fuelcen;
        else if (seg.Type == SegmentType::Matcen)
            return AutomapType::Matcen;
        else if (seg.Type == SegmentType::Reactor)
            return AutomapType::Reactor;
        else
            return AutomapType::Normal;
    }

    struct AutomapSideInfo {
        AutomapType Type = AutomapType::Unrevealed;
        AutomapVisibility Visibility = AutomapVisibility::Hidden;
        Tag Tag;
        bool IsSecretDoor = false;
        bool IsDoor = false;
        bool IsOpenDoor = false;
        bool IsTransparent = false;
        Wall* Wall = nullptr;
        bool UnrevealedBoundary = false;
    };

    AutomapType GetAutomapWallType(AutomapSideInfo& info) {
        auto wall = info.Wall;

        if (wall->Type == WallType::Door) {
            info.IsSecretDoor = HasFlag(Resources::GetDoorClip(wall->Clip).Flags, DoorClipFlag::Secret);
            info.IsOpenDoor = wall->HasFlag(WallFlag::DoorOpened) || wall->State == WallState::DoorOpening || wall->State == WallState::DoorClosing;

            // Use special door colors if possible
            if (HasFlag(wall->Keys, WallKey::Blue))
                return AutomapType::BlueDoor;
            else if (HasFlag(wall->Keys, WallKey::Gold))
                return AutomapType::GoldDoor;
            else if (HasFlag(wall->Keys, WallKey::Red))
                return AutomapType::RedDoor;
            else if (info.IsSecretDoor) {
                if (info.IsOpenDoor) {
                    // Secret door is open but not revealed, keep it hidden
                    return info.UnrevealedBoundary ? AutomapType::Normal : AutomapType::Door;
                }
                else {
                    return AutomapType::Normal; // Hide closed secret doors
                }
            }
            else if (HasFlag(wall->Flags, WallFlag::DoorLocked))
                return AutomapType::LockedDoor;
            else
                return AutomapType::Door;
        }
        else if (wall->Type == WallType::Destroyable) {
            // Destroyable walls are also doors, mark them if they are transparent
            if (info.IsTransparent)
                return AutomapType::Door;
        }
        else {
            // Not a door
            if (info.IsTransparent && info.UnrevealedBoundary)
                return AutomapType::Unrevealed; // Mark transparent walls as unrevealed
        }

        return AutomapType::Normal;
    }

    AutomapType GetBoundaryType(const Level& level, const Segment& seg, const Segment& conn) {
        if (conn.Type != seg.Type) {
            // Special segment facing a normal segment
            if (seg.Type == SegmentType::Energy)
                return AutomapType::Fuelcen;
            else if (seg.Type == SegmentType::Reactor && !level.HasBoss)
                return AutomapType::Reactor;
            else if (seg.Type == SegmentType::Matcen)
                return AutomapType::Matcen;
            else if (seg.Type == SegmentType::None) {
                // Normal segment facing a special segment
                if (conn.Type == SegmentType::Energy)
                    return AutomapType::Fuelcen;
                else if (conn.Type == SegmentType::Reactor && !level.HasBoss)
                    return AutomapType::Reactor;
                else if (conn.Type == SegmentType::Matcen)
                    return AutomapType::Matcen;
            }
        }

        return AutomapType::Normal;
    }

    // Transforms level state into meshes to draw the automap
    void UpdateAutomapMesh() {
        AutomapMesh unrevealed; // non-visited connections
        auto& level = Game::Level;
        Render::LevelResources.AutomapMeshes = make_unique<Render::AutomapMeshes>();
        auto meshes = Render::LevelResources.AutomapMeshes.get();

        const auto packMesh = [&meshes](const AutomapMesh& mesh) {
            return Render::PackedMesh{
                .VertexBuffer = meshes->Buffer.PackVertices(span{ mesh.Vertices }),
                .IndexBuffer = meshes->Buffer.PackIndices(span{ mesh.Indices }),
                .IndexCount = (uint)mesh.Indices.size()
            };
        };

        struct Meshes {
            AutomapMesh walls, solidWalls, fuelcen, matcen, reactor;
            List<Render::AutomapMeshInstance>* mesh;
            AutomapType type = AutomapType::Normal;
        };

        Meshes fullMap, revealed;
        fullMap.mesh = &meshes->FullmapWalls;
        fullMap.type = AutomapType::FullMap;
        revealed.mesh = &meshes->Walls;

        for (size_t segIndex = 0; segIndex < Game::Automap.Segments.size(); segIndex++) {
            auto visibility = Game::Automap.Segments[segIndex];
            auto& destMesh = visibility == AutomapVisibility::Visible ? revealed : fullMap;

            auto seg = level.TryGetSegment((SegID)segIndex);
            if (!seg) continue;

            for (auto& sideId : SIDE_IDS) {
                AutomapSideInfo info;
                info.Tag = { (SegID)segIndex, sideId };
                info.Wall = level.TryGetWall(info.Tag);
                info.Type = GetAutomapSegmentType(*seg);
                info.Visibility = visibility;
                info.IsTransparent = SideIsTransparent(level, info.Tag);

                if (auto connState = Seq::tryItem(Game::Automap.Segments, (int)seg->GetConnection(sideId))) {
                    info.UnrevealedBoundary =
                        (*connState != AutomapVisibility::Visible && visibility == AutomapVisibility::Visible) ||
                        (visibility != AutomapVisibility::Visible && *connState == AutomapVisibility::Visible);
                }

                auto boundaryType = AutomapType::Normal;

                if (auto conn = level.TryGetSegment(seg->GetConnection(sideId)))
                    boundaryType = GetBoundaryType(level, *seg, *conn);

                auto& side = seg->GetSide(sideId);

                const auto addTransparent = [&](AutomapType connType) {
                    AutomapMesh mesh;
                    mesh.AddSide(level, *seg, sideId);

                    Render::AutomapMeshInstance instance{
                        .Texture = Resources::LookupTexID(side.TMap),
                        .Decal = side.TMap2 > LevelTexID::Unset ? Resources::LookupTexID(side.TMap2) : TexID::None,
                        .Mesh = packMesh(mesh),
                        .Type = connType
                    };

                    meshes->TransparentWalls.push_back(instance);
                };

                if (info.UnrevealedBoundary && seg->Type != SegmentType::None) {
                    addTransparent(info.Type);
                    continue; // Mark unexplored open special sides using their colors
                }

                if (info.Wall) {
                    if (info.Wall->Type == WallType::Illusion) {
                        if (info.Visibility == AutomapVisibility::Hidden && !info.IsTransparent) {
                            continue; // Skip the back of unrevealed, opaque illusionary walls
                        }
                        else if (!info.UnrevealedBoundary && info.Visibility != AutomapVisibility::Hidden && info.IsTransparent) {
                            // special case energy center illusion boundaries
                            if (boundaryType == AutomapType::Fuelcen)
                                addTransparent(boundaryType);

                            continue; // Skip revealed, transparent illusionary walls
                        }
                    }

                    info.Type = GetAutomapWallType(info);
                }
                else if (info.UnrevealedBoundary) {
                    info.Type = AutomapType::Unrevealed;
                }

                if (boundaryType == AutomapType::Fuelcen && info.IsTransparent && info.Visibility != AutomapVisibility::Hidden) {
                    addTransparent(boundaryType);
                    continue;
                }

                if (visibility == AutomapVisibility::Hidden && (!info.UnrevealedBoundary || info.IsSecretDoor))
                    continue; // Skip hidden, non-boundary sides and the backs of secret doors

                if (info.IsOpenDoor && info.IsSecretDoor && !info.UnrevealedBoundary)
                    continue; // Skip open secret doors

                if (info.Type == AutomapType::Unrevealed && info.UnrevealedBoundary) {
                    unrevealed.AddSide(level, *seg, sideId);
                }
                else if (info.Wall) {
                    auto wallType = info.Wall->Type;

                    // Add 'walls' as individual sides
                    if (wallType == WallType::Door ||
                        wallType == WallType::Closed ||
                        wallType == WallType::Destroyable ||
                        wallType == WallType::Illusion) {
                        AutomapMesh mesh;
                        mesh.AddSide(level, *seg, sideId);

                        Render::AutomapMeshInstance instance{
                            .Texture = Resources::LookupTexID(side.TMap),
                            .Decal = side.TMap2 > LevelTexID::Unset ? Resources::LookupTexID(side.TMap2) : TexID::None,
                            .Mesh = packMesh(mesh),
                            .Type = info.Type
                        };

                        // Remove textures from open doors
                        if (wallType == WallType::Door && info.IsOpenDoor)
                            instance.Texture = instance.Decal = TexID::None;

                        if (visibility == AutomapVisibility::FullMap && info.Type == AutomapType::Normal)
                            instance.Type = AutomapType::FullMap; // Draw walls as blue

                        // Make doors transparent when open, the outline shader looks odd on them
                        if (info.IsOpenDoor && !info.UnrevealedBoundary)
                            meshes->TransparentWalls.push_back(instance);
                        else
                            destMesh.mesh->push_back(instance);
                    }
                }
                else if (seg->SideIsSolid(sideId, level)) {
                    // Add solid walls as their special types if possible
                    if (visibility == AutomapVisibility::Visible || visibility == AutomapVisibility::FullMap) {
                        if (seg->Type == SegmentType::Energy)
                            destMesh.fuelcen.AddSide(level, *seg, sideId);
                        else if (seg->Type == SegmentType::Matcen)
                            destMesh.matcen.AddSide(level, *seg, sideId);
                        else if (seg->Type == SegmentType::Reactor && !level.HasBoss)
                            destMesh.reactor.AddSide(level, *seg, sideId);
                        else
                            destMesh.solidWalls.AddSide(level, *seg, sideId);
                    }
                }
                else if (boundaryType != AutomapType::Normal) {
                    // Add boundary faces between normal and special segments
                    addTransparent(boundaryType);
                }
            }
        }

        auto submitMeshes = [&packMesh](const Meshes& src) {
            // add solid walls as a single mesh
            src.mesh->push_back({ .Mesh = packMesh(src.solidWalls), .Type = src.type });
            src.mesh->push_back({ .Mesh = packMesh(src.fuelcen), .Type = AutomapType::Fuelcen });
            src.mesh->push_back({ .Mesh = packMesh(src.matcen), .Type = AutomapType::Matcen });
            src.mesh->push_back({ .Mesh = packMesh(src.reactor), .Type = AutomapType::Reactor });
        };

        submitMeshes(revealed);
        submitMeshes(fullMap);

        // Glowing unrevealed portals
        meshes->TransparentWalls.push_back({ .Mesh = packMesh(unrevealed), .Type = AutomapType::Unrevealed });
    }

    void AutomapInfo::Update(const Inferno::Level& level) {
        if (Game::LevelNumber < 0)
            LevelNumber = fmt::format("Secret Level {}", -Game::LevelNumber);
        else
            LevelNumber = fmt::format("Level {}", Game::LevelNumber);

        if (Game::Player.Stats.HostagesOnLevel > 0) {
            auto hostagesLeft = Game::Player.Stats.HostagesOnLevel - Game::Player.HostagesRescued;
            HostageText = hostagesLeft <= 0 ? "all hostages rescued" : hostagesLeft == 1 ? "1 hostage left" : fmt::format("{} hostages left", hostagesLeft);
        }
        else {
            HostageText = {};
        }

        RobotScore = 0;
        for (auto& obj : level.Objects) {
            if (!obj.IsRobot()) continue;

            auto& info = Resources::GetRobotInfo(obj);
            RobotScore += info.Score;
        }

        for (auto& matcen : level.Matcens) {
            auto matcenSum = 0;

            auto robots = matcen.GetEnabledRobots();
            for (auto& id : robots) {
                auto& info = Resources::GetRobotInfo(id);
                matcenSum += info.Score;
            }

            // Multiply matcen score by max spawns
            int8 activations = 3;
            if (Game::Difficulty == DifficultyLevel::Ace) activations = 4;
            if (Game::Difficulty >= DifficultyLevel::Insane) activations = 5;
            auto spawnCount = (int8)Game::Difficulty + 3;
            matcenSum *= activations * spawnCount;

            // Average the matcenValue
            if (robots.size() > 0) {
                matcenSum = int((float)matcenSum / robots.size());
            }

            RobotScore += matcenSum;
        }

        if (RobotScore > 80'000) {
            Threat = "threat: extreme";
        }
        else if (RobotScore > 60'000) {
            Threat = "threat: high";
        }
        else if (RobotScore > 40'000) {
            Threat = "threat: moderate";
        }
        else if (RobotScore > 20'000) {
            Threat = "threat: light";
        }
        else if (RobotScore > 0) {
            Threat = "threat: minimal";
        }
        else {
            Threat = "threat: none";
        }

#ifdef _DEBUG
        Threat += " " + std::to_string(RobotScore);
#endif

        UpdateAutomapMesh();

        // Update navigation flags
        for (size_t i = 0; i < Game::Automap.Segments.size() && i < level.Segments.size(); i++) {
            if (Game::Automap.Segments[i] == AutomapVisibility::Hidden)
                continue;

            auto& seg = level.Segments[i];
            if (seg.Type == SegmentType::Energy)
                Game::Automap.FoundEnergy = true;

            if (seg.Type == SegmentType::Reactor)
                Game::Automap.FoundReactor = true;

            // Check for exit
            for (auto& side : seg.Sides) {
                if (auto wall = level.TryGetWall(side.Wall)) {
                    if (auto trigger = Seq::tryItem(level.Triggers, (int)wall->Trigger)) {
                        if (IsExit(level, *trigger))
                            Game::Automap.FoundExit = true;
                    }
                }
            }
        }
    }

    AutomapInfo::AutomapInfo(const Inferno::Level& level) {
        Segments.resize(level.Segments.size());
        ranges::fill(Segments, AutomapVisibility::Hidden);
    }

    constexpr float NAVIGATE_SPEED = 800.0f;

    void PanAutomapTo(const Vector3& target) {
        auto distance = Vector3::Distance(target, Game::AutomapCamera.Target);
        if (distance > 1.0f) {
            auto duration = distance / NAVIGATE_SPEED;
            Game::AutomapCamera.LerpTo(target, duration);
        }
    }

    void NavigateToEnergy() {
        if (!Game::Automap.FoundEnergy) return;

        static int index = -1;
        List<RoomID> roomIds;
        //List<Room*> energyRooms;

        for (size_t i = 0; i < Game::Automap.Segments.size() && i < Game::Level.Segments.size(); i++) {
            if (Game::Automap.Segments[i] == AutomapVisibility::Hidden)
                continue;

            auto& seg = Game::Level.Segments[i];
            if (seg.Type == SegmentType::Energy) {
                if (!Seq::contains(roomIds, seg.Room))
                    roomIds.push_back(seg.Room);
            }
        }

        //for (auto& room : Game::Level.Rooms) {
        //    if (room.Type != SegmentType::Energy) continue;
        //    energyRooms.push_back(&room);
        //}

        //if (energyRooms.empty())
        //    return;

        index++;
        if (index >= roomIds.size())
            index = 0;

        if (auto room = Seq::tryItem(Game::Level.Rooms, (int)roomIds[index]))
            PanAutomapTo(room->Center);
    }

    void NavigateToReactor() {
        if (!Game::Automap.FoundReactor) return;

        Object* reactor = nullptr;

        for (auto& obj : Game::Level.Objects) {
            if (obj.IsReactor()) {
                reactor = &obj;
                break;
            }
        }

        if (!reactor) return;
        PanAutomapTo(reactor->Position);
    }

    void NavigateToExit() {
        if (!Game::Automap.FoundExit) return;

        //if (auto side = Game::Level.TryGetSide(Game::Automap.Exit))
        //    PanAutomapTo(side->Center);

        auto exit = FindExit(Game::Level);

        if (auto side = Game::Level.TryGetSide(exit))
            PanAutomapTo(side->Center);
    }

    void ResetAutomapCamera(bool instant) {
        auto& player = Game::GetPlayerObject();

        // overload style camera positioned directly behind the player
        //auto vOffset = player.Rotation.Up() * 5.0f;
        //auto position = player.Position + player.Rotation.Backward() * 15.0f + vOffset;
        //auto target = player.Position + vOffset + player.Rotation.Forward() * 25.0f;
        //auto dir = target - position;

        constexpr float hDistance = 120;
        constexpr float vDistance = 100;
        auto vOffset = player.Rotation.Up() * vDistance;
        auto position = player.Position + player.Rotation.Backward() * hDistance + vOffset;
        auto target = player.Position;
        auto dir = target - position;

        dir.Normalize();
        auto right = dir.Cross(player.Rotation.Up());
        auto up = right.Cross(dir);

        if (instant) {
            Game::AutomapCamera.MoveTo(position, target, up);
        }
        else {
            PanAutomapTo(target);
        }
    }

    void OpenAutomap() {
        Game::Automap.Update(Game::Level);

        Input::SetMouseMode(Input::MouseMode::Mouselook);
        ResetAutomapCamera(true);
    }

    void CloseAutomap() {
        Input::SetMouseMode(Input::MouseMode::Mouselook);
    }

    void HandleAutomapInput() {
        using Input::Keys;
        if (!Input::HasFocus) return;

        GenericCameraController(Game::AutomapCamera, 300);

        if (Game::Bindings.Pressed(GameAction::Afterburner))
            ResetAutomapCamera(false);

        if (Input::OnKeyPressed(Keys::D1))
            NavigateToEnergy();

        if (Input::OnKeyPressed(Keys::D2))
            NavigateToReactor();

        if (Input::OnKeyPressed(Keys::D3))
            NavigateToExit();
    }
}
