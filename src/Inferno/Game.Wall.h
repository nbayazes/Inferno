#pragma once

#include "Level.h"
#include "Resources.h"
#include "Types.h"
#include "Editor/Events.h"

namespace Inferno {
    // Tries to open a door
    void OpenDoor(Level& level, Tag tag);
    void DestroyWall(Level& level, Tag tag);

    // Updates opened doors
    void UpdateDoors(Level& level, float dt);

    void ActivateTrigger(Level& level, Trigger& trigger, Tag src);

    // Returns true if the wall has transparent or supertransparent textures, or is an open side.
    bool WallIsTransparent(const Level& level, Tag tag);
    void UpdateExplodingWalls(Level& level, float dt);
    void HitWall(Level& level, const Vector3& point, const Object& src, const Wall& wall);

    //// Tracks one-shot animations on a wall
    //class DestroyedClipSystem {
    //    struct Animation {
    //        Tag tag;
    //        EClipID clip;
    //        float time = 0;
    //        static bool IsAlive(const Animation& t) { return t.time >= 0; };
    //    };

    //    DataPool<Animation> _animations = { Animation::IsAlive, 5 };

    //public:
    //    void Add(Tag tag, EClipID id) {
    //        if (id <= EClipID::None) return;
    //        //auto& clip = Resources::GetEffectClip(id);
    //        _animations.Add({ tag, id });
    //    }

    //    void Update(Level& level, float dt) {
    //        for (auto& anim : _animations) {
    //            if (!Animation::IsAlive(anim)) continue;
    //            anim.time += dt;
    //            auto& clip = Resources::GetEffectClip(anim.clip);
    //            auto frame = Resources::LookupLevelTexID(clip.VClip.GetFrame(anim.time));
    //            auto& side = level.GetSide(anim.tag);
    //            if (side.TMap2 != frame) {
    //                fmt::print("Switching wall texture to TexID: {}\n", frame);
    //                Editor::Events::LevelChanged();
    //            }
    //            side.TMap2 = frame;

    //            //Render::LoadTextureDynamic(side.TMap2);

    //            if (anim.time > clip.VClip.PlayTime)
    //                anim.time = -1; // remove
    //        }
    //    }
    //};

    // Tracks one-shot animations on a wall
    class DestroyedClipSystem {
        struct Animation {
            Tag tag;
            LevelTexID id;
            float time = 0;
            static bool IsAlive(const Animation& t) { return t.time > 0; };
        };

        DataPool<Animation> _animations = { Animation::IsAlive, 5 };

    public:
        void Add(Tag tag, LevelTexID id, float time) {
            if (id <= LevelTexID::None) return;
            _animations.Add({ tag, id, time });
        }

        void Update(Level& level, float dt) {
            for (auto& anim : _animations) {
                if (!Animation::IsAlive(anim)) continue;
                anim.time -= dt;
                if (anim.time <= 0) {
                    level.GetSide(anim.tag).TMap2 = anim.id;
                    Editor::Events::LevelChanged();
                }
            }
        }

        // Returns -1 if no clip playing on this side
        float GetElapsed(Tag tag) {
            for (auto& anim : _animations) {
                if (anim.tag == tag) return anim.time;
            }

            return -1;
        }
    };

    inline DestroyedClipSystem DestroyedClips;

    // Tracks objects stuck to a wall
    class StuckObjectTracker {
        struct StuckObject {
            ObjID Object = ObjID::None;
            Tag Tag;
        };

        DataPool<StuckObject> _objects{ [](auto& o) { return o.Object != ObjID::None; }, 10 };

    public:
        void Add(Tag tag, ObjID id) {
            if (id == ObjID::None) return;
            _objects.Add({ id, tag });
        }

        void Remove(Level& level, Tag tag) {
            for (auto& o : _objects) {
                if (o.Tag != tag) continue;

                if (auto obj = level.TryGetObject(o.Object))
                    obj->Lifespan = -1;

                o = {};
            }
        }
    };

    inline StuckObjectTracker StuckObjects;
}
