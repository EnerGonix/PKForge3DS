#pragma once

#include <3ds/types.h>
#include <string>
#include <vector>

// The Android PKForge's sprite pack: HOME renders and animated Showdown sprites, fetched one by one when shown
// (or all at once).
namespace spritepack
{
    constexpr int SIZE = 96; // pictures are kept SIZE x SIZE RGBA

    enum class Mode
    {
        BuiltIn,
        Home,
        Animated
    };
    Mode mode();
    void mode(Mode m);
    bool enabled(); // HOME renders on
    void enabled(bool v);

    struct Animation
    {
        int frames = 0;
        std::vector<u16> delays; // milliseconds per frame
        std::vector<u8> pixels;  // frames x SIZE x SIZE RGBA
    };

    // The render of a species (RGBA, SIZE x SIZE) once on the SD card; nullptr while it is fetched or not wanted.
    const u8* home(int species, bool shiny);
    // Its Showdown animation, the same way.
    const Animation* animated(int species, bool shiny);
    // Everything of the chosen kind, in the background; status() tells how far it got.
    void downloadAll();
    std::string status();
    void stop();
}
