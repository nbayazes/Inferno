#include "pch.h"
#include "Briefing.h"

namespace Inferno {
    constexpr auto BITMAP_TBL_XOR = 0xD3;

    // rotates a byte left one bit, preserving the bit falling off the right
    void EncodeRotateLeft(ubyte& c) {
        int found = 0;
        if (c & 0x80)
            found = 1;

        c = c << 1;

        if (found)
            c |= 0x01;
    }

    // Discards the remainder of the line
    constexpr void NextLine(span<ubyte>::iterator& p) {
        while (*p++ != '\n');
    }

    string GetMessageName(span<ubyte>::iterator& p) {
        while (*p++ == ' ');
        string result;
        while (*p != ' ' && *p != 10) {
            if (*p != 13)
                result += *p;
            p++;
        }

        if (*p != 10)
            while (*p++ != 10);

        return result;

        //while ((**message != ' ') && (**message != 10)) {
        //    if (**message != 13)
        //        *result++ = **message;
        //    (*message)++;
        //}

        //if (**message != 10)
        //    while (*(*message)++ != 10)		//	Get and drop eoln
        //        ;

        //*result = 0;
    }

    constexpr int GetMessageNum(span<ubyte>::iterator& p) {
        int num = 0;

        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') {
            num = 10 * num + *p - '0';
            p++;
        }

        NextLine(p);
        return num;

        //for (auto& c : data) {
        //    if(c == ' ') continue;
        //    if (c >= '0' && c <= '9') {

        //    }
        //}

        //int	num = 0;

        //while (**message == ' ')
        //    (*message)++;

        //while ((**message >= '0') && (**message <= '9')) {
        //    num = 10 * num + **message - '0';
        //    (*message)++;
        //}

        //while (*(*message)++ != 10)		//	Get and drop eoln
        //    ;

        //return num;
    }

    List<Briefing::Screen> ParseScreens(span<ubyte> data) {
        List<Briefing::Screen> screens;
        Briefing::Screen nullScreen{}; // discarded, but exists to prevent null checks on poorly formed briefings
        Briefing::Screen* screen = &nullScreen;
        string* page = &nullScreen.Pages.emplace_back();

        auto p = data.begin();
        while (p != data.end()) {
            auto c = *p++;
            if (c == '$') {
                c = *p++;

                switch (c) {
                    case 'S': // screen / background change
                    {
                        screen = &screens.emplace_back();
                        screen->Number = GetMessageNum(p);
                        page = &screen->Pages.emplace_back();
                        break;
                    }
                    case 'P':
                    {
                        page = &screen->Pages.emplace_back();
                        NextLine(p);
                        break;
                    }
                    default:
                        *page += '$';
                        *page += c; // keep other tokens
                        break;
                }
            }
            else {
                *page += c;
            }
        }

        return screens;
    }

    Briefing::Screen ReadScreen(span<ubyte> data) {
        List<Briefing::Screen> screens;

        bool done = false;
        bool flashingCursor = false;
        int currentColor = 0;
        int prevChar = -1;
        int tabStop = 0;
        int robotNum = -1;
        int animatingBitmapType = 0;

        Briefing::Screen nullScreen{}; // discarded, but exists to prevent null checks on poorly formed briefings
        Briefing::Screen* screen = &nullScreen;
        string* page = &nullScreen.Pages.emplace_back();

        auto p = data.begin();
        while (p != data.end()) {
            auto c = *p++;
            if (c == '$') {
                c = *p++;

                switch (c) {
                    case 'C':
                    {
                        currentColor = GetMessageNum(p) - 1;
                        assert(currentColor >= 0 && currentColor < 2);
                        prevChar = '\n';
                        fmt::print("color {}\n", currentColor);
                        *page += "$C";
                        break;
                    }
                    case 'F': // toggle flashing cursor
                    {
                        flashingCursor = !flashingCursor;
                        prevChar = '\n';
                        while (*p++ != '\n'); // eat whitespace
                        *page += "$F";
                        break;
                    }
                    case 'T':
                    {
                        tabStop = GetMessageNum(p);
                        prevChar = '\n';
                        break;
                    }
                    case 'R':
                    {
                        robotNum = GetMessageNum(p);
                        prevChar = '\n';
                        fmt::print("spinning robot {}\n", robotNum);
                        *page += "$R";
                        break;
                    }
                    case 'N':
                    {
                        auto bitmap = GetMessageName(p) + "#0";
                        prevChar = '\n';
                        animatingBitmapType = 0;
                        fmt::print("animation t0 {}\n", bitmap);
                        *page += "$N";
                        break;
                    }
                    case 'O':
                    {
                        auto bitmap = GetMessageName(p) + "#0";
                        prevChar = '\n';
                        animatingBitmapType = 1;
                        fmt::print("animation t1 {}\n", bitmap);
                        *page += "$O";
                        break;
                    }
                    case 'B':
                    {
                        auto bitmap = GetMessageName(p) + ".bbm";
                        // showBitmap();
                        prevChar = '\n';
                        fmt::print("bitmap {}\n", bitmap);
                        *page += "$B";
                        break;
                    }
                    case 'S': // screen / background change
                    {
                        auto screenNum = GetMessageNum(p);
                        fmt::print("screen{}\n", screenNum);
                        screen = &screens.emplace_back();
                        page = &screen->Pages.emplace_back();
                        //done = true;

                        break;
                    }

                    case 'P':
                    {
                        fmt::print("new page\n");
                        page = &screen->Pages.emplace_back();
                        NextLine(p);
                        prevChar = '\n';
                        break;
                    }
                }
            }
            else if (c == '\t') {
                // tab over
                *page += c;
            }
            else if (c == ';' && prevChar == '\n') {
                NextLine(p);
                prevChar = '\n';
            }
            else if (c == '\\') {
                prevChar = c;
            }
            else if (c == '\n') {
                if (prevChar == '\\') {
                    prevChar = c;
                    // move cursor down by 8

                    // if text y > screen y, load screen
                }
                else {
                    if (c == '\r') throw Exception("\\r\\n encoding is invalid. Must be \\n.");
                    prevChar = c;
                }
                *page += c;
            }
            else {
                prevChar = c;
                *page += c;
                // print stuff
            }
        }

        return screens[0];
    }

    Briefing Briefing::Read(span<ubyte> data) {
        // briefings can be either plain text or binary
        // binary requires bit shifts
        for (auto& c : data) {
            if (c != '\n') {
                EncodeRotateLeft(c);
                c = c ^ BITMAP_TBL_XOR;
                EncodeRotateLeft(c);
            }
        }

        Briefing briefing;
        briefing.Raw.resize(data.size() + 1);
        std::copy(data.begin(), data.end(), briefing.Raw.data());
        briefing.Screens = ParseScreens(data);
        return briefing;
    }
}
