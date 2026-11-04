#include "input.hpp"
#include "gfx.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <sstream>
#include <vector>

namespace autotest
{
    namespace
    {
        struct Step
        {
            enum Kind
            {
                Wait,
                Keys,
                Touch,
                Drag,
                Shot,
                Text,
                Open,
                Quit
            } kind;
            u32 keys = 0;
            int frames = 1, x = 0, y = 0, x2 = 0, y2 = 0;
            std::string arg;
        };

        std::deque<Step> steps;
        bool running = false, quit = false;
        int frame    = 0; // frames spent on the current step
        std::deque<std::string> texts;
        std::deque<std::string> opens;

        u32 parseKeys(const std::string& s)
        {
            static const struct
            {
                const char* name;
                u32 key;
            } names[] = {{"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y}, {"L", KEY_L}, {"R", KEY_R},
                {"ZL", KEY_ZL}, {"ZR", KEY_ZR}, {"START", KEY_START}, {"SELECT", KEY_SELECT}, {"UP", KEY_DUP},
                {"DOWN", KEY_DDOWN}, {"LEFT", KEY_DLEFT}, {"RIGHT", KEY_DRIGHT}};
            u32 keys = 0;
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, '+'))
            {
                for (auto& n : names)
                {
                    if (part == n.name)
                    {
                        keys |= n.key;
                    }
                }
            }
            return keys;
        }
    }

    void load()
    {
        FILE* f = fopen("sdmc:/3ds/PKForge/autotest.txt", "r");
        if (!f)
        {
            return;
        }
        char line[512];
        while (fgets(line, sizeof(line), f))
        {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
            {
                l.pop_back();
            }
            if (l.empty() || l[0] == '#')
            {
                continue;
            }
            std::stringstream ss(l);
            std::string cmd;
            ss >> cmd;
            Step s{Step::Wait};
            if (cmd == "wait")
            {
                ss >> s.frames;
            }
            else if (cmd == "press" || cmd == "hold")
            {
                std::string keys;
                ss >> keys;
                s.kind = Step::Keys;
                s.keys = parseKeys(keys);
                s.frames = 2;
                if (cmd == "hold")
                {
                    ss >> s.frames;
                }
            }
            else if (cmd == "touch")
            {
                s.kind   = Step::Touch;
                s.frames = 4;
                ss >> s.x >> s.y;
            }
            else if (cmd == "drag")
            {
                s.kind = Step::Drag;
                ss >> s.x >> s.y >> s.x2 >> s.y2 >> s.frames;
            }
            else if (cmd == "shot")
            {
                s.kind = Step::Shot;
                ss >> s.arg;
                s.frames = 3;
            }
            else if (cmd == "text")
            {
                s.kind = Step::Text;
                std::getline(ss, s.arg);
                if (!s.arg.empty() && s.arg[0] == ' ')
                {
                    s.arg.erase(0, 1);
                }
            }
            else if (cmd == "open")
            {
                // The rest of the line: save file names have spaces.
                s.kind = Step::Open;
                std::getline(ss, s.arg);
                if (!s.arg.empty() && s.arg[0] == ' ')
                {
                    s.arg.erase(0, 1);
                }
                s.frames = 2;
            }
            else if (cmd == "quit")
            {
                s.kind = Step::Quit;
            }
            else
            {
                continue;
            }
            steps.push_back(s);
        }
        fclose(f);
        // Run once: the script never traps someone who left it on the card.
        remove("sdmc:/3ds/PKForge/autotest.done");
        rename("sdmc:/3ds/PKForge/autotest.txt", "sdmc:/3ds/PKForge/autotest.done");
        running = !steps.empty();
    }

    bool active() { return running; }

    bool wantsQuit() { return quit; }

    bool takeOpen(std::string& out)
    {
        if (opens.empty())
        {
            return false;
        }
        out = opens.front();
        opens.pop_front();
        return true;
    }

    bool takeText(std::string& out)
    {
        if (texts.empty())
        {
            return false;
        }
        out = texts.front();
        texts.pop_front();
        return true;
    }

    // Fills one frame of scripted input; false when the script has ended.
    bool step(u32& held, bool& touching, int& x, int& y)
    {
        held     = 0;
        touching = false;
        while (!steps.empty())
        {
            Step& s = steps.front();
            if (s.kind == Step::Text)
            {
                texts.push_back(s.arg);
                steps.pop_front();
                continue;
            }
            if (s.kind == Step::Quit)
            {
                quit = true;
                steps.pop_front();
                continue;
            }
            if (s.kind == Step::Open && frame == 0)
            {
                opens.push_back(s.arg);
            }
            if (s.kind == Step::Shot && frame == 0)
            {
                gfx::requestShot("sdmc:/3ds/PKForge/shots/" + s.arg + ".bmp");
            }
            if (frame < s.frames)
            {
                if (s.kind == Step::Keys && frame < s.frames)
                {
                    held = s.keys;
                }
                else if (s.kind == Step::Touch && frame < s.frames - 1)
                {
                    touching = true;
                    x = s.x;
                    y = s.y;
                }
                else if (s.kind == Step::Drag && frame < s.frames - 1)
                {
                    float t  = s.frames > 2 ? float(frame) / (s.frames - 2) : 1;
                    touching = true;
                    x = int(s.x + (s.x2 - s.x) * t);
                    y = int(s.y + (s.y2 - s.y) * t);
                }
                frame++;
                return true;
            }
            frame = 0;
            steps.pop_front();
            // One idle frame between steps so a repeated key reads as a new press.
            return true;
        }
        running = false;
        return false;
    }
}

void Input::scan()
{
    hidScanInput();
    u32 nowHeld = hidKeysHeld();
    touchPosition pos;
    hidTouchRead(&pos);
    bool nowTouching = (nowHeld & KEY_TOUCH) != 0;
    int x = pos.px, y = pos.py;

    if (autotest::active())
    {
        // The script owns the controls; the real ones are ignored until it ends.
        autotest::step(nowHeld, nowTouching, x, y);
    }

    down = nowHeld & ~held;
    up   = held & ~nowHeld;
    held = nowHeld;

    static int repeatTimer = 0;
    constexpr u32 repeating = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_L | KEY_R;
    repeat = down;
    if (held & repeating)
    {
        if (down & repeating)
        {
            repeatTimer = 18;
        }
        else if (--repeatTimer <= 0)
        {
            repeat |= held & repeating;
            repeatTimer = 5;
        }
    }

    touchDown = nowTouching && !touching;
    touchUp   = !nowTouching && touching;
    dx = dy = 0;
    if (nowTouching)
    {
        if (touchDown)
        {
            startX  = x;
            startY  = y;
            dragged = false;
        }
        else
        {
            dx = x - tx;
            dy = y - ty;
        }
        tx = x;
        ty = y;
        if (abs(tx - startX) > 6 || abs(ty - startY) > 6)
        {
            dragged = true;
        }
    }
    touching = nowTouching;
}

bool Input::pressed(int x, int y, int w, int h) const { return touchDown && inside(x, y, w, h); }

bool Input::tapped(int x, int y, int w, int h) const
{
    return touchUp && !dragged && startX >= x && startX < x + w && startY >= y && startY < y + h;
}
