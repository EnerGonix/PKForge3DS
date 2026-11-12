// AAC songs in .m4a files (the Android PKForge's Bad Apple!! song), turned once into a .wav the music
// player can stream: a small MP4 reader for the one audio track and faad2 (GPLv2+) for the AAC frames.
#pragma once

#include <functional>
#include <string>

namespace m4a
{
    // Decodes the first AAC track of `from` into a 16-bit PCM .wav at `to`. progress gets 0..1 now and then;
    // returning false from it stops the work. On any failure `to` is removed and error says why.
    bool toWav(const std::string& from, const std::string& to, std::string& error,
        const std::function<bool(float)>& progress = {});
}
