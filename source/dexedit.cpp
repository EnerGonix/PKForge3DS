#include "dexedit.hpp"
#include "gamecube.hpp"
#include "pkdata.hpp"

#include "pkx/PKX.hpp"
#include "sav/Sav1.hpp"
#include "sav/Sav2.hpp"
#include "sav/Sav3.hpp"

// The original's Dex editor (PKHeX's per-generation Pokédex): every species unseen, seen or caught. Marking a
// species seen or caught goes through PKSM-Core's own Sav::dex (the game's gender, form, language and display
// flags); clearing it and reading it use the same save regions that function writes.
namespace dexedit
{
    namespace
    {
        // PKSM-Core keeps the Gen 1-3 flag helpers and the Pokédex offset protected; pointers to members taken
        // through a derived class reach them without changing the library.
        struct P1 : pksm::Sav1
        {
            static constexpr auto caught = &P1::getCaught;
            static constexpr auto seen   = &P1::getSeen;
            static constexpr auto setC   = &P1::setCaught;
            static constexpr auto setS   = &P1::setSeen;
        };
        struct P2 : pksm::Sav2
        {
            static constexpr auto caught = &P2::getCaught;
            static constexpr auto seen   = &P2::getSeen;
            static constexpr auto setC   = &P2::setCaught;
            static constexpr auto setS   = &P2::setSeen;
        };
        struct P3 : pksm::Sav3
        {
            static constexpr auto caught = &P3::getCaught;
            static constexpr auto seen   = &P3::getSeen;
            static constexpr auto setC   = &P3::setCaught;
            static constexpr auto setS   = &P3::setSeen;
        };
        struct PDex : pksm::Sav
        {
            static constexpr auto offset = &PDex::PokeDex;
        };

        int gen(const pksm::Sav& sav) { return pkdata::genNumber(sav.generation()); }
        u8* data(pksm::Sav& sav) { return sav.rawData().get(); }
        int dexOffset(const pksm::Sav& sav) { return sav.*(PDex::offset); }

        bool bit(const u8* d, int at, int b) { return d[at + (b >> 3)] >> (b & 7) & 1; }
        void clear(u8* d, int at, int b) { d[at + (b >> 3)] &= u8(~(1 << (b & 7))); }

        // Gen 4-7: where the owned flag is, and the seen/display regions (offset of each, for the base bit).
        struct Regions
        {
            std::vector<int> owned, seen, other;
        };

        Regions regions(const pksm::Sav& sav)
        {
            int p = dexOffset(sav);
            Regions r;
            switch (gen(sav))
            {
                case 4:
                {
                    const int br = 0x40;
                    int o        = p + 4;
                    r.owned      = {o};
                    r.seen       = {o + br};
                    r.other      = {o + br * 2, o + br * 3}; // gender display
                    break;
                }
                case 5:
                {
                    const int br = 0x54;
                    int o        = p + 8;
                    r.owned      = {o};
                    r.seen       = {o + br, o + br * 2, o + br * 3, o + br * 4};
                    r.other      = {o + br * 5, o + br * 6, o + br * 7, o + br * 8};
                    break;
                }
                case 6:
                {
                    const int br = 0x60;
                    int o        = p + 8;
                    r.owned      = {o, o + 0x644}; // native, and X/Y's foreign (Gen 1-5 origin)
                    r.seen       = {o + br, o + br * 2, o + br * 3, o + br * 4};
                    r.other      = {o + br * 5, o + br * 6, o + br * 7, o + br * 8};
                    break;
                }
                case 7:
                {
                    const int br = 0x8C;
                    int o        = p + 0x88;
                    r.owned      = {o};
                    int s        = o + 0x68;
                    r.seen       = {s, s + br, s + br * 2, s + br * 3};
                    r.other      = {s + br * 4, s + br * 5, s + br * 6, s + br * 7};
                    break;
                }
                default: break;
            }
            return r;
        }

        std::unique_ptr<pksm::PKX> sample(pksm::Sav& sav, u16 species)
        {
            auto pk = sav.emptyPkm();
            pk->species(pksm::Species{species});
            pk->alternativeForm(0);
            pk->egg(false);
            pk->language(sav.language());
            pk->version(sav.version());
            u8 ratio = pk->genderType();
            pk->gender(ratio == 255 ? pksm::Gender::Genderless : ratio == 254 ? pksm::Gender::Female : pksm::Gender::Male);
            return pk;
        }
    }

    bool supported(const pksm::Sav& sav)
    {
        int g = gen(sav);
        return g >= 1 && g <= 7 && u8(sav.version()) != 42 && u8(sav.version()) != 43 && !gamecube::is(sav);
    }

    State get(const pksm::Sav& sav, u16 species)
    {
        pksm::Species sp{species};
        switch (gen(sav))
        {
            case 1:
            {
                auto& s = static_cast<const pksm::Sav1&>(sav);
                return (s.*P1::caught)(sp) ? State::Caught : (s.*P1::seen)(sp) ? State::Seen : State::Unseen;
            }
            case 2:
            {
                auto& s = static_cast<const pksm::Sav2&>(sav);
                return (s.*P2::caught)(sp) ? State::Caught : (s.*P2::seen)(sp) ? State::Seen : State::Unseen;
            }
            case 3:
            {
                auto& s = static_cast<const pksm::Sav3&>(sav);
                return (s.*P3::caught)(sp) ? State::Caught : (s.*P3::seen)(sp) ? State::Seen : State::Unseen;
            }
            default:
            {
                Regions r     = regions(sav);
                const u8* d   = sav.rawData().get();
                int b         = species - 1;
                for (int at : r.owned)
                {
                    if (bit(d, at, b))
                    {
                        return State::Caught;
                    }
                }
                for (int at : r.seen)
                {
                    if (bit(d, at, b))
                    {
                        return State::Seen;
                    }
                }
                return State::Unseen;
            }
        }
    }

    void set(pksm::Sav& sav, u16 species, State state)
    {
        pksm::Species sp{species};
        int g = gen(sav);
        if (state != State::Unseen)
        {
            if (get(sav, species) == State::Unseen || state == State::Caught)
            {
                sav.dex(*sample(sav, species));
            }
            if (state == State::Seen)
            {
                switch (g)
                {
                    case 1: (static_cast<pksm::Sav1&>(sav).*P1::setC)(sp, false); break;
                    case 2: (static_cast<pksm::Sav2&>(sav).*P2::setC)(sp, false); break;
                    case 3: (static_cast<pksm::Sav3&>(sav).*P3::setC)(sp, false); break;
                    default:
                        for (int at : regions(sav).owned)
                        {
                            clear(data(sav), at, species - 1);
                        }
                        break;
                }
            }
            return;
        }
        switch (g)
        {
            case 1:
                (static_cast<pksm::Sav1&>(sav).*P1::setC)(sp, false);
                (static_cast<pksm::Sav1&>(sav).*P1::setS)(sp, false);
                break;
            case 2:
                (static_cast<pksm::Sav2&>(sav).*P2::setC)(sp, false);
                (static_cast<pksm::Sav2&>(sav).*P2::setS)(sp, false);
                break;
            case 3:
                (static_cast<pksm::Sav3&>(sav).*P3::setC)(sp, false);
                (static_cast<pksm::Sav3&>(sav).*P3::setS)(sp, false);
                break;
            default:
            {
                Regions r = regions(sav);
                for (auto* list : {&r.owned, &r.seen, &r.other})
                {
                    for (int at : *list)
                    {
                        clear(data(sav), at, species - 1);
                    }
                }
                break;
            }
        }
    }
}
