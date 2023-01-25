#pragma once

#include "Types.h"

namespace Inferno::Editor {
    template <class...TArgs>
    class Event {
        using TFunc = std::function<void(TArgs...)>;
        std::vector<TFunc> _subscribers;

    public:
        void Subscribe(TFunc fn) { _subscribers.push_back(fn); }
        void operator+=(TFunc fn) { Subscribe(fn); }

        template<class...TForwardArgs>
        void operator()(TForwardArgs&&...args) const {
            for (auto fn : _subscribers)
                fn(std::forward<TForwardArgs>(args)...);
        }
    };

    enum class DialogType { 
        HogEditor, 
        MissionEditor,
        GotoSegment,
        NewLevel, 
        RenameLevel,
        Settings,
        Help,
        About,
        Briefings
    };

    namespace Events {
        inline Event SelectSegment, SelectObject, LevelLoaded;
        inline Event<LevelTexID, LevelTexID> SelectTexture;
        inline Event<LevelTexID> TextureInfo;
        inline Event LevelChanged; // Level mesh needs regenerating
        inline Event TexturesChanged; // Textures maybe need to be reloaded
        inline Event SegmentsChanged; // Number of segments changed
        inline Event ObjectsChanged; // Number of objects changed

        inline Event<DialogType> ShowDialog; // More of a command than an event
        inline Event SettingsChanged;
        inline Event SnapshotChanged; // Snapshot undo/redo
    }
}
