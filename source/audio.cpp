#include "audio.hpp"
#include "m4a.hpp"
#include "files.hpp"

#include <3ds.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace audio
{
    namespace
    {
        constexpr int RATE = 22050, MUSIC_RATE = 32000, CHUNK = 2048, BUFFERS = 4, MUSIC = 0, FIRST_FX = 1, FX_CHANNELS = 6, CRY = 7;
        constexpr const char* SETTINGS = "sdmc:/3ds/PKForge/audio.txt";

        bool ready = false, wantMusic = true, wantFx = true;
        constexpr const char* LOG = "sdmc:/3ds/PKForge/audio.log";
        int logged = 0; // effect plays logged so far (only the first few)

        void log(const char* format, ...)
        {
            FILE* f = fopen(LOG, "a");
            if (!f)
            {
                return;
            }
            va_list args;
            va_start(args, format);
            vfprintf(f, format, args);
            va_end(args);
            fputc('\n', f);
            fclose(f);
        }
        std::atomic<bool> running{false}; // the music and converter threads run while true
        std::atomic<bool> effectsReady{false}; // the converter thread has made the effects
        std::atomic<int> pendingPlace{-1};     // a place asked for before the sound was up
        void makeEffects();
        std::string why = "Sound not started.";

        // ---------------------------------------------------------------- music sources

        struct Source
        {
            int channels = 1, rate = RATE;
            virtual ~Source() = default;
            // Interleaved frames; fewer than asked at the end (the caller loops or moves on).
            virtual int read(s16* out, int frames) = 0;
        };

        // Two seconds of nothing: what plays while the built-in music is still being prepared.
        struct SilenceSource : Source
        {
            int left = MUSIC_RATE * 2;
            SilenceSource()
            {
                channels = 2;
                rate     = MUSIC_RATE;
            }
            int read(s16* out, int frames) override
            {
                frames = std::min(frames, left);
                memset(out, 0, size_t(frames) * 2 * sizeof(s16));
                left -= frames;
                return frames;
            }
        };

        // The user's tracks, decoded as they play.
        struct FileSource : Source
        {
            drmp3 mp3{};
            drwav wav{};
            stb_vorbis* ogg = nullptr;
            m4a::Stream aac;
            int kind        = 0; // 1 mp3, 2 wav, 3 ogg, 4 m4a (AAC)
            ~FileSource() override
            {
                if (kind == 1)
                {
                    drmp3_uninit(&mp3);
                }
                else if (kind == 2)
                {
                    drwav_uninit(&wav);
                }
                else if (kind == 3)
                {
                    stb_vorbis_close(ogg);
                }
            }
            bool open(const std::string& path)
            {
                std::string low = files::lower(path);
                if (low.size() > 4 && low.compare(low.size() - 4, 4, ".mp3") == 0 && drmp3_init_file(&mp3, path.c_str(), nullptr))
                {
                    kind     = 1;
                    channels = int(mp3.channels);
                    rate     = int(mp3.sampleRate);
                }
                else if (low.size() > 4 && low.compare(low.size() - 4, 4, ".wav") == 0 && drwav_init_file(&wav, path.c_str(), nullptr))
                {
                    kind     = 2;
                    channels = int(wav.channels);
                    rate     = int(wav.sampleRate);
                }
                else if (low.size() > 4 && low.compare(low.size() - 4, 4, ".ogg") == 0 &&
                         (ogg = stb_vorbis_open_filename(path.c_str(), nullptr, nullptr)))
                {
                    kind               = 3;
                    stb_vorbis_info in = stb_vorbis_get_info(ogg);
                    channels           = in.channels;
                    rate               = int(in.sample_rate);
                }
                else if (low.size() > 4 && low.compare(low.size() - 4, 4, ".m4a") == 0 && aac.open(path))
                {
                    kind     = 4;
                    channels = aac.channels;
                    rate     = aac.rate;
                }
                channels = std::clamp(channels, 1, 2);
                return kind != 0;
            }
            int read(s16* out, int frames) override
            {
                if (kind == 1)
                {
                    return int(drmp3_read_pcm_frames_s16(&mp3, frames, out));
                }
                if (kind == 2)
                {
                    return int(drwav_read_pcm_frames_s16(&wav, frames, out));
                }
                if (kind == 3)
                {
                    return stb_vorbis_get_samples_short_interleaved(ogg, channels, out, frames * channels);
                }
                if (kind == 4)
                {
                    return aac.read(out, frames);
                }
                return 0;
            }
        };

        // A built-in track already converted: raw stereo PCM at MUSIC_RATE on the SD card.
        struct PcmSource : Source
        {
            FILE* f = nullptr;
            ~PcmSource() override
            {
                if (f)
                {
                    fclose(f);
                }
            }
            bool open(const std::string& path)
            {
                f        = fopen(path.c_str(), "rb");
                channels = 2;
                rate     = MUSIC_RATE;
                return f != nullptr;
            }
            int read(s16* out, int frames) override { return int(fread(out, 2 * sizeof(s16), size_t(frames), f)); }
        };

        // The built-in tracks (romfs:/music, Kevin MacLeod, CC BY 4.0: see romfs:/licenses/music.txt) are
        // converted once to MUSIC_RATE stereo PCM in sdmc:/3ds/PKForge/cache by a lowest-priority thread: it only
        // uses the time the app spends waiting for the screen, so decoding never costs a frame. MUSIC_RATE is
        // about what the 3DS's DSP plays out (32728 Hz), so nothing audible is lost.
        constexpr const char* CACHE = "sdmc:/3ds/PKForge/cache";

        // Every MP3 the build carries for a place (romfs:/music/<place>), in name order.
        std::vector<std::string> builtInTracks(Place p)
        {
            std::string dir = p == Place::Title ? "romfs:/music/title" : p == Place::Menu ? "romfs:/music/menu" : p == Place::ParkDay ? "romfs:/music/park-day" : "romfs:/music/park-night";
            std::vector<std::string> out;
            files::list(dir, [&](const std::string& name, bool isDir, u64)
                {
                    std::string low = files::lower(name);
                    if (!isDir && low.size() > 4 && low.compare(low.size() - 4, 4, ".mp3") == 0)
                    {
                        out.push_back(dir + "/" + name);
                    }
                });
            std::sort(out.begin(), out.end());
            return out;
        }

        // "2-using-pokemon-home.mp3" -> "Using pokemon home"; Kevin MacLeod's track keeps its credit.
        std::string trackTitle(const std::string& track)
        {
            std::string name = files::baseName(track);
            if (name.find("dreamy_flashback") == 0)
            {
                return "\"Dreamy Flashback\" by Kevin MacLeod (CC BY 4.0)";
            }
            name       = name.substr(0, name.rfind('.'));
            size_t cut = name.find('-');
            if (cut != std::string::npos && cut < 3)
            {
                name = name.substr(cut + 1);
            }
            for (char& c : name)
            {
                c = c == '-' || c == '_' ? ' ' : c;
            }
            if (!name.empty())
            {
                name[0] = char(toupper(name[0]));
            }
            return name;
        }

        std::string cacheOf(const std::string& track)
        {
            return std::string(CACHE) + "/" + files::baseName(track) + ".32k.pcm";
        }

        // ---------------------------------------------------------------- the music menu's settings (the
        // original's MusicMenu): per place all tracks / one track / same as menus / silence, order, autostart.
        constexpr const char* MUSIC_SETTINGS = "sdmc:/3ds/PKForge/music.txt";
        constexpr const char* LIBRARY        = "sdmc:/3ds/PKForge/music/library";
        std::string choiceOf[4];            // "" every track, "same", "silence", or one track's path
        bool shuffleOn   = false;
        bool autostartOn = true;
        std::atomic<bool> pausedFlag{false};

        const char* placeKey(int p) { return p == 0 ? "title" : p == 1 ? "menu" : p == 2 ? "park-day" : "park-night"; }

        void loadMusicSettings()
        {
            std::string text;
            if (!files::readText(MUSIC_SETTINGS, text))
            {
                return;
            }
            size_t a = 0;
            while (a < text.size())
            {
                size_t b = text.find('\n', a);
                std::string line = text.substr(a, b == std::string::npos ? std::string::npos : b - a);
                a = b == std::string::npos ? text.size() : b + 1;
                size_t eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                std::string k = line.substr(0, eq), v = line.substr(eq + 1);
                if (k == "order")
                {
                    shuffleOn = v == "shuffle";
                }
                else if (k == "autostart")
                {
                    autostartOn = v != "0";
                }
                for (int p = 0; p < 4; p++)
                {
                    if (k == placeKey(p))
                    {
                        choiceOf[p] = v;
                    }
                }
            }
        }

        void saveMusicSettings()
        {
            std::string out = std::string("order=") + (shuffleOn ? "shuffle" : "in-order") + "\nautostart=" +
                              (autostartOn ? "1" : "0") + "\n";
            for (int p = 0; p < 4; p++)
            {
                out += std::string(placeKey(p)) + "=" + choiceOf[p] + "\n";
            }
            files::writeText(MUSIC_SETTINGS, out);
        }

        bool isAudioFile(const std::string& name)
        {
            std::string low = files::lower(name);
            for (const char* ext : {".mp3", ".ogg", ".wav"})
            {
                if (low.size() > 4 && low.compare(low.size() - 4, 4, ext) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> folderTracks(const std::string& dir)
        {
            std::vector<std::string> out;
            files::list(dir, [&](const std::string& name, bool isDir, u64)
                {
                    if (!isDir && isAudioFile(name))
                    {
                        out.push_back(dir + "/" + name);
                    }
                });
            std::sort(out.begin(), out.end());
            return out;
        }

        Thread converter = nullptr;

        void convertThread(void*)
        {
            // The effects first (a few hundred milliseconds of synthesis): out of start-up, before the music.
            makeEffects();
            effectsReady = true;
            files::mkdirs(CACHE);
            // The title's track first, then the menus': that is where PKForge starts.
            std::vector<std::string> all;
            for (Place p : {Place::Title, Place::Menu, Place::ParkDay, Place::ParkNight})
            {
                for (auto& t : builtInTracks(p))
                {
                    all.push_back(t);
                }
            }
            // Caches of tracks this build no longer carries, and earlier formats, are no use now.
            std::vector<std::string> stale;
            files::list(CACHE, [&](const std::string& name, bool dir, u64)
                {
                    std::string path = std::string(CACHE) + "/" + name;
                    bool keep        = false;
                    for (auto& t : all)
                    {
                        keep = keep || path == cacheOf(t);
                    }
                    if (!dir && !keep)
                    {
                        stale.push_back(path);
                    }
                });
            for (auto& path : stale)
            {
                remove(path.c_str());
            }
            for (const std::string& track : all)
            {
                std::string out = cacheOf(track);
                if (files::exists(out))
                {
                    continue;
                }
                auto mp3 = std::make_unique<drmp3>(); // big: on the heap
                if (!drmp3_init_file(mp3.get(), track.c_str(), nullptr))
                {
                    continue;
                }
                int ch          = std::clamp(int(mp3->channels), 1, 2);
                double step     = double(mp3->sampleRate) / MUSIC_RATE, pos = 0;
                std::string temp = out + ".part";
                FILE* f          = fopen(temp.c_str(), "wb");
                bool ok          = f != nullptr;
                std::vector<s16> in(4096 * ch), stereo;
                s16 last[2] = {0, 0}; // the previous chunk's final frame, for interpolating across chunks
                int got;
                while (ok && running && (got = int(drmp3_read_pcm_frames_s16(mp3.get(), 4096, in.data()))) > 0)
                {
                    stereo.clear();
                    // Linear interpolation between neighbouring frames; pos runs from -1 (last) to got - 1.
                    for (; pos < got - 1; pos += step)
                    {
                        int i0     = int(std::floor(pos));
                        double fr  = pos - i0;
                        for (int c = 0; c < 2; c++)
                        {
                            int cc  = std::min(c, ch - 1);
                            double a = i0 < 0 ? last[cc] : in[i0 * ch + cc];
                            double b = in[(i0 + 1) * ch + cc];
                            stereo.push_back(s16(a + (b - a) * fr));
                        }
                    }
                    pos -= got;
                    last[0] = in[(got - 1) * ch];
                    last[1] = in[(got - 1) * ch + (ch - 1)];
                    ok      = fwrite(stereo.data(), sizeof(s16), stereo.size(), f) == stereo.size();
                    svcSleepThread(1000 * 1000); // let everything else go first
                }
                drmp3_uninit(mp3.get());
                if (f)
                {
                    fclose(f);
                }
                log("converted %s: %s", track.c_str(), ok && running ? "ok" : "stopped");
                if (ok && running)
                {
                    remove(out.c_str());
                    rename(temp.c_str(), out.c_str());
                }
                else
                {
                    remove(temp.c_str());
                }
            }
        }

        // ---------------------------------------------------------------- the music thread

        Thread thread = nullptr;
        LightLock lock;
        Place current = Place::Menu, wanted = Place::Menu;
        std::unique_ptr<Source> source;
        std::vector<std::string> playlist; // the place's tracks, the user's first, then the build's
        bool silenceHere = false;
        int track = 0;
        std::string overridePath;
        ndspWaveBuf waves[BUFFERS];
        s16* pcm = nullptr; // BUFFERS x CHUNK frames x 2 channels, linear memory
        int musicChannels = 1, musicRate = RATE;

        const char* folderOf(Place p)
        {
            return p == Place::Title ? "sdmc:/3ds/PKForge/music/title" : p == Place::Menu ? "sdmc:/3ds/PKForge/music/menu" : p == Place::ParkDay ? "sdmc:/3ds/PKForge/music/park-day"
                                                                                            : "sdmc:/3ds/PKForge/music/park-night";
        }

        // A place's tracks as the music menu set them: every track (the place's folder, the library, the
        // build's), one track, the menus' music, or silence.
        void loadPlaylist(Place p)
        {
            playlist.clear();
            track       = 0;
            silenceHere = false;
            if (!overridePath.empty())
            {
                playlist.push_back(overridePath);
                return;
            }
            int at      = int(p);
            std::string choice = choiceOf[at];
            if (choice == "same" && (p == Place::ParkDay || p == Place::ParkNight))
            {
                at     = int(Place::Menu);
                choice = choiceOf[at];
            }
            if (choice == "silence")
            {
                silenceHere = true;
                return;
            }
            if (!choice.empty() && choice != "same")
            {
                playlist.push_back(choice);
                return;
            }
            for (auto& t : folderTracks(folderOf(Place(at))))
            {
                playlist.push_back(t);
            }
            for (auto& t : folderTracks(LIBRARY))
            {
                playlist.push_back(t);
            }
            for (auto& t : builtInTracks(Place(at)))
            {
                playlist.push_back(t);
            }
        }

        // The next track of the place (in order or shuffled); the build's own play from their converted cache.
        std::unique_ptr<Source> nextSource()
        {
            if (silenceHere)
            {
                why = "Silence here.";
                return std::make_unique<SilenceSource>();
            }
            for (size_t tries = 0; tries < playlist.size(); tries++)
            {
                size_t i = shuffleOn && playlist.size() > 1 ? size_t(rand()) % playlist.size() : size_t(track++) % playlist.size();
                const std::string& t = playlist[i];
                if (t.compare(0, 6, "romfs:") == 0 && files::lower(t).find(".m4a") == std::string::npos)
                {
                    auto pcm = std::make_unique<PcmSource>();
                    if (pcm->open(cacheOf(t)))
                    {
                        why = "Playing " + trackTitle(t);
                        log("music: %s", cacheOf(t).c_str());
                        return pcm;
                    }
                    continue;
                }
                auto f = std::make_unique<FileSource>();
                if (f->open(t))
                {
                    why = "Playing " + trackTitle(t);
                    log("music: %s (%d ch, %d Hz)", t.c_str(), f->channels, f->rate);
                    return f;
                }
            }
            why = playlist.empty() ? "No music here." : "Preparing the music (first start only, a few minutes)...";
            return std::make_unique<SilenceSource>();
        }

        void configureMusicChannel()
        {
            ndspChnWaveBufClear(MUSIC);
            ndspChnReset(MUSIC);
            ndspChnSetInterp(MUSIC, NDSP_INTERP_LINEAR);
            ndspChnSetRate(MUSIC, float(musicRate));
            ndspChnSetFormat(MUSIC, musicChannels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
            float mix[12] = {0.6f, 0.6f};
            ndspChnSetMix(MUSIC, mix);
            for (auto& w : waves)
            {
                w.status = NDSP_WBUF_DONE;
            }
        }

        void musicThread(void*)
        {
            while (running)
            {
                LightLock_Lock(&lock);
                if (wanted != current || !source)
                {
                    current = wanted;
                    loadPlaylist(current);
                    source = nextSource();
                    musicChannels = source->channels;
                    musicRate     = source->rate;
                    configureMusicChannel();
                }
                for (int i = 0; i < BUFFERS && wantMusic && !pausedFlag; i++)
                {
                    ndspWaveBuf& w = waves[i];
                    if (w.status != NDSP_WBUF_DONE && w.status != NDSP_WBUF_FREE)
                    {
                        continue;
                    }
                    s16* data = pcm + i * CHUNK * 2;
                    int got   = source->read(data, CHUNK);
                    if (got < CHUNK)
                    {
                        // End of a track: on to the next (or the tune again), same format or not.
                        auto next = nextSource();
                        if (next->channels == musicChannels && next->rate == musicRate)
                        {
                            source = std::move(next);
                            got += source->read(data + got * musicChannels, CHUNK - got);
                        }
                        else
                        {
                            source = std::move(next);
                            musicChannels = source->channels;
                            musicRate     = source->rate;
                            configureMusicChannel();
                            break;
                        }
                    }
                    memset(&w, 0, sizeof(w));
                    w.data_vaddr = data;
                    w.nsamples   = u32(std::max(got, 1));
                    DSP_FlushDataCache(data, CHUNK * 2 * sizeof(s16));
                    ndspChnWaveBufAdd(MUSIC, &w);
                }
                if (!wantMusic)
                {
                    ndspChnWaveBufClear(MUSIC);
                    for (auto& w : waves)
                    {
                        w.status = NDSP_WBUF_DONE;
                    }
                }
                LightLock_Unlock(&lock);
                svcSleepThread(10 * 1000 * 1000);
            }
        }

        // ---------------------------------------------------------------- effects and cries

        struct Clip
        {
            s16* data = nullptr;
            int frames = 0;
        };

        Clip clips[11];
        Clip cryClip; // the last cry, reused
        int nextFx = 0;

        Clip makeClip(const std::vector<float>& v)
        {
            Clip c;
            c.frames = int(v.size());
            c.data   = static_cast<s16*>(linearAlloc(v.size() * sizeof(s16)));
            for (size_t i = 0; c.data && i < v.size(); i++)
            {
                c.data[i] = s16(std::clamp(v[i], -1.0f, 1.0f) * 24000);
            }
            if (c.data)
            {
                DSP_FlushDataCache(c.data, v.size() * sizeof(s16));
            }
            return c;
        }

        // Soft sounds: sine tones with a gentle rise and an exponential fade, nothing sharp or buzzy.
        void soft(std::vector<float>& out, double f0, double f1, double len, double vol)
        {
            int n     = int(len * RATE);
            double ph = 0;
            for (int i = 0; i < n; i++)
            {
                double t = double(i) / n, f = f0 + (f1 - f0) * t;
                ph += f / RATE;
                double env = std::min(1.0, i / 180.0) * std::exp(-4.0 * t);
                // A touch of the octave makes it round rather than a pure beep.
                out.push_back(float((std::sin(ph * 6.2831853) + 0.25 * std::sin(ph * 12.566371)) * vol * env));
            }
        }

        void makeEffects()
        {
            std::vector<float> v;
            soft(v, 880, 880, 0.035, 0.18); // a light tick
            clips[int(Sfx::Blip)] = makeClip(v);
            v.clear();
            soft(v, 1320, 1320, 0.018, 0.1); // the cursor's tick: shorter, higher, quieter
            clips[int(Sfx::Cursor)] = makeClip(v);
            v.clear();
            soft(v, 587, 587, 0.06, 0.18);
            soft(v, 784, 784, 0.09, 0.16);
            clips[int(Sfx::Open)] = makeClip(v);
            v.clear();
            soft(v, 784, 784, 0.05, 0.15);
            soft(v, 587, 587, 0.08, 0.13);
            clips[int(Sfx::Back)] = makeClip(v);
            v.clear();
            soft(v, 330, 294, 0.16, 0.22);
            clips[int(Sfx::Error)] = makeClip(v);
            v.clear();
            soft(v, 260, 620, 0.22, 0.25); // a soft swish up
            clips[int(Sfx::Throw)] = makeClip(v);
            v.clear();
            soft(v, 196, 150, 0.12, 0.35); // a muffled bounce
            soft(v, 175, 150, 0.08, 0.2);
            clips[int(Sfx::Land)] = makeClip(v);
            v.clear();
            soft(v, 440, 660, 0.1, 0.22);
            clips[int(Sfx::Hop)] = makeClip(v);
            v.clear();
            for (double f : {1047.0, 1319.0, 1568.0})
            {
                soft(v, f, f, 0.08, 0.14);
            }
            clips[int(Sfx::Sparkle)] = makeClip(v);
            v.clear();
            for (double f : {659.0, 784.0, 988.0})
            {
                soft(v, f, f, 0.1, 0.2);
            }
            clips[int(Sfx::Found)] = makeClip(v);
            v.clear();
            for (double f : {523.0, 659.0, 784.0, 1047.0})
            {
                soft(v, f, f, 0.14, 0.2);
            }
            clips[int(Sfx::Win)] = makeClip(v);
        }

        // On a channel of its own when given one (cries), else the next of the effect channels.
        void play(const Clip& c, float volume, int channel = -1)
        {
            if (!ready || !wantFx || !c.data)
            {
                return;
            }
            int ch = channel;
            if (ch < 0)
            {
                ch     = FIRST_FX + nextFx;
                nextFx = (nextFx + 1) % FX_CHANNELS;
            }
            static ndspWaveBuf bufs[CRY + 1];
            ndspWaveBuf& w = bufs[ch];
            ndspChnWaveBufClear(ch);
            ndspChnReset(ch);
            ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
            ndspChnSetRate(ch, float(RATE));
            ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
            float mix[12] = {volume, volume};
            ndspChnSetMix(ch, mix);
            memset(&w, 0, sizeof(w));
            w.data_vaddr = c.data;
            w.nsamples   = u32(c.frames);
            ndspChnWaveBufAdd(ch, &w);
            if (logged < 6)
            {
                logged++;
                svcSleepThread(30 * 1000 * 1000);
                log("effect on channel %d: %d frames at %p, volume %.2f, wavebuf status %d, playing %d, position %lu", ch,
                    c.frames, (void*)c.data, double(volume), int(w.status), int(ndspChnIsPlaying(ch)),
                    (unsigned long)ndspChnGetSamplePos(ch));
            }
        }

        u32 hashOf(u32 v)
        {
            v ^= v >> 16;
            v *= 0x7feb352d;
            v ^= v >> 15;
            v *= 0x846ca68b;
            return v ^ (v >> 16);
        }

        // A voice made up from the species number: 2-3 chirps with a warble.
        std::vector<float> madeUpCry(int species)
        {
            u32 h         = hashOf(u32(species) * 2654435761u + 7);
            double base   = 220 + (h % 700);
            int parts     = 2 + (h >> 10) % 2;
            double warble = 4 + (h >> 12) % 12;
            std::vector<float> v;
            double ph = 0;
            for (int p = 0; p < parts; p++)
            {
                double len = 0.08 + ((h >> (14 + p * 3)) % 6) * 0.03;
                double f0  = base * (1 + ((h >> (20 + p)) % 5) * 0.12);
                double f1  = f0 * (((h >> (24 + p)) % 2) ? 1.35 : 0.7);
                int n      = int(len * RATE);
                for (int i = 0; i < n; i++)
                {
                    double t = double(i) / n;
                    double f = (f0 + (f1 - f0) * t) * (1 + 0.03 * std::sin(i * warble * 6.28 / RATE));
                    ph += f / RATE;
                    double sq = std::fmod(ph, 1.0) < 0.35 ? 1 : -1;
                    double saw = 2 * std::fmod(ph, 1.0) - 1;
                    double env = std::min(1.0, i / 80.0) * (1 - t * 0.7);
                    v.push_back(float((sq * 0.6 + saw * 0.4) * 0.45 * env));
                }
                for (int i = 0; i < RATE / 50; i++)
                {
                    v.push_back(0); // a breath between chirps
                }
            }
            return v;
        }

        // A cry file of the user's, decoded whole and mixed to mono at RATE.
        bool userCry(int species, std::vector<float>& out)
        {
            for (const char* ext : {".wav", ".ogg", ".mp3"})
            {
                std::string path = "sdmc:/3ds/PKForge/cries/" + std::to_string(species) + ext;
                if (!files::exists(path))
                {
                    continue;
                }
                // On the heap: the three decoders' state (MP3's most of all) would overflow the caller's stack.
                auto f = std::make_unique<FileSource>();
                if (!f->open(path))
                {
                    continue;
                }
                std::vector<s16> buf(4096 * f->channels);
                double step = double(f->rate) / RATE, pos = 0;
                std::vector<float> mono;
                int got;
                while ((got = f->read(buf.data(), 4096)) > 0 && mono.size() < size_t(f->rate * 4))
                {
                    for (int i = 0; i < got; i++)
                    {
                        float s = 0;
                        for (int c = 0; c < f->channels; c++)
                        {
                            s += buf[i * f->channels + c] / 32768.0f;
                        }
                        mono.push_back(s / f->channels);
                    }
                }
                for (; pos < mono.size(); pos += step)
                {
                    out.push_back(mono[size_t(pos)]);
                }
                return !out.empty();
            }
            return false;
        }
    }

    void init(int appPriority)
    {
        LightLock_Init(&lock);
        std::string text;
        if (files::readText(SETTINGS, text) && text.size() >= 2)
        {
            wantMusic = text[0] == '1';
            wantFx    = text[1] == '1';
        }
        loadMusicSettings();
        pausedFlag = !autostartOn;
        remove(LOG);
        FILE* firm = fopen("sdmc:/3ds/dspfirm.cdc", "rb");
        long firmSize = 0;
        if (firm)
        {
            fseek(firm, 0, SEEK_END);
            firmSize = ftell(firm);
            fclose(firm);
        }
        log("PKForge audio: music %s, effects %s; sdmc:/3ds/dspfirm.cdc %ld bytes", wantMusic ? "on" : "off",
            wantFx ? "on" : "off", firmSize);
        Result res = ndspInit();
        if (R_FAILED(res))
        {
            // Once more after a moment: the DSP can still be busy right after the HOME menu let go of it.
            svcSleepThread(200 * 1000 * 1000);
            res = ndspInit();
        }
        log("ndspInit: %08lX", (unsigned long)res);
        if (R_FAILED(res))
        {
            char text[200];
            snprintf(text, sizeof(text),
                "No sound (DSP error %08lX). If sdmc:/3ds/dspfirm.cdc is missing: Rosalina menu (L + Down + Select), "
                "Miscellaneous options, Dump DSP firmware, then restart PKForge.",
                (unsigned long)res);
            why = text;
            return;
        }
        ready = true;
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspSetOutputCount(1);
        ndspSetMasterVol(1.0f);
        pcm = static_cast<s16*>(linearAlloc(BUFFERS * CHUNK * 2 * sizeof(s16)));
        if (pendingPlace >= 0)
        {
            wanted = Place(pendingPlace.load());
        }
        running   = true;
        thread    = threadCreate(musicThread, nullptr, 32 * 1024, appPriority - 1, -2, false);
        converter = threadCreate(convertThread, nullptr, 256 * 1024, 0x3F, -2, false);
    }

    void exit()
    {
        if (!ready)
        {
            return;
        }
        running = false;
        if (thread)
        {
            threadJoin(thread, U64_MAX);
            threadFree(thread);
        }
        if (converter)
        {
            threadJoin(converter, U64_MAX);
            threadFree(converter);
        }
        source.reset();
        for (int ch = 0; ch <= CRY; ch++)
        {
            ndspChnWaveBufClear(ch);
        }
        ndspExit();
        linearFree(pcm);
        for (Clip& c : clips)
        {
            linearFree(c.data);
        }
        linearFree(cryClip.data);
        ready = false;
    }

    bool available() { return ready; }

    void place(Place p)
    {
        if (!ready)
        {
            pendingPlace = int(p); // the sound starts on a thread: it plays this place when it is up
            return;
        }
        LightLock_Lock(&lock);
        wanted = p;
        LightLock_Unlock(&lock);
    }

    void sfx(Sfx s)
    {
        // Menu sounds stay in the background; the park's a little more present.
        if (!effectsReady)
        {
            return;
        }
        bool menu = s == Sfx::Blip || s == Sfx::Open || s == Sfx::Back || s == Sfx::Error || s == Sfx::Cursor;
        play(clips[int(s)], menu ? 0.35f : 0.6f);
    }

    void cry(int species, float volume)
    {
        if (!ready || !wantFx || species <= 0)
        {
            return;
        }
        std::vector<float> v;
        if (!userCry(species, v))
        {
            v = madeUpCry(species);
        }
        // The previous cry may still be playing from its buffer: stop it before freeing.
        ndspChnWaveBufClear(CRY);
        linearFree(cryClip.data);
        cryClip = makeClip(v);
        play(cryClip, volume, CRY);
    }

    void test()
    {
        if (!ready)
        {
            return;
        }
        std::vector<float> v;
        for (double f : {523.0, 659.0, 784.0, 1047.0})
        {
            soft(v, f, f, 0.25, 0.5);
        }
        static Clip clip;
        ndspChnWaveBufClear(CRY);
        linearFree(clip.data);
        clip = makeClip(v);
        bool was = wantFx;
        wantFx   = true;
        play(clip, 1.0f, CRY);
        wantFx = was;
        log("test tone played");
    }

    bool musicOn() { return wantMusic; }
    bool effectsOn() { return wantFx; }

    void musicOn(bool on)
    {
        wantMusic = on;
        files::writeText(SETTINGS, std::string(wantMusic ? "1" : "0") + (wantFx ? "1" : "0"));
        if (ready && on)
        {
            LightLock_Lock(&lock);
            source.reset(); // start the place's music afresh
            LightLock_Unlock(&lock);
        }
    }

    void effectsOn(bool on)
    {
        wantFx = on;
        files::writeText(SETTINGS, std::string(wantMusic ? "1" : "0") + (wantFx ? "1" : "0"));
    }

    std::string status() { return !ready ? why : !wantMusic ? "Music off" : pausedFlag ? "Paused" : why; }

    bool paused() { return pausedFlag; }
    void pause(bool on)
    {
        pausedFlag = on;
        if (ready)
        {
            ndspChnSetPaused(MUSIC, on);
        }
    }

    void skip()
    {
        if (!ready)
        {
            return;
        }
        LightLock_Lock(&lock);
        auto next = nextSource();
        source    = std::move(next);
        musicChannels = source->channels;
        musicRate     = source->rate;
        configureMusicChannel();
        LightLock_Unlock(&lock);
    }

    bool shuffle() { return shuffleOn; }
    void shuffle(bool on)
    {
        shuffleOn = on;
        saveMusicSettings();
    }

    bool autostart() { return autostartOn; }
    void autostart(bool on)
    {
        autostartOn = on;
        saveMusicSettings();
    }

    std::string choice(Place p) { return choiceOf[int(p)]; }
    void choice(Place p, const std::string& value)
    {
        choiceOf[int(p)] = value;
        saveMusicSettings();
        if (ready)
        {
            LightLock_Lock(&lock);
            if (current == p || (current != Place::Menu && p == Place::Menu))
            {
                source.reset(); // reload the place's tracks now
            }
            LightLock_Unlock(&lock);
        }
    }

    std::vector<std::pair<std::string, std::string>> library()
    {
        std::vector<std::pair<std::string, std::string>> out;
        for (Place p : {Place::Title, Place::Menu, Place::ParkDay, Place::ParkNight})
        {
            for (auto& t : builtInTracks(p))
            {
                out.push_back({t, trackTitle(t)});
            }
            for (auto& t : folderTracks(folderOf(p)))
            {
                out.push_back({t, trackTitle(t) + " (your file)"});
            }
        }
        for (auto& t : folderTracks(LIBRARY))
        {
            out.push_back({t, trackTitle(t) + " (your file)"});
        }
        // One row per title: the title screen and the menus carry the same file.
        std::vector<std::pair<std::string, std::string>> unique;
        for (auto& t : out)
        {
            bool seen = false;
            for (auto& u : unique)
            {
                seen |= u.second == t.second;
            }
            if (!seen)
            {
                unique.push_back(t);
            }
        }
        return unique;
    }

    std::string libraryFolder() { return LIBRARY; }

    void override(const std::string& path)
    {
        if (!ready)
        {
            return;
        }
        LightLock_Lock(&lock);
        overridePath = path;
        source.reset(); // reload the playlist now
        LightLock_Unlock(&lock);
    }
}
