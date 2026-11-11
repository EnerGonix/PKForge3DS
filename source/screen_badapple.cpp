#include "app.hpp"
#include "audio.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "widgets.hpp"
#include <cstring>

#include <3ds.h>
#include <string>
#include <vector>
#include <zlib.h>

using namespace gfx;

// The About screen's easter egg, as the Android PKForge's (BadAppleEgg, BadApplePainter): Bad Apple!! as text art
// made of Pokémon names, one character per 2x2 block of the video, the stream of names scrolling as it plays,
// the picture following the song's clock. The video's frames (BAPL: 12-byte header, then gzip-packed XOR deltas)
// and the song are not shipped with this port (they are not free to share): PKForge plays them from
// sdmc:/3ds/PKForge/badapple/frames.bin and badapple.mp3/.ogg/.wav when the player has them.
namespace
{
    constexpr const char* DIR = "sdmc:/3ds/PKForge/badapple";

    class BadApple : public Screen
    {
    public:
        BadApple()
        {
            std::vector<u8> file;
            if (!files::read(std::string(DIR) + "/frames.bin", file) || file.size() < 12 || memcmp(file.data(), "BAPL", 4) != 0)
            {
                return;
            }
            w = file[4] | file[5] << 8;
            h = file[6] | file[7] << 8;
            fps   = file[8] | file[9] << 8;
            count = file[10] | file[11] << 8;
            size_t frame = size_t(w) * h / 8;
            deltas.resize(frame * count);
            z_stream z{};
            inflateInit2(&z, 16 + MAX_WBITS); // gzip
            z.next_in   = file.data() + 12;
            z.avail_in  = uInt(file.size() - 12);
            z.next_out  = deltas.data();
            z.avail_out = uInt(deltas.size());
            inflate(&z, Z_FINISH);
            count = int(z.total_out / frame);
            inflateEnd(&z);
            bits.assign(frame, 0);
            for (int s = 1; s < 1026; s++)
            {
                std::string n = pkdata::species(s);
                for (char c : n)
                {
                    if (c >= 'A' && c <= 'z')
                    {
                        words += char(toupper(c));
                    }
                }
            }
            if (words.empty())
            {
                words = "PKFORGE";
            }
            for (const char* ext : {".mp3", ".ogg", ".wav"})
            {
                std::string song = std::string(DIR) + "/badapple" + ext;
                if (files::exists(song))
                {
                    audio::override(song);
                    break;
                }
            }
            start = osGetTime();
        }

        ~BadApple() override { audio::override(""); }

        bool cursorSounds() const override { return false; }

        void update(Input& in) override
        {
            if ((in.down & (KEY_B | KEY_A | KEY_START)) || in.tapped(0, 0, BOTTOM_W, H))
            {
                app::pop();
                return;
            }
            if (!count)
            {
                return;
            }
            int want = int((osGetTime() - start) * u64(fps) / 1000);
            if (want >= count)
            {
                app::pop();
                return;
            }
            size_t frame = bits.size();
            while (index < want)
            {
                index++;
                const u8* d = deltas.data() + size_t(index) * frame;
                for (size_t i = 0; i < frame; i++)
                {
                    bits[i] ^= d[i];
                }
            }
        }

        bool bit(int x, int y) const
        {
            int i = y * w + x;
            return bits[size_t(i >> 3)] & (0x80 >> (i & 7));
        }

        void drawTop() override
        {
            rect(0, 0, TOP_W, H, rgb(0, 0, 0));
            if (!count)
            {
                wrap(30, 80, 340,
                    "Bad Apple!! needs its video and song on the SD card: frames.bin (from PKForge for Android's "
                    "resources) and badapple.mp3 in 3ds/PKForge/badapple. They are not shipped with this port.",
                    T::Ink, 1, 8);
                return;
            }
            int cols = w / 2, rows = h / 2;
            float cell = std::min(float(TOP_W) / cols, float(H) / rows);
            float left = (TOP_W - cell * cols) / 2, top = (H - cell * rows) / 2;
            // A character of the names in each light cell: full blocks bright, edges softer.
            char one[2] = {0, 0};
            for (int r = 0; r < rows; r++)
            {
                for (int c = 0; c < cols; c++)
                {
                    int x = c * 2, y = r * 2;
                    int cover = bit(x, y) + bit(x + 1, y) + bit(x, y + 1) + bit(x + 1, y + 1);
                    if (cover < 2)
                    {
                        continue;
                    }
                    one[0] = words[size_t((r * cols + c + index) % int(words.size()))];
                    text(left + c * cell + cell / 2, top + r * cell, one, cover == 4 ? rgb(235, 240, 255) : rgb(150, 160, 190),
                        Center, cell / 8.0f);
                }
            }
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::header(BOTTOM_W, "Bad Apple!!", count ? std::to_string(index * 100 / std::max(1, count)) + "%" : "");
            text(BOTTOM_W / 2, 110, "B or a tap stops it.", T::InkSoft, Center);
        }

    private:
        int w = 0, h = 0, fps = 30, count = 0, index = -1;
        std::vector<u8> deltas, bits;
        std::string words;
        u64 start = 0;
    };
}

namespace screens
{
    std::unique_ptr<Screen> badApple() { return std::make_unique<BadApple>(); }
}
