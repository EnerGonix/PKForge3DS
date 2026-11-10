#include "updater.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "json.hpp"
#include "version.hpp"
#include "widgets.hpp"

#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

using namespace gfx;

namespace updater
{
    namespace
    {
        constexpr const char* API   = "https://api.github.com/repos/EnerGonix/PKForge3DS/releases/latest";
        constexpr const char* ASSET = "PKForge.cia";
        constexpr const char* STAMP = "sdmc:/3ds/PKForge/update-check.txt";
        constexpr u64 SECOND        = 1000ULL * 1000 * 1000;
        constexpr u64 PKFORGE_TITLE = 0x000400000E4F0B00ULL;

        // ---------------------------------------------------------------- SHA-256 (FIPS 180-4)

        struct Sha256
        {
            u32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
            u8 block[64];
            size_t used = 0;
            u64 total   = 0;

            static u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }

            void compress(const u8* p)
            {
                static const u32 k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
                    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
                    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
                    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
                    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
                    0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624,
                    0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
                    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb,
                    0xbef9a3f7, 0xc67178f2};
                u32 w[64];
                for (int i = 0; i < 16; i++)
                {
                    w[i] = u32(p[4 * i]) << 24 | u32(p[4 * i + 1]) << 16 | u32(p[4 * i + 2]) << 8 | p[4 * i + 3];
                }
                for (int i = 16; i < 64; i++)
                {
                    u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i]   = w[i - 16] + s0 + w[i - 7] + s1;
                }
                u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
                for (int i = 0; i < 64; i++)
                {
                    u32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                    u32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                    hh     = g;
                    g      = f;
                    f      = e;
                    e      = d + t1;
                    d      = c;
                    c      = b;
                    b      = a;
                    a      = t1 + t2;
                }
                h[0] += a;
                h[1] += b;
                h[2] += c;
                h[3] += d;
                h[4] += e;
                h[5] += f;
                h[6] += g;
                h[7] += hh;
            }

            void add(const u8* data, size_t n)
            {
                total += n;
                while (n)
                {
                    size_t take = std::min(n, 64 - used);
                    memcpy(block + used, data, take);
                    used += take;
                    data += take;
                    n -= take;
                    if (used == 64)
                    {
                        compress(block);
                        used = 0;
                    }
                }
            }

            std::string hex()
            {
                u64 bits = total * 8;
                u8 pad   = 0x80;
                add(&pad, 1);
                pad = 0;
                while (used != 56)
                {
                    add(&pad, 1);
                }
                u8 len[8];
                for (int i = 0; i < 8; i++)
                {
                    len[i] = u8(bits >> (56 - 8 * i));
                }
                add(len, 8);
                char out[65];
                for (int i = 0; i < 8; i++)
                {
                    snprintf(out + 8 * i, 9, "%08lx", (unsigned long)h[i]);
                }
                return out;
            }
        };

        // ---------------------------------------------------------------- versions and the release

        // "3ds-v1.2.3", "v1.2.3" or "1.2.3" as {1, 2, 3}. The port's tags carry "3ds-" because the fork also
        // has the Android PKForge's own v1.x-v3.x tags.
        void parseVersion(const std::string& s, int v[3])
        {
            v[0] = v[1] = v[2] = 0;
            size_t at = s.compare(0, 4, "3ds-") == 0 ? 4 : 0;
            if (at < s.size() && (s[at] == 'v' || s[at] == 'V'))
            {
                at++;
            }
            sscanf(s.c_str() + at, "%d.%d.%d", &v[0], &v[1], &v[2]);
        }

        bool newer(const std::string& tag)
        {
            int a[3], b[3];
            parseVersion(tag, a);
            parseVersion(PORT_VERSION, b);
            for (int i = 0; i < 3; i++)
            {
                if (a[i] != b[i])
                {
                    return a[i] > b[i];
                }
            }
            return false;
        }

        struct Release
        {
            std::string tag, url, digest, error;
            u64 size = 0;
        };

        // The root certificates GitHub's servers chain to (romfs:/certs, exported from a PC's trusted store),
        // added to every request so the connection is verified: the 3DS's own list predates them.
        const std::vector<std::vector<u8>>& roots()
        {
            static std::vector<std::vector<u8>> certs;
            static bool loaded = false;
            if (!loaded)
            {
                loaded = true;
                files::list("romfs:/certs", [](const std::string& name, bool dir, u64)
                    {
                        std::vector<u8> der;
                        if (!dir && files::read("romfs:/certs/" + name, der) && !der.empty())
                        {
                            certs.push_back(std::move(der));
                        }
                    });
            }
            return certs;
        }

        // One verified GET through the system's HTTPS service, following GitHub's redirects. sink gets the body.
        bool get(std::string url, const std::function<bool(const u8*, u32)>& sink, std::string& error)
        {
            for (int hop = 0; hop < 5; hop++)
            {
                httpcContext ctx;
                if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_GET, url.c_str(), 0)))
                {
                    error = "No connection (is the Wi-Fi on?).";
                    return false;
                }
                for (const auto& der : roots())
                {
                    httpcAddTrustedRootCA(&ctx, der.data(), u32(der.size()));
                }
                httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
                httpcAddRequestHeaderField(&ctx, "User-Agent", "PKForge-3DS-updater");
                httpcAddRequestHeaderField(&ctx, "Accept", "*/*");
                u32 status = 0;
                Result res = httpcBeginRequest(&ctx);
                if (R_SUCCEEDED(res))
                {
                    res = httpcGetResponseStatusCodeTimeout(&ctx, &status, 30 * SECOND);
                }
                if (R_FAILED(res))
                {
                    httpcCloseContext(&ctx);
                    char text[120];
                    snprintf(text, sizeof(text), "Could not reach GitHub securely (%08lX). Is the Wi-Fi on?",
                        (unsigned long)res);
                    error = text;
                    return false;
                }
                if (status >= 300 && status < 400)
                {
                    char location[1024] = {};
                    httpcGetResponseHeader(&ctx, "Location", location, sizeof(location) - 1);
                    httpcCloseContext(&ctx);
                    if (strncmp(location, "https://", 8) != 0)
                    {
                        error = "GitHub sent the download somewhere unexpected.";
                        return false;
                    }
                    url = location;
                    continue;
                }
                if (status != 200)
                {
                    httpcCloseContext(&ctx);
                    error = "GitHub answered HTTP " + std::to_string(status) + ".";
                    return false;
                }
                std::vector<u8> chunk(0x10000);
                bool ok = true;
                for (;;)
                {
                    u32 got     = 0;
                    Result rr   = httpcDownloadData(&ctx, chunk.data(), u32(chunk.size()), &got);
                    if (got && !sink(chunk.data(), got))
                    {
                        ok    = false;
                        error = "Stopped.";
                        break;
                    }
                    if (rr != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
                    {
                        if (R_FAILED(rr))
                        {
                            ok    = false;
                            error = "The download broke off. Try again near the router.";
                        }
                        break;
                    }
                }
                httpcCloseContext(&ctx);
                return ok;
            }
            error = "Too many redirects.";
            return false;
        }

        Release latest()
        {
            Release r;
            std::string body;
            if (!get(API, [&](const u8* d, u32 n)
                    {
                        body.append(reinterpret_cast<const char*>(d), n);
                        return body.size() < 1024 * 1024;
                    },
                    r.error))
            {
                return r;
            }
            Json json;
            if (!Json::parse(body, json) || !json.isObject())
            {
                r.error = "GitHub's answer could not be read.";
                return r;
            }
            r.tag              = json.get("tag_name").asString();
            const Json& assets = json.get("assets");
            for (size_t i = 0; i < assets.size(); i++)
            {
                const Json& a = assets[i];
                if (a.get("name").asString() == ASSET)
                {
                    r.url    = a.get("browser_download_url").asString();
                    r.size   = u64(a.get("size").asNumber());
                    r.digest = a.get("digest").asString(); // "sha256:<hex>"
                }
            }
            if (r.tag.empty() || r.url.empty())
            {
                r.error = "The latest release has no " + std::string(ASSET) + ".";
            }
            return r;
        }

        // ---------------------------------------------------------------- the background check

        std::atomic<bool> checked{false};
        std::string found; // written by the thread before checked is set

        int today()
        {
            time_t now   = time(nullptr);
            struct tm* t = gmtime(&now);
            return (t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday;
        }

        void checkWork(void*)
        {
            if (R_SUCCEEDED(httpcInit(0)))
            {
                Release r = latest();
                httpcExit();
                if (r.error.empty())
                {
                    if (newer(r.tag))
                    {
                        found = r.tag;
                    }
                    files::writeText(STAMP, std::to_string(today()));
                }
            }
            checked = true;
        }

        // ---------------------------------------------------------------- the screen

        enum class State
        {
            Checking,
            UpToDate,
            Available,
            Installing,
            Done,
            Failed
        };

        struct Job
        {
            std::atomic<State> state{State::Checking};
            std::atomic<u64> got{0};
            std::atomic<bool> stop{false};
            Release release;
            std::string error;
            Thread thread = nullptr;
        };

        void checkJob(void* arg)
        {
            Job* j = static_cast<Job*>(arg);
            if (R_FAILED(httpcInit(0)))
            {
                j->error = "The 3DS's HTTP service did not start.";
                j->state = State::Failed;
                return;
            }
            j->release = latest();
            httpcExit();
            if (!j->release.error.empty())
            {
                j->error = j->release.error;
                j->state = State::Failed;
                return;
            }
            j->state = newer(j->release.tag) ? State::Available : State::UpToDate;
        }

        // The CIA goes straight into AM as it arrives; the install is finished only when the bytes match the
        // size and the SHA-256 GitHub publishes for the file, else it is cancelled and nothing changes.
        void installJob(void* arg)
        {
            Job* j = static_cast<Job*>(arg);
            if (R_FAILED(httpcInit(0)))
            {
                j->error = "The 3DS's HTTP service did not start.";
                j->state = State::Failed;
                return;
            }
            if (R_FAILED(amInit()))
            {
                httpcExit();
                j->error = "The 3DS's installer (AM) did not start.";
                j->state = State::Failed;
                return;
            }
            Handle cia = 0;
            Result res = AM_StartCiaInstall(MEDIATYPE_SD, &cia);
            if (R_FAILED(res))
            {
                amExit();
                httpcExit();
                char text[80];
                snprintf(text, sizeof(text), "The installer refused to start (%08lX).", (unsigned long)res);
                j->error = text;
                j->state = State::Failed;
                return;
            }
            Sha256 sha;
            u64 offset = 0;
            std::string error;
            bool ok = get(j->release.url, [&](const u8* d, u32 n)
                {
                    if (j->stop)
                    {
                        return false;
                    }
                    u32 written = 0;
                    if (R_FAILED(FSFILE_Write(cia, &written, offset, d, n, 0)) || written != n)
                    {
                        return false;
                    }
                    sha.add(d, n);
                    offset += n;
                    j->got = offset;
                    return true;
                },
                error);
            std::string digest = sha.hex();
            std::string want   = j->release.digest.rfind("sha256:", 0) == 0 ? j->release.digest.substr(7) : "";
            if (ok && j->release.size && offset != j->release.size)
            {
                ok    = false;
                error = "The download is incomplete. Try again.";
            }
            if (ok && want.empty())
            {
                ok    = false;
                error = "GitHub gave no checksum for the file, so it was not installed.";
            }
            if (ok && want != digest)
            {
                ok    = false;
                error = "The download does not match GitHub's checksum. Nothing was installed.";
            }
            if (ok)
            {
                res = AM_FinishCiaInstall(cia);
                if (R_FAILED(res))
                {
                    ok = false;
                    char text[80];
                    snprintf(text, sizeof(text), "The install did not finish (%08lX).", (unsigned long)res);
                    error = text;
                }
            }
            else
            {
                AM_CancelCIAInstall(cia);
            }
            amExit();
            httpcExit();
            j->error = error;
            j->state = ok ? State::Done : State::Failed;
        }

        class Update : public Screen
        {
        public:
            Update() { start(checkJob); }

            ~Update() override
            {
                job.stop = true;
                join();
            }

            bool overlay() const override { return true; }

            void update(Input& in) override
            {
                State s = job.state;
                if (s == State::Checking || s == State::Installing)
                {
                    if ((in.down & KEY_B) && s == State::Installing)
                    {
                        job.stop = true;
                    }
                    return;
                }
                join();
                if (s == State::Available && (in.down & KEY_A))
                {
                    job.got   = 0;
                    job.state = State::Installing;
                    start(installJob);
                    return;
                }
                if (s == State::Done && (in.down & KEY_A))
                {
                    // The new version starts as PKForge closes.
                    aptSetChainloader(PKFORGE_TITLE, MEDIATYPE_SD);
                    app::quit();
                    return;
                }
                if (in.down & KEY_B)
                {
                    app::pop();
                }
            }

            void drawTop() override {}

            void drawBottom() override
            {
                panel(10, 40, BOTTOM_W - 20, 150, T::Deck, T::FrameEdge);
                panelGrad(16, 46, BOTTOM_W - 32, 18, T::PartySelected, T::ChipTop, T::PartyRim);
                text(24, 49, "PKForge updates", T::Bright);
                std::string line1, line2;
                std::vector<std::pair<const char*, const char*>> keys;
                char buf[120];
                switch (State(job.state))
                {
                    case State::Checking: line1 = "Looking for a new version on GitHub..."; break;
                    case State::UpToDate:
                        line1 = "PKForge is up to date.";
                        line2 = "This is version " + std::string(PORT_VERSION) + ", the latest one.";
                        keys  = {{"B", "Close"}};
                        break;
                    case State::Available:
                        snprintf(buf, sizeof(buf), "Version %s is out (this is %s).", job.release.tag.c_str(), PORT_VERSION);
                        line1 = buf;
                        snprintf(buf, sizeof(buf), "%.1f MB from GitHub. Your saves, Bank and settings stay.",
                            double(job.release.size) / (1024 * 1024));
                        line2 = buf;
                        keys  = {{"A", "Install"}, {"B", "Not now"}};
                        break;
                    case State::Installing:
                    {
                        u64 got = job.got, all = job.release.size ? job.release.size : 1;
                        snprintf(buf, sizeof(buf), "Downloading and installing %s...", job.release.tag.c_str());
                        line1 = buf;
                        snprintf(buf, sizeof(buf), "%.1f / %.1f MB", double(got) / (1024 * 1024), double(all) / (1024 * 1024));
                        line2 = buf;
                        rect(24, 130, BOTTOM_W - 48, 10, T::Band);
                        rect(24, 130, float(BOTTOM_W - 48) * float(std::min(got, all)) / float(all), 10, T::Cyan);
                        keys = {{"B", "Stop"}};
                        break;
                    }
                    case State::Done:
                        line1 = "Installed " + job.release.tag + ".";
                        line2 = "PKForge restarts into the new version.";
                        keys  = {{"A", "Restart"}};
                        break;
                    case State::Failed:
                        line1 = "No update this time.";
                        line2 = job.error;
                        keys  = {{"B", "Close"}};
                        break;
                }
                wrap(24, 76, BOTTOM_W - 48, line1, T::Ink, 1, 2);
                wrap(24, 104, BOTTOM_W - 48, line2, T::Value, 1, 2);
                float x = 24;
                for (auto& [k, label] : keys)
                {
                    keyDisc(x, 160, k);
                    x += keyDiscWidth(k) + 4;
                    x += text(x, 160, label, T::Ink) + 14;
                }
            }

        private:
            Job job;

            void start(ThreadFunc f)
            {
                s32 prio = 0x30;
                svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
                job.thread = threadCreate(f, &job, 64 * 1024, prio + 1, -2, false);
                if (!job.thread)
                {
                    job.error = "Could not start the download.";
                    job.state = State::Failed;
                }
            }

            void join()
            {
                if (job.thread)
                {
                    threadJoin(job.thread, U64_MAX);
                    threadFree(job.thread);
                    job.thread = nullptr;
                }
            }
        };
    }

    void checkInBackground()
    {
        std::string text;
        if (files::readText(STAMP, text) && atoi(text.c_str()) == today())
        {
            checked = true;
            return;
        }
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        if (!threadCreate(checkWork, nullptr, 64 * 1024, prio + 2, -2, true))
        {
            checked = true;
        }
    }

    std::string available() { return checked ? found : std::string(); }

    std::unique_ptr<Screen> open() { return std::make_unique<Update>(); }
}
