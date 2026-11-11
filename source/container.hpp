#pragma once

#include <3ds/types.h>
#include <vector>

// Save files with a wrapper around the game's data: RetroArch's compressed saves (#RZIPv1#) and GBA saves
// padded past their chip or with an RTC footer.
namespace container
{
    // The game's save inside the file; true when it differed from the file (a wrapper was found).
    bool unwrap(const std::vector<u8>& file, std::vector<u8>& save);
    // The file to write for an edited save, with the original file's wrapper put back.
    std::vector<u8> rewrap(const u8* data, size_t n, const std::vector<u8>& original);
}
