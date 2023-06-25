#include "pch.h"

#include "Types.h"
#include "Game.AI.h"
#include "Game.h"
#include "Resources.h"
#include "Physics.h"

namespace Inferno {
    void UpdateAI(Object& obj) {
        return;

        if (obj.NextThinkTime == NEVER_THINK || obj.NextThinkTime > Game::Time)
            return;

        // todo: check if object is in active set of segments

        if (obj.Type == ObjectType::Robot) {
            auto id = ObjID(&obj - Game::Level.Objects.data());

            // check fov

            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto& diff = robot.Difficulty[Game::Difficulty];

            auto& player = Game::Level.Objects[0];
            auto playerDir = player.Position - obj.Position;
            auto dist = playerDir.Length();
            playerDir.Normalize();

            LevelHit hit{};
            Ray ray = { obj.Position, playerDir };
            if (IntersectRayLevel(Game::Level, ray, obj.Segment, dist, true, false, hit))
                return; // player not in line of sight

            //auto angle = AngleBetweenVectors(playerDir, obj.Rotation.Forward());
            auto dot = playerDir.Dot(obj.Rotation.Forward());
            //dot = vm_vec_dot(vec_to_player, &objp->orient.fvec);
            //if ((dot > robptr->field_of_view[Difficulty_level])
            if (dot > diff.FieldOfView && !Game::Player.HasPowerup(PowerupFlag::Cloak)) {
                obj.Control.AI.ail.Awareness = 1;
                Sound3D sound(id);
                sound.AttachToSource = true;
                sound.Resource = Resources::GetSoundResource(robot.SeeSound);
                //Sound::Play(sound);
            }

            if (obj.Control.AI.ail.Awareness > 0.7) {
                TurnTowardsVector(obj, playerDir, diff.TurnTime);
                obj.NextThinkTime = Game::Time + Game::TICK_RATE;
            }
            else {
                obj.NextThinkTime = Game::Time + 1.0f;
            }
        }
        else if (obj.Type == ObjectType::Reactor) {
            // check facing, fire weapon from gunpoint
        }
    }
}
