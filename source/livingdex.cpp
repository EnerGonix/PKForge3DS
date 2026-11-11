#include "livingdex.hpp"
#include "app.hpp"
#include "bank.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "saves.hpp"
#include "screens.hpp"

#include "utils/i18n.hpp"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>

using namespace gfx;

namespace livingdex
{
    namespace
    {
        constexpr int SPECIES = pkdata::LAST_SPECIES; // up to Ultra Sun and Ultra Moon

        struct Count
        {
            std::vector<std::vector<std::string>> owners = std::vector<std::vector<std::string>>(SPECIES + 1);
            std::vector<SaveRef> saves;
            std::atomic<int> done{0};
            std::atomic<bool> finished{false};
            Thread thread = nullptr;

            void add(int species, const std::string& where)
            {
                if (species < 1 || species > SPECIES)
                {
                    return;
                }
                auto& list = owners[species];
                if (std::find(list.begin(), list.end(), where) == list.end())
                {
                    list.push_back(where);
                }
            }
        };

        void work(void* arg)
        {
            Count* c = static_cast<Count*>(arg);
            for (const SaveRef& ref : c->saves)
            {
                std::string error;
                auto save = OpenSave::open(ref, error);
                if (save && save->sav)
                {
                    pksm::Sav& sav  = *save->sav;
                    std::string who = pkdata::game(ref.version);
                    int slots       = sav.maxBoxes() ? sav.maxSlot() / sav.maxBoxes() : 0;
                    for (int b = 0; b < sav.maxBoxes(); b++)
                    {
                        for (int s = 0; s < slots; s++)
                        {
                            auto pk = sav.pkm(u8(b), u8(s));
                            if (pk && u16(pk->species()) && !pk->egg())
                            {
                                c->add(u16(pk->species()), who);
                            }
                        }
                    }
                    for (int s = 0; s < sav.partyCount(); s++)
                    {
                        auto pk = sav.pkm(u8(s));
                        if (pk && u16(pk->species()) && !pk->egg())
                        {
                            c->add(u16(pk->species()), who);
                        }
                    }
                }
                c->done++;
            }
            c->finished = true;
        }

        void showList(Count& c)
        {
            std::vector<screens::PickItem> items;
            int owned = 0;
            for (int s = 1; s <= SPECIES; s++)
            {
                const auto& where = c.owners[s];
                owned += !where.empty();
                std::string list;
                for (const std::string& w : where)
                {
                    list += (list.empty() ? "" : ", ") + w;
                }
                items.push_back({s, i18n::species(pkdata::lang, pksm::Species{u16(s)}) + "   " +
                                        (where.empty() ? "missing" : list),
                    ""});
            }
            char title[48];
            snprintf(title, sizeof(title), "Living Dex %d/%d", owned, SPECIES);
            app::push(screens::picker(title, std::move(items), 1, [](int) {}, screens::PickPreview::Species));
        }

        class Counting : public Screen
        {
        public:
            explicit Counting(std::unique_ptr<Count> c) : count(std::move(c)) {}

            bool overlay() const override { return true; }

            void update(Input&) override
            {
                if (!count->finished)
                {
                    return;
                }
                threadJoin(count->thread, U64_MAX);
                threadFree(count->thread);
                auto own = std::move(count);
                app::pop();
                showList(*own);
            }

            void drawTop() override {}

            void drawBottom() override
            {
                panel(16, 70, 288, 80, T::Deck, T::FrameEdge);
                text(24, 76, "Living Dex", T::Ink);
                char line[80];
                snprintf(line, sizeof(line), "Counting the Bank and %d of %d saves...", count ? count->done.load() : 0,
                    count ? int(count->saves.size()) : 0);
                wrap(26, 100, 270, line, T::Value, 1, 3);
            }

        private:
            std::unique_ptr<Count> count;
        };
    }

    void open()
    {
        auto c = std::make_unique<Count>();
        Bank& bank = app::bank();
        for (int b = 0; b < bank.boxes(); b++)
        {
            for (int s = 0; s < Bank::SLOTS; s++)
            {
                if (const BankEntry* e = bank.at(b, s))
                {
                    c->add(e->species, "Bank");
                }
            }
        }
        c->saves = saves::found();
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        c->thread = threadCreate(work, c.get(), 128 * 1024, prio + 1, -2, false);
        if (!c->thread)
        {
            c->finished = true;
        }
        app::push(std::make_unique<Counting>(std::move(c)));
    }
}
