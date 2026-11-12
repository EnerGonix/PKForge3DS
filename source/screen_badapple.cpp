#include "app.hpp"
#include "audio.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "updater.hpp"
#include "widgets.hpp"

#include <3ds.h>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

using namespace gfx;

// The About screen's easter egg, as the Android PKForge's (BadAppleEgg, BadApplePainter): Bad Apple!! as text art
// made of Pokémon names, one character per 2x2 block of the video, the stream of names scrolling as it plays,
// the picture following the song's clock. The video's frames (BAPL: 12-byte header, then gzip-packed XOR deltas)
// and the song are the original's own (frames.bin and audio.m4a), shipped in the RomFS as the Android app ships
// them, so it starts at once; the music player decodes the AAC song as it plays. A build without them fetches
// them from github.com/sofianeelhor/PKForge into sdmc:/3ds/PKForge/badapple once.
namespace
{
    constexpr const char* DIR  = "sdmc:/3ds/PKForge/badapple";
    constexpr const char* FROM = "https://raw.githubusercontent.com/sofianeelhor/PKForge/main/src/PKForge.App/Resources/BadApple/";

    constexpr const char* ROM  = "romfs:/badapple";

    std::string framesPath()
    {
        std::string rom = std::string(ROM) + "/frames.bin";
        return files::exists(rom) ? rom : std::string(DIR) + "/frames.bin";
    }

    std::string songPath()
    {
        for (std::string song : {std::string(ROM) + "/audio.m4a", std::string(DIR) + "/audio.m4a"})
        {
            if (files::exists(song))
            {
                return song;
            }
        }
        return "";
    }

    class BadApple : public Screen
    {
    public:
        BadApple()
        {
            if (files::exists(framesPath()) && !songPath().empty())
            {
                begin();
            }
        }

        ~BadApple() override
        {
            cancel = true;
            if (worker)
            {
                threadJoin(worker, U64_MAX);
                threadFree(worker);
            }
            audio::override("");
        }

        bool cursorSounds() const override { return false; }

        void update(Input& in) override
        {
            if (playing)
            {
                if ((in.down & (KEY_B | KEY_A | KEY_START)) || in.tapped(0, 0, BOTTOM_W, H))
                {
                    app::pop();
                    return;
                }
                advance();
                return;
            }
            if (worker)
            {
                if (!done)
                {
                    return; // the screen stays until the step at hand ends
                }
                threadJoin(worker, U64_MAX);
                threadFree(worker);
                worker = nullptr;
                if (ok)
                {
                    begin();
                    return;
                }
            }
            if (in.down & KEY_B)
            {
                app::pop();
                return;
            }
            if ((in.down & KEY_A) || in.tapped(60, 150, BOTTOM_W - 120, 30))
            {
                fetch();
            }
        }

        void drawTop() override
        {
            rect(0, 0, TOP_W, H, rgb(0, 0, 0));
            if (!playing)
            {
                std::string msg;
                if (worker)
                {
                    msg = step;
                    float p = part;
                    if (p >= 0)
                    {
                        msg += " " + std::to_string(int(p * 100)) + "%";
                    }
                }
                else if (!error.empty())
                {
                    msg = "That did not work: " + error + ". A tries again.";
                }
                else
                {
                    msg = "Bad Apple!! plays from the original PKForge's own video and song, which are not shipped with "
                          "this port. A gets them from github.com/sofianeelhor/PKForge (about 3 MB) into "
                          "3ds/PKForge/badapple, once.";
                }
                wrap(30, 80, 340, msg, rgb(235, 240, 255), 1, 8);
                return;
            }
            // The whole top screen (400x240, wider than the video's 4:3): cells a little wider than tall.
            int cols = w / 2, rows = h / 2;
            float cw = float(TOP_W) / cols, ch = float(H) / rows;
            // A character of the names in each light cell: full blocks bright, edges softer.
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
                    char one = words[size_t((r * cols + c + index) % int(words.size()))];
                    glyph(c * cw + cw / 2, r * ch, one, cover == 4 ? rgb(235, 240, 255) : rgb(150, 160, 190), cw / 8.0f, ch / 8.0f);
                }
            }
        }

        void drawBottom() override
        {
            if (playing)
            {
                // Dark, like the screen above: only the progress, quietly.
                rect(0, 0, BOTTOM_W, H, rgb(0, 0, 0));
                rect(20, H - 14, BOTTOM_W - 40, 2, rgb(40, 44, 56));
                rect(20, H - 14, float(BOTTOM_W - 40) * index / std::max(1, count), 2, rgb(150, 160, 190));
                text(BOTTOM_W / 2, 112, "Bad Apple!!", rgb(150, 160, 190), Center);
                return;
            }
            housing(0, 0, BOTTOM_W, H);
            widgets::header(BOTTOM_W, "Bad Apple!!", "");
            if (worker)
            {
                text(BOTTOM_W / 2, 110, step.load(), T::InkSoft, Center);
                return;
            }
            Input none{};
            widgets::button(none, 60, 150, BOTTOM_W - 120, 30, "Get it", true);
            widgets::footer(BOTTOM_W, {{"A", "Get it"}, {"B", "Back"}});
        }

    private:
        // The video into memory, the names into a stream, the song on; then the clock runs.
        void begin()
        {
            std::vector<u8> file;
            if (!files::read(framesPath(), file) || file.size() < 12 || memcmp(file.data(), "BAPL", 4) != 0)
            {
                error = "frames.bin is not the Bad Apple!! video";
                return;
            }
            w     = file[4] | file[5] << 8;
            h     = file[6] | file[7] << 8;
            fps   = file[8] | file[9] << 8;
            count = file[10] | file[11] << 8;
            size_t frame = size_t(w) * h / 8;
            if (!frame || !fps)
            {
                error = "frames.bin is damaged";
                return;
            }
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
            if (!count)
            {
                error = "frames.bin is damaged";
                return;
            }
            bits.assign(frame, 0);
            words.clear();
            for (int s = 1; s < 1026; s++)
            {
                for (char c : pkdata::species(s))
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
            std::string song = songPath();
            if (!song.empty())
            {
                audio::override(song);
            }
            index   = -1;
            start   = osGetTime();
            playing = true;
        }

        void advance()
        {
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

        // Whatever is missing, on a worker thread: the video, the song, the song as a .wav.
        void fetch()
        {
            error.clear();
            done = false;
            ok   = false;
            part = -1;
            step = "Starting...";
            s32 prio = 0x30;
            svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
            worker = threadCreate(run, this, 256 * 1024, prio + 1, -2, false);
            if (!worker)
            {
                error = "no thread";
            }
        }

        static void run(void* arg) { static_cast<BadApple*>(arg)->work(); }

        void work()
        {
            files::mkdirs(DIR);
            std::string why;
            bool good = true;
            auto get  = [&](const char* name, const char* label) {
                std::string to = std::string(DIR) + "/" + name;
                if (!good || files::exists(to) || files::exists(std::string(ROM) + "/" + name))
                {
                    return;
                }
                step = label;
                std::vector<u8> body;
                good = updater::fetch(std::string(FROM) + name, body, why) && !body.empty() &&
                       files::write(to, body.data(), body.size());
                if (!good && why.empty())
                {
                    why = "could not save " + std::string(name);
                }
            };
            if (!files::exists(framesPath()))
            {
                get("frames.bin", "Getting the video...");
            }
            if (songPath().empty())
            {
                get("audio.m4a", "Getting the song...");
            }
            error = good ? "" : why;
            ok    = good;
            done  = true;
        }

        int w = 0, h = 0, fps = 30, count = 0, index = -1;
        std::vector<u8> deltas, bits;
        std::string words, error;
        u64 start     = 0;
        bool playing  = false;
        Thread worker = nullptr;
        std::atomic<bool> done{false}, ok{false}, cancel{false};
        std::atomic<float> part{-1};
        std::atomic<const char*> step{""};
    };
}

namespace screens
{
    std::unique_ptr<Screen> badApple() { return std::make_unique<BadApple>(); }
}
