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
        About
    };

    namespace Events {
        inline Event SelectSegment, SelectObject, LevelLoaded;
        inline Event<LevelTexID, LevelTexID> SelectTexture;
        inline Event<LevelTexID> TextureInfo;
        inline Event LevelChanged, TexturesChanged;

        inline Event<DialogType> ShowDialog;
        inline Event SettingsChanged;
    }
}
