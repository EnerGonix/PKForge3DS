#include "collection.hpp"
#include "app.hpp"
#include "bank.hpp"
#include "files.hpp"
#include "json.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "updater.hpp"

#include <algorithm>
#include <ctime>
#include <memory>

// The Android PKForge's Collection Center (CommunityBoxService): RoC's PC, the community's event collections on
// GitHub (ReignOfComputer/RoCs-PC), browsed folder by folder; a file or a whole folder goes into the Bank.
// Listings are kept a week in sdmc:/3ds/PKForge/cache/community, as the original keeps them.
namespace collection
{
    namespace
    {
        constexpr const char* REPO  = "ReignOfComputer/RoCs-PC";
        constexpr const char* CACHE = "sdmc:/3ds/PKForge/cache/community";
        const char* EXTENSIONS[]    = {".pk1", ".pk2", ".pk3", ".pk4", ".pk5", ".pk6", ".pk7", ".pk8", ".pk9", ".pb7",
            ".pa8", ".pa9"};

        struct Node
        {
            std::string name, path, url;
            bool dir = false;
        };

        bool isEntity(const std::string& name)
        {
            std::string low = files::lower(name);
            for (const char* e : EXTENSIONS)
            {
                size_t n = strlen(e);
                if (low.size() > n && low.compare(low.size() - n, n, e) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        std::string escape(const std::string& path)
        {
            std::string out;
            for (unsigned char c : path)
            {
                if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                {
                    out += char(c);
                }
                else
                {
                    char b[4];
                    snprintf(b, sizeof(b), "%%%02X", c);
                    out += b;
                }
            }
            return out;
        }

        std::string cacheFile(const std::string& path)
        {
            u32 h = 2166136261u;
            for (unsigned char c : path)
            {
                h = (h ^ c) * 16777619u;
            }
            char b[24];
            snprintf(b, sizeof(b), "%08lx.json", (unsigned long)h);
            return std::string(CACHE) + "/" + b;
        }

        bool listing(const std::string& path, std::vector<Node>& out, std::string& error)
        {
            std::string text;
            std::string cache = cacheFile(path);
            bool fresh        = false;
            if (files::readText(cache, text))
            {
                size_t nl = text.find('\n');
                long when = atol(text.c_str());
                fresh     = nl != std::string::npos && time(nullptr) - when < 7 * 86400;
                if (fresh)
                {
                    text = text.substr(nl + 1);
                }
            }
            if (!fresh)
            {
                std::vector<u8> body;
                std::string url = std::string("https://api.github.com/repos/") + REPO + "/contents/" + escape(path);
                if (!updater::fetch(url, body, error))
                {
                    if (text.empty())
                    {
                        return false;
                    }
                    size_t nl = text.find('\n');
                    text      = nl == std::string::npos ? "" : text.substr(nl + 1); // offline: the older listing
                }
                else
                {
                    text.assign(body.begin(), body.end());
                    files::mkdirs(CACHE);
                    files::writeText(cache, std::to_string(long(time(nullptr))) + "\n" + text);
                }
            }
            Json j;
            if (!Json::parse(text, j) || !j.isArray())
            {
                error = "GitHub's answer could not be read.";
                return false;
            }
            for (const Json& e : j.items())
            {
                Node n;
                n.name = e.get("name").asString();
                n.path = e.get("path").asString();
                n.dir  = e.get("type").asString() == "dir";
                n.url  = e.get("download_url").asString();
                if (n.dir || isEntity(n.name))
                {
                    out.push_back(n);
                }
            }
            std::stable_sort(out.begin(), out.end(), [](const Node& a, const Node& b)
                {
                    if (a.dir != b.dir)
                    {
                        return a.dir;
                    }
                    return files::lower(a.name) < files::lower(b.name);
                });
            return true;
        }

        // Into the first empty Bank slots.
        bool deposit(const Node& n, std::string& error)
        {
            std::vector<u8> bytes;
            if (!updater::fetch(n.url, bytes, error))
            {
                return false;
            }
            size_t dot         = n.name.rfind('.');
            std::string format = pkdata::formatFromExtension(n.name.substr(dot));
            auto pk            = pkdata::fromBytes(format, bytes.data(), bytes.size());
            if (!pk)
            {
                error = n.name + " could not be read.";
                return false;
            }
            Bank& bank = app::bank();
            for (int b = 0; b < bank.boxes() + 1; b++)
            {
                for (int s = 0; s < Bank::SLOTS; s++)
                {
                    if (!bank.at(b, s))
                    {
                        return bank.put(*pk, b, s, "RoC's PC", error);
                    }
                }
            }
            error = "The Bank is full.";
            return false;
        }

        void open(const std::string& path);

        void folderMenu(const std::string& path, std::shared_ptr<std::vector<Node>> nodes)
        {
            std::vector<screens::PickItem> items;
            int files = 0;
            for (const Node& n : *nodes)
            {
                files += !n.dir;
            }
            if (files)
            {
                items.push_back({-1, "Deposit all " + std::to_string(files) + " Pokémon here into the Bank", ""});
            }
            for (int i = 0; i < int(nodes->size()); i++)
            {
                const Node& n = (*nodes)[size_t(i)];
                items.push_back({i, (n.dir ? "[Folder]  " : "") + n.name, ""});
            }
            std::string title = path.empty() ? "RoC's PC" : files::baseName(path);
            app::push(screens::picker(title, std::move(items), -1, [path, nodes](int i)
                {
                    if (i == -1)
                    {
                        int done = 0;
                        std::string error;
                        for (const Node& n : *nodes)
                        {
                            if (!n.dir)
                            {
                                if (!deposit(n, error))
                                {
                                    break;
                                }
                                done++;
                            }
                        }
                        app::message("Collection Center", std::to_string(done) + " Pokémon went into the Bank." +
                                                              (error.empty() ? "" : "\n" + error));
                        return;
                    }
                    const Node n = (*nodes)[size_t(i)];
                    if (n.dir)
                    {
                        open(n.path);
                        return;
                    }
                    app::confirm("Put " + n.name + " in the Bank?", [n]()
                        {
                            std::string error;
                            if (deposit(n, error))
                            {
                                app::toast(n.name + " is in the Bank.");
                            }
                            else
                            {
                                app::message("Collection Center", error);
                            }
                        },
                        "Deposit", "Cancel");
                }));
        }

        void open(const std::string& path)
        {
            auto nodes = std::make_shared<std::vector<Node>>();
            std::string error;
            if (!listing(path, *nodes, error))
            {
                app::message("Collection Center", error.empty() ? "RoC's PC could not be reached." : error);
                return;
            }
            folderMenu(path, nodes);
        }
    }

    void open() { open(std::string()); }
}
