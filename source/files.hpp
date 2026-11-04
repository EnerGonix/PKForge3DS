// SD card helpers: whole-file reads, crash-safe writes, folders, time stamps.
#pragma once

#include <3ds.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace files
{
    constexpr const char* ROOT = "sdmc:/3ds/PKForge";

    bool read(const std::string& path, std::vector<u8>& out);
    bool readText(const std::string& path, std::string& out);
    // Writes beside the target, then swaps it in: a crash leaves the old file or the new one.
    bool write(const std::string& path, const void* data, size_t size);
    inline bool writeText(const std::string& path, std::string_view text) { return write(path, text.data(), text.size()); }
    bool exists(const std::string& path);
    bool isDir(const std::string& path);
    void mkdirs(const std::string& path);
    bool copy(const std::string& from, const std::string& to);
    // Calls back for every entry: name, folder?, size.
    void list(const std::string& dir, const std::function<void(const std::string&, bool, u64)>& fn);

    std::string stamp();   // 2026-11-02_07-35-12, for file names
    std::string iso8601(); // 2026-11-02T07:35:12+00:00
    std::string baseName(std::string_view path);
    std::string dirName(std::string_view path);
    std::string sanitize(std::string_view name); // safe as a file name
    std::string lower(std::string_view s);
}
