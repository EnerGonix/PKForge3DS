#include "gamecube.hpp"
#include "utils/ValueConverter.hpp"
#include "item_plain.hpp"

#include "pkx/PK3.hpp"
#include "pkx/PKX.hpp"
#include "sav/Item.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mbedtls/sha1.h>
#include <set>
#include <vector>

// Layouts and crypto from PKHeX (GPLv3): SAV3Colosseum, SAV3XD, ColoCrypto, XDCrypto, GeniusCrypto, CK3, XK3.
namespace gamecube
{
    namespace
    {
        constexpr u32 GCI_HEADER = 0x40;
        constexpr u32 COLO_SIZE = 0x60000, XD_SIZE = 0x56000;
        constexpr u32 SLOT_START = 0x6000;
        constexpr u32 COLO_SLOT = 0x1E000, XD_SLOT = 0x28000;
        constexpr u32 CK3_SIZE = 0x138, XK3_SIZE = 0xC4;

        u16 be16(const u8* p) { return u16(p[0] << 8 | p[1]); }
        u32 be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
        void be16(u8* p, u16 v)
        {
            p[0] = u8(v >> 8);
            p[1] = u8(v);
        }
        void be32(u8* p, u32 v)
        {
            p[0] = u8(v >> 24);
            p[1] = u8(v >> 16);
            p[2] = u8(v >> 8);
            p[3] = u8(v);
        }

        void sha1(const u8* data, size_t n, u8 out[20]) { mbedtls_sha1_ret(data, n, out); }

        // ---------------------------------------------------------------- Colosseum: SHA-1 chained XOR
        void coloDecrypt(u8* slot)
        {
            u8 digest[20], hash[20];
            memcpy(digest, slot + COLO_SLOT - 20, 20);
            for (u8& b : digest)
            {
                b = u8(~b);
            }
            u8* p   = slot + 0x18;
            u8* end = slot + COLO_SLOT - 20;
            while (end - p >= 20)
            {
                sha1(p, 20, hash);
                for (int j = 0; j < 20; j++)
                {
                    p[j] ^= digest[j];
                }
                memcpy(digest, hash, 20);
                p += 20;
            }
        }
        void coloEncrypt(u8* slot)
        {
            u8 digest[20];
            memcpy(digest, slot + COLO_SLOT - 20, 20);
            for (u8& b : digest)
            {
                b = u8(~b);
            }
            u8* p   = slot + 0x18;
            u8* end = slot + COLO_SLOT - 20;
            while (end - p >= 20)
            {
                for (int j = 0; j < 20; j++)
                {
                    p[j] ^= digest[j];
                }
                sha1(p, 20, digest);
                p += 20;
            }
        }
        void coloChecksums(u8* slot)
        {
            be32(slot + 0x0C, 0);
            u8* hash = slot + COLO_SLOT - 20;
            sha1(slot, COLO_SLOT - 40, hash);
            s32 r = 0;
            for (int i = 0; i < 0x18; i += 4)
            {
                r -= s32(be32(slot + i));
            }
            r -= s32(be32(slot + 0x18) ^ ~be32(hash));
            r -= s32(be32(slot + 0x1C) ^ ~be32(hash + 4));
            be32(slot + 0x0C, u32(r));
        }

        // ---------------------------------------------------------------- XD: GeniusCrypto
        void advance(u16 k[4])
        {
            int k3 = k[3] + 0x13, k2 = k[2] + 0x17, k1 = k[1] + 0x29, k0 = k[0] + 0x43;
            k[3] = u16(((k0 >> 12) & 0xf) | ((k1 >> 8) & 0xf0) | ((k2 >> 4) & 0xf00) | (k3 & 0xf000));
            k[2] = u16(((k0 >> 8) & 0xf) | ((k1 >> 4) & 0xf0) | (k2 & 0xf00) | ((k3 << 4) & 0xf000));
            k[1] = u16(((k0 >> 4) & 0xf) | (k1 & 0xf0) | ((k2 << 4) & 0xf00) | ((k3 << 8) & 0xf000));
            k[0] = u16((k0 & 0xf) | ((k1 << 4) & 0xf0) | ((k2 << 8) & 0xf00) | ((k3 << 12) & 0xf000));
        }
        void xdCrypt(u8* slot, bool encrypt)
        {
            u16 k[4];
            for (int i = 0; i < 4; i++)
            {
                k[i] = be16(slot + 0x08 + 2 * i);
            }
            for (u32 i = 0x10; i < 0x27FD8;)
            {
                for (int j = 0; j < 4 && i < 0x27FD8; j++, i += 2)
                {
                    be16(slot + i, u16(be16(slot + i) + (encrypt ? k[j] : -k[j])));
                }
                advance(k);
            }
        }
        void xdChecksums(u8* slot, u32 sub0)
        {
            const u32 start = 0xA8;
            s32 hc = 0;
            for (int i = 0; i < 8; i++)
            {
                hc += slot[i];
            }
            be32(slot + start + sub0 + 0x38, u32(hc));
            memset(slot + 0x10, 0, 0x10);
            u32 sums[4];
            u32 dt = 8;
            for (u32& s : sums)
            {
                u32 v = 0, end = dt + 0x9FF4;
                for (u32 j = dt; j < end; j += 2)
                {
                    v += be16(slot + j);
                }
                dt = end;
                s  = v;
            }
            u16 parts[8];
            for (int i = 0; i < 4; i++)
            {
                parts[i * 2]     = u16(sums[i] >> 16);
                parts[i * 2 + 1] = u16(sums[i]);
            }
            for (int i = 0; i < 8; i++)
            {
                be16(slot + 0x10 + 2 * i, parts[7 - i]);
            }
        }

        // ---------------------------------------------------------------- strings, versions, languages
        std::string gcString(const u8* p, int maxChars)
        {
            std::u16string s;
            for (int i = 0; i < maxChars; i++)
            {
                u16 c = be16(p + 2 * i);
                if (!c)
                {
                    break;
                }
                s.push_back(char16_t(c));
            }
            return StringUtils::UTF16toUTF8(s);
        }
        void gcString(u8* p, int bytes, const std::string& v, int maxChars)
        {
            memset(p, 0, size_t(bytes));
            std::u16string s = StringUtils::UTF8toUTF16(v);
            for (int i = 0; i < int(s.size()) && i < maxChars; i++)
            {
                be16(p + 2 * i, u16(s[size_t(i)]));
            }
        }
        // GC version byte <-> GameVersion (FR 1, LG 2, S 8, R 9, E 10, CXD 11)
        u8 g3Version(u8 gc)
        {
            switch (gc)
            {
                case 1: return 4;
                case 2: return 5;
                case 8: return 1;
                case 9: return 2;
                case 10: return 3;
                case 11: return 15;
                default: return 0;
            }
        }
        u8 gcVersion(u8 v)
        {
            switch (v)
            {
                case 4: return 1;
                case 5: return 2;
                case 1: return 8;
                case 2: return 9;
                case 3: return 10;
                case 15: return 11;
                default: return 0;
            }
        }
        // GC language (JP 1, EN 2, DE 3, FR 4, IT 5, ES 6) <-> main (JP 1, EN 2, FR 3, IT 4, DE 5, ES 7)
        u8 mainLanguage(u8 gc)
        {
            switch (gc)
            {
                case 3: return 5;
                case 4: return 3;
                case 5: return 4;
                case 6: return 7;
                case 7: return 6;
                default: return gc;
            }
        }
        u8 gcLanguage(u8 main)
        {
            switch (main)
            {
                case 5: return 3;
                case 3: return 4;
                case 4: return 5;
                case 7: return 6;
                case 6: return 7;
                default: return main;
            }
        }

        // ---------------------------------------------------------------- CK3 / XK3 field offsets
        struct Layout
        {
            u32 size, species, item, pid, version, curRegion, origRegion, lang, metLoc, metLevel, ball, otGender, id32,
                ot, nickDisplay, nick, exp, level, moves, hpCur, stats, evs, friendship, sheen, contest, ribbonCounts,
                shadow;
            bool ivBytes; // XK3 keeps IVs as bytes at ivs, CK3 as u16
            u32 ivs;
        };
        constexpr Layout CK3{CK3_SIZE, 0x00, 0x88, 0x04, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x0F, 0x10, 0x14, 0x18,
            0x2E, 0x44, 0x5C, 0x60, 0x78, 0x8A, 0x8C, 0x98, 0xB0, 0xBC, 0xB2, 0xB7, 0xD8, false, 0xA4};
        constexpr Layout XK3{XK3_SIZE, 0x00, 0x02, 0x28, 0x34, 0x35, 0x36, 0x37, 0x08, 0x0E, 0x0F, 0x10, 0x24, 0x38,
            0x4E, 0x64, 0x20, 0x11, 0x80, 0x04, 0x90, 0x9C, 0x06, 0x12, 0xAE, 0xB3, 0xBA, true, 0xA8};

        // PKSM's stat order HP, ATK, DEF, SPE, SPA, SPD; GC keeps HP, ATK, DEF, SPA, SPD, SPE.
        const pksm::Stat GC_ORDER[6] = {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPATK,
            pksm::Stat::SPDEF, pksm::Stat::SPD};
        const pksm::Ribbon RIBBONS[12] = {pksm::Ribbon::ChampionG3Hoenn, pksm::Ribbon::Winning, pksm::Ribbon::Victory,
            pksm::Ribbon::Artist, pksm::Ribbon::Effort, pksm::Ribbon::ChampionBattle, pksm::Ribbon::ChampionRegional,
            pksm::Ribbon::ChampionNational, pksm::Ribbon::Country, pksm::Ribbon::National, pksm::Ribbon::Earth,
            pksm::Ribbon::World};

        std::unique_ptr<pksm::PKX> toPK3(const u8* d, bool xd, bool japanese, bool party)
        {
            const Layout& L = xd ? XK3 : CK3;
            auto pk = pksm::PKX::getPKM<pksm::Generation::THREE>(nullptr, party ? pksm::PK3::PARTY_LENGTH : pksm::PK3::BOX_LENGTH);
            u16 internal = be16(d + L.species);
            if (!internal)
            {
                return pk;
            }
            pk->language(pksm::Language{mainLanguage(d[L.lang])});
            pk->species(pksm::Species{u16(pksm::SpeciesConverter::g3ToNational(internal))});
            pk->version(pksm::GameVersion{g3Version(d[L.version])});
            pk->metLocation(be16(d + L.metLoc));
            pk->metLevel(d[L.metLevel]);
            pk->ball(pksm::Ball{d[L.ball]});
            pk->otGender(pksm::Gender{u8(d[L.otGender] & 1)});
            pk->SID(be16(d + L.id32));
            pk->TID(be16(d + L.id32 + 2));
            pk->heldItem(be16(d + L.item));
            pk->experience(be32(d + L.exp));
            for (int i = 0; i < 4; i++)
            {
                pk->move(u8(i), pksm::Move{be16(d + L.moves + 4 * i)});
                pk->PP(u8(i), d[L.moves + 4 * i + 2]);
                pk->PPUp(u8(i), d[L.moves + 4 * i + 3]);
            }
            for (int i = 0; i < 6; i++)
            {
                pk->ev(GC_ORDER[i], u8(std::min<u16>(255, be16(d + L.evs + 2 * i))));
                pk->iv(GC_ORDER[i], u8(std::min<u16>(31, L.ivBytes ? d[L.ivs + i] : be16(d + L.ivs + 2 * i))));
            }
            pk->otFriendship(u8(be16(d + L.friendship)));
            for (u8 i = 0; i < 5; i++)
            {
                pk->contest(i, d[L.contest + i]);
            }
            pk->contest(5, d[L.sheen]);
            auto& p3 = static_cast<pksm::PK3&>(*pk);
            for (u8 i = 0; i < 5; i++)
            {
                p3.contestRibbonCount(i, std::min<u8>(4, d[L.ribbonCounts + i]));
            }
            bool egg, abilityBit;
            u8 strain, days, marks;
            bool fateful;
            if (xd)
            {
                u16 rib = be16(d + 0x7C);
                for (int i = 0; i < 12; i++)
                {
                    pk->ribbon(RIBBONS[i], (rib >> (15 - i)) & 1);
                }
                egg        = d[0x1D] >> 7 & 1;
                abilityBit = d[0x1D] >> 6 & 1;
                strain     = d[0x13] & 0xF;
                days       = s8(d[0x15]) > 0 ? d[0x15] : 0;
                marks      = d[0x14];
                fateful    = d[0x33] != 0 || d[0x30] == 1;
                // As PKHeX: a Shadow Pokémon leaving XD is purified first and keeps its obedience.
                if (g3Version(d[L.version]) == 15 && be16(d + L.shadow))
                {
                    pk->ribbon(pksm::Ribbon::National, true);
                    fateful = true;
                }
            }
            else
            {
                for (int i = 0; i < 12; i++)
                {
                    pk->ribbon(RIBBONS[i], d[0xBD + i] == 1);
                }
                egg        = d[0xCB] == 1;
                abilityBit = d[0xCC] == 1;
                strain     = d[0xCA] & 0xF;
                days       = s8(d[0xD0]) > 0 ? d[0xD0] : 0;
                marks      = d[0xCF];
                fateful    = ((d[0xC9] >> 4 & 1) != 0) != ((d[0xFB] & 1) != 0);
                (void)japanese;
            }
            // Markings: bits 1 and 2 swapped on the GameCube.
            pk->markValue(u16((marks & ~6) | ((marks >> 1) & 2) | ((marks << 1) & 4)));
            pk->pkrsStrain(strain);
            pk->pkrsDays(days);
            pk->fatefulEncounter(fateful);
            pk->egg(egg);
            pk->PID(be32(d + L.pid));
            p3.abilityBit(abilityBit);
            pk->otName(gcString(d + L.ot, 10));
            pk->nickname(gcString(d + L.nick, 10));
            pk->nicknamed(true);
            pk->refreshChecksum();
            if (party)
            {
                pk->updatePartyData();
            }
            return pk;
        }

        // Writes a PK3 into a CK3/XK3 slot. What PK3 has no room for (Shadow data, the game's own bytes) is
        // kept when the slot already holds this same Pokémon.
        void fromPK3(u8* d, const pksm::PKX& pk, bool xd, bool japanese, u8 region)
        {
            const Layout& L = xd ? XK3 : CK3;
            u16 internal    = u16(pksm::SpeciesConverter::nationalToG3(pk.species()));
            bool same       = be16(d + L.species) == internal && be32(d + L.pid) == pk.PID() && internal;
            if (!same)
            {
                memset(d, 0, L.size);
            }
            if (u16(pk.species()) == 0)
            {
                memset(d, 0, L.size);
                return;
            }
            be16(d + L.species, internal);
            be32(d + L.pid, pk.PID());
            d[L.version]    = gcVersion(u8(pk.version()));
            d[L.curRegion]  = region;
            d[L.origRegion] = region;
            d[L.lang]       = gcLanguage(u8(pk.language()));
            be16(d + L.metLoc, pk.metLocation());
            d[L.metLevel] = pk.metLevel();
            d[L.ball]     = u8(pk.ball());
            d[L.otGender] = u8(pk.otGender()) & 1;
            be16(d + L.id32, pk.SID());
            be16(d + L.id32 + 2, pk.TID());
            be16(d + L.item, pk.heldItem());
            be32(d + L.exp, pk.experience());
            d[L.level] = pk.level();
            for (int i = 0; i < 4; i++)
            {
                be16(d + L.moves + 4 * i, u16(pk.move(u8(i))));
                d[L.moves + 4 * i + 2] = pk.PP(u8(i));
                d[L.moves + 4 * i + 3] = pk.PPUp(u8(i));
            }
            for (int i = 0; i < 6; i++)
            {
                be16(d + L.evs + 2 * i, pk.ev(GC_ORDER[i]));
                if (L.ivBytes)
                {
                    d[L.ivs + i] = pk.iv(GC_ORDER[i]);
                }
                else
                {
                    be16(d + L.ivs + 2 * i, pk.iv(GC_ORDER[i]));
                }
                be16(d + L.stats + 2 * i, u16(pk.stat(GC_ORDER[i])));
            }
            be16(d + L.hpCur, u16(pk.stat(pksm::Stat::HP)));
            be16(d + L.friendship, pk.otFriendship());
            for (u8 i = 0; i < 5; i++)
            {
                d[L.contest + i] = pk.contest(i);
            }
            d[L.sheen] = pk.contest(5);
            auto& p3   = static_cast<const pksm::PK3&>(pk);
            for (u8 i = 0; i < 5; i++)
            {
                d[L.ribbonCounts + i] = p3.contestRibbonCount(i);
            }
            u16 marks = pk.markValue();
            u8 gcMarks = u8((marks & ~6) | ((marks >> 1) & 2) | ((marks << 1) & 4));
            if (xd)
            {
                u16 rib = be16(d + 0x7C) & 0xF;
                for (int i = 0; i < 12; i++)
                {
                    rib |= u16(pk.ribbon(RIBBONS[i]) ? 1 : 0) << (15 - i);
                }
                be16(d + 0x7C, rib);
                u8 f = d[0x1D] & 0x1F & ~(1 << 5);
                f |= u8(p3.abilityBit() ? 1 << 6 : 0) | u8(pk.egg() ? 1 << 7 : 0);
                d[0x1D] = f;
                d[0x13] = pk.pkrsStrain() & 0xF;
                d[0x15] = pk.pkrsDays() ? u8(pk.pkrsDays() & 0xF) : 0xFF;
                d[0x14] = gcMarks;
                if (d[0x33] == 0)
                {
                    d[0x30] = pk.fatefulEncounter() ? 1 : 0; // obedient
                }
            }
            else
            {
                for (int i = 0; i < 12; i++)
                {
                    d[0xBD + i] = pk.ribbon(RIBBONS[i]) ? 1 : 0;
                }
                d[0xCB] = pk.egg() ? 1 : 0;
                d[0xCC] = p3.abilityBit() ? 1 : 0;
                d[0xCD] = 0; // valid
                d[0xCA] = pk.pkrsStrain() & 0xF;
                d[0xD0] = pk.pkrsDays() ? u8(pk.pkrsDays() & 0xF) : 0xFF;
                d[0xCF] = gcMarks;
                bool fateful = pk.fatefulEncounter();
                d[0xC9]      = u8((d[0xC9] & ~16) | (japanese && fateful ? 16 : 0));
                d[0xFB]      = u8((d[0xFB] & ~1) | (!japanese && fateful ? 1 : 0));
            }
            gcString(d + L.ot, 22, pk.otName(), 10);
            gcString(d + L.nick, 22, pk.nickname(), 10);
            memcpy(d + L.nickDisplay, d + L.nick, 22);
            if (region == 0) // NTSC-J shows 5 characters at most
            {
                memset(d + L.nickDisplay + 10, 0, 12);
            }
        }

        // ---------------------------------------------------------------- the save
        class SavCXD : public pksm::Sav
        {
        public:
            SavCXD(const std::shared_ptr<u8[]>& file, u32 length, bool xd, u32 base) : Sav(file, length), xd(xd), base(base)
            {
                game       = Game::E;
                u32 size   = xd ? XD_SLOT : COLO_SLOT;
                int slots  = xd ? 2 : 3;
                int best   = -1;
                s32 count  = -1;
                for (int i = 0; i < slots; i++)
                {
                    s32 c = s32(be32(file.get() + base + SLOT_START + i * size + 4));
                    if (c > count)
                    {
                        count = c;
                        best  = i;
                    }
                }
                slotIndex = std::max(best, 0);
                slot.assign(file.get() + base + SLOT_START + slotIndex * size, file.get() + base + SLOT_START + (slotIndex + 1) * size);
                if (xd)
                {
                    xdCrypt(slot.data(), false);
                    for (int i = 0; i < 16; i++)
                    {
                        subLength[i] = be16(slot.data() + 0x20 + 2 * i);
                        subOffset[i] = u32(be16(slot.data() + 0x40 + 4 * i)) | u32(be16(slot.data() + 0x40 + 4 * i + 2)) << 16;
                    }
                    config   = subOffset[0] + 0xA8;
                    trainer  = subOffset[1] + 0xA8;
                    partyAt  = trainer + 0x30;
                    boxAt    = subOffset[2] + 0xA8;
                    japanese = subLength[7] == 0x1E00;
                }
                else
                {
                    coloDecrypt(slot.data());
                    config   = 0x08;
                    trainer  = 0x78;
                    partyAt  = 0xA8;
                    boxAt    = 0xB90;
                    japanese = file.get()[base] == 0x83;
                }
            }

            bool xd;
            u32 base;
            int slotIndex = 0;
            std::vector<u8> slot; // the newest save slot, decrypted
            u16 subLength[16]{};
            u32 subOffset[16]{};
            u32 config = 0, trainer = 0, partyAt = 0, boxAt = 0;
            bool japanese = false;

            u32 monSize() const { return xd ? XK3_SIZE : CK3_SIZE; }
            u8 region() const { return slot[config + 1]; }
            u8* boxInfo(u8 box) { return slot.data() + boxAt + box * (30 * monSize() + 0x14); }
            const u8* boxInfo(u8 box) const { return slot.data() + boxAt + box * (30 * monSize() + 0x14); }

            void finishEditing() override
            {
                std::vector<u8> out = slot;
                u32 size            = xd ? XD_SLOT : COLO_SLOT;
                if (xd)
                {
                    xdChecksums(out.data(), subOffset[0]);
                    xdCrypt(out.data(), true);
                }
                else
                {
                    coloChecksums(out.data());
                    coloEncrypt(out.data());
                }
                memcpy(data.get() + base + SLOT_START + slotIndex * size, out.data(), size);
            }
            void beginEditing() override {}

            u16 TID() const override { return be16(slot.data() + (xd ? trainer + 0x2E : 0xA6)); }
            void TID(u16 v) override { be16(slot.data() + (xd ? trainer + 0x2E : 0xA6), v); }
            u16 SID() const override { return be16(slot.data() + (xd ? trainer + 0x2C : 0xA4)); }
            void SID(u16 v) override { be16(slot.data() + (xd ? trainer + 0x2C : 0xA4), v); }
            pksm::GameVersion version() const override { return pksm::GameVersion::E; }
            void version(pksm::GameVersion) override {}
            pksm::Gender gender() const override { return pksm::Gender{u8(slot[xd ? trainer + 0x8E0 : 0xAF8] & 1)}; }
            void gender(pksm::Gender v) override { slot[xd ? trainer + 0x8E0 : 0xAF8] = u8(v); }
            u8 subRegion() const override { return 0; }
            void subRegion(u8) override {}
            u8 country() const override { return 0; }
            void country(u8) override {}
            u8 consoleRegion() const override { return 0; }
            void consoleRegion(u8) override {}
            pksm::Language language() const override { return pksm::Language{mainLanguage(slot[config + 3])}; }
            void language(pksm::Language v) override { slot[config + 3] = gcLanguage(u8(v)); }
            std::string otName() const override { return gcString(slot.data() + trainer, 10); }
            void otName(const std::string_view& v) override
            {
                gcString(slot.data() + trainer, 20, std::string(v), 10);
                if (!xd)
                {
                    gcString(slot.data() + 0x8C, 20, std::string(v), 10);
                }
            }
            u32 money() const override { return be32(slot.data() + (xd ? trainer + 0x8E4 : 0xAFC)); }
            void money(u32 v) override { be32(slot.data() + (xd ? trainer + 0x8E4 : 0xAFC), std::min<u32>(v, 9999999)); }
            u32 BP() const override { return 0; }
            void BP(u32) override {}
            u8 badges() const override { return 0; }

            double seconds() const
            {
                const u8* p = slot.data() + config;
                if (xd && !japanese)
                {
                    u64 bits = u64(be32(p + 0x30)) << 32 | be32(p + 0x34);
                    double d;
                    memcpy(&d, &bits, 8);
                    return d;
                }
                u32 bits = be32(p + 0x20);
                float f;
                memcpy(&f, &bits, 4);
                return f;
            }
            void seconds(double s)
            {
                u8* p = slot.data() + config;
                if (xd && !japanese)
                {
                    u64 bits;
                    memcpy(&bits, &s, 8);
                    be32(p + 0x30, u32(bits >> 32));
                    be32(p + 0x34, u32(bits));
                    return;
                }
                float f = float(s);
                u32 bits;
                memcpy(&bits, &f, 4);
                be32(p + 0x20, bits);
            }
            u16 playedHours() const override { return u16(seconds() / 3600); }
            void playedHours(u16 v) override { seconds(std::fmod(seconds(), 3600.0) + v * 3600.0); }
            u8 playedMinutes() const override { return u8(std::fmod(seconds(), 3600.0) / 60); }
            void playedMinutes(u8 v) override
            {
                double s = seconds();
                seconds(s - std::fmod(s, 3600.0) + v * 60.0 + std::fmod(s, 60.0));
            }
            u8 playedSeconds() const override { return u8(std::fmod(seconds(), 60.0)); }
            void playedSeconds(u8 v) override
            {
                double s = seconds();
                seconds(s - std::fmod(s, 60.0) + v);
            }

            u8 currentBox() const override { return 0; }
            void currentBox(u8) override {}
            u8 unlockedBoxes() const override { return u8(maxBoxes()); }
            void unlockedBoxes(u8) override {}
            u8 legendBoxUnlockSize() const override { return 0; }
            u32 boxOffset(u8 box, u8 slotNo) const override { return boxAt + box * (30 * monSize() + 0x14) + 0x14 + slotNo * monSize(); }
            u32 partyOffset(u8 slotNo) const override { return partyAt + slotNo * monSize(); }

            std::unique_ptr<pksm::PKX> pkm(u8 slotNo) const override
            {
                return toPK3(slot.data() + partyOffset(slotNo), xd, japanese, true);
            }
            void pkm(const pksm::PKX& pk, u8 slotNo) override
            {
                fromPK3(slot.data() + partyOffset(slotNo), pk, xd, japanese, region());
            }
            std::unique_ptr<pksm::PKX> pkm(u8 box, u8 slotNo) const override
            {
                return toPK3(slot.data() + boxOffset(box, slotNo), xd, japanese, false);
            }
            void pkm(const pksm::PKX& pk, u8 box, u8 slotNo, bool) override
            {
                fromPK3(slot.data() + boxOffset(box, slotNo), pk, xd, japanese, region());
            }
            void trade(pksm::PKX&, const Date&) const override {}
            std::unique_ptr<pksm::PKX> emptyPkm() const override
            {
                return pksm::PKX::getPKM<pksm::Generation::THREE>(nullptr, pksm::PK3::BOX_LENGTH);
            }
            void dex(const pksm::PKX& pk) override
            {
                if (!xd && !pk.egg() && u16(pk.species()) && u16(pk.species()) <= 386 && memoState(u16(pk.species())) < 2)
                {
                    memoSet(u16(pk.species()), 2, pk.PID(), u32(pk.SID()) << 16 | pk.TID());
                }
            }
            int dexSeen() const override
            {
                if (xd)
                {
                    return 0;
                }
                return memoCount();
            }
            int dexCaught() const override
            {
                int n = 0;
                for (int i = 0; !xd && i < memoCount(); i++)
                {
                    const u8* e = memoEntry(i);
                    n += (e[0] >> 6 & 1) || !(e[0] >> 7 & 1);
                }
                return n;
            }
            int currentGiftAmount() const override { return 0; }
            std::unique_ptr<pksm::WCX> mysteryGift(int) const override { return nullptr; }
            void mysteryGift(const pksm::WCX&, int&) override {}
            void cryptBoxData(bool) override {}
            std::string boxName(u8 box) const override { return gcString(boxInfo(box), 8); }
            void boxName(u8 box, const std::string_view& name) override { gcString(boxInfo(box), 16, std::string(name), 8); }
            u8 boxWallpaper(u8) const override { return 0; }
            void boxWallpaper(u8, const u8) override {}
            u8 partyCount() const override
            {
                u8 n = 0;
                for (u8 i = 0; i < 6; i++)
                {
                    n += be16(slot.data() + partyOffset(i)) != 0;
                }
                return n;
            }
            void partyCount(u8) override {} // the games count the filled slots
            int maxBoxes() const override { return xd ? 8 : 3; }
            size_t maxWondercards() const override { return 0; }
            pksm::Generation generation() const override { return pksm::Generation::THREE; }
            // The bag (PKHeX PlayerBag3Colosseum / PlayerBag3XD): big-endian Gen 3 item ids and counts. The
            // GameCube's own key items, discs and colognes keep their places but are not listed here.
            u32 pouchAt(Pouch p) const
            {
                u32 base = xd ? trainer + 0x4C8 : 0x7F8;
                switch (p)
                {
                    case Pouch::NormalItem: return base;
                    case Pouch::Ball: return base + (xd ? 0x124 : 0x0FC);
                    case Pouch::TM: return base + (xd ? 0x164 : 0x13C);
                    case Pouch::Berry: return base + (xd ? 0x264 : 0x23C);
                    default: return 0;
                }
            }
            void item(const pksm::Item& it, Pouch p, u16 index) override
            {
                u32 at = pouchAt(p);
                if (!at)
                {
                    return;
                }
                u16 g3 = it.id() ? u16(pksm::ItemConverter::nationalToG3(it.id())) : 0;
                be16(slot.data() + at + index * 4, g3);
                be16(slot.data() + at + index * 4 + 2, g3 ? it.count() : 0);
            }
            std::unique_ptr<pksm::Item> item(Pouch p, u16 slotNo) const override
            {
                u32 at = pouchAt(p);
                if (!at)
                {
                    return std::make_unique<ItemPlain>();
                }
                u16 g3 = be16(slot.data() + at + slotNo * 4);
                u16 n  = g3 && g3 < 377 ? u16(pksm::ItemConverter::g3ToNational(g3)) : 0;
                return std::make_unique<ItemPlain>(n, n ? be16(slot.data() + at + slotNo * 4 + 2) : 0);
            }
            u16 maxCount(Pouch p) const override { return xd || p == Pouch::Berry ? 999 : 99; }
            SmallVector<std::pair<Pouch, int>, 15> pouches() const override
            {
                return {std::pair{Pouch::NormalItem, xd ? 30 : 20}, std::pair{Pouch::Ball, 16}, std::pair{Pouch::TM, 64},
                    std::pair{Pouch::Berry, 46}};
            }
            SmallVector<std::pair<Pouch, std::span<const int>>, 15> validItems() const override
            {
                static std::vector<int> general, balls, tms, berries;
                if (general.empty())
                {
                    static const u16 g[] = {13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                        33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 63, 64, 65, 66, 67, 68,
                        69, 70, 71, 73, 74, 75, 76, 77, 78, 79, 80, 81, 83, 84, 85, 86, 93, 94, 95, 96, 97, 98, 103, 104,
                        106, 107, 108, 109, 110, 111, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 179, 180,
                        181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200,
                        201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
                        221, 222, 223, 224, 225, 254, 255, 256, 257, 258};
                    for (u16 i : g)
                    {
                        general.push_back(int(pksm::ItemConverter::g3ToNational(i)));
                    }
                    for (u16 i = 1; i <= 12; i++)
                    {
                        balls.push_back(int(pksm::ItemConverter::g3ToNational(i)));
                    }
                    for (u16 i = 289; i <= 338; i++)
                    {
                        tms.push_back(int(pksm::ItemConverter::g3ToNational(i)));
                    }
                    for (u16 i = 133; i <= 175; i++)
                    {
                        berries.push_back(int(pksm::ItemConverter::g3ToNational(i)));
                    }
                }
                return {std::pair{Pouch::NormalItem, std::span<const int>{general}}, std::pair{Pouch::Ball, std::span<const int>{balls}},
                    std::pair{Pouch::TM, std::span<const int>{tms}}, std::pair{Pouch::Berry, std::span<const int>{berries}}};
            }

            // Colosseum's Strategy Memo (PKHeX StrategyMemo): 12-byte entries after a count; the first Pokémon met
            // of each species, seen and owned flags in the top bits. XD's memo is left alone, as PKHeX leaves it.
            u32 memoAt() const { return 0x82B0; }
            int memoCount() const { return std::min<int>(be16(slot.data() + memoAt()), 500); }
            u8* memoEntry(int i) { return slot.data() + memoAt() + 4 + 12 * i; }
            const u8* memoEntry(int i) const { return slot.data() + memoAt() + 4 + 12 * i; }
            int memoFind(u16 national) const
            {
                u16 internal = u16(pksm::SpeciesConverter::nationalToG3(pksm::Species{national}));
                for (int i = 0; i < memoCount(); i++)
                {
                    if ((be16(memoEntry(i)) & 0x1FF) == internal)
                    {
                        return i;
                    }
                }
                return -1;
            }
            // 0 unseen, 1 seen, 2 owned
            int memoState(u16 national) const
            {
                int i = memoFind(national);
                if (i < 0)
                {
                    return 0;
                }
                const u8* e = memoEntry(i);
                bool flag0 = e[0] >> 6 & 1, flag1 = e[0] >> 7 & 1;
                return (flag0 || !flag1) ? 2 : 1;
            }
            void memoSet(u16 national, int state, u32 pid = 0, u32 id32 = 0)
            {
                int i = memoFind(national);
                if (state == 0)
                {
                    if (i >= 0)
                    {
                        // Close the gap: the entries after it move up one.
                        int n = memoCount();
                        memmove(memoEntry(i), memoEntry(i + 1), size_t(12 * (n - i - 1)));
                        memset(memoEntry(n - 1), 0, 12);
                        be16(slot.data() + memoAt(), u16(n - 1));
                    }
                    return;
                }
                if (i < 0)
                {
                    i = memoCount();
                    if (i >= 500)
                    {
                        return;
                    }
                    u8* e = memoEntry(i);
                    memset(e, 0, 12);
                    be16(e, u16(pksm::SpeciesConverter::nationalToG3(pksm::Species{national}) & 0x1FF));
                    be32(e + 4, id32);
                    be32(e + 8, pid);
                    be16(slot.data() + memoAt(), u16(i + 1));
                }
                u8* e = memoEntry(i);
                e[0]  = u8((e[0] & 0x3F) | (state == 2 ? 0 : 0x80));
            }
        };

        std::set<const pksm::Sav*>& known()
        {
            static std::set<const pksm::Sav*> s;
            return s;
        }

        struct Tracked : SavCXD
        {
            using SavCXD::SavCXD;
            ~Tracked() override { known().erase(this); }
        };
    }

    std::unique_ptr<pksm::Sav> load(const std::shared_ptr<u8[]>& data, u32 length)
    {
        u32 base = 0;
        if (length == COLO_SIZE + GCI_HEADER || length == XD_SIZE + GCI_HEADER)
        {
            base = GCI_HEADER;
        }
        u32 body = length - base;
        bool xd;
        if (body == COLO_SIZE)
        {
            xd = false;
        }
        else if (body == XD_SIZE)
        {
            xd = true;
        }
        else
        {
            return nullptr;
        }
        if (base)
        {
            // Dolphin's GCI header names the game: GC6x Colosseum, GXXx XD.
            const char* code = reinterpret_cast<const char*>(data.get());
            if (xd ? memcmp(code, "GXX", 3) != 0 : memcmp(code, "GC6", 3) != 0)
            {
                return nullptr;
            }
        }
        auto sav = std::make_unique<Tracked>(data, length, xd, base);
        known().insert(sav.get());
        return sav;
    }

    bool is(const pksm::Sav& sav) { return known().count(&sav) != 0; }
    int dexState(const pksm::Sav& sav, u16 species) { return static_cast<const SavCXD&>(sav).memoState(species); }
    void dexState(pksm::Sav& sav, u16 species, int state)
    {
        auto& s = static_cast<SavCXD&>(sav);
        s.memoSet(species, state, 0, u32(s.SID()) << 16 | s.TID());
    }
    bool isXD(const pksm::Sav& sav) { return is(sav) && static_cast<const SavCXD&>(sav).xd; }
    const char* name(const pksm::Sav& sav) { return isXD(sav) ? "XD: Gale of Darkness" : "Colosseum"; }
}
