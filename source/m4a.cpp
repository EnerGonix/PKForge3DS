#include "m4a.hpp"
#include "files.hpp"

#include <array>
#include <algorithm>
#include <cstdio>
#include <memory>
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
}

namespace m4a
{
    struct Stream::Impl
    {
        std::vector<u8> file;
        std::vector<u64> offsets;
        std::vector<u32> sizes;
        NeAACDecHandle dec = nullptr;
        size_t next        = 0;
        std::vector<s16> pending; // decoded samples not handed out yet
        size_t used        = 0;

        ~Impl()
        {
            if (dec)
            {
                NeAACDecClose(dec);
            }
        }
    };

    Stream::Stream() = default;
    Stream::~Stream() = default;

    bool Stream::open(const std::string& path)
    {
        auto m = std::make_unique<Impl>();
        if (!files::read(path, m->file))
        {
            return false;
        }
        Track t;
        boxes(m->file.data(), m->file.data() + m->file.size(), t);
        if (t.config.empty() || t.sizes.empty() || t.chunks.empty() || t.runs.empty())
        {
            return false;
        }
        // Each sample's place in the file, chunk by chunk.
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
                if (at + t.sizes[sample] > m->file.size())
                {
                    break;
                }
                m->offsets.push_back(at);
                m->sizes.push_back(t.sizes[sample]);
                at += t.sizes[sample];
            }
        }
        m->dec = NeAACDecOpen();
        NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(m->dec);
        cfg->outputFormat            = FAAD_FMT_16BIT;
        cfg->downMatrix              = 1; // at most stereo
        NeAACDecSetConfiguration(m->dec, cfg);
        unsigned long r   = 0;
        unsigned char ch  = 0;
        if (NeAACDecInit2(m->dec, t.config.data(), (unsigned long)t.config.size(), &r, &ch) < 0 || !r || !ch)
        {
            return false;
        }
        rate     = int(r);
        channels = ch > 2 ? 2 : int(ch);
        impl     = std::move(m);
        return true;
    }

    int Stream::read(s16* out, int frames)
    {
        if (!impl)
        {
            return 0;
        }
        Impl& m  = *impl;
        int done = 0;
        while (done < frames)
        {
            if (m.used >= m.pending.size())
            {
                if (m.next >= m.offsets.size())
                {
                    break;
                }
                NeAACDecFrameInfo info;
                void* pcm = NeAACDecDecode(m.dec, &info, m.file.data() + m.offsets[m.next], m.sizes[m.next]);
                m.next++;
                m.pending.clear();
                m.used = 0;
                if (info.error || !pcm || !info.samples)
                {
                    continue; // one bad frame: skip it, as players do
                }
                const s16* p = static_cast<const s16*>(pcm);
                int got      = info.channels ? info.channels : channels;
                size_t n     = info.samples / size_t(got);
                for (size_t i = 0; i < n; i++)
                {
                    s16 l = p[i * got], rr = got > 1 ? p[i * got + 1] : l;
                    m.pending.push_back(l);
                    if (channels == 2)
                    {
                        m.pending.push_back(rr);
                    }
                }
                continue;
            }
            size_t take = std::min(size_t(frames - done) * channels, m.pending.size() - m.used);
            memcpy(out + size_t(done) * channels, m.pending.data() + m.used, take * sizeof(s16));
            m.used += take;
            done += int(take / channels);
        }
        return done;
    }
}
