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

// The Android PKForge's sprite pack (SpritePackDownloader): HOME renders from sofianeelhor's pkforge-sprites.zip
// release. A 3DS does not need all 625 MB: the zip's directory is read once, then each render is pulled out of
// the archive on its own with an HTTP range request, unpacked, shrunk to SIZE x SIZE and kept on the SD card
// (sdmc:/3ds/PKForge/sprites/home). The animated Showdown sprites are left out: the 3DS cannot play them.
namespace spritepack
{
    namespace
    {
        constexpr const char* URL      = "https://github.com/sofianeelhor/PKForge/releases/download/sprites-1/pkforge-sprites.zip";
        constexpr u64 ZIP_SIZE         = 625017170;
        constexpr const char* DIR      = "sdmc:/3ds/PKForge/sprites/home";
        constexpr const char* CD_CACHE = "sdmc:/3ds/PKForge/cache/spritepack-cd.bin";
        constexpr const char* SETTING  = "sdmc:/3ds/PKForge/spritepack.txt";

        struct Entry
        {
            u32 offset = 0, compressed = 0, size = 0;
            u16 method = 0;
        };

        LightLock lock;
        bool lockReady = false, on = false, loaded = false;
        std::map<std::string, Entry> entries;
        std::map<std::string, std::vector<u8>> images; // name -> SIZE x SIZE RGBA
        std::deque<std::string> queue;
        std::map<std::string, bool> failed;
        Thread worker   = nullptr;
        bool running    = false;
        int doneAll = 0, totalAll = 0;

        void init()
        {
            if (!lockReady)
            {
                LightLock_Init(&lock);
                lockReady = true;
                std::string t;
                on = files::readText(SETTING, t) && t.rfind("on", 0) == 0;
            }
        }

        u16 rd16(const u8* p) { return u16(p[0] | p[1] << 8); }
        u32 rd32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

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
                std::string name(reinterpret_cast<const char*>(cd.data() + p + 46), nl);
                if (name.rfind("home/", 0) == 0)
                {
                    entries[name.substr(5)] = e;
                }
                p += 46 + nl + el + cl;
            }
            loaded = !entries.empty();
            if (!loaded)
            {
                error = "The sprite pack's directory is empty.";
            }
            return loaded;
        }

        // One render: its bytes out of the archive, unpacked, decoded and shrunk.
        bool fetch(const std::string& name, std::vector<u8>& rgba, std::string& error)
        {
            auto it = entries.find(name);
            if (it == entries.end())
            {
                error = "No such render.";
                return false;
            }
            const Entry e = it->second;
            std::vector<u8> raw;
            // The local header (30 bytes + name + extra), then the data.
            if (!range(e.offset, u64(e.offset) + 30 + 512 + e.compressed, raw, error) || raw.size() < 30)
            {
                return false;
            }
            u16 nl = rd16(raw.data() + 26), el = rd16(raw.data() + 28);
            size_t start = 30 + nl + el;
            if (raw.size() < start + e.compressed)
            {
                error = "The render was cut short.";
                return false;
            }
            std::vector<u8> png(e.size);
            if (e.method == 0)
            {
                memcpy(png.data(), raw.data() + start, e.size);
            }
            else
            {
                z_stream z{};
                inflateInit2(&z, -15);
                z.next_in   = raw.data() + start;
                z.avail_in  = e.compressed;
                z.next_out  = png.data();
                z.avail_out = e.size;
                int r       = inflate(&z, Z_FINISH);
                inflateEnd(&z);
                if (r != Z_STREAM_END)
                {
                    error = "The render could not be unpacked.";
                    return false;
                }
            }
            // The pack's "PNG" renders are WebP pictures (libwebp, BSD); a real PNG goes through stb_image.
            int w = 0, h = 0, n = 0;
            bool webp = png.size() > 12 && memcmp(png.data(), "RIFF", 4) == 0 && memcmp(png.data() + 8, "WEBP", 4) == 0;
            u8* px    = webp ? WebPDecodeRGBA(png.data(), png.size(), &w, &h)
                             : stbi_load_from_memory(png.data(), int(png.size()), &w, &h, &n, 4);
            if (!px)
            {
                error = "The render could not be read.";
                return false;
            }
            // Box-filter down to SIZE x SIZE, keeping the alpha.
            rgba.assign(SIZE * SIZE * 4, 0);
            for (int y = 0; y < SIZE; y++)
            {
                for (int x = 0; x < SIZE; x++)
                {
                    int x0 = x * w / SIZE, x1 = std::max(x0 + 1, (x + 1) * w / SIZE);
                    int y0 = y * h / SIZE, y1 = std::max(y0 + 1, (y + 1) * h / SIZE);
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
                    for (int c = 0; c < 4; c++)
                    {
                        rgba[size_t((y * SIZE + x) * 4 + c)] = u8(s[c] / std::max<u32>(count, 1));
                    }
                }
            }
            if (webp)
            {
                WebPFree(px);
            }
            else
            {
                stbi_image_free(px);
            }
            return true;
        }

        std::string fileOf(const std::string& name) { return std::string(DIR) + "/" + name.substr(0, name.rfind('.')) + ".bin"; }

        void work(void*)
        {
            while (running)
            {
                std::string name;
                LightLock_Lock(&lock);
                if (!queue.empty())
                {
                    name = queue.front();
                    queue.pop_front();
                }
                LightLock_Unlock(&lock);
                if (name.empty())
                {
                    svcSleepThread(100 * 1000 * 1000);
                    continue;
                }
                std::string error;
                std::vector<u8> rgba;
                bool ok = directory(error) && fetch(name, rgba, error);
                if (ok)
                {
                    files::mkdirs(DIR);
                    files::write(fileOf(name), rgba.data(), rgba.size());
                }
                LightLock_Lock(&lock);
                if (ok)
                {
                    images[name] = std::move(rgba);
                }
                else
                {
                    failed[name] = true;
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
    }

    bool enabled()
    {
        init();
        return on;
    }

    void enabled(bool v)
    {
        init();
        on = v;
        files::writeText(SETTING, v ? "on" : "off");
    }

    const u8* home(int species, bool shiny)
    {
        init();
        if (!on || species <= 0)
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
        bool skip = failed.count(name) || std::find(queue.begin(), queue.end(), name) != queue.end();
        LightLock_Unlock(&lock);
        if (skip)
        {
            return nullptr;
        }
        std::vector<u8> disk;
        if (files::read(fileOf(name), disk) && disk.size() == size_t(SIZE * SIZE * 4))
        {
            LightLock_Lock(&lock);
            auto& slot = images[name] = std::move(disk);
            LightLock_Unlock(&lock);
            return slot.data();
        }
        LightLock_Lock(&lock);
        queue.push_back(name);
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
        LightLock_Lock(&lock);
        totalAll = doneAll = 0;
        for (auto& [name, e] : entries)
        {
            if (!files::exists(fileOf(name)))
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
        std::string s = totalAll ? std::to_string(doneAll) + " of " + std::to_string(totalAll) + " renders fetched" : "";
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
