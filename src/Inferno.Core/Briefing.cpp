#include "pch.h"
#include "Briefing.h"
#include "Utility.h"

namespace Inferno {
    constexpr uint CountVisibleCharacters(string_view str) {
        bool inToken = false;
        uint count = 0;

        for (int i = 0; i < str.size(); i++) {
            auto c = str[i];

            if (c == '\n') continue;
            if (c == '$') {
                inToken = true;
                continue;
            }

            if (inToken) {
                i++;
                inToken = false;
            }

            count++;
        }

        return count;
    }

    List<Briefing::Screen> ParseScreens(span<string> lines) {
        List<Briefing::Screen> screens;
        Briefing::Screen screen;
        Briefing::Page page;
        //List<Briefing::Page> pages;
        //Briefing::Page nullPage;
        //Briefing::Page* page = &nullPage;

        for (auto& line : lines) {
            if (line.starts_with('$')) {
                if (line.length() < 2) continue;

                switch (line[1]) {
                    case 'S': // screen / background change
                    {
                        if (screen.Number != -1) {
                            if (!page.Text.empty()) {
                                screen.Pages.push_back(page);
                                page = {};
                            }

                            for (auto& p : screen.Pages)
                                p.VisibleCharacters = CountVisibleCharacters(p.Text);

                            screens.push_back(screen);
                            screen = {};
                        }

                        String::TryParse(line.substr(2), screen.Number);
                        break;
                    }
                    case 'P':
                        if (!page.Text.empty()) {
                            screen.Pages.push_back(page);
                            page = {};
                        }

                        break;

                    case 'T':
                        String::TryParse(String::Trim(line.substr(2)), screen.TabStop);
                        break;

                    case 'F':
                        screen.Cursor = true;
                        break;

                    case 'N':
                    case 'B':
                        page.Image = line.substr(2);
                        break;

                    case 'R':
                        String::TryParse(line.substr(2), page.Robot);
                        break;

                    default:
                        page.Text += line;
                        break;
                }
            }
            else {
                page.Text += line + '\n';
            }
        }

        return screens;
    }

    Briefing Briefing::Read(span<ubyte> data) {
        // briefings can be either plain text or encoded text
        DecodeText(data);

        Briefing briefing;
        briefing.Raw = string(data.begin(), data.end());
        std::stringstream stream(briefing.Raw);

        List<string> lines;
        string line;
        while (std::getline(stream, line))
            lines.push_back(line);

        briefing.Screens = ParseScreens(lines);
        //briefing.Screens = ParseScreens(data);
        return briefing;
    }

    void SetD1BriefingBackgrounds(Briefing& briefing, bool shareware) {
        // D1 uses hard coded backgrounds based on the screen number
        List<Briefing::Screen> screens;
        screens.push_back({ "brief01.pcx", 0, 1, 13, 140, 290, 59 });
        screens.push_back({ "brief02.pcx", 0, 2, 27, 34, 257, 177 });
        screens.push_back({ "brief03.pcx", 0, 3, 20, 22, 257, 177 });
        screens.push_back({ "brief02.pcx", 0, 4, 27, 34, 257, 177 });
        screens.push_back({ "moon01.pcx", 1, 5, 10, 10, 300, 170 });
        screens.push_back({ "moon01.pcx", 2, 6, 10, 10, 300, 170 });
        screens.push_back({ "moon01.pcx", 3, 7, 10, 10, 300, 170 });
        screens.push_back({ "venus01.pcx", 4, 8, 15, 15, 300, 200 });
        screens.push_back({ "venus01.pcx", 5, 9, 15, 15, 300, 200 });

        // Demo is missing the class 1 driller screen
        if (!shareware) screens.push_back({ "brief03.pcx", 6, 10, 20, 22, 257, 177 });

        screens.push_back({ "merc01.pcx", 6, 11, 10, 15, 300, 200 });
        screens.push_back({ "merc01.pcx", 7, 12, 10, 15, 300, 200 });

        if (!shareware) {
            screens.push_back({ "brief03.pcx", 8, 13, 20, 22, 257, 177 });
            screens.push_back({ "mars01.pcx", 8, 14, 10, 100, 300, 200 });
            screens.push_back({ "mars01.pcx", 9, 15, 10, 100, 300, 200 });
            screens.push_back({ "brief03.pcx", 10, 16, 20, 22, 257, 177 });
            screens.push_back({ "mars01.pcx", 10, 17, 10, 100, 300, 200 });
            screens.push_back({ "jup01.pcx", 11, 18, 10, 40, 300, 200 });
            screens.push_back({ "jup01.pcx", 12, 19, 10, 40, 300, 200 });
            screens.push_back({ "brief03.pcx", 13, 20, 20, 22, 257, 177 });
            screens.push_back({ "jup01.pcx", 13, 21, 10, 40, 300, 200 });
            screens.push_back({ "jup01.pcx", 14, 22, 10, 40, 300, 200 });
            screens.push_back({ "saturn01.pcx", 15, 23, 10, 40, 300, 200 });
            screens.push_back({ "brief03.pcx", 16, 24, 20, 22, 257, 177 });
            screens.push_back({ "saturn01.pcx", 16, 25, 10, 40, 300, 200 });
            screens.push_back({ "brief03.pcx", 17, 26, 20, 22, 257, 177 });
            screens.push_back({ "saturn01.pcx", 17, 27, 10, 40, 300, 200 });
            screens.push_back({ "uranus01.pcx", 18, 28, 100, 100, 300, 200 });
            screens.push_back({ "uranus01.pcx", 19, 29, 100, 100, 300, 200 });
            screens.push_back({ "uranus01.pcx", 20, 30, 100, 100, 300, 200 });
            screens.push_back({ "uranus01.pcx", 21, 31, 100, 100, 300, 200 });
            screens.push_back({ "neptun01.pcx", 22, 32, 10, 20, 300, 200 });
            screens.push_back({ "neptun01.pcx", 23, 33, 10, 20, 300, 200 });
            screens.push_back({ "neptun01.pcx", 24, 34, 10, 20, 300, 200 });
            screens.push_back({ "pluto01.pcx", 25, 35, 10, 20, 300, 200 });
            screens.push_back({ "pluto01.pcx", 26, 36, 10, 20, 300, 200 });
            screens.push_back({ "pluto01.pcx", 27, 37, 10, 20, 300, 200 });
            screens.push_back({ "aster01.pcx", -1, 38, 10, 90, 300, 200 });
            screens.push_back({ "aster01.pcx", -2, 39, 10, 90, 300, 200 });
            screens.push_back({ "aster01.pcx", -3, 40, 10, 90, 300, 200 });
        }

        for (int i = 0; i < screens.size() && i < briefing.Screens.size(); i++) {
            briefing.Screens[i].Background = screens[i].Background;
            //briefing.Screens[i].Number = screens[i].Number;
            briefing.Screens[i].Level = screens[i].Level;
            briefing.Screens[i].x = screens[i].x;
            briefing.Screens[i].y = screens[i].y;
        }
    }

    void SetD1EndBriefingBackground(Briefing& briefing, bool shareware) {
        List<Briefing::Screen> screens;
        screens.push_back({ "end01.pcx", 1, 23, 40, 320, 200 });

        if (!shareware) {
            screens.push_back({ "end02.pcx", 1, 5, 5, 300, 200 });
            screens.push_back({ "end01.pcx", 2, 23, 40, 320, 200 });
            screens.push_back({ "end03.pcx", 3, 5, 5, 300, 200 });
        }

        for (int i = 0; i < screens.size() && i < briefing.Screens.size(); i++) {
            briefing.Screens[i].Background = screens[i].Background;
            //briefing.Screens[i].Number = screens[i].Number;
            briefing.Screens[i].Level = screens[i].Level;
            briefing.Screens[i].x = screens[i].x;
            briefing.Screens[i].y = screens[i].y;
        }
    }
}
