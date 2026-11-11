// The Bank: PKForge's own layout (index.json plus one .bin of decrypted party data per Pokémon),
// in sdmc:/3ds/PKForge/bank, so a bank folder moves between PKForge on Android and the 3DS.
#pragma once

#include "json.hpp"
#include "pkx/PKX.hpp"
#include <memory>
#include <string>
#include <vector>

struct BankEntry
{
    std::string id; // GUID, "D" format
    int box = 0, slot = 0;
    int species = 0, form = 0, level = 0, generation = 0, heldItem = 0;
    bool shiny = false, female = false;
    std::string nickname, source, format;
    Json raw; // the entry as stored, so fields this port does not know survive a rewrite
};

class Bank
{
public:
    static constexpr int SLOTS = 30;

    bool load(std::string& error);
    bool readOnly() const { return readOnly_; }
    int boxes() const { return boxCount; }
    void addBox();
    std::string boxName(int box) const;
    void boxName(int box, const std::string& name);
    // The box's wallpaper as the Android PKForge keeps it (box-wallpapers.json): "color:N" (the storage
    // palette), "art:<asset>" (a game's wallpaper picture), or "" for the default.
    std::string wallpaper(int box) const;
    void wallpaper(int box, const std::string& id);

    const BankEntry* at(int box, int slot) const;
    std::unique_ptr<pksm::PKX> pkm(const BankEntry& e) const;
    // Stores a copy in an empty slot.
    bool put(const pksm::PKX& pk, int box, int slot, const std::string& source, std::string& error);
    // Replaces the Pokémon in an occupied slot (after editing).
    bool replace(int box, int slot, const pksm::PKX& pk, std::string& error);
    // Swaps two slots (either may be empty).
    bool move(int fromBox, int fromSlot, int toBox, int toSlot, std::string& error);
    bool remove(int box, int slot, std::string& error);
    // Writes every Pokémon of a box as .pkN files into sdmc:/3ds/PKForge/export/<stamp>; returns the count.
    int exportBox(int box, std::string& folder);
    // Imports every .pkN file of a folder into empty slots; returns the count.
    int importFolder(const std::string& folder, std::string& error);
    int count() const { return int(entries.size()); }

private:
    std::vector<BankEntry> entries;
    int boxCount = 3;
    int migrationVersion = 0;
    bool readOnly_ = false;
    Json names;      // box-names.json
    Json wallpapers; // box-wallpapers.json

    std::string dir() const;
    bool saveIndex(std::string& error);
    void describe(BankEntry& e, const pksm::PKX& pk) const;
};
