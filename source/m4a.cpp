#include "m4a.hpp"
#include "files.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <neaacdec.h>
#include <vector>
#include <3ds/types.h>

namespace
{

    u32 be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
    u64 be64(const u8* p) { return u64(be32(p)) << 32 | be32(p + 4); }

    // What the reader needs from the moov box of the first track with an mp4a sample entry.
    struct Track
    {
        std::vector<u8> config;               // AudioSpecificConfig from the esds
        std::vector<u32> sizes;               // stsz
        std::vector<u64> chunks;              // stco / co64
        std::vector<std::array<u32, 2>> runs; // stsc: first chunk (1-based), samples per chunk
        bool audio = false;
    };

    // The descriptor length in an esds: up to four bytes of 7 bits.
    size_t descLen(const u8*& p, const u8* end)
    {
        size_t n = 0;
        for (int i = 0; i < 4 && p < end; i++)
        {
            u8 b = *p++;
            n    = n << 7 | (b & 0x7F);
            if (!(b & 0x80))
            {
                break;
            }
        }
        return n;
    }

    void esds(const u8* p, const u8* end, Track& t)
    {
        p += 4; // version + flags
        while (p + 2 <= end)
        {
            u8 tag     = *p++;
            size_t len = descLen(p, end);
            if (tag == 0x03) // ES_Descriptor: id, flags, then its own descriptors
            {
                if (p + 3 > end)
                {
                    return;
                }
                u8 flags = p[2];
                p += 3;
                if (flags & 0x80)
                {
                    p += 2;
                }
                if (flags & 0x40 && p < end)
                {
                    p += 1 + *p;
                }
                if (flags & 0x20)
                {
                    p += 2;
                }
                continue;
            }
            if (tag == 0x04) // DecoderConfigDescriptor: 13 bytes, then the specific info
            {
                p += 13;
                continue;
            }
            if (tag == 0x05)
            {
                if (p + len <= end)
                {
                    t.config.assign(p, p + len);
                }
                return;
            }
            p += len;
        }
    }

    void boxes(const u8* p, const u8* end, Track& t)
    {
        while (p + 8 <= end)
        {
            u64 size = be32(p);
            const u8* body = p + 8;
            if (size == 1 && p + 16 <= end)
            {
                size = be64(p + 8);
                body = p + 16;
            }
            else if (size == 0)
            {
                size = u64(end - p);
            }
            if (size < 8 || size > u64(end - p))
            {
                return;
            }
            const u8* next = p + size;
            char type[5] = {char(p[4]), char(p[5]), char(p[6]), char(p[7]), 0};
            std::string ty = type;
            if (ty == "trak" && !t.config.empty())
            {
                return; // the first audio track is enough
            }
            if (ty == "moov" || ty == "trak" || ty == "mdia" || ty == "minf" || ty == "stbl")
            {
                Track inner;
                boxes(body, next, inner);
                if (ty == "trak" || ty == "moov")
                {
                    if (inner.audio && !inner.config.empty() && t.config.empty())
                    {
                        t = std::move(inner);
                    }
                }
                else
                {
                    // mdia/minf/stbl: hand their findings up
                    if (!inner.config.empty())
                    {
                        t.config = std::move(inner.config);
                    }
                    if (!inner.sizes.empty())
                    {
                        t.sizes = std::move(inner.sizes);
                    }
                    if (!inner.chunks.empty())
                    {
                        t.chunks = std::move(inner.chunks);
                    }
                    if (!inner.runs.empty())
                    {
                        t.runs = std::move(inner.runs);
                    }
                    t.audio = t.audio || inner.audio;
                }
            }
            else if (ty == "stsd" && body + 8 <= next)
            {
                // version/flags, count, then sample entries; an mp4a entry has 28 bytes before its boxes
                const u8* e = body + 8;
                if (e + 8 <= next && memcmp(e + 4, "mp4a", 4) == 0)
                {
                    t.audio = true;
                    const u8* eEnd = e + be32(e);
                    if (eEnd > next)
                    {
                        eEnd = next;
                    }
                    u32 version = e + 18 <= eEnd ? (u32(e[16]) << 8 | e[17]) : 0;
                    const u8* q = e + 36 + (version == 1 ? 16 : version == 2 ? 36 : 0);
                    while (q + 8 <= eEnd)
                    {
                        u32 s = be32(q);
                        if (s < 8 || q + s > eEnd)
                        {
                            break;
                        }
                        if (memcmp(q + 4, "esds", 4) == 0)
                        {
                            esds(q + 8, q + s, t);
                        }
                        q += s;
                    }
                }
            }
            else if (ty == "stsz" && body + 12 <= next)
            {
                u32 fixed = be32(body + 4), n = be32(body + 8);
                if (fixed)
                {
                    t.sizes.assign(n, fixed);
                }
                else
                {
                    for (u32 i = 0; i < n && body + 12 + 4 * (i + 1) <= next; i++)
                    {
                        t.sizes.push_back(be32(body + 12 + 4 * i));
                    }
                }
            }
            else if ((ty == "stco" || ty == "co64") && body + 8 <= next)
            {
                u32 n = be32(body + 4), w = ty == "co64" ? 8 : 4;
                for (u32 i = 0; i < n && body + 8 + w * (i + 1) <= next; i++)
                {
                    const u8* q = body + 8 + w * i;
                    t.chunks.push_back(w == 8 ? be64(q) : be32(q));
                }
            }
            else if (ty == "stsc" && body + 8 <= next)
            {
                u32 n = be32(body + 4);
                for (u32 i = 0; i < n && body + 8 + 12 * (i + 1) <= next; i++)
                {
                    const u8* q = body + 8 + 12 * i;
                    t.runs.push_back({be32(q), be32(q + 4)});
                }
            }
            p = next;
        }
    }

    void le16(FILE* f, unsigned v) { u8 b[2] = {u8(v), u8(v >> 8)}; fwrite(b, 1, 2, f); }
    void le32(FILE* f, u32 v) { u8 b[4] = {u8(v), u8(v >> 8), u8(v >> 16), u8(v >> 24)}; fwrite(b, 1, 4, f); }

    void wavHeader(FILE* f, u32 rate, unsigned channels, u32 bytes)
    {
        fseek(f, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, f);
        le32(f, 36 + bytes);
        fwrite("WAVEfmt ", 1, 8, f);
        le32(f, 16);
        le16(f, 1);
        le16(f, channels);
        le32(f, rate);
        le32(f, rate * channels * 2);
        le16(f, channels * 2);
        le16(f, 16);
        fwrite("data", 1, 4, f);
        le32(f, bytes);
    }
}

namespace m4a
{
    bool toWav(const std::string& from, const std::string& to, std::string& error, const std::function<bool(float)>& progress)
    {
        std::vector<u8> file;
        if (!files::read(from, file))
        {
            error = "Could not read " + from;
            return false;
        }
        Track t;
        boxes(file.data(), file.data() + file.size(), t);
        if (t.config.empty() || t.sizes.empty() || t.chunks.empty() || t.runs.empty())
        {
            error = "No AAC track in the file";
            return false;
        }
        // Each sample's place in the file, chunk by chunk.
        std::vector<u64> offsets;
        offsets.reserve(t.sizes.size());
        size_t sample = 0;
        for (size_t c = 0; c < t.chunks.size() && sample < t.sizes.size(); c++)
        {
            u32 per = 0;
            for (auto& r : t.runs)
            {
                if (r[0] <= c + 1)
                {
                    per = r[1];
                }
            }
            u64 at = t.chunks[c];
            for (u32 i = 0; i < per && sample < t.sizes.size(); i++, sample++)
            {
                offsets.push_back(at);
                at += t.sizes[sample];
            }
        }

        NeAACDecHandle dec = NeAACDecOpen();
        NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(dec);
        cfg->outputFormat            = FAAD_FMT_16BIT;
        cfg->downMatrix              = 1; // at most stereo
        NeAACDecSetConfiguration(dec, cfg);
        unsigned long rate = 0;
        unsigned char channels = 0;
        if (NeAACDecInit2(dec, t.config.data(), (unsigned long)t.config.size(), &rate, &channels) < 0)
        {
            NeAACDecClose(dec);
            error = "Unsupported AAC settings";
            return false;
        }
        FILE* f = fopen(to.c_str(), "wb");
        if (!f)
        {
            NeAACDecClose(dec);
            error = "Could not write " + to;
            return false;
        }
        std::vector<u8> buffer(256 * 1024);
        setvbuf(f, nullptr, _IOFBF, 64 * 1024);
        wavHeader(f, u32(rate), channels, 0);
        u32 bytes = 0;
        unsigned outChannels = channels;
        bool ok = true;
        for (size_t i = 0; i < offsets.size(); i++)
        {
            if (offsets[i] + t.sizes[i] > file.size())
            {
                break;
            }
            NeAACDecFrameInfo info;
            void* pcm = NeAACDecDecode(dec, &info, file.data() + offsets[i], t.sizes[i]);
            if (info.error)
            {
                continue; // one bad frame: skip it, as players do
            }
            if (pcm && info.samples)
            {
                outChannels = info.channels ? info.channels : outChannels;
                size_t n = info.samples * 2;
                fwrite(pcm, 1, n, f);
                bytes += u32(n);
            }
            if (progress && (i & 63) == 0 && !progress(float(i) / offsets.size()))
            {
                ok    = false;
                error = "Stopped";
                break;
            }
        }
        NeAACDecClose(dec);
        if (ok && bytes)
        {
            wavHeader(f, u32(rate), outChannels, bytes);
        }
        fclose(f);
        if (!ok || !bytes)
        {
            remove(to.c_str());
            if (error.empty())
            {
                error = "The song could not be decoded";
            }
            return false;
        }
        return true;
    }
}
