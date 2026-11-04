#include "legality_online.hpp"
#include "app.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "json.hpp"
#include "widgets.hpp"

#include <3ds.h>
#include <atomic>
#include <cstring>

using namespace gfx;

namespace online
{
    namespace
    {
        constexpr const char* CONFIG = "sdmc:/3ds/PKForge/legality-server.txt";
        constexpr const char* BOUNDARY = "PKForgeBoundary7d2a91c4";
        constexpr u64 SECOND = 1000000000ULL;

        std::string base64Decode(const std::string& in)
        {
            static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            u32 acc = 0;
            int bits = 0;
            for (char c : in)
            {
                size_t v = chars.find(c);
                if (v == std::string::npos)
                {
                    continue; // '=' and whitespace
                }
                acc = (acc << 6) | u32(v);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back(char((acc >> bits) & 0xFF));
                }
            }
            return out;
        }

        // PKHeX's names for the generation of a Pokémon file (local-gpss Helpers.EntityContextFromString).
        std::string generationHeader(const pksm::PKX& pk)
        {
            switch (pk.generation())
            {
                case pksm::Generation::ONE: return "1";
                case pksm::Generation::TWO: return "2";
                case pksm::Generation::THREE: return "3";
                case pksm::Generation::FOUR: return "4";
                case pksm::Generation::FIVE: return "5";
                case pksm::Generation::SIX: return "6";
                case pksm::Generation::SEVEN: return "7";
                case pksm::Generation::EIGHT: return "8";
                case pksm::Generation::NINE: return "9";
                default: return "";
            }
        }

        struct Job
        {
            std::string url, generation, version;
            std::vector<u8> pokemon;
            pksm::Generation gen;
            Result result;
            std::atomic<bool> done{false};
            Thread thread = nullptr;
        };

        Result request(Job& job)
        {
            Result r;
            std::string body = std::string("--") + BOUNDARY +
                               "\r\nContent-Disposition: form-data; name=\"pkmn\"; filename=\"pkmn\"\r\n"
                               "Content-Type: application/octet-stream\r\n\r\n";
            body.append(job.pokemon.begin(), job.pokemon.end());
            body += std::string("\r\n--") + BOUNDARY + "--\r\n";
            std::vector<u32> words((body.size() + 3) / 4);
            memcpy(words.data(), body.data(), body.size());

            httpcContext ctx;
            if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_POST, job.url.c_str(), 0)))
            {
                r.error = "Could not reach " + job.url;
                return r;
            }
            httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
            httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
            httpcAddRequestHeaderField(&ctx, "User-Agent", "PKForge-3DS");
            httpcAddRequestHeaderField(&ctx, "generation", job.generation.c_str());
            httpcAddRequestHeaderField(&ctx, "version", job.version.c_str());
            std::string type = std::string("multipart/form-data; boundary=") + BOUNDARY;
            httpcAddRequestHeaderField(&ctx, "Content-Type", type.c_str());
            httpcAddPostDataRaw(&ctx, words.data(), u32(body.size()));
            u32 status = 0;
            ::Result res = httpcBeginRequest(&ctx);
            if (R_SUCCEEDED(res))
            {
                // PKSM gives auto-legalization two minutes.
                res = httpcGetResponseStatusCodeTimeout(&ctx, &status, 120 * SECOND);
            }
            std::string reply;
            if (R_SUCCEEDED(res))
            {
                std::vector<u8> chunk(0x4000);
                for (;;)
                {
                    u32 got = 0;
                    ::Result rr = httpcDownloadData(&ctx, chunk.data(), u32(chunk.size()), &got);
                    reply.append(reinterpret_cast<char*>(chunk.data()), got);
                    if (rr != (::Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
                    {
                        break;
                    }
                }
            }
            httpcCloseContext(&ctx);
            if (R_FAILED(res))
            {
                char text[120];
                snprintf(text, sizeof(text), "The legality server did not answer (%08lX). Is local-gpss running at %s?",
                    (unsigned long)res, job.url.c_str());
                r.error = text;
                return r;
            }
            Json json;
            if (!Json::parse(reply, json) || !json.isObject())
            {
                r.error = "The server's answer was not what local-gpss sends (HTTP " + std::to_string(status) + ").";
                return r;
            }
            if (!json.get("error").asString().empty())
            {
                r.error = "The server said: " + json.get("error").asString();
                return r;
            }
            r.legal = json.get("legal").asBool();
            r.ran   = json.get("ran").asBool();
            const Json& report = json.get("report");
            for (size_t i = 0; i < report.size(); i++)
            {
                if (!report[i].asString().empty())
                {
                    r.report.push_back(report[i].asString());
                }
            }
            std::string b64 = json.get("pokemon").asString();
            if (!b64.empty())
            {
                std::string raw = base64Decode(b64);
                std::vector<u8> bytes(raw.begin(), raw.end());
                r.pokemon = pksm::PKX::getPKM(job.gen, bytes.data(), bytes.size());
            }
            return r;
        }

        void worker(void* arg)
        {
            Job* job = static_cast<Job*>(arg);
            if (R_SUCCEEDED(httpcInit(0)))
            {
                job->result = request(*job);
                httpcExit();
            }
            else
            {
                job->result.error = "The 3DS HTTP service did not start.";
            }
            job->done = true;
        }

        class Waiting : public Screen
        {
        public:
            Waiting(std::unique_ptr<Job> j, std::function<void(Result&)> cb)
                : job(std::move(j)), done(std::move(cb)), started(osGetTime())
            {
            }

            bool overlay() const override { return true; }

            void update(Input&) override
            {
                if (!job->done)
                {
                    return;
                }
                threadJoin(job->thread, U64_MAX);
                threadFree(job->thread);
                auto cb  = std::move(done);
                auto own = std::move(job);
                app::pop();
                cb(own->result);
            }

            void drawTop() override {}

            void drawBottom() override
            {
                panel(X, Y, W, H, T::Deck, T::FrameEdge);
                vgrad(X + 1, Y + 1, W - 2, 18, T::Grid, T::Deck);
                text(X + 8, Y + 4, "Legalize with PKHeX", T::Ink, Left, 1, W - 16);
                char line[96];
                snprintf(line, sizeof(line), "Asking %s ...  %llu s", job ? job->url.c_str() : "",
                    (unsigned long long)((osGetTime() - started) / 1000));
                wrap(X + 10, Y + 30, W - 20, line, T::Value, 1, 4);
                wrap(X + 10, Y + 70, W - 20, "PKHeX can take up to two minutes on a hard one.", T::InkSoft, 1, 3);
            }

        private:
            static constexpr int X = 16, Y = 50, W = 288, H = 120;
            std::unique_ptr<Job> job;
            std::function<void(Result&)> done;
            u64 started;
        };
    }

    std::string server()
    {
        std::string url;
        files::readText(CONFIG, url);
        while (!url.empty() && (url.back() == '\n' || url.back() == '\r' || url.back() == ' '))
        {
            url.pop_back();
        }
        return url;
    }

    void setServer(const std::string& url)
    {
        std::string u = url;
        if (!u.empty() && u.rfind("http", 0) != 0)
        {
            u = "http://" + u;
        }
        if (!u.empty() && u.back() != '/')
        {
            u += '/';
        }
        files::writeText(CONFIG, u);
    }

    bool askServer()
    {
        std::string url = server();
        if (url.empty())
        {
            url = "http://192.168.1.";
        }
        if (!app::keyboard("local-gpss address, e.g. http://192.168.1.20:8080/", url, 120))
        {
            return false;
        }
        setServer(url);
        return true;
    }

    void legalize(const pksm::PKX& pk, u8 version, std::function<void(Result&)> done)
    {
        auto job        = std::make_unique<Job>();
        job->url        = server() + "api/v2/pksm/legalize";
        job->generation = generationHeader(pk);
        job->version    = std::to_string(version);
        job->gen        = pk.generation();
        auto raw        = pk.rawData();
        job->pokemon.assign(raw.begin(), raw.end());
        if (job->generation.empty())
        {
            Result r;
            r.error = "PKHeX's server takes Generation 1 to 9 Pokémon from the main games only.";
            done(r);
            return;
        }
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        Job* raw_job = job.get();
        job->thread  = threadCreate(worker, raw_job, 64 * 1024, prio + 1, -2, false);
        if (!job->thread)
        {
            Result r;
            r.error = "Could not start the request.";
            done(r);
            return;
        }
        app::push(std::make_unique<Waiting>(std::move(job), std::move(done)));
    }
}
