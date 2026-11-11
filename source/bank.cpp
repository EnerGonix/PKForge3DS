#include "bank.hpp"
#include "romhack.hpp"
#include "files.hpp"
#include "pkdata.hpp"

#include <3ds.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace
{
    std::string newGuid()
    {
        u8 b[16];
        for (int i = 0; i < 16; i += 4)
        {
            u32 r = rand() ^ (u32(osGetTime()) * 2654435761u) ^ (u32(svcGetSystemTick()) >> (i % 7));
            memcpy(b + i, &r, 4);
        }
        b[6] = (b[6] & 0x0F) | 0x40; // version 4
        b[8] = (b[8] & 0x3F) | 0x80;
        char out[40];
        snprintf(out, sizeof(out), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
            b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return out;
    }

    std::string fileId(const std::string& guid)
    {
        std::string out;
        for (char c : guid)
        {
            if (c != '-')
            {
                out += tolower((unsigned char)c);
            }
        }
        return out;
    }

    BankEntry fromJson(const Json& j)
    {
        BankEntry e;
        e.raw    = j;
        e.id     = j.get("Id").asString();
        e.box    = j.get("Box").asInt();
        e.slot   = j.get("Slot").asInt();
        const Json& info = j.get("Info");
        e.species    = info.get("Species").asInt();
        e.form       = info.get("Form").asInt();
        e.shiny      = info.get("Shiny").asBool();
        e.nickname   = info.get("Nickname").asString();
        e.level      = info.get("Level").asInt();
        e.generation = info.get("Generation").asInt();
        e.source     = info.get("SourceName").asString();
        e.format     = info.get("Format").asString();
        e.heldItem   = info.get("HeldItem").asInt();
        e.female     = info.get("Traits").get("Female").asBool();
        return e;
    }

    Json toJson(const BankEntry& e)
    {
        Json j = e.raw.isObject() ? e.raw : Json::object();
        j.set("Id", e.id);
        j.set("Box", e.box);
        j.set("Slot", e.slot);
        Json info = j.get("Info").isObject() ? j.get("Info") : Json::object();
        info.set("Species", e.species);
        info.set("Form", e.form);
        info.set("Shiny", e.shiny);
        info.set("Nickname", e.nickname);
        info.set("Level", e.level);
        info.set("Generation", e.generation);
        info.set("SourceName", e.source);
        info.set("Format", e.format);
        info.set("HeldItem", e.heldItem);
        Json traits = info.get("Traits").isObject() ? info.get("Traits") : Json::object();
        traits.set("Female", e.female);
        if (!traits.has("FormArgument"))
        {
            traits.set("FormArgument", 0);
        }
        if (!traits.has("Cosplay"))
        {
            traits.set("Cosplay", false);
        }
        if (!traits.has("Gigantamax"))
        {
            traits.set("Gigantamax", false);
        }
        info.set("Traits", traits);
        j.set("Info", info);
        if (!j.has("AddedUtc"))
        {
            j.set("AddedUtc", files::iso8601());
        }
        return j;
    }
}

std::string Bank::dir() const { return std::string(files::ROOT) + "/bank"; }

bool Bank::load(std::string& error)
{
    entries.clear();
    files::mkdirs(dir());
    std::string text;
    bool anyIndex = false;
    for (auto name : {"/index.json", "/index.json.bak"})
    {
        if (!files::readText(dir() + name, text))
        {
            continue;
        }
        anyIndex = true;
        Json j;
        if (!Json::parse(text, j) || !j.isObject())
        {
            continue;
        }
        boxCount         = std::max(1, j.get("BoxCount").asInt(3));
        migrationVersion = j.get("MigrationVersion").asInt();
        for (auto& item : j.get("Entries").items())
        {
            BankEntry e = fromJson(item);
            if (!e.id.empty())
            {
                entries.push_back(e);
                boxCount = std::max(boxCount, e.box + 1);
            }
        }
        readOnly_ = false;
        Json n;
        if (files::readText(dir() + "/box-names.json", text) && Json::parse(text, n))
        {
            names = n;
        }
        Json w;
        if (files::readText(dir() + "/box-wallpapers.json", text) && Json::parse(text, w))
        {
            wallpapers = w;
        }
        return true;
    }
    if (anyIndex)
    {
        // An index that exists but cannot be read is never replaced by an empty one.
        readOnly_ = true;
        error     = "The Bank index could not be read; the Bank is read-only.";
        return false;
    }
    boxCount = 3;
    return true;
}

bool Bank::saveIndex(std::string& error)
{
    if (readOnly_)
    {
        error = "The Bank is read-only.";
        return false;
    }
    Json root = Json::object();
    root.set("BoxCount", boxCount);
    Json list = Json::array();
    for (auto& e : entries)
    {
        list.push(toJson(e));
    }
    root.set("Entries", list);
    root.set("MigrationVersion", migrationVersion);
    std::string index = dir() + "/index.json";
    if (files::exists(index))
    {
        files::copy(index, index + ".bak");
    }
    if (!files::writeText(index, root.dump()))
    {
        error = "Could not write the Bank index.";
        return false;
    }
    return true;
}

void Bank::addBox()
{
    boxCount++;
    std::string err;
    saveIndex(err);
}

std::string Bank::boxName(int box) const
{
    const Json& n = names.get(std::to_string(box));
    if (n.type() == Json::Type::String && !n.asString().empty())
    {
        return n.asString();
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "Bank %02d", box + 1);
    return buf;
}

std::string Bank::wallpaper(int box) const
{
    return wallpapers.get(std::to_string(box)).asString();
}

void Bank::wallpaper(int box, const std::string& id)
{
    if (id.empty())
    {
        wallpapers.erase(std::to_string(box));
    }
    else
    {
        wallpapers.set(std::to_string(box), id);
    }
    files::writeText(dir() + "/box-wallpapers.json", wallpapers.dump());
}

void Bank::boxName(int box, const std::string& name)
{
    names.set(std::to_string(box), name);
    files::writeText(dir() + "/box-names.json", names.dump());
}

const BankEntry* Bank::at(int box, int slot) const
{
    for (auto& e : entries)
    {
        if (e.box == box && e.slot == slot)
        {
            return &e;
        }
    }
    return nullptr;
}

std::unique_ptr<pksm::PKX> Bank::pkm(const BankEntry& e) const
{
    std::vector<u8> bytes;
    if (!files::read(dir() + "/" + fileId(e.id) + ".bin", bytes))
    {
        return nullptr;
    }
    return pkdata::fromBytes(e.format, bytes.data(), bytes.size());
}

void Bank::describe(BankEntry& e, const pksm::PKX& pk) const
{
    e.species    = u16(pk.species());
    e.form       = pk.alternativeForm();
    e.shiny      = pk.shiny();
    e.female     = pk.gender() == pksm::Gender::Female;
    e.nickname   = pk.nickname();
    e.level      = pk.level();
    e.generation = pkdata::genNumber(pk.generation());
    e.format     = pkdata::format(pk);
    e.heldItem   = pk.heldItem();
}

bool Bank::put(const pksm::PKX& pk, int box, int slot, const std::string& source, std::string& error)
{
    if (at(box, slot))
    {
        error = "That Bank slot is taken.";
        return false;
    }
    BankEntry e;
    e.id     = newGuid();
    e.box    = box;
    e.slot   = slot;
    e.source = source;
    describe(e, pk);
    // PKForge keeps decrypted party data; Gen I/II keep their names alongside, as PKHeX's lists do.
    std::vector<u8> raw;
    if (romhack::is(pk))
    {
        raw = romhack::bankBytes(pk);
    }
    else
    {
        auto party = pk.partyClone();
        auto r     = party->rawData();
        raw.assign(r.begin(), r.end());
    }
    std::string bin = dir() + "/" + fileId(e.id) + ".bin";
    if (!files::write(bin, raw.data(), raw.size()))
    {
        error = "Could not write to the Bank.";
        return false;
    }
    entries.push_back(e);
    boxCount = std::max(boxCount, box + 1);
    if (!saveIndex(error))
    {
        entries.pop_back();
        ::remove(bin.c_str());
        return false;
    }
    return true;
}

bool Bank::replace(int box, int slot, const pksm::PKX& pk, std::string& error)
{
    for (auto& e : entries)
    {
        if (e.box == box && e.slot == slot)
        {
            auto party = pk.partyClone();
            auto raw   = party->rawData();
            if (!files::write(dir() + "/" + fileId(e.id) + ".bin", raw.data(), raw.size()))
            {
                error = "Could not write to the Bank.";
                return false;
            }
            describe(e, pk);
            return saveIndex(error);
        }
    }
    error = "Nothing in that Bank slot.";
    return false;
}

bool Bank::move(int fromBox, int fromSlot, int toBox, int toSlot, std::string& error)
{
    BankEntry* a = nullptr;
    BankEntry* b = nullptr;
    for (auto& e : entries)
    {
        if (e.box == fromBox && e.slot == fromSlot)
        {
            a = &e;
        }
        else if (e.box == toBox && e.slot == toSlot)
        {
            b = &e;
        }
    }
    if (!a)
    {
        return true;
    }
    a->box  = toBox;
    a->slot = toSlot;
    if (b)
    {
        b->box  = fromBox;
        b->slot = fromSlot;
    }
    boxCount = std::max(boxCount, toBox + 1);
    return saveIndex(error);
}

bool Bank::remove(int box, int slot, std::string& error)
{
    for (auto it = entries.begin(); it != entries.end(); ++it)
    {
        if (it->box == box && it->slot == slot)
        {
            std::string bin = dir() + "/" + fileId(it->id) + ".bin";
            BankEntry gone  = *it;
            entries.erase(it);
            if (!saveIndex(error))
            {
                entries.push_back(gone);
                return false;
            }
            ::remove(bin.c_str()); // the index is authoritative: an orphan .bin is harmless
            return true;
        }
    }
    return true;
}

int Bank::exportBox(int box, std::string& folder)
{
    folder = std::string(files::ROOT) + "/export/" + files::stamp();
    files::mkdirs(folder);
    int n = 0;
    for (auto& e : entries)
    {
        if (e.box != box)
        {
            continue;
        }
        auto pk = pkm(e);
        if (!pk)
        {
            continue;
        }
        char name[160];
        snprintf(name, sizeof(name), "%03d - %s %s%s", e.species, files::sanitize(e.nickname).c_str(),
            fileId(e.id).substr(0, 8).c_str(), std::string(pk->extension()).c_str());
        auto raw = pk->rawData();
        if (files::write(folder + "/" + name, raw.data(), raw.size()))
        {
            n++;
        }
    }
    return n;
}

int Bank::importFolder(const std::string& folder, std::string& error)
{
    std::vector<std::string> paths;
    files::list(folder, [&](const std::string& name, bool dir, u64)
        {
            if (!dir)
            {
                paths.push_back(name);
            }
        });
    int n = 0, box = 0, slot = 0;
    for (auto& name : paths)
    {
        size_t dot = name.find_last_of('.');
        std::string format = dot == std::string::npos ? "" : pkdata::formatFromExtension(name.substr(dot));
        if (format.empty())
        {
            continue;
        }
        std::vector<u8> bytes;
        if (!files::read(folder + "/" + name, bytes))
        {
            continue;
        }
        auto pk = pkdata::fromBytes(format, bytes.data(), bytes.size());
        if (!pk)
        {
            continue;
        }
        while (at(box, slot))
        {
            if (++slot == SLOTS)
            {
                slot = 0;
                box++;
            }
        }
        if (put(*pk, box, slot, "Import", error))
        {
            n++;
        }
    }
    return n;
}
