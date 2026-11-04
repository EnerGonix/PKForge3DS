// Buttons and touch for one frame, with D-pad auto-repeat and tap/drag detection.
// When sdmc:/3ds/PKForge/autotest.txt exists, a script drives the input instead (testing aid).
#pragma once

#include <3ds.h>
#include <string>

struct Input
{
    u32 down = 0, held = 0, up = 0;
    u32 repeat = 0; // down, plus auto-repeat while a direction or L/R is held
    bool touching = false, touchDown = false, touchUp = false;
    int tx = 0, ty = 0;
    int startX = 0, startY = 0;
    int dx = 0, dy = 0;   // touch movement since the last frame
    bool dragged = false; // moved past the tap threshold since the touch began

    void scan();
    // A touch that began inside the rectangle this frame.
    bool pressed(int x, int y, int w, int h) const;
    // A touch released inside the rectangle without having dragged.
    bool tapped(int x, int y, int w, int h) const;
    bool inside(int x, int y, int w, int h) const { return tx >= x && tx < x + w && ty >= y && ty < y + h; }
};

namespace autotest
{
    void load();
    bool active();
    bool wantsQuit();
    // The next scripted keyboard answer, if the script queued one.
    bool takeText(std::string& out);
    // A save the script asked to open by path ("open sdmc:/..."), once.
    bool takeOpen(std::string& out);
    // Scripts may only write saves in this folder.
    constexpr const char* TEST_SAVES = "sdmc:/3ds/PKForge/saves/";
}
