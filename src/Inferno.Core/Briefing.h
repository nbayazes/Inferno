#pragma once

#include "Types.h"

namespace Inferno {
    struct Briefing {
        struct Page {
            string Text;
            int Robot = -1; // Robot id to display
            string Image; // static image (BBM)
            uint VisibleCharacters = 0; // number of visible characters (non-control characters)
            ModelID Model = ModelID::None; // Model to display
            DClipID Door = DClipID::None; // animated door
        };

        struct Screen {
            string Background;
            int Level = 0;
            int Number = -1;
            int x = 0, y = 0; // Top left of text window
            int width = 320, height = 200; // size of text window
            List<Page> Pages;
            int TabStop = 0; // X-offset for tab characters
            bool Cursor = false; // Show a flashing cursor
        };

        List<Screen> Screens;
        string Raw;

        // Reads encoded TXB byte data
        static Briefing Read(span<ubyte> data, bool d1);

        // Reads plain briefing text
        static Briefing Read(const string& text, bool d1);
    };

    void SetD1BriefingBackgrounds(Briefing& briefing, bool shareware);
    void SetD1EndBriefingBackground(Briefing& briefing, bool shareware);
}