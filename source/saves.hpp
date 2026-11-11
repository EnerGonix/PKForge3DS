// Finding saves on the SD card and in 3DS games, opening them with PKSM-Core and writing them
// back safely.
#pragma once

#include "sav/Sav.hpp"
#include <memory>
#include <string>
#include <vector>

struct SaveRef
{
    std::string path;   // sdmc:/..., or "title:<id>" / "card:<id>" for a 3DS game's own save
    std::string source; // who keeps it: "mGBA", "Installed", "Game card", or its folder
    u32 size     = 0;
    u8 version   = 0; // pksm::GameVersion
    int gen      = 0; // 1-9
    std::string ot;
    u32 tid = 0;
    int hours = 0, minutes = 0, seconds = 0;
    int caught = 0, seen = 0;
    int badges = 0;
    u64 titleId = 0; // a 3DS game's save, read through the FS service
    u8 media    = 0; // FS_MediaType of that game
    bool gbFile = false; // the Virtual Console's /sav.dat rather than /main
    std::string label;   // a ROM hack's name ("Radical Red"), shown instead of the game

    bool isTitle() const { return titleId != 0; }
};

namespace saves
{
    // Looks through the 3DS games and the SD card on a background thread; results arrive while it runs.
    void startScan();
    bool scanning();
    // Stops the scan and waits for it: the SD card must not go away under its readdir (leaving, rebooting).
    void stopScan();
    // Drops a deleted file from the results, so a refresh does not bring it back.
    void forget(const std::string& path);
    // A file PKForge wrote: its next scan reads it again instead of trusting the scan cache.
    void changed(const std::string& path);
    int foldersSeen();
    std::vector<SaveRef> found();
    // Reads one file now; false when it is not a save PKSM-Core knows.
    bool describe(const std::string& path, SaveRef& out);
    // Fills the game facts of a save already read; false when PKSM-Core does not know it.
    bool describeBytes(const std::vector<u8>& bytes, SaveRef& out);
}

class OpenSave
{
public:
    SaveRef ref;
    std::shared_ptr<u8[]> data;
    std::vector<u8> container; // the file as read, when a wrapper (RetroArch, padding) surrounds the save
    u32 size = 0;
    std::unique_ptr<pksm::Sav> sav;
    bool dirty = false;

    static std::unique_ptr<OpenSave> open(const SaveRef& ref, std::string& error);
    // Keeps a restore point of the save as it is now, then writes the edited one over it.
    bool write(std::string& error);
};

namespace backups
{
    constexpr int KEEP = 20;
    std::string folderFor(const SaveRef& ref);
    bool make(const SaveRef& ref, std::string& error);
    // The save's restore points ("2026-11-11_08-20-53.bak"), newest first.
    std::vector<std::string> list(const SaveRef& ref);
    // Puts one back: a restore point of what the save holds now first (so a restore can be undone too),
    // then the backup's bytes written the way a save is written.
    bool restore(const SaveRef& ref, const std::string& name, std::string& error);
}
