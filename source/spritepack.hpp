#pragma once

#include <3ds/types.h>
#include <string>

// HOME renders from the Android PKForge's sprite pack, fetched one by one when shown (or all at once).
namespace spritepack
{
    constexpr int SIZE = 96; // renders are kept SIZE x SIZE RGBA

    bool enabled();
    void enabled(bool v);
    // The render of a species (RGBA, SIZE x SIZE) once on the SD card; nullptr while it is fetched.
    const u8* home(int species, bool shiny);
    // Every render the pack has, in the background; status() tells how far it got.
    void downloadAll();
    std::string status();
    void stop();
}
