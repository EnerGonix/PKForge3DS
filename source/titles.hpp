// Saves of 3DS games: the cartridge in the slot and installed titles (Pokémon X to Ultra Moon in
// /main, the Virtual Console Red to Crystal in /sav.dat), read and written through the FS
// service the way PKSM and Checkpoint do. A DS cartridge in the slot (Diamond to Black 2) has no
// archive: its save chip is read and written over SPI (spi.cpp, from Checkpoint and TWLSaveTool).
#pragma once

#include "saves.hpp"
#include <functional>
#include <string>
#include <vector>

namespace titles
{
    // A title ID no game uses: autotest scripts may write only this one.
    constexpr u64 TEST_TITLE = 0x000400000FF3FF00ULL;
    // A DS cartridge's save: this marker with the game code ("IPKE") as the low word, "ndscard:IPKE".
    constexpr u64 NDS_CARD = 0x0004800000000000ULL;
    inline bool isNdsCard(const SaveRef& ref) { return (ref.titleId >> 32) == (NDS_CARD >> 32); }

    // Probes the cartridge, then every installed application, for a save PKSM-Core knows.
    void scan(const std::function<void(const SaveRef&)>& found);
    // Probes one title; false when it has no save PKSM-Core knows.
    bool describe(u64 id, u8 media, SaveRef& out);
    // "title:0004000000055D00" or "card:..." to the title and media; false for files.
    bool parse(const std::string& path, u64& id, u8& media);
    bool read(const SaveRef& ref, std::vector<u8>& out, std::string& error);
    // Writes, commits, and clears the secure value so the game takes the save as its own.
    bool write(const SaveRef& ref, const u8* data, size_t size, std::string& error);
}
