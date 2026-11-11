#include "romhack.hpp"
#include "files.hpp"

#include "personal/personal.hpp"
#include "pkx/PK3.hpp"
#include "sav/Item.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

// Layouts from the Android PKForge (src/PKForge.Engine/RadicalRed, Unbound, GsChronicles; GPLv3).
namespace romhack
{
    namespace
    {
        u16 rd16(const u8* p) { return u16(p[0] | p[1] << 8); }
        u32 rd32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
        void wr16(u8* p, u16 v)
        {
            p[0] = u8(v);
            p[1] = u8(v >> 8);
        }
        void wr32(u8* p, u32 v)
        {
            for (int i = 0; i < 4; i++)
            {
                p[i] = u8(v >> (8 * i));
            }
        }

        // ---------------------------------------------------------------- the hack's tables
        struct Tables
        {
            std::map<u16, std::pair<u16, u8>> species; // hack id -> national, form
            std::map<u32, u16> speciesOf;               // national << 8 | form -> lowest hack id
            std::map<u16, u16> move, moveOf, item, itemOf;
            std::map<u16, std::array<u16, 3>> abilities; // Unbound only
        };

        const char* fileOf(Hack h)
        {
            return h == Hack::RadicalRed ? "radicalred" : h == Hack::Unbound ? "unbound" : "gschronicles";
        }

        const Tables& tables(Hack h)
        {
            static std::map<Hack, Tables> cache;
            auto it = cache.find(h);
            if (it != cache.end())
            {
                return it->second;
            }
            Tables& t = cache[h];
            std::string text;
            files::readText(std::string("romfs:/hacks/") + fileOf(h) + ".txt", text);
            size_t a = 0;
            while (a < text.size())
            {
                size_t b = text.find('\n', a);
                std::string line = text.substr(a, b == std::string::npos ? std::string::npos : b - a);
                a = b == std::string::npos ? text.size() : b + 1;
                int v[5] = {0, 0, 0, 0, 0};
                if (line.size() < 3)
                {
                    continue;
                }
                sscanf(line.c_str() + 2, "%d\t%d\t%d\t%d\t%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
                switch (line[0])
                {
                    case 'S':
                        if (v[1])
                        {
                            t.species[u16(v[0])] = {u16(v[1]), u8(v[2])};
                            t.speciesOf.emplace(u32(v[1]) << 8 | u8(v[2]), u16(v[0]));
                        }
                        break;
                    case 'M':
                        if (v[1])
                        {
                            t.move[u16(v[0])] = u16(v[1]);
                            t.moveOf.emplace(u16(v[1]), u16(v[0]));
                        }
                        break;
                    case 'I':
                        if (v[1])
                        {
                            t.item[u16(v[0])] = u16(v[1]);
                            t.itemOf.emplace(u16(v[1]), u16(v[0]));
                        }
                        break;
                    case 'A': t.abilities[u16(v[0])] = {u16(v[1]), u16(v[2]), u16(v[3])}; break;
                    default: break;
                }
            }
            return t;
        }

        // CFRU ball ids (item-table order) <-> the games' (UnboundMon.BallToPkHeX).
        const u8 BALL_TO_NATIONAL[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0, 17, 18, 19, 20, 21, 22,
            23, 24, 26, 25};

        // ---------------------------------------------------------------- the Pokémon
        constexpr int PC_SIZE = 58, PARTY_SIZE = 100;

        // The 58-byte PC record <-> the 100-byte party layout (CfruEntity.Compact and its inverse).
        void expand(const u8* c, u8* p)
        {
            memset(p, 0, PARTY_SIZE);
            memcpy(p, c, 0x1C);
            memcpy(p + 0x20, c + 0x1C, 0x0B);
            u64 packed = 0;
            for (int i = 0; i < 5; i++)
            {
                packed |= u64(c[0x27 + i]) << (8 * i);
            }
            for (int i = 0; i < 4; i++)
            {
                wr16(p + 0x2C + 2 * i, u16((packed >> (10 * i)) & 0x3FF));
            }
            memcpy(p + 0x38, c + 0x2C, 6);
            memcpy(p + 0x44, c + 0x32, 8);
        }
        void compact(const u8* p, u8* c)
        {
            memset(c, 0, PC_SIZE);
            memcpy(c, p, 0x1C);
            memcpy(c + 0x1C, p + 0x20, 0x0B);
            u64 packed = 0;
            for (int i = 0; i < 4; i++)
            {
                packed |= u64(rd16(p + 0x2C + 2 * i) & 0x3FF) << (10 * i);
            }
            for (int i = 0; i < 5; i++)
            {
                c[0x27 + i] = u8(packed >> (8 * i));
            }
            memcpy(c + 0x2C, p + 0x38, 6);
            memcpy(c + 0x32, p + 0x44, 8);
        }
        // PKSM-Core's PK3 decrypts whatever fails its checksum; the hacks keep plain data, so the checksum is
        // made to agree first.
        void sealChecksum(u8* p)
        {
            u16 sum = 0;
            for (int i = 0x20; i < 0x50; i += 2)
            {
                sum = u16(sum + rd16(p + i));
            }
            wr16(p + 0x1C, sum);
        }

        std::set<const pksm::PKX*>& livePk()
        {
            static std::set<const pksm::PKX*> s;
            return s;
        }

        class PKHack : public pksm::PK3
        {
        public:
            Hack hack;

            static std::unique_ptr<PKHack> make(Hack h, const u8* party100)
            {
                u8 buf[PARTY_SIZE];
                memcpy(buf, party100, PARTY_SIZE);
                sealChecksum(buf);
                return std::unique_ptr<PKHack>(new PKHack(h, buf));
            }
            ~PKHack() override { livePk().erase(this); }

            const Tables& t() const { return tables(hack); }
            u16 rawSpecies() const { return rd16(rawData().data() + 0x20); }

            pksm::Species species() const override
            {
                auto it = t().species.find(rawSpecies());
                return pksm::Species{it == t().species.end() ? u16(0) : it->second.first};
            }
            void species(pksm::Species v) override
            {
                auto it = t().speciesOf.find(u32(u16(v)) << 8);
                wr16(rawData().data() + 0x20, it == t().speciesOf.end() ? 0 : it->second);
                flagHasSpecies(u16(v) != 0);
            }
            u16 alternativeForm() const override
            {
                auto it = t().species.find(rawSpecies());
                return it == t().species.end() ? 0 : it->second.second;
            }
            void alternativeForm(u16 v) override
            {
                auto it = t().speciesOf.find(u32(u16(species())) << 8 | u8(v));
                if (it != t().speciesOf.end())
                {
                    wr16(rawData().data() + 0x20, it->second);
                }
            }
            u16 heldItem() const override
            {
                auto it = t().item.find(rd16(rawData().data() + 0x22));
                return it == t().item.end() ? 0 : it->second;
            }
            void heldItem(u16 v) override
            {
                auto it = t().itemOf.find(v);
                wr16(rawData().data() + 0x22, v && it != t().itemOf.end() ? it->second : 0);
            }
            pksm::Move move(u8 i) const override
            {
                auto it = t().move.find(rd16(rawData().data() + 0x2C + 2 * i));
                u16 m   = it == t().move.end() ? 0 : it->second;
                return pksm::Move{m};
            }
            void move(u8 i, pksm::Move v) override
            {
                auto it = t().moveOf.find(u16(v));
                wr16(rawData().data() + 0x2C + 2 * i, u16(v) && it != t().moveOf.end() ? it->second : 0);
            }
            pksm::Ball ball() const override
            {
                u8 b = rawData().data()[0x2A];
                return pksm::Ball{b < sizeof(BALL_TO_NATIONAL) ? BALL_TO_NATIONAL[b] : u8(4)};
            }
            void ball(pksm::Ball v) override
            {
                for (u8 i = 0; i < sizeof(BALL_TO_NATIONAL); i++)
                {
                    if (BALL_TO_NATIONAL[i] == u8(v) && u8(v))
                    {
                        rawData().data()[0x2A] = i;
                        return;
                    }
                }
                rawData().data()[0x2A] = 3;
            }
            // Bit 31 of the IV word is the hidden ability in the CFRU hacks (not the second-ability bit).
            bool hidden() const { return rd32(rawData().data() + 0x48) >> 31; }
            void hidden(bool v)
            {
                u32 w = rd32(rawData().data() + 0x48);
                wr32(rawData().data() + 0x48, v ? (w | 0x80000000u) : (w & 0x7FFFFFFFu));
            }
            pksm::Ability abilities(u8 n) const override
            {
                auto it = t().abilities.find(rawSpecies());
                if (it != t().abilities.end())
                {
                    return pksm::Ability{it->second[std::min<u8>(n, 2)]};
                }
                return pksm::PersonalSV::ability(formSpecies(), std::min<u8>(n, 2));
            }
            pksm::Ability ability() const override
            {
                if (hidden())
                {
                    return abilities(2);
                }
                return (PID() & 1) && u16(abilities(1)) ? abilities(1) : abilities(0);
            }
            void ability(pksm::Ability v) override
            {
                if (v == abilities(2) && v != abilities(0) && v != abilities(1))
                {
                    hidden(true);
                }
                else
                {
                    hidden(false);
                }
            }
            void setAbility(u8 n) override { hidden(n >= 2); }
            u8 abilityNumber() const override { return hidden() ? 4 : (PID() & 1) ? 2 : 1; }
            void abilityNumber(u8 v) override { hidden(v == 4); }
            u16 formSpecies() const override
            {
                u16 s = u16(species());
                u16 f = alternativeForm();
                if (!f)
                {
                    return s;
                }
                u16 base = pksm::PersonalSV::formStatIndex(s);
                return base ? u16(base + f - 1) : s;
            }
            u8 baseHP() const override { return pksm::PersonalSV::baseHP(formSpecies()); }
            u8 baseAtk() const override { return pksm::PersonalSV::baseAtk(formSpecies()); }
            u8 baseDef() const override { return pksm::PersonalSV::baseDef(formSpecies()); }
            u8 baseSpe() const override { return pksm::PersonalSV::baseSpe(formSpecies()); }
            u8 baseSpa() const override { return pksm::PersonalSV::baseSpa(formSpecies()); }
            u8 baseSpd() const override { return pksm::PersonalSV::baseSpd(formSpecies()); }
            pksm::Type type1() const override { return pksm::PersonalSV::type1(formSpecies()); }
            pksm::Type type2() const override { return pksm::PersonalSV::type2(formSpecies()); }
            u8 genderType() const override { return pksm::PersonalSV::gender(formSpecies()); }
            u8 baseFriendship() const override { return pksm::PersonalSV::baseFriendship(formSpecies()); }
            u8 expType() const override { return pksm::PersonalSV::expType(formSpecies()); }
            void refreshChecksum() override {}
            bool isEncrypted() const override { return false; }
            void encrypt() override {}
            void decrypt() override {}
            std::unique_ptr<pksm::PKX> clone() const override
            {
                u8 buf[PARTY_SIZE] = {};
                memcpy(buf, rawData().data(), std::min<size_t>(getLength(), PARTY_SIZE));
                auto c = make(hack, buf);
                livePk().insert(c.get());
                return c;
            }

        private:
            PKHack(Hack h, u8* buf) : PK3(PrivateConstructor{}, buf, true, false), hack(h) {}
        };

        std::unique_ptr<pksm::PKX> track(std::unique_ptr<PKHack> p)
        {
            livePk().insert(p.get());
            return p;
        }

        // A Pokémon of any format as this hack's 100-byte party layout (national ids through the tables).
        void toHack(const pksm::PKX& pk, Hack h, u8* p)
        {
            if (romhack::is(pk) && static_cast<const PKHack&>(pk).hack == h)
            {
                memset(p, 0, PARTY_SIZE);
                memcpy(p, pk.rawData().data(), std::min<size_t>(pk.getLength(), PARTY_SIZE));
                return;
            }
            memset(p, 0, PARTY_SIZE);
            auto out = PKHack::make(h, p);
            out->PID(pk.PID());
            out->TID(pk.TID());
            out->SID(pk.SID());
            out->language(pk.language());
            out->species(pk.species());
            out->alternativeForm(pk.alternativeForm());
            out->nickname(pk.nickname());
            out->otName(pk.otName());
            out->otGender(pk.otGender());
            out->heldItem(pk.heldItem());
            out->experience(pk.experience());
            out->otFriendship(pk.otFriendship());
            out->ball(pk.ball());
            out->metLevel(std::min<u8>(pk.metLevel(), 100));
            out->version(pksm::GameVersion::FR);
            for (u8 i = 0; i < 4; i++)
            {
                out->move(i, pk.move(i));
                out->PPUp(i, pk.PPUp(i));
            }
            for (pksm::Stat st : {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD, pksm::Stat::SPATK,
                     pksm::Stat::SPDEF})
            {
                out->iv(st, std::min<u8>(pk.iv(st), 31));
                out->ev(st, std::min<u8>(u8(std::min<u16>(pk.ev(st), 255)), 255));
            }
            out->egg(pk.egg());
            out->setAbility(pk.abilityNumber() == 4 ? 2 : 0);
            out->healPP();
            memcpy(p, out->rawData().data(), PARTY_SIZE);
        }

        // ---------------------------------------------------------------- the save
        constexpr int SECTOR = 0x1000;
        const u16 CFRU_WINDOWS[14] = {0xF24, 0xFF0, 0xFF0, 0xFF0, 0xD98, 0xFF0, 0xFF0, 0xFF0, 0xFF0, 0xFF0, 0xFF0, 0xFF0,
            0xFF0, 0x450};
        const u16 VANILLA_WINDOWS[14] = {0xF2C, 0xF80, 0xF80, 0xF80, 0xF08, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80,
            0xF80, 0xF80, 0x7D0};
        constexpr u32 GSC_SIGNATURE = 0x66290096, UNBOUND_SIGNATURE = 0x01121999;

        u16 checksum(const u8* p, int len)
        {
            u32 total = 0;
            len &= ~3;
            for (int i = 0; i < len; i += 4)
            {
                total += rd32(p + i);
            }
            return u16(total + (total >> 16));
        }

        int stamps(const u8* d, u32 sig)
        {
            int n = 0;
            for (int s = 0; s < 32; s++)
            {
                n += rd32(d + s * SECTOR + 0xFF8) == sig;
            }
            return n;
        }

        bool footer(const u8* d, int off, Hack h)
        {
            u32 sig = rd32(d + off + 0xFF8);
            return h == Hack::Unbound || (sig & 0xFFFFFF00u) == 0x08012000u || sig == GSC_SIGNATURE;
        }

        void sections(const u8* d, Hack h, int out[14])
        {
            u32 index[14] = {};
            for (int i = 0; i < 14; i++)
            {
                out[i] = -1;
            }
            for (int s = 0; s < 32; s++)
            {
                int off = s * SECTOR;
                if (!footer(d, off, h))
                {
                    continue;
                }
                u16 id = rd16(d + off + 0xFF4);
                if (id >= 14)
                {
                    continue;
                }
                u32 idx = rd32(d + off + 0xFFC);
                if (out[id] < 0 || idx >= index[id])
                {
                    index[id] = idx;
                    out[id]   = off;
                }
            }
        }

        bool isRadicalRed(const u8* d)
        {
            int sec[14];
            sections(d, Hack::RadicalRed, sec);
            int vanillaFailures = 0;
            for (int id = 0; id < 14; id++)
            {
                if (sec[id] < 0)
                {
                    return false;
                }
                u16 stored = rd16(d + sec[id] + 0xFF6);
                if (checksum(d + sec[id], CFRU_WINDOWS[id]) != stored)
                {
                    return false;
                }
                vanillaFailures += checksum(d + sec[id], VANILLA_WINDOWS[id]) != stored;
            }
            if (!vanillaFailures)
            {
                return false; // a stock FireRed save
            }
            u32 count = rd32(d + sec[1] + 0x34);
            if (count > 6)
            {
                return false;
            }
            return count == 0 || tables(Hack::RadicalRed).species.count(rd16(d + sec[1] + 0x38 + 0x20));
        }

        struct Byte
        {
            int file;    // offset in the save, -1 when none
            int section; // whose checksum covers it, -1 for none
        };

        class SavHack : public pksm::Sav
        {
        public:
            SavHack(const std::shared_ptr<u8[]>& d, u32 len, Hack h) : Sav(d, len), hack(h)
            {
                game = Game::FRLG;
                sections(d.get(), h, sec);
            }

            Hack hack;
            int sec[14];

            int boxes() const { return hack == Hack::Unbound ? 25 : hack == Hack::GSChronicles ? 25 : 22; }
            static constexpr int STRIDE = 30 * PC_SIZE;

            // Where byte i of a box slot lives in the file, and which section's checksum covers it.
            Byte slotByte(int box, int slot, int i) const
            {
                if (hack == Hack::Unbound)
                {
                    if (box <= 18)
                    {
                        int s = (box * 30 + slot) * PC_SIZE + i; // stream: sections 5-12, 0xFF0 after a 4-byte header
                        int id = 5 + s / 0xFF0;
                        return {sec[id] + 4 + s % 0xFF0, id};
                    }
                    switch (box)
                    {
                        case 19: return slot <= 20 ? Byte{0x1EB0C + slot * PC_SIZE + i, -1} : Byte{-1, -1};
                        case 20: return {0x1F1E8 + slot * PC_SIZE + i, -1};
                        case 21: return {0x1F8B4 + slot * PC_SIZE + i, -1};
                        case 22:
                            return slot <= 3 ? Byte{sec[2] + 0x0F18 + slot * PC_SIZE + i, -1}
                                             : Byte{sec[3] + 0x0010 + (slot - 4) * PC_SIZE + i, -1};
                        case 23: return {sec[3] + 0x05F4 + slot * PC_SIZE + i, -1};
                        default: return {sec[0] + 0xB0 + slot * PC_SIZE + i, -2}; // the preset box: section 0, short window
                    }
                }
                if (box < 19)
                {
                    int s = 4 + (box * 30 + slot) * PC_SIZE + i; // stream: sections 5-13 by their CFRU windows
                    for (int id = 5; id <= 13; id++)
                    {
                        if (s < CFRU_WINDOWS[id])
                        {
                            return {sec[id] + s, id};
                        }
                        s -= CFRU_WINDOWS[id];
                    }
                    return {-1, -1};
                }
                if (box < 22)
                {
                    // The raw sector-30/31 region: region offset 0xB0C on, 0xFF0 of each sector.
                    int r = 0xB0C + (box - 19) * STRIDE + slot * PC_SIZE + i;
                    return {r < 0xFF0 ? 0x1E000 + r : 0x1F000 + (r - 0xFF0), -1};
                }
                // GS Chronicles: boxes 23-25 inside the save blocks.
                static const int first[3] = {1, 1, 0}, offset[3] = {0x1F08, 0x1F08 + STRIDE, 0xB0};
                int k = box - 22;
                int b = offset[k] + slot * PC_SIZE + i;
                int id = first[k] + b / 0xFF0;
                return {sec[id] + b % 0xFF0, id};
            }

            void commit(int id)
            {
                u8* d = data.get();
                if (id == -2)
                {
                    wr16(d + sec[0] + 0xFF6, checksum(d + sec[0], 0xADC));
                    return;
                }
                if (id < 0)
                {
                    return;
                }
                int window = hack == Hack::Unbound ? 0xFF4 : CFRU_WINDOWS[id];
                // Every rotating copy of a section gets the live one, then its checksum (CommitSection).
                bool stream = id >= 5;
                for (int s = 0; s < 32; s++)
                {
                    int off = s * SECTOR;
                    if (!footer(d, off, hack) || rd16(d + off + 0xFF4) != id)
                    {
                        continue;
                    }
                    if (off != sec[id] && !stream)
                    {
                        memcpy(d + off, d + sec[id], SECTOR);
                    }
                    if (off == sec[id] || !stream)
                    {
                        wr16(d + off + 0xFF6, checksum(d + off, window));
                    }
                }
            }

            const u8* partyAt(u8 slot) const { return data.get() + sec[1] + 0x38 + slot * PARTY_SIZE; }

            void finishEditing() override {}
            void beginEditing() override {}
            u16 TID() const override { return rd16(data.get() + sec[0] + 0x0A); }
            void TID(u16 v) override
            {
                wr16(data.get() + sec[0] + 0x0A, v);
                commit(0);
            }
            u16 SID() const override { return rd16(data.get() + sec[0] + 0x0C); }
            void SID(u16 v) override
            {
                wr16(data.get() + sec[0] + 0x0C, v);
                commit(0);
            }
            pksm::GameVersion version() const override { return pksm::GameVersion::FR; }
            void version(pksm::GameVersion) override {}
            pksm::Gender gender() const override { return pksm::Gender{u8(data.get()[sec[0] + 8] & 1)}; }
            void gender(pksm::Gender v) override
            {
                data.get()[sec[0] + 8] = u8(v);
                commit(0);
            }
            u8 subRegion() const override { return 0; }
            void subRegion(u8) override {}
            u8 country() const override { return 0; }
            void country(u8) override {}
            u8 consoleRegion() const override { return 0; }
            void consoleRegion(u8) override {}
            pksm::Language language() const override { return pksm::Language::ENG; }
            void language(pksm::Language) override {}
            std::string otName() const override { return StringUtils::getString3(data.get(), sec[0], 7, false); }
            void otName(const std::string_view& v) override
            {
                StringUtils::setString3(data.get(), v, sec[0], 7, false);
                commit(0);
            }
            u32 money() const override
            {
                if (hack == Hack::Unbound)
                {
                    return 0;
                }
                return rd32(data.get() + sec[1] + 0x290) ^ rd32(data.get() + sec[0] + 0xF20);
            }
            void money(u32 v) override
            {
                if (hack == Hack::Unbound)
                {
                    return;
                }
                wr32(data.get() + sec[1] + 0x290, std::min<u32>(v, 999999) ^ rd32(data.get() + sec[0] + 0xF20));
                commit(1);
            }
            u32 BP() const override { return 0; }
            void BP(u32) override {}
            u8 badges() const override { return 0; }
            u16 playedHours() const override { return rd16(data.get() + sec[0] + 0x0E); }
            void playedHours(u16 v) override
            {
                wr16(data.get() + sec[0] + 0x0E, v);
                commit(0);
            }
            u8 playedMinutes() const override { return data.get()[sec[0] + 0x10]; }
            void playedMinutes(u8 v) override
            {
                data.get()[sec[0] + 0x10] = v;
                commit(0);
            }
            u8 playedSeconds() const override { return data.get()[sec[0] + 0x11]; }
            void playedSeconds(u8 v) override
            {
                data.get()[sec[0] + 0x11] = v;
                commit(0);
            }
            u8 currentBox() const override { return 0; }
            void currentBox(u8) override {}
            u8 unlockedBoxes() const override { return u8(boxes()); }
            void unlockedBoxes(u8) override {}
            u8 legendBoxUnlockSize() const override { return 0; }
            u32 boxOffset(u8 box, u8 slot) const override { return u32(std::max(slotByte(box, slot, 0).file, 0)); }
            u32 partyOffset(u8 slot) const override { return u32(sec[1] + 0x38 + slot * PARTY_SIZE); }

            std::unique_ptr<pksm::PKX> pkm(u8 slot) const override
            {
                if (slot >= partyCount())
                {
                    u8 empty[PARTY_SIZE] = {};
                    return track(PKHack::make(hack, empty));
                }
                return track(PKHack::make(hack, partyAt(slot)));
            }
            void pkm(const pksm::PKX& pk, u8 slot) override
            {
                u8 p[PARTY_SIZE];
                toHack(pk, hack, p);
                if (u16(pk.species()) == 0)
                {
                    memset(p, 0, PARTY_SIZE);
                }
                else
                {
                    auto h = PKHack::make(hack, p);
                    h->updatePartyData();
                    memcpy(p, h->rawData().data(), PARTY_SIZE);
                }
                memcpy(data.get() + sec[1] + 0x38 + slot * PARTY_SIZE, p, PARTY_SIZE);
                commit(1);
            }
            std::unique_ptr<pksm::PKX> pkm(u8 box, u8 slot) const override
            {
                u8 c[PC_SIZE] = {};
                for (int i = 0; i < PC_SIZE; i++)
                {
                    Byte b = slotByte(box, slot, i);
                    c[i]   = b.file >= 0 ? data.get()[b.file] : 0;
                }
                u8 p[PARTY_SIZE];
                expand(c, p);
                auto pk = PKHack::make(hack, p);
                if (u16(pk->species()) != 0 || rd16(c + 0x1C))
                {
                    pk->healPP();
                }
                return track(std::move(pk));
            }
            void pkm(const pksm::PKX& pk, u8 box, u8 slot, bool) override
            {
                u8 p[PARTY_SIZE], c[PC_SIZE];
                toHack(pk, hack, p);
                compact(p, c);
                if (u16(pk.species()) == 0)
                {
                    memset(c, 0, PC_SIZE);
                }
                std::set<int> touched;
                for (int i = 0; i < PC_SIZE; i++)
                {
                    Byte b = slotByte(box, slot, i);
                    if (b.file >= 0)
                    {
                        data.get()[b.file] = c[i];
                        touched.insert(b.section);
                    }
                }
                for (int id : touched)
                {
                    commit(id);
                }
            }
            void trade(pksm::PKX&, const Date&) const override {}
            std::unique_ptr<pksm::PKX> emptyPkm() const override
            {
                u8 empty[PARTY_SIZE] = {};
                return track(PKHack::make(hack, empty));
            }
            void dex(const pksm::PKX&) override {}
            int dexSeen() const override { return 0; }
            int dexCaught() const override { return 0; }
            int currentGiftAmount() const override { return 0; }
            std::unique_ptr<pksm::WCX> mysteryGift(int) const override { return nullptr; }
            void mysteryGift(const pksm::WCX&, int&) override {}
            void cryptBoxData(bool) override {}
            std::string boxName(u8 box) const override { return "Box " + std::to_string(box + 1); }
            void boxName(u8, const std::string_view&) override {}
            u8 boxWallpaper(u8) const override { return 0; }
            void boxWallpaper(u8, const u8) override {}
            u8 partyCount() const override { return u8(std::min<u32>(rd32(data.get() + sec[1] + 0x34), 6)); }
            void partyCount(u8 n) override
            {
                wr32(data.get() + sec[1] + 0x34, n);
                commit(1);
            }
            int maxBoxes() const override { return boxes(); }
            size_t maxWondercards() const override { return 0; }
            pksm::Generation generation() const override { return pksm::Generation::THREE; }
            void item(const pksm::Item&, Pouch, u16) override {}
            std::unique_ptr<pksm::Item> item(Pouch, u16) const override { return std::make_unique<pksm::Item3>(); }
            SmallVector<std::pair<Pouch, int>, 15> pouches() const override { return {}; }
            SmallVector<std::pair<Pouch, std::span<const int>>, 15> validItems() const override { return {}; }
        };

        std::map<const pksm::Sav*, Hack>& liveSav()
        {
            static std::map<const pksm::Sav*, Hack> s;
            return s;
        }

        struct TrackedSav : SavHack
        {
            using SavHack::SavHack;
            ~TrackedSav() override { liveSav().erase(this); }
        };
    }

    std::unique_ptr<pksm::Sav> load(const std::shared_ptr<u8[]>& data, u32 length)
    {
        if (length != 0x20000)
        {
            return nullptr;
        }
        const u8* d = data.get();
        Hack h      = Hack::None;
        if (stamps(d, UNBOUND_SIGNATURE) >= 8)
        {
            h = Hack::Unbound;
        }
        else if (stamps(d, GSC_SIGNATURE) >= 8)
        {
            h = Hack::GSChronicles;
        }
        else if (isRadicalRed(d))
        {
            h = Hack::RadicalRed;
        }
        if (h == Hack::None)
        {
            return nullptr;
        }
        auto sav = std::make_unique<TrackedSav>(data, length, h);
        for (int id : {0, 1})
        {
            if (sav->sec[id] < 0)
            {
                return nullptr;
            }
        }
        liveSav()[sav.get()] = h;
        return sav;
    }

    bool is(const pksm::Sav& sav) { return liveSav().count(&sav) != 0; }
    bool is(const pksm::PKX& pk) { return livePk().count(&pk) != 0; }
    Hack hackOf(const pksm::Sav& sav)
    {
        auto it = liveSav().find(&sav);
        return it == liveSav().end() ? Hack::None : it->second;
    }
    const char* name(Hack hack)
    {
        switch (hack)
        {
            case Hack::RadicalRed: return "Radical Red";
            case Hack::Unbound: return "Unbound";
            case Hack::GSChronicles: return "GS Chronicles";
            default: return "";
        }
    }

    std::string bankFormat(const pksm::PKX& pk)
    {
        if (!is(pk))
        {
            return "";
        }
        Hack h = static_cast<const PKHack&>(pk).hack;
        return h == Hack::RadicalRed ? "PK3RR" : h == Hack::Unbound ? "PK3UB" : "PK3GSC";
    }

    std::vector<u8> bankBytes(const pksm::PKX& pk)
    {
        std::vector<u8> c(PC_SIZE);
        u8 p[PARTY_SIZE] = {};
        memcpy(p, pk.rawData().data(), std::min<size_t>(pk.getLength(), PARTY_SIZE));
        compact(p, c.data());
        return c;
    }

    std::unique_ptr<pksm::PKX> fromBank(const std::string& format, const std::vector<u8>& bytes)
    {
        Hack h = format == "PK3RR" ? Hack::RadicalRed : format == "PK3UB" ? Hack::Unbound : format == "PK3GSC" ? Hack::GSChronicles : Hack::None;
        if (h == Hack::None || bytes.size() < PC_SIZE)
        {
            return nullptr;
        }
        u8 p[PARTY_SIZE];
        expand(bytes.data(), p);
        auto pk = PKHack::make(h, p);
        pk->healPP();
        return track(std::move(pk));
    }

    bool canHold(const pksm::Sav& sav, const pksm::PKX& pk)
    {
        const Tables& t = tables(hackOf(sav));
        return t.speciesOf.count(u32(u16(pk.species())) << 8 | u8(pk.alternativeForm())) ||
               t.speciesOf.count(u32(u16(pk.species())) << 8);
    }

    std::vector<int> species(Hack hack)
    {
        std::set<int> s;
        for (auto& [id, nf] : tables(hack).species)
        {
            s.insert(nf.first);
        }
        return {s.begin(), s.end()};
    }
    std::vector<int> moves(Hack hack)
    {
        std::set<int> s;
        for (auto& [id, n] : tables(hack).move)
        {
            s.insert(n);
        }
        return {s.begin(), s.end()};
    }
    std::vector<int> items(Hack hack)
    {
        std::set<int> s;
        for (auto& [id, n] : tables(hack).item)
        {
            s.insert(n);
        }
        return {s.begin(), s.end()};
    }
}
