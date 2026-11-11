#include "spritepack.hpp"
#include "files.hpp"
#include "updater.hpp"

#include "stb_image.h"
#include "webp/decode.h"
#include <3ds.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <zlib.h>

// The Android PKForge's sprite pack (SpritePackDownloader): HOME renders and the animated Showdown sprites from
// sofianeelhor's pkforge-sprites.zip release. A 3DS does not need all 625 MB: the zip's directory is read once,
// then each picture is pulled out of the archive on its own with an HTTP range request, unpacked, shrunk to
// SIZE x SIZE and kept on the SD card (sdmc:/3ds/PKForge/sprites/home, .../showdown).
namespace spritepack
{
    namespace
    {
        constexpr const char* URL      = "https://github.com/sofianeelhor/PKForge/releases/download/sprites-1/pkforge-sprites.zip";
        constexpr u64 ZIP_SIZE         = 625017170;
        constexpr const char* ROOT     = "sdmc:/3ds/PKForge/sprites";
        constexpr const char* CD_CACHE = "sdmc:/3ds/PKForge/cache/spritepack-cd.bin";
        constexpr const char* SETTING  = "sdmc:/3ds/PKForge/spritepack.txt";
        constexpr int MAX_FRAMES       = 96;

        struct Entry
        {
            u32 offset = 0, compressed = 0, size = 0;
            u16 method = 0;
        };

        LightLock lock;
        bool lockReady = false, loaded = false;
        Mode current   = Mode::BuiltIn;
        std::map<std::string, Entry> entries;            // "home/63.png", "showdown/63.gif"
        std::map<std::string, std::vector<u8>> images;   // home renders, SIZE x SIZE RGBA
        std::map<std::string, Animation> anims;          // Showdown animations
        std::deque<std::string> queue;
        std::map<std::string, bool> failed;
        Thread worker = nullptr;
        bool running  = false;
        int doneAll = 0, totalAll = 0;

        void init()
        {
            if (!lockReady)
            {
                LightLock_Init(&lock);
                lockReady = true;
                std::string t;
                if (files::readText(SETTING, t))
                {
                    current = t.rfind("on", 0) == 0 || t.rfind("home", 0) == 0 ? Mode::Home
                              : t.rfind("animated", 0) == 0                    ? Mode::Animated
                                                                               : Mode::BuiltIn;
                }
            }
        }

        u16 rd16(const u8* p) { return u16(p[0] | p[1] << 8); }
        u32 rd32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
        void wr32(std::vector<u8>& v, u32 x)
        {
            for (int i = 0; i < 4; i++)
            {
                v.push_back(u8(x >> (8 * i)));
            }
        }

        bool range(u64 from, u64 to, std::vector<u8>& out, std::string& error)
        {
            return updater::fetchRange(URL, from, to, out, error);
        }

        // The zip's central directory: kept on the SD card after the first read.
        bool directory(std::string& error)
        {
            if (loaded)
            {
                return true;
            }
            std::vector<u8> cd;
            if (!files::read(CD_CACHE, cd) || cd.size() < 22)
            {
                std::vector<u8> tail;
                if (!range(ZIP_SIZE - 65536, ZIP_SIZE - 1, tail, error))
                {
                    return false;
                }
                size_t at = tail.size() >= 22 ? tail.size() - 22 : 0;
                while (at > 0 && rd32(tail.data() + at) != 0x06054B50)
                {
                    at--;
                }
                if (rd32(tail.data() + at) != 0x06054B50)
                {
                    error = "The sprite pack's directory could not be found.";
                    return false;
                }
                u32 size = rd32(tail.data() + at + 12), offset = rd32(tail.data() + at + 16);
                if (!range(offset, u64(offset) + size - 1, cd, error))
                {
                    return false;
                }
                files::mkdirs("sdmc:/3ds/PKForge/cache");
                files::write(CD_CACHE, cd.data(), cd.size());
            }
            size_t p = 0;
            while (p + 46 <= cd.size() && rd32(cd.data() + p) == 0x02014B50)
            {
                Entry e;
                e.method     = rd16(cd.data() + p + 10);
                e.compressed = rd32(cd.data() + p + 20);
                e.size       = rd32(cd.data() + p + 24);
                u16 nl = rd16(cd.data() + p + 28), el = rd16(cd.data() + p + 30), cl = rd16(cd.data() + p + 32);
                e.offset = rd32(cd.data() + p + 42);
                entries[std::string(reinterpret_cast<const char*>(cd.data() + p + 46), nl)] = e;
                p += 46 + nl + el + cl;
            }
            loaded = !entries.empty();
            if (!loaded)
            {
                error = "The sprite pack's directory is empty.";
            }
            return loaded;
        }

        // A file's bytes, pulled out of the archive.
        bool unpack(const std::string& name, std::vector<u8>& out, std::string& error)
        {
            auto it = entries.find(name);
            if (it == entries.end())
            {
                error = "Not in the pack.";
                return false;
            }
            const Entry e = it->second;
            std::vector<u8> raw;
            if (!range(e.offset, u64(e.offset) + 30 + 512 + e.compressed, raw, error) || raw.size() < 30)
            {
                return false;
            }
            size_t start = 30 + rd16(raw.data() + 26) + rd16(raw.data() + 28);
            if (raw.size() < start + e.compressed)
            {
                error = "Cut short.";
                return false;
            }
            out.assign(e.size, 0);
            if (e.method == 0)
            {
                memcpy(out.data(), raw.data() + start, e.size);
                return true;
            }
            z_stream z{};
            inflateInit2(&z, -15);
            z.next_in   = raw.data() + start;
            z.avail_in  = e.compressed;
            z.next_out  = out.data();
            z.avail_out = e.size;
            int r       = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (r != Z_STREAM_END)
            {
                error = "Could not be unpacked.";
                return false;
            }
            return true;
        }

        // Fitted into SIZE x SIZE (keeping the shape, centred, alpha kept) by box filtering.
        void fit(const u8* px, int w, int h, u8* out)
        {
            memset(out, 0, SIZE * SIZE * 4);
            float scale = std::min(1.0f, std::min(float(SIZE) / w, float(SIZE) / h));
            int ow = std::max(1, int(w * scale)), oh = std::max(1, int(h * scale));
            int ox = (SIZE - ow) / 2, oy = (SIZE - oh) / 2;
            for (int y = 0; y < oh; y++)
            {
                for (int x = 0; x < ow; x++)
                {
                    int x0 = x * w / ow, x1 = std::max(x0 + 1, (x + 1) * w / ow);
                    int y0 = y * h / oh, y1 = std::max(y0 + 1, (y + 1) * h / oh);
                    u32 s[4] = {}, count = 0;
                    for (int yy = y0; yy < y1; yy++)
                    {
                        for (int xx = x0; xx < x1; xx++)
                        {
                            const u8* p = px + (yy * w + xx) * 4;
                            for (int c = 0; c < 4; c++)
                            {
                                s[c] += p[c];
                            }
                            count++;
                        }
                    }
                    u8* d = out + ((oy + y) * SIZE + ox + x) * 4;
                    for (int c = 0; c < 4; c++)
                    {
                        d[c] = u8(s[c] / std::max<u32>(count, 1));
                    }
                }
            }
        }

        bool fetchHome(const std::string& name, std::vector<u8>& rgba, std::string& error)
        {
            std::vector<u8> file;
            if (!unpack("home/" + name, file, error))
            {
                return false;
            }
            // The pack's "PNG" renders are WebP pictures (libwebp, BSD); a real PNG goes through stb_image.
            int w = 0, h = 0, n = 0;
            bool webp = file.size() > 12 && memcmp(file.data(), "RIFF", 4) == 0 && memcmp(file.data() + 8, "WEBP", 4) == 0;
            u8* px    = webp ? WebPDecodeRGBA(file.data(), file.size(), &w, &h)
                             : stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4);
            if (!px)
            {
                error = "The render could not be read.";
                return false;
            }
            rgba.assign(SIZE * SIZE * 4, 0);
            fit(px, w, h, rgba.data());
            webp ? WebPFree(px) : stbi_image_free(px);
            return true;
        }

        bool fetchAnim(const std::string& name, Animation& a, std::string& error)
        {
            std::vector<u8> file;
            if (!unpack("showdown/" + name, file, error))
            {
                return false;
            }
            int w = 0, h = 0, frames = 0, n = 0;
            int* delays = nullptr;
            u8* px      = stbi_load_gif_from_memory(file.data(), int(file.size()), &delays, &w, &h, &frames, &n, 4);
            if (!px || frames <= 0)
            {
                error = "The animation could not be read.";
                return false;
            }
            int keep = std::min(frames, MAX_FRAMES);
            a.frames = keep;
            a.pixels.assign(size_t(keep) * SIZE * SIZE * 4, 0);
            a.delays.assign(size_t(keep), 0);
            for (int f = 0; f < keep; f++)
            {
                fit(px + size_t(f) * w * h * 4, w, h, a.pixels.data() + size_t(f) * SIZE * SIZE * 4);
                a.delays[size_t(f)] = u16(std::clamp(delays ? delays[f] : 50, 20, 500));
            }
            stbi_image_free(px);
            free(delays);
            return true;
        }

        std::string fileOf(const std::string& kind, const std::string& name)
        {
            return std::string(ROOT) + "/" + kind + "/" + name.substr(0, name.rfind('.')) + ".bin";
        }

        void work(void*)
        {
            while (running)
            {
                std::string job;
                LightLock_Lock(&lock);
                if (!queue.empty())
                {
                    job = queue.front();
                    queue.pop_front();
                }
                LightLock_Unlock(&lock);
                if (job.empty())
                {
                    svcSleepThread(100 * 1000 * 1000);
                    continue;
                }
                std::string error;
                bool anim = job.rfind("showdown/", 0) == 0;
                std::string name = job.substr(job.find('/') + 1);
                bool ok          = directory(error);
                std::vector<u8> rgba;
                Animation a;
                if (ok)
                {
                    ok = anim ? fetchAnim(name, a, error) : fetchHome(name, rgba, error);
                }
                if (ok)
                {
                    files::mkdirs(std::string(ROOT) + (anim ? "/showdown" : "/home"));
                    if (anim)
                    {
                        // frames, then each delay, then the pixels
                        std::vector<u8> out;
                        wr32(out, u32(a.frames));
                        for (u16 d : a.delays)
                        {
                            out.push_back(u8(d));
                            out.push_back(u8(d >> 8));
                        }
                        out.insert(out.end(), a.pixels.begin(), a.pixels.end());
                        // Kept zlib-packed: the frames are mostly transparent (2.9 MB becomes a few hundred KB).
                        uLongf zlen = compressBound(uLong(out.size()));
                        std::vector<u8> z(8 + zlen);
                        memcpy(z.data(), "ANZ1", 4);
                        u32 raw = u32(out.size());
                        memcpy(z.data() + 4, &raw, 4);
                        compress2(z.data() + 8, &zlen, out.data(), uLong(out.size()), 6);
                        files::write(fileOf("showdown", name), z.data(), 8 + zlen);
                    }
                    else
                    {
                        files::write(fileOf("home", name), rgba.data(), rgba.size());
                    }
                }
                LightLock_Lock(&lock);
                if (!ok)
                {
                    failed[job] = true;
                }
                else if (anim)
                {
                    if (anims.size() > 4)
                    {
                        anims.erase(anims.begin()); // a few animations in memory at most
                    }
                    anims[name] = std::move(a);
                }
                else
                {
                    images[name] = std::move(rgba);
                }
                if (totalAll)
                {
                    doneAll++;
                }
                LightLock_Unlock(&lock);
            }
        }

        void start()
        {
            if (worker)
            {
                return;
            }
            running = true;
            s32 prio = 0x30;
            svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
            worker = threadCreate(work, nullptr, 256 * 1024, prio + 1, -2, false);
        }

        bool queued(const std::string& job) { return failed.count(job) || std::find(queue.begin(), queue.end(), job) != queue.end(); }
    }

    Mode mode()
    {
        init();
        return current;
    }

    void mode(Mode m)
    {
        init();
        current = m;
        files::writeText(SETTING, m == Mode::Home ? "home" : m == Mode::Animated ? "animated" : "off");
    }

    bool enabled() { return mode() == Mode::Home; }
    void enabled(bool v) { mode(v ? Mode::Home : Mode::BuiltIn); }

    const u8* home(int species, bool shiny)
    {
        init();
        if (current != Mode::Home || species <= 0)
        {
            return nullptr;
        }
        std::string name = std::to_string(species) + (shiny ? "-s" : "") + ".png";
        LightLock_Lock(&lock);
        auto it = images.find(name);
        if (it != images.end())
        {
            LightLock_Unlock(&lock);
            return it->second.data();
        }
        bool skip = queued("home/" + name);
        LightLock_Unlock(&lock);
        if (skip)
        {
            return nullptr;
        }
        std::vector<u8> disk;
        if (files::read(fileOf("home", name), disk) && disk.size() == size_t(SIZE * SIZE * 4))
        {
            LightLock_Lock(&lock);
            auto& slot = images[name] = std::move(disk);
            LightLock_Unlock(&lock);
            return slot.data();
        }
        LightLock_Lock(&lock);
        queue.push_back("home/" + name);
        LightLock_Unlock(&lock);
        start();
        return nullptr;
    }

    const Animation* animated(int species, bool shiny)
    {
        init();
        if (current != Mode::Animated || species <= 0)
        {
            return nullptr;
        }
        std::string name = std::to_string(species) + (shiny ? "-s" : "") + ".gif";
        LightLock_Lock(&lock);
        auto it = anims.find(name);
        if (it != anims.end())
        {
            LightLock_Unlock(&lock);
            return &it->second;
        }
        bool skip = queued("showdown/" + name);
        LightLock_Unlock(&lock);
        if (skip)
        {
            return nullptr;
        }
        std::vector<u8> disk, packed;
        if (files::read(fileOf("showdown", name), packed) && packed.size() > 8 && memcmp(packed.data(), "ANZ1", 4) == 0)
        {
            u32 raw = 0;
            memcpy(&raw, packed.data() + 4, 4);
            disk.resize(raw);
            uLongf len = raw;
            if (uncompress(disk.data(), &len, packed.data() + 8, uLong(packed.size() - 8)) != Z_OK || len != raw)
            {
                disk.clear();
            }
        }
        if (disk.size() > 4)
        {
            Animation a;
            a.frames = int(rd32(disk.data()));
            size_t need = 4 + size_t(a.frames) * 2 + size_t(a.frames) * SIZE * SIZE * 4;
            if (a.frames > 0 && a.frames <= MAX_FRAMES && disk.size() == need)
            {
                for (int f = 0; f < a.frames; f++)
                {
                    a.delays.push_back(rd16(disk.data() + 4 + 2 * f));
                }
                a.pixels.assign(disk.begin() + long(4 + a.frames * 2), disk.end());
                LightLock_Lock(&lock);
                if (anims.size() > 4)
                {
                    anims.erase(anims.begin());
                }
                auto& slot = anims[name] = std::move(a);
                LightLock_Unlock(&lock);
                return &slot;
            }
        }
        LightLock_Lock(&lock);
        queue.push_back("showdown/" + name);
        LightLock_Unlock(&lock);
        start();
        return nullptr;
    }

    void downloadAll()
    {
        init();
        std::string error;
        if (!directory(error))
        {
            return;
        }
        std::string prefix = current == Mode::Animated ? "showdown/" : "home/";
        LightLock_Lock(&lock);
        totalAll = doneAll = 0;
        for (auto& [name, e] : entries)
        {
            if (name.rfind(prefix, 0) == 0 && !files::exists(fileOf(prefix.substr(0, prefix.size() - 1), name.substr(prefix.size()))))
            {
                queue.push_back(name);
                totalAll++;
            }
        }
        LightLock_Unlock(&lock);
        start();
    }

    std::string status()
    {
        init();
        LightLock_Lock(&lock);
        std::string s = totalAll ? std::to_string(doneAll) + " of " + std::to_string(totalAll) + " fetched" : "";
        LightLock_Unlock(&lock);
        return s;
    }

    void stop()
    {
        if (worker)
        {
            running = false;
            threadJoin(worker, U64_MAX);
            threadFree(worker);
            worker = nullptr;
        }
    }
}
