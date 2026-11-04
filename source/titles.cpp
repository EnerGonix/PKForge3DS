#include "titles.hpp"
#include "spi.hpp"

#include <3ds.h>
#include <cstring>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

namespace titles
{
    namespace
    {
        bool amReady = false;

        // The 3DS games' /main files run 0x65600 (X/Y) to 0x76000 (ORAS); the Virtual
        // Console keeps the cartridge's 32KB (plus a clock on Crystal).
        bool plausible(u64 size, bool gb) { return gb ? size >= 0x2000 && size <= 0x10000 : size >= 0x60000 && size <= 0x100000; }

        const char16_t* fileName(bool gb) { return gb ? u"/sav.dat" : u"/main"; }

        std::string failure(const char* what, Result res)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s (error %08" PRIX32 ").", what, u32(res));
            return buf;
        }

        // The save archive of one title, closed when it goes out of scope.
        struct SaveArchive
        {
            FS_Archive handle = 0;
            Result result;

            SaveArchive(u64 id, u8 media)
            {
                const u32 path[3] = {media, u32(id), u32(id >> 32)};
                result = FSUSER_OpenArchive(&handle, ARCHIVE_USER_SAVEDATA, {PATH_BINARY, sizeof(path), path});
            }

            ~SaveArchive()
            {
                if (R_SUCCEEDED(result))
                {
                    FSUSER_CloseArchive(handle);
                }
            }

            bool open() const { return R_SUCCEEDED(result); }
        };

        Result readFile(FS_Archive archive, bool gb, std::vector<u8>& out, bool onlyPlausible)
        {
            Handle file;
            Result res = FSUSER_OpenFile(&file, archive, fsMakePath(PATH_UTF16, fileName(gb)), FS_OPEN_READ, 0);
            if (R_FAILED(res))
            {
                return res;
            }
            u64 size = 0;
            res      = FSFILE_GetSize(file, &size);
            if (R_SUCCEEDED(res) && onlyPlausible && !plausible(size, gb))
            {
                res = -1;
            }
            if (R_SUCCEEDED(res))
            {
                out.resize(size);
                u32 read = 0;
                res      = FSFILE_Read(file, &read, 0, out.data(), u32(size));
                if (R_SUCCEEDED(res) && read != size)
                {
                    res = -1;
                }
            }
            FSFILE_Close(file);
            return res;
        }

        void probe(u64 id, u8 media, const std::function<void(const SaveRef&)>& found)
        {
            SaveArchive archive(id, media);
            if (!archive.open())
            {
                return;
            }
            for (bool gb : {false, true})
            {
                std::vector<u8> bytes;
                // The test title holds whatever save a script put there, of any size.
                if (R_FAILED(readFile(archive.handle, gb, bytes, id != TEST_TITLE)))
                {
                    continue;
                }
                SaveRef ref;
                if (!saves::describeBytes(bytes, ref))
                {
                    continue;
                }
                char path[48];
                snprintf(path, sizeof(path), "%s:%016" PRIX64, media == MEDIATYPE_GAME_CARD ? "card" : "title", id);
                ref.path    = path;
                ref.titleId = id;
                ref.media   = media;
                ref.gbFile  = gb;
                ref.source  = media == MEDIATYPE_GAME_CARD ? "Game card" : gb ? "Virtual Console" : "Installed";
                found(ref);
                return;
            }
        }

        void probeMedia(FS_MediaType media, const std::function<void(const SaveRef&)>& found)
        {
            u32 count = 0;
            if (R_FAILED(AM_GetTitleCount(media, &count)) || count == 0)
            {
                return;
            }
            std::vector<u64> ids(count);
            u32 read = 0;
            if (R_FAILED(AM_GetTitleList(&read, media, count, ids.data())))
            {
                return;
            }
            for (u32 i = 0; i < read; i++)
            {
                // Applications only: system titles, DLC and updates hold no game saves.
                if ((ids[i] >> 32) == 0x00040000)
                {
                    probe(ids[i], media, found);
                }
            }
        }
    }

    namespace
    {
        // The DS cartridge in the slot: its game code and save chip, or why not.
        bool ndsCard(std::string& code, CardType& type, std::string& error)
        {
            u8 header[0x3B4];
            Result res = FSUSER_GetLegacyRomHeader(MEDIATYPE_GAME_CARD, 0, header);
            if (R_FAILED(res))
            {
                error = failure("The DS card's header could not be read", res);
                return false;
            }
            code.assign(reinterpret_cast<const char*>(header) + 0x0C, 4);
            if (R_FAILED(pxiDevInit()))
            {
                error = "The card's SPI service did not start.";
                return false;
            }
            // Game codes starting with I are the infrared carts (HeartGold and SoulSilver).
            res = SPIGetCardType(&type, code[0] == 'I');
            if (R_FAILED(res) || type == NO_CHIP)
            {
                pxiDevExit();
                error = failure("The DS card's save chip did not answer", res);
                return false;
            }
            return true; // pxi:dev stays open for the caller, which closes it
        }

        u64 ndsId(const std::string& code)
        {
            u32 low = 0;
            memcpy(&low, code.data(), 4);
            return NDS_CARD | low;
        }

        void probeNds(const std::function<void(const SaveRef&)>& found)
        {
            std::string code, error;
            CardType type;
            if (!ndsCard(code, type, error))
            {
                return;
            }
            std::vector<u8> bytes(SPIGetCapacity(type));
            Result res = SPIReadSaveData(type, 0, bytes.data(), u32(bytes.size()));
            pxiDevExit();
            SaveRef ref;
            if (R_FAILED(res) || !saves::describeBytes(bytes, ref))
            {
                return;
            }
            ref.path    = "ndscard:" + code;
            ref.titleId = ndsId(code);
            ref.media   = MEDIATYPE_GAME_CARD;
            ref.source  = "DS card";
            found(ref);
        }

        bool readNds(const SaveRef& ref, std::vector<u8>& out, std::string& error)
        {
            std::string code;
            CardType type;
            if (!ndsCard(code, type, error))
            {
                return false;
            }
            if (ndsId(code) != ref.titleId)
            {
                pxiDevExit();
                error = "A different DS card is in the slot now.";
                return false;
            }
            out.resize(SPIGetCapacity(type));
            Result res = SPIReadSaveData(type, 0, out.data(), u32(out.size()));
            pxiDevExit();
            if (R_FAILED(res))
            {
                error = failure("Reading the DS card's save failed", res);
                return false;
            }
            return true;
        }

        // Page by page as Checkpoint restores, then read back: the chip must hold exactly what was sent.
        bool writeNds(const SaveRef& ref, const u8* data, size_t size, std::string& error)
        {
            std::string code;
            CardType type;
            if (!ndsCard(code, type, error))
            {
                return false;
            }
            u32 capacity = SPIGetCapacity(type), page = SPIGetPageSize(type);
            Result res   = 0;
            if (ndsId(code) != ref.titleId)
            {
                error = "A different DS card is in the slot now.";
            }
            else if (size != capacity)
            {
                error = "The save is not the size of the card's chip.";
            }
            else
            {
                std::vector<u8> copy(data, data + size);
                for (u32 at = 0; at < capacity && R_SUCCEEDED(res); at += page)
                {
                    res = SPIWriteSaveData(type, at, copy.data() + at, page);
                }
                std::vector<u8> check(capacity);
                if (R_FAILED(res))
                {
                    error = failure("Writing the DS card's save failed", res);
                }
                else if (R_FAILED(res = SPIReadSaveData(type, 0, check.data(), capacity)) || check != copy)
                {
                    error = "The DS card did not read back what was written. Its restore point is in "
                            "3ds/PKForge/backups.";
                }
            }
            pxiDevExit();
            return error.empty();
        }
    }

    void scan(const std::function<void(const SaveRef&)>& found)
    {
        if (!amReady)
        {
            amReady = R_SUCCEEDED(amInit());
        }
        if (!amReady)
        {
            return;
        }
        FS_CardType card;
        if (R_SUCCEEDED(FSUSER_GetCardType(&card)))
        {
            if (card == CARD_CTR)
            {
                probeMedia(MEDIATYPE_GAME_CARD, found);
            }
            else if (card == CARD_TWL)
            {
                probeNds(found);
            }
        }
        probeMedia(MEDIATYPE_SD, found);
    }

    bool describe(u64 id, u8 media, SaveRef& out)
    {
        bool ok = false;
        probe(id, media, [&](const SaveRef& ref)
            {
                out = ref;
                ok  = true;
            });
        return ok;
    }

    bool parse(const std::string& path, u64& id, u8& media)
    {
        size_t colon = path.find(':');
        if (colon == std::string::npos)
        {
            return false;
        }
        std::string kind = path.substr(0, colon);
        if (kind != "title" && kind != "card")
        {
            return false;
        }
        id    = strtoull(path.c_str() + colon + 1, nullptr, 16);
        media = kind == "card" ? MEDIATYPE_GAME_CARD : MEDIATYPE_SD;
        return id != 0;
    }

    bool read(const SaveRef& ref, std::vector<u8>& out, std::string& error)
    {
        if (isNdsCard(ref))
        {
            return readNds(ref, out, error);
        }
        SaveArchive archive(ref.titleId, ref.media);
        if (!archive.open())
        {
            error = failure("The game's save data could not be opened", archive.result);
            return false;
        }
        Result res = readFile(archive.handle, ref.gbFile, out, false);
        if (R_FAILED(res))
        {
            error = failure("Reading the game's save failed", res);
            return false;
        }
        return true;
    }

    bool write(const SaveRef& ref, const u8* data, size_t size, std::string& error)
    {
        if (isNdsCard(ref))
        {
            return writeNds(ref, data, size, error);
        }
        SaveArchive archive(ref.titleId, ref.media);
        if (!archive.open())
        {
            error = failure("The game's save data could not be opened", archive.result);
            return false;
        }
        Handle file;
        Result res =
            FSUSER_OpenFile(&file, archive.handle, fsMakePath(PATH_UTF16, fileName(ref.gbFile)), FS_OPEN_WRITE, 0);
        if (R_FAILED(res))
        {
            error = failure("The game's save file could not be opened for writing", res);
            return false;
        }
        u32 written = 0;
        res         = FSFILE_Write(file, &written, 0, data, u32(size), FS_WRITE_FLUSH);
        FSFILE_Close(file);
        if (R_FAILED(res) || written != size)
        {
            error = failure("Writing the game's save failed", res);
            return false;
        }
        res = FSUSER_ControlArchive(archive.handle, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
        if (R_FAILED(res))
        {
            error = failure("The game's save was written but not committed", res);
            return false;
        }
        if (ref.media == MEDIATYPE_SD)
        {
            // Installed games compare a secure value against restored saves; with it gone the
            // game accepts the edited save, as after PKSM or Checkpoint.
            u64 secure  = (u64(SECUREVALUE_SLOT_SD) << 32) | (u32(ref.titleId) & 0xFFFFFF00);
            u8 existed  = 0;
            FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secure, sizeof(secure), &existed, 1);
        }
        return true;
    }
}
