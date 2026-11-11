#include "container.hpp"

#include <algorithm>
#include <cstring>
#include <zlib.h>

// The Android PKForge's RetroArchSaveContainer and SramPadding: a save written by RetroArch with "save file
// compression" (#RZIPv1#: 20-byte header, then zlib chunks each after its u32 length), and GBA saves padded
// past their chip size or carrying an emulator's 16-byte RTC footer. PKSM-Core sees the bare chip; the
// container is put back exactly when the save is written.
namespace container
{
    namespace
    {
        const u8 MAGIC[8] = {'#', 'R', 'Z', 'I', 'P', 'v', 1, '#'};
        constexpr size_t MAX_SIZE = 32 * 1024 * 1024;
        const size_t CHIPS[] = {0x20000, 0x10000, 0x8000, 0x2000};
        constexpr size_t RTC_FOOTER = 16;

        u32 rd32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
        u64 rd64(const u8* p) { return u64(rd32(p)) | u64(rd32(p + 4)) << 32; }
        void wr32(std::vector<u8>& v, u32 x)
        {
            for (int i = 0; i < 4; i++)
            {
                v.push_back(u8(x >> (8 * i)));
            }
        }

        bool compressed(const std::vector<u8>& b) { return b.size() >= 5 && memcmp(b.data(), "#RZIP", 5) == 0; }

        bool decode(const std::vector<u8>& in, std::vector<u8>& out)
        {
            if (in.size() < 20 || memcmp(in.data(), MAGIC, 8) != 0)
            {
                return false;
            }
            u32 chunk  = rd32(in.data() + 8);
            u64 length = rd64(in.data() + 12);
            if (!chunk || chunk > 64u * 1024 * 1024 || !length || length > MAX_SIZE)
            {
                return false;
            }
            out.assign(size_t(length), 0);
            size_t pos = 20;
            for (size_t offset = 0; offset < out.size();)
            {
                if (in.size() - pos < 4)
                {
                    return false;
                }
                u32 clen = rd32(in.data() + pos);
                pos += 4;
                if (clen < 6 || clen > in.size() - pos)
                {
                    return false;
                }
                uLongf count = uLongf(std::min<size_t>(chunk, out.size() - offset));
                uLongf got   = count;
                // uncompress checks the zlib stream's Adler-32 trailer itself.
                if (uncompress(out.data() + offset, &got, in.data() + pos, clen) != Z_OK || got != count)
                {
                    return false;
                }
                pos += clen;
                offset += count;
            }
            return pos == in.size();
        }

        std::vector<u8> encode(const u8* data, size_t n, u32 chunk)
        {
            std::vector<u8> out(MAGIC, MAGIC + 8);
            wr32(out, chunk);
            wr32(out, u32(n));
            wr32(out, u32(u64(n) >> 32));
            for (size_t offset = 0; offset < n;)
            {
                size_t count = std::min<size_t>(chunk, n - offset);
                uLongf len   = compressBound(uLong(count));
                std::vector<u8> z(len);
                compress2(z.data(), &len, data + offset, uLong(count), 6);
                wr32(out, u32(len));
                out.insert(out.end(), z.begin(), z.begin() + long(len));
                offset += count;
            }
            return out;
        }

        bool uniform(const u8* p, size_t n)
        {
            if (!n)
            {
                return false;
            }
            for (size_t i = 1; i < n; i++)
            {
                if (p[i] != p[0])
                {
                    return false;
                }
            }
            return p[0] == 0x00 || p[0] == 0xFF;
        }

        bool rtcFooter(size_t len) { return len - RTC_FOOTER == 0x20000 || len - RTC_FOOTER == 0x10000; }

        size_t padding(const std::vector<u8>& b)
        {
            for (size_t c : CHIPS)
            {
                if (b.size() == c)
                {
                    return 0; // the dump is the chip
                }
            }
            if (b.size() > RTC_FOOTER && rtcFooter(b.size()))
            {
                return RTC_FOOTER;
            }
            for (size_t c : CHIPS)
            {
                if (c < b.size() && uniform(b.data() + c, b.size() - c))
                {
                    return b.size() - c;
                }
            }
            return 0;
        }
    }

    bool unwrap(const std::vector<u8>& file, std::vector<u8>& save)
    {
        bool changed = false;
        save         = file;
        if (compressed(file))
        {
            if (!decode(file, save))
            {
                save = file;
                return false;
            }
            changed = true;
        }
        size_t pad = padding(save);
        if (pad)
        {
            save.resize(save.size() - pad);
            changed = true;
        }
        return changed;
    }

    std::vector<u8> rewrap(const u8* data, size_t n, const std::vector<u8>& original)
    {
        std::vector<u8> plain;
        if (compressed(original))
        {
            if (!decode(original, plain))
            {
                plain = original;
            }
        }
        else
        {
            plain = original;
        }
        std::vector<u8> padded(data, data + n);
        if (n < plain.size())
        {
            padded.insert(padded.end(), plain.begin() + long(n), plain.end()); // the padding or RTC footer, as it was
        }
        if (!compressed(original))
        {
            return padded;
        }
        if (padded == plain)
        {
            return original; // unchanged: the exact original compression
        }
        return encode(padded.data(), padded.size(), rd32(original.data() + 8));
    }
}
