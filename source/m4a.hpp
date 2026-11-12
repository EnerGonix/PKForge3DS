// AAC songs in .m4a files (the Android PKForge's Bad Apple!! song), decoded as they play: a small MP4 reader for
// the one audio track and faad2 (GPLv2+, external/faad2) for the AAC frames.
#pragma once

#include <3ds/types.h>
#include <memory>
#include <string>

namespace m4a
{
    class Stream
    {
    public:
        Stream();
        ~Stream();
        // Reads the whole file (a song is a couple of MB) and readies the decoder for its first AAC track.
        bool open(const std::string& path);
        // Up to `frames` interleaved 16-bit frames (channels samples each); 0 at the end.
        int read(s16* out, int frames);
        int rate = 0, channels = 0;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
