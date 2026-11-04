#include "files.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace files
{
    bool read(const std::string& path, std::vector<u8>& out)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f)
        {
            return false;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        if (size < 0)
        {
            fclose(f);
            return false;
        }
        out.resize(size);
        bool ok = size == 0 || fread(out.data(), 1, size, f) == size_t(size);
        fclose(f);
        return ok;
    }

    bool readText(const std::string& path, std::string& out)
    {
        std::vector<u8> bytes;
        if (!read(path, bytes))
        {
            return false;
        }
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool write(const std::string& path, const void* data, size_t size)
    {
        std::string tmp = path + ".tmp";
        FILE* f         = fopen(tmp.c_str(), "wb");
        if (!f)
        {
            return false;
        }
        bool ok = fwrite(data, 1, size, f) == size;
        ok      = (fflush(f) == 0) && ok;
        fclose(f);
        if (!ok)
        {
            remove(tmp.c_str());
            return false;
        }
        remove(path.c_str());
        return rename(tmp.c_str(), path.c_str()) == 0;
    }

    bool exists(const std::string& path)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

    bool isDir(const std::string& path)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    void mkdirs(const std::string& path)
    {
        size_t start = path.find(":/");
        start        = start == std::string::npos ? 0 : start + 2;
        for (size_t i = start; i <= path.size(); i++)
        {
            if (i == path.size() || path[i] == '/')
            {
                std::string part = path.substr(0, i);
                if (!part.empty() && part.back() != ':')
                {
                    mkdir(part.c_str(), 0777);
                }
            }
        }
    }

    bool copy(const std::string& from, const std::string& to)
    {
        std::vector<u8> bytes;
        return read(from, bytes) && write(to, bytes.data(), bytes.size());
    }

    void list(const std::string& dir, const std::function<void(const std::string&, bool, u64)>& fn)
    {
        DIR* d = opendir(dir.c_str());
        if (!d)
        {
            return;
        }
        while (dirent* e = readdir(d))
        {
            std::string name = e->d_name;
            if (name == "." || name == "..")
            {
                continue;
            }
            std::string full = dir + (dir.back() == '/' ? "" : "/") + name;
            bool folder      = e->d_type == DT_DIR;
            u64 size         = 0;
            if (!folder)
            {
                struct stat st;
                if (stat(full.c_str(), &st) == 0)
                {
                    size = st.st_size;
                }
            }
            fn(name, folder, size);
        }
        closedir(d);
    }

    std::string stamp()
    {
        time_t now = time(nullptr);
        tm* t      = gmtime(&now);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", t);
        return buf;
    }

    std::string iso8601()
    {
        time_t now = time(nullptr);
        tm* t      = gmtime(&now);
        char buf[40];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", t);
        return buf;
    }

    std::string baseName(std::string_view path)
    {
        size_t slash = path.find_last_of('/');
        return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
    }

    std::string dirName(std::string_view path)
    {
        size_t slash = path.find_last_of('/');
        return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
    }

    std::string sanitize(std::string_view name)
    {
        std::string out;
        for (char c : name)
        {
            out += (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == ' ') ? c : '_';
        }
        return out.empty() ? "save" : out;
    }

    std::string lower(std::string_view s)
    {
        std::string out(s);
        for (auto& c : out)
        {
            c = tolower((unsigned char)c);
        }
        return out;
    }
}
