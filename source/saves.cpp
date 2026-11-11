#include "saves.hpp"
#include "romhack.hpp"
#include "gamecube.hpp"
#include "container.hpp"
#include "app.hpp"
#include "files.hpp"
#include "input.hpp"
#include "pkdata.hpp"
#include "titles.hpp"

#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace
{
    LightLock lock;
    bool lockReady = false;
    std::vector<SaveRef> results;
    std::atomic<bool> busy{false};
    std::atomic<bool> stopping{false};
    std::atomic<int> folders{0};
    Thread worker = nullptr;

    // What earlier scans learnt about each file, so a file that has not changed (same size and time) is not
    // read and parsed again: saves keep their facts, other files are remembered as not saves.
    constexpr const char* CACHE = "sdmc:/3ds/PKForge/cache/saves.txt";
    struct Known
    {
        u32 size = 0;
        u64 mtime = 0;
        bool save = false;
        SaveRef ref;
    };
    std::unordered_map<std::string, Known> known, seen; // from the last scan, and from this one
    std::vector<std::string> touched;                   // written or deleted by PKForge since it started

    bool wasTouched(const std::string& path)
    {
        LightLock_Lock(&lock);
        bool yes = std::find(touched.begin(), touched.end(), path) != touched.end();
        LightLock_Unlock(&lock);
        return yes;
    }

    void loadKnown()
    {
        known.clear();
        FILE* f = fopen(CACHE, "r");
        if (!f)
        {
            return;
        }
        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\r\n")] = 0;
            std::vector<std::string> v;
            for (char *at = line, *tab; at; at = tab ? tab + 1 : nullptr)
            {
                tab = strchr(at, '\t');
                v.emplace_back(at, tab ? size_t(tab - at) : strlen(at));
            }
            if (v.size() != 15 && v.size() != 16)
            {
                continue;
            }
            Known k;
            k.size        = u32(strtoul(v[1].c_str(), nullptr, 10));
            k.mtime       = strtoull(v[2].c_str(), nullptr, 10);
            k.save        = v[3] == "1";
            k.ref.path    = v[0];
            k.ref.size    = k.size;
            k.ref.version = u8(atoi(v[4].c_str()));
            k.ref.gen     = atoi(v[5].c_str());
            k.ref.tid     = u32(strtoul(v[6].c_str(), nullptr, 10));
            k.ref.hours   = atoi(v[7].c_str());
            k.ref.minutes = atoi(v[8].c_str());
            k.ref.seconds = atoi(v[9].c_str());
            k.ref.caught  = atoi(v[10].c_str());
            k.ref.seen    = atoi(v[11].c_str());
            k.ref.badges  = atoi(v[12].c_str());
            k.ref.source  = v[13];
            k.ref.ot      = v[14];
            k.ref.label   = v.size() > 15 ? v[15] : "";
            if (k.save && !pkdata::inScope(k.ref.gen, k.ref.version))
            {
                k.save = false; // a Let's Go or Switch save found by an older scan: not listed any more
            }
            known[v[0]]   = k;
        }
        fclose(f);
    }

    void saveKnown()
    {
        files::mkdirs("sdmc:/3ds/PKForge/cache");
        std::string temp = std::string(CACHE) + ".part";
        FILE* f          = fopen(temp.c_str(), "w");
        if (!f)
        {
            return;
        }
        auto clean = [](std::string s)
        {
            for (char& c : s)
            {
                c = c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
            }
            return s;
        };
        for (auto& [path, k] : seen)
        {
            const SaveRef& r = k.ref;
            fprintf(f, "%s\t%lu\t%llu\t%d\t%d\t%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", path.c_str(),
                (unsigned long)k.size, (unsigned long long)k.mtime, k.save ? 1 : 0, int(r.version), r.gen,
                (unsigned long)r.tid, r.hours, r.minutes, r.seconds, r.caught, r.seen, r.badges, clean(r.source).c_str(),
                clean(r.ot).c_str(), clean(r.label).c_str());
        }
        fclose(f);
        remove(CACHE);
        rename(temp.c_str(), CACHE);
    }

    // A folder of the SD card through the FS service, 64 entries per call and with their sizes: libc's
    // readdir + stat opens every file just to learn its size.
    FS_Archive sdArchive = 0;
    bool sdOpen          = false;

    bool listSd(const std::string& dir, const std::function<void(const std::string&, bool, u64)>& fn)
    {
        if (!sdOpen)
        {
            sdOpen = R_SUCCEEDED(FSUSER_OpenArchive(&sdArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")));
        }
        if (!sdOpen || dir.compare(0, 5, "sdmc:") != 0)
        {
            return false;
        }
        std::string inner = dir.substr(5);
        if (inner.empty())
        {
            inner = "/";
        }
        std::vector<u16> wide(inner.size() + 1, 0);
        ssize_t units = utf8_to_utf16(wide.data(), (const u8*)inner.c_str(), inner.size());
        if (units < 0)
        {
            return false;
        }
        wide.resize(size_t(units) + 1);
        wide[size_t(units)] = 0;
        FS_Path path{PATH_UTF16, u32(wide.size() * 2), wide.data()};
        Handle handle;
        if (R_FAILED(FSUSER_OpenDirectory(&handle, sdArchive, path)))
        {
            return false;
        }
        static FS_DirectoryEntry entries[64];
        u32 got = 0;
        char name[0x106 * 3 + 1];
        while (!stopping && R_SUCCEEDED(FSDIR_Read(handle, &got, 64, entries)) && got > 0)
        {
            for (u32 i = 0; i < got; i++)
            {
                ssize_t n = utf16_to_utf8((u8*)name, entries[i].name, sizeof(name) - 1);
                name[n < 0 ? 0 : n] = 0;
                fn(name, entries[i].attributes & FS_ATTRIBUTE_DIRECTORY, entries[i].fileSize);
            }
        }
        FSDIR_Close(handle);
        return true;
    }

    // Folders that never hold game saves, or hold encrypted 3DS data, are not walked.
    bool skipFolder(const std::string& path, const std::string& name)
    {
        static const char* names[] = {"nintendo 3ds", "dcim", "private", "luma", "gm9", "cias", "updates",
            "nintendo dsiware", "_nds", "_pico", "_gba", "themes", "music", "movies", "videos", "photos",
            "boot", "system volume information"};
        std::string low = files::lower(name);
        if (low.empty() || low[0] == '.')
        {
            return true;
        }
        for (auto n : names)
        {
            if (low == n)
            {
                return true;
            }
        }
        std::string lowPath = files::lower(path);
        return lowPath == "sdmc:/3ds/pkforge/backups" || lowPath == "sdmc:/3ds/pkforge/bank" ||
               lowPath == "sdmc:/3ds/pkforge/shots";
    }

    bool candidate(const std::string& name, u64 size)
    {
        if (size < 0x2000 || size > 0x400000)
        {
            return false;
        }
        std::string low = files::lower(name);
        if (low == "main" || low == "backup")
        {
            return true;
        }
        size_t dot = low.find_last_of('.');
        if (dot == std::string::npos)
        {
            return false;
        }
        std::string ext = low.substr(dot + 1);
        static const char* exts[] = {"sav", "srm", "dsv", "sa1", "sa2", "sa3", "fla", "sram", "sv", "bak", "duc"};
        for (auto e : exts)
        {
            if (ext == e)
            {
                return true;
            }
        }
        return false;
    }

    std::string sourceOf(const std::string& path)
    {
        std::string low = files::lower(path);
        struct
        {
            const char* part;
            const char* name;
        } known[] = {{"/retroarch/", "RetroArch"}, {"/mgba/", "mGBA"}, {"/3ds/mgba", "mGBA"},
            {"/open_agb_firm/", "open_agb_firm"}, {"/roms/", "TWiLight Menu++"}, {"/3ds/checkpoint/", "Checkpoint"},
            {"/jksv/", "JKSV"}, {"/3ds/pksm/", "PKSM"}, {"/3ds/firered3ds/", "FireRed 3DS"},
            {"/3ds/fireredduplo/", "FireRed Duplo"}, {"/3ds/emerald3ds/", "Emerald 3DS"},
            {"/3ds/pkforge/saves", "PKForge"}, {"/gameyob/", "GameYob"}, {"/3ds/gbarunner", "GBARunner"}};
        for (auto& k : known)
        {
            if (low.find(k.part) != std::string::npos)
            {
                return k.name;
            }
        }
        std::string dir = files::dirName(path);
        std::string folder = files::baseName(dir);
        return folder.empty() ? "SD card" : folder;
    }

    void walk(const std::string& dir, int depth)
    {
        if (stopping)
        {
            return;
        }
        folders++;
        std::vector<std::pair<std::string, u64>> entries;
        std::vector<std::string> subdirs;
        auto add = [&](const std::string& name, bool folder, u64 size)
            {
                std::string full = dir + (dir.back() == '/' ? "" : "/") + name;
                if (folder)
                {
                    if (depth < 6 && !skipFolder(full, name))
                    {
                        subdirs.push_back(full);
                    }
                }
                else if (candidate(name, size))
                {
                    entries.emplace_back(full, size);
                }
            };
        if (!listSd(dir, add))
        {
            files::list(dir, add);
        }
        for (auto& [path, size] : entries)
        {
            if (stopping)
            {
                return;
            }
            Known k;
            k.size = u32(size);
            archive_getmtime(path.c_str(), &k.mtime);
            auto old = known.find(path);
            if (old != known.end() && old->second.size == k.size && old->second.mtime == k.mtime && k.mtime != 0 &&
                !wasTouched(path))
            {
                k = old->second; // unchanged since the last scan
            }
            else
            {
                k.save = saves::describe(path, k.ref);
            }
            k.ref.path = path;
            seen[path] = k;
            if (k.save)
            {
                LightLock_Lock(&lock);
                results.push_back(k.ref);
                LightLock_Unlock(&lock);
            }
        }
        for (auto& sub : subdirs)
        {
            walk(sub, depth + 1);
        }
    }

    void scanThread(void*)
    {
        // The games' own saves first: they are what most people come to edit.
        titles::scan([](const SaveRef& ref)
            {
                LightLock_Lock(&lock);
                results.push_back(ref);
                LightLock_Unlock(&lock);
            });
        loadKnown();
        seen.clear();
        walk("sdmc:/", 0);
        if (!stopping)
        {
            saveKnown();
        }
        known.clear();
        seen.clear();
        busy = false;
    }
}

namespace saves
{
    bool describe(const std::string& path, SaveRef& out)
    {
        std::vector<u8> bytes;
        if (!files::read(path, bytes) || !describeBytes(bytes, out))
        {
            return false;
        }
        out.path   = path;
        out.source = sourceOf(path);
        return true;
    }

    bool describeBytes(const std::vector<u8>& file, SaveRef& out)
    {
        std::vector<u8> bytes;
        container::unwrap(file, bytes);
        if (bytes.empty())
        {
            return false;
        }
        std::shared_ptr<u8[]> data(new u8[bytes.size()]);
        memcpy(data.get(), bytes.data(), bytes.size());
        std::unique_ptr<pksm::Sav> sav;
        sav = romhack::load(data, u32(bytes.size()));
        try
        {
            if (!sav)
            {
                sav = pksm::Sav::getSave(data, bytes.size());
            }
        }
        catch (...)
        {
            sav = nullptr;
        }
        if (!sav)
        {
            sav = gamecube::load(data, u32(bytes.size()));
        }
        if (!sav)
        {
            return false;
        }
        out.size    = bytes.size();
        out.version = gamecube::is(*sav) ? 15 : u8(sav->version());
        out.label   = romhack::is(*sav) ? romhack::name(romhack::hackOf(*sav)) : "";
        out.gen     = pkdata::genNumber(sav->generation());
        if (!pkdata::inScope(out.gen, out.version))
        {
            return false; // Let's Go and the Switch games are not covered
        }
        out.ot      = sav->otName();
        out.tid     = sav->displayTID();
        out.hours   = sav->playedHours();
        out.minutes = sav->playedMinutes();
        out.seconds = sav->playedSeconds();
        out.badges  = sav->badges();
        try
        {
            out.caught = sav->dexCaught();
            out.seen   = sav->dexSeen();
        }
        catch (...)
        {
        }
        return true;
    }

    void startScan()
    {
        if (!lockReady)
        {
            LightLock_Init(&lock);
            lockReady = true;
        }
        if (busy)
        {
            return;
        }
        if (worker)
        {
            threadJoin(worker, U64_MAX);
            threadFree(worker);
            worker = nullptr;
        }
        LightLock_Lock(&lock);
        results.clear();
        LightLock_Unlock(&lock);
        folders = 0;
        busy    = true;
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        worker = threadCreate(scanThread, nullptr, 128 * 1024, prio + 1, -2, false);
        if (!worker)
        {
            busy = false;
        }
    }

    bool scanning() { return busy; }

    void forget(const std::string& path)
    {
        LightLock_Lock(&lock);
        std::erase_if(results, [&](const SaveRef& r) { return !r.isTitle() && r.path == path; });
        LightLock_Unlock(&lock);
        changed(path);
    }

    void changed(const std::string& path)
    {
        if (!lockReady)
        {
            LightLock_Init(&lock);
            lockReady = true;
        }
        LightLock_Lock(&lock);
        touched.push_back(path);
        LightLock_Unlock(&lock);
        // Out of the scan cache now too, in case PKForge closes before the next scan.
        std::vector<u8> bytes;
        if (busy || !files::read(CACHE, bytes))
        {
            return;
        }
        std::string text(bytes.begin(), bytes.end()), kept, prefix = path + "\t";
        for (size_t at = 0; at < text.size();)
        {
            size_t end = text.find('\n', at);
            end        = end == std::string::npos ? text.size() : end + 1;
            if (text.compare(at, prefix.size(), prefix) != 0)
            {
                kept.append(text, at, end - at);
            }
            at = end;
        }
        files::write(CACHE, (const u8*)kept.data(), kept.size());
    }

    void stopScan()
    {
        if (!worker)
        {
            return;
        }
        stopping = true;
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker   = nullptr;
        stopping = false;
        busy     = false;
    }

    int foldersSeen() { return folders; }

    std::vector<SaveRef> found()
    {
        if (!lockReady)
        {
            return {};
        }
        LightLock_Lock(&lock);
        auto copy = results;
        LightLock_Unlock(&lock);
        return copy;
    }
}

std::unique_ptr<OpenSave> OpenSave::open(const SaveRef& ref, std::string& error)
{
    app::Timed timed("open save");
    std::vector<u8> bytes;
    if (ref.isTitle())
    {
        if (!titles::read(ref, bytes, error))
        {
            return nullptr;
        }
    }
    else if (!files::read(ref.path, bytes))
    {
        error = "Could not read " + ref.path;
        return nullptr;
    }
    if (bytes.empty())
    {
        error = "The save is empty.";
        return nullptr;
    }
    auto out  = std::make_unique<OpenSave>();
    out->ref  = ref;
    {
        std::vector<u8> inner;
        if (!ref.isTitle() && container::unwrap(bytes, inner))
        {
            out->container = bytes;
            bytes.swap(inner);
        }
    }
    out->size = bytes.size();
    out->data = std::shared_ptr<u8[]>(new u8[bytes.size()]);
    memcpy(out->data.get(), bytes.data(), bytes.size());
    out->sav = romhack::load(out->data, out->size);
    try
    {
        if (!out->sav)
        {
            out->sav = pksm::Sav::getSave(out->data, out->size);
        }
    }
    catch (...)
    {
        out->sav = nullptr;
    }
    if (!out->sav)
    {
        out->sav = gamecube::load(out->data, out->size);
    }
    if (!out->sav)
    {
        error = "PKSM-Core does not recognise this save.";
        return nullptr;
    }
    if (!pkdata::inScope(pkdata::genNumber(out->sav->generation()), int(out->sav->version())))
    {
        error = "PKForge 3DS covers the games up to Ultra Sun and Ultra Moon.";
        return nullptr;
    }
    return out;
}

bool OpenSave::write(std::string& error)
{
    bool testSave =
        ref.isTitle() ? ref.titleId == titles::TEST_TITLE : ref.path.rfind(autotest::TEST_SAVES, 0) == 0;
    if (autotest::active() && !testSave)
    {
        // A test script never writes a real save, whatever the screen it reached.
        error = "Test scripts only write saves in " + std::string(autotest::TEST_SAVES);
        return false;
    }
    if (!backups::make(ref, error))
    {
        return false;
    }
    sav->finishEditing();
    std::string failure;
    bool ok;
    if (ref.isTitle())
    {
        ok = titles::write(ref, data.get(), size, failure);
    }
    else if (!container.empty())
    {
        // The wrapper the file came in (RetroArch compression, chip padding, an RTC footer) goes back on.
        std::vector<u8> file = container::rewrap(data.get(), size, container);
        ok = files::write(ref.path, file.data(), file.size());
        if (ok)
        {
            container = file;
        }
    }
    else
    {
        ok = files::write(ref.path, data.get(), size);
    }
    sav->beginEditing();
    if (!ok)
    {
        error = (failure.empty() ? "Writing " + ref.path + " failed." : failure) + " A restore point was kept.";
        return false;
    }
    dirty = false;
    if (!ref.isTitle())
    {
        saves::changed(ref.path);
    }
    return true;
}

namespace backups
{
    std::string folderFor(const SaveRef& ref)
    {
        std::string root = std::string(files::ROOT) + "/backups/";
        if (ref.isTitle())
        {
            // A game's save: one folder per title, the cartridge and the installed copy apart.
            char name[48];
            snprintf(name, sizeof(name), "%s_%016llX", ref.media == MEDIATYPE_GAME_CARD ? "card" : "title",
                (unsigned long long)ref.titleId);
            return root + name;
        }
        // One folder per file, named by the file and its folder so two "main" files never mix.
        std::string dir  = files::baseName(files::dirName(ref.path));
        std::string name = files::baseName(ref.path);
        return root + files::sanitize(dir + "_" + name);
    }

    bool make(const SaveRef& ref, std::string& error)
    {
        std::string folder = folderFor(ref);
        files::mkdirs(folder);
        std::string target = folder + "/" + files::stamp() + ".bak";
        bool kept          = true;
        if (ref.isTitle())
        {
            // What the game holds right now, read back from it.
            std::vector<u8> bytes;
            std::string why;
            kept = titles::read(ref, bytes, why) && files::write(target, bytes.data(), bytes.size());
        }
        else if (files::exists(ref.path))
        {
            kept = files::copy(ref.path, target);
        }
        if (!kept)
        {
            error = "Could not make a restore point in " + folder + "; nothing was written.";
            return false;
        }
        // Keep the newest KEEP restore points.
        std::vector<std::string> names;
        files::list(folder, [&](const std::string& name, bool dir, u64)
            {
                if (!dir && name.size() > 4 && name.substr(name.size() - 4) == ".bak")
                {
                    names.push_back(name);
                }
            });
        std::sort(names.begin(), names.end());
        while (names.size() > size_t(KEEP))
        {
            remove((folder + "/" + names.front()).c_str());
            names.erase(names.begin());
        }
        return true;
    }

    std::vector<std::string> list(const SaveRef& ref)
    {
        std::vector<std::string> names;
        files::list(folderFor(ref), [&](const std::string& name, bool dir, u64)
            {
                if (!dir && name.size() > 4 && name.substr(name.size() - 4) == ".bak")
                {
                    names.push_back(name);
                }
            });
        std::sort(names.rbegin(), names.rend());
        return names;
    }

    bool restore(const SaveRef& ref, const std::string& name, std::string& error)
    {
        bool testSave =
            ref.isTitle() ? ref.titleId == titles::TEST_TITLE : ref.path.rfind(autotest::TEST_SAVES, 0) == 0;
        if (autotest::active() && !testSave)
        {
            error = "Test scripts only write saves in " + std::string(autotest::TEST_SAVES);
            return false;
        }
        std::vector<u8> bytes;
        if (!files::read(folderFor(ref) + "/" + name, bytes) || bytes.empty())
        {
            error = "The restore point could not be read.";
            return false;
        }
        if (!make(ref, error))
        {
            return false;
        }
        std::string failure;
        bool ok = ref.isTitle() ? titles::write(ref, bytes.data(), bytes.size(), failure)
                                : files::write(ref.path, bytes.data(), bytes.size());
        if (!ok)
        {
            error = failure.empty() ? "Writing " + ref.path + " failed." : failure;
            return false;
        }
        if (!ref.isTitle())
        {
            saves::changed(ref.path);
        }
        return true;
    }
}
