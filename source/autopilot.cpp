#include "autopilot.hpp"
#include "app.hpp"
#include "bank.hpp"
#include "files.hpp"
#include "hax.hpp"
#include "livingdex.hpp"
#include "locks.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "saves.hpp"
#include "screens.hpp"
#include "widgets.hpp"

#include "utils/i18n.hpp"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>

using namespace gfx;

namespace autopilot
{
    namespace
    {
        constexpr int SPECIES = 1025, SLOTS = Bank::SLOTS;
        constexpr int BOXES = (SPECIES + SLOTS - 1) / SLOTS;
        // Where the living dex boxes start, kept so a later run fills the same boxes.
        constexpr const char* START_FILE = "sdmc:/3ds/PKForge/autopilot.txt";

        // Plain link-trade evolutions (no held item, no partner).
        constexpr int TRADES[][2] = {{64, 65}, {67, 68}, {75, 76}, {93, 94}, {525, 526}, {533, 534}, {708, 709},
            {710, 711}};

        struct Holding
        {
            int source; // index into Work::saves; -1 is the Bank
            int box, slot, species, form;
            bool shiny;
            std::string label;
        };

        enum class Kind
        {
            Move,
            Evolve,
            Arrange
        };

        struct Step
        {
            Kind kind;
            int species;   // what fills the slot
            Holding from;  // the Pokémon used
            int toBox, toSlot;
        };

        struct Work
        {
            bool shiny = false;
            std::vector<SaveRef> saves;
            std::vector<Holding> holdings;
            std::atomic<int> done{0};
            std::atomic<bool> finished{false};
            Thread thread = nullptr;
        };

        void collect(void* arg)
        {
            Work* w = static_cast<Work*>(arg);
            for (int i = 0; i < int(w->saves.size()); i++)
            {
                const SaveRef& ref = w->saves[i];
                std::string error;
                auto save = OpenSave::open(ref, error);
                if (save && save->sav)
                {
                    pksm::Sav& sav = *save->sav;
                    int slots      = sav.maxBoxes() ? sav.maxSlot() / sav.maxBoxes() : 0;
                    std::string game = pkdata::game(ref.version);
                    for (int b = 0; b < sav.maxBoxes(); b++)
                    {
                        if (locks::locked(ref.path, b))
                        {
                            continue; // a locked box keeps its Pokémon
                        }
                        for (int s = 0; s < slots; s++)
                        {
                            auto pk = sav.pkm(u8(b), u8(s));
                            if (pk && u16(pk->species()) && !pk->egg())
                            {
                                w->holdings.push_back({i, b, s, u16(pk->species()), pk->alternativeForm(), pk->shiny(), game});
                            }
                        }
                    }
                }
                w->done++;
            }
            w->finished = true;
        }

        int savedStart()
        {
            std::vector<u8> bytes;
            if (!files::read(START_FILE, bytes) || bytes.empty())
            {
                return -1;
            }
            return atoi(std::string(bytes.begin(), bytes.end()).c_str());
        }

        // The first BOXES boxes in a row with nothing in them (boxes past the last count as empty).
        int firstEmptyStretch(Bank& bank)
        {
            int run = 0;
            for (int b = 0; b < bank.boxes(); b++)
            {
                bool empty = true;
                for (int s = 0; s < SLOTS && empty; s++)
                {
                    empty = !bank.at(b, s);
                }
                run = empty ? run + 1 : 0;
                if (run == BOXES)
                {
                    return b - BOXES + 1;
                }
            }
            return bank.boxes() - run;
        }

        struct Plan
        {
            std::unique_ptr<Work> work;
            int start = 0, coveredBefore = 0, missing = 0, blocked = 0;
            std::vector<Step> steps;
            std::vector<std::string> lines;
        };

        void evolve(pksm::PKX& pk, int to)
        {
            bool named = pk.nicknamed();
            pk.species(pksm::Species{u16(to)});
            if (!named)
            {
                pk.nickname(i18n::species(pk.language(), pksm::Species{u16(to)}));
            }
            pk.setAbility(pk.abilityNumber());
            pk.refreshChecksum();
        }

        std::unique_ptr<Plan> makePlan(std::unique_ptr<Work> work)
        {
            auto plan = std::make_unique<Plan>();
            Bank& bank = app::bank();
            bool shiny = work->shiny;
            plan->start = savedStart();
            if (plan->start < 0)
            {
                plan->start = firstEmptyStretch(bank);
            }
            auto slotOf = [&](int species, int& box, int& slot)
            {
                box  = plan->start + (species - 1) / SLOTS;
                slot = (species - 1) % SLOTS;
            };
            auto counts = [&](int species, bool isShiny) { return species >= 1 && species <= SPECIES && (!shiny || isShiny); };

            // What the Bank already holds: each species' first entry is arranged into its slot.
            std::vector<bool> covered(SPECIES + 1, false);
            std::map<std::pair<int, int>, bool> taken; // region slots already promised
            std::vector<Holding> bankHeld;
            for (int b = 0; b < bank.boxes(); b++)
            {
                for (int s = 0; s < SLOTS; s++)
                {
                    const BankEntry* e = bank.at(b, s);
                    if (e && !locks::locked(locks::BANK, b))
                    {
                        bankHeld.push_back({-1, b, s, e->species, e->form, e->shiny, "Bank"});
                    }
                }
            }
            // Entries already in their own slot first, so they keep it.
            for (const Holding& h : bankHeld)
            {
                int box, slot;
                if (!counts(h.species, h.shiny) || covered[h.species])
                {
                    continue;
                }
                slotOf(h.species, box, slot);
                if (h.box == box && h.slot == slot)
                {
                    covered[h.species] = true;
                    taken[{box, slot}] = true;
                }
            }
            plan->coveredBefore = int(std::count(covered.begin(), covered.end(), true));
            for (const Holding& h : bankHeld)
            {
                int box, slot;
                if (!counts(h.species, h.shiny) || covered[h.species])
                {
                    continue;
                }
                slotOf(h.species, box, slot);
                const BankEntry* there = box < bank.boxes() ? bank.at(box, slot) : nullptr;
                covered[h.species] = true;
                if (there)
                {
                    plan->blocked++; // something else sits in its slot: it stays where it is
                    continue;
                }
                taken[{box, slot}] = true;
                plan->steps.push_back({Kind::Arrange, h.species, h, box, slot});
            }

            // Saves: how many of each species each one keeps (the last copy never leaves).
            std::map<std::pair<int, int>, int> copies;
            for (const Holding& h : work->holdings)
            {
                copies[{h.source, h.species}]++;
            }
            std::vector<bool> used(work->holdings.size(), false);
            auto mayTake = [&](const Holding& h) { return copies[{h.source, h.species}] > 1; };
            auto take = [&](int i)
            {
                used[i] = true;
                copies[{work->holdings[i].source, work->holdings[i].species}]--;
            };
            // Best candidate: plain before shiny in a normal dex, the save with the most copies first.
            auto best = [&](auto match) -> int
            {
                int pick = -1;
                for (int i = 0; i < int(work->holdings.size()); i++)
                {
                    const Holding& h = work->holdings[i];
                    if (used[i] || !match(h) || !mayTake(h) || (shiny && !h.shiny))
                    {
                        continue;
                    }
                    if (pick < 0)
                    {
                        pick = i;
                        continue;
                    }
                    const Holding& p = work->holdings[pick];
                    int hs = !shiny && h.shiny, ps = !shiny && p.shiny;
                    if (hs != ps ? hs < ps : copies[{h.source, h.species}] > copies[{p.source, p.species}])
                    {
                        pick = i;
                    }
                }
                return pick;
            };

            // Pass 1: direct moves.
            for (int species = 1; species <= SPECIES; species++)
            {
                if (covered[species])
                {
                    continue;
                }
                int i = best([&](const Holding& h) { return h.species == species; });
                if (i < 0)
                {
                    continue;
                }
                int box, slot;
                slotOf(species, box, slot);
                if ((box < bank.boxes() && bank.at(box, slot)) || taken[{box, slot}])
                {
                    plan->blocked++;
                    continue;
                }
                take(i);
                covered[species]   = true;
                taken[{box, slot}] = true;
                plan->steps.push_back({Kind::Move, species, work->holdings[i], box, slot});
            }
            // Pass 2: a spare pre-evolution evolves by trade for its evolution's slot.
            for (auto& t : TRADES)
            {
                if (hax::hardcore())
                {
                    break; // Hardcore: Pokémon move, nothing evolves
                }
                int to = t[1];
                if (covered[to])
                {
                    continue;
                }
                int i = best([&](const Holding& h) { return h.species == t[0]; });
                if (i < 0)
                {
                    continue;
                }
                int box, slot;
                slotOf(to, box, slot);
                if ((box < bank.boxes() && bank.at(box, slot)) || taken[{box, slot}])
                {
                    plan->blocked++;
                    continue;
                }
                take(i);
                covered[to]        = true;
                taken[{box, slot}] = true;
                plan->steps.push_back({Kind::Evolve, to, work->holdings[i], box, slot});
            }
            plan->missing = int(std::count(covered.begin() + 1, covered.end(), false));
            std::sort(plan->steps.begin(), plan->steps.end(), [](const Step& a, const Step& b) { return a.species < b.species; });

            for (const Step& s : plan->steps)
            {
                char line[128];
                std::string name = i18n::species(pkdata::lang, pksm::Species{u16(s.species)});
                if (s.kind == Kind::Arrange)
                {
                    snprintf(line, sizeof(line), "#%04d %s  -  arranged in the Bank", s.species, name.c_str());
                }
                else if (s.kind == Kind::Evolve)
                {
                    snprintf(line, sizeof(line), "#%04d %s  -  %s evolves, from %s", s.species, name.c_str(),
                        i18n::species(pkdata::lang, pksm::Species{u16(s.from.species)}).c_str(), s.from.label.c_str());
                }
                else
                {
                    snprintf(line, sizeof(line), "#%04d %s  -  from %s", s.species, name.c_str(), s.from.label.c_str());
                }
                plan->lines.push_back(line);
            }
            plan->work = std::move(work);
            return plan;
        }

        // Runs the plan: the Bank's own entries first, then each save's Pokémon into the Bank, the save written
        // (with its restore point) after its Pokémon are in; if that write fails they leave the Bank again.
        void run(Plan& plan)
        {
            Bank& bank = app::bank();
            while (bank.boxes() < plan.start + BOXES)
            {
                bank.addBox();
            }
            char text[32];
            snprintf(text, sizeof(text), "%d", plan.start);
            files::writeText(START_FILE, text);
            int arranged = 0, moved = 0, evolved = 0, failed = 0;
            std::string problems;
            for (const Step& s : plan.steps)
            {
                std::string error;
                if (s.kind == Kind::Arrange && !bank.at(s.toBox, s.toSlot) &&
                    bank.move(s.from.box, s.from.slot, s.toBox, s.toSlot, error))
                {
                    arranged++;
                }
            }
            for (int src = 0; src < int(plan.work->saves.size()); src++)
            {
                std::vector<const Step*> mine;
                for (const Step& s : plan.steps)
                {
                    if (s.kind != Kind::Arrange && s.from.source == src)
                    {
                        mine.push_back(&s);
                    }
                }
                if (mine.empty())
                {
                    continue;
                }
                const SaveRef& ref = plan.work->saves[src];
                std::string error;
                auto save = OpenSave::open(ref, error);
                if (!save || !save->sav)
                {
                    failed += int(mine.size());
                    problems += pkdata::game(ref.version) + ": could not open it.\n";
                    continue;
                }
                pksm::Sav& sav = *save->sav;
                auto empty     = sav.emptyPkm();
                std::vector<std::pair<int, int>> placed;
                int evolvedHere = 0;
                for (const Step* s : mine)
                {
                    auto pk = sav.pkm(u8(s->from.box), u8(s->from.slot));
                    if (!pk || u16(pk->species()) != s->from.species || bank.at(s->toBox, s->toSlot))
                    {
                        failed++; // the save changed since the plan, or the slot filled up
                        continue;
                    }
                    if (s->kind == Kind::Evolve)
                    {
                        evolve(*pk, s->species);
                        evolvedHere++;
                    }
                    if (!bank.put(*pk, s->toBox, s->toSlot, "Autopilot", error))
                    {
                        failed++;
                        continue;
                    }
                    placed.push_back({s->toBox, s->toSlot});
                    sav.pkm(*empty, u8(s->from.box), u8(s->from.slot), false);
                }
                if (placed.empty())
                {
                    continue;
                }
                save->dirty = true;
                if (!save->write(error))
                {
                    // The save keeps its Pokémon: the copies leave the Bank again.
                    for (auto& [b, sl] : placed)
                    {
                        std::string ignore;
                        bank.remove(b, sl, ignore);
                    }
                    failed += int(placed.size());
                    problems += pkdata::game(ref.version) + ": " + error + "\n";
                    continue;
                }
                moved += int(placed.size()) - evolvedHere;
                evolved += evolvedHere;
            }
            char summary[256];
            snprintf(summary, sizeof(summary),
                "%d moved into the Bank, %d evolved by trade, %d arranged.%s\nThe living dex is in Bank boxes %d to %d.",
                moved, evolved, arranged, failed ? (" " + std::to_string(failed) + " could not move.").c_str() : "",
                plan.start + 1, plan.start + BOXES);
            app::message("Dex Autopilot", std::string(summary) + (problems.empty() ? "" : "\n" + problems));
        }

        // The dry run: what will move where, nothing touched yet.
        class Preview : public Screen
        {
        public:
            explicit Preview(std::unique_ptr<Plan> p) : plan(std::move(p)) {}

            void update(Input& in) override
            {
                int n = int(plan->lines.size());
                if (in.repeat & KEY_DDOWN)
                {
                    first = std::min(std::max(0, n - VISIBLE), first + 1);
                }
                if (in.repeat & KEY_DUP)
                {
                    first = std::max(0, first - 1);
                }
                if (in.repeat & KEY_R)
                {
                    first = std::min(std::max(0, n - VISIBLE), first + VISIBLE);
                }
                if (in.repeat & KEY_L)
                {
                    first = std::max(0, first - VISIBLE);
                }
                if (in.touching && in.dragged)
                {
                    drag += in.dy;
                    while (drag <= -14 && first + VISIBLE < n)
                    {
                        drag += 14;
                        first++;
                    }
                    while (drag >= 14 && first > 0)
                    {
                        drag -= 14;
                        first--;
                    }
                }
                else if (!in.touching)
                {
                    drag = 0;
                }
                if ((in.down & KEY_A) && !plan->steps.empty())
                {
                    Plan* p = plan.get();
                    app::confirm("Run the plan? Pokémon leave their saves for the Bank (each save keeps a restore "
                                 "point first).",
                        [p]()
                        {
                            run(*p);
                            app::pop();
                        },
                        "Run", "Not yet");
                }
                else if (in.down & KEY_B)
                {
                    app::pop();
                }
            }

            void drawTop() override
            {
                housing(0, 0, TOP_W, H);
                panel(6, 6, TOP_W - 12, 228, T::PanelFill, T::Cyan);
                panelGrad(12, 12, TOP_W - 24, 22, T::PartySelected, T::ChipTop, T::PartyRim);
                text(20, 16, plan->work->shiny ? "Dex Autopilot  -  shiny living dex" : "Dex Autopilot", T::Bright);
                int moves = 0, evolves = 0, arranges = 0;
                std::map<std::string, int> bySave;
                for (const Step& s : plan->steps)
                {
                    moves += s.kind == Kind::Move;
                    evolves += s.kind == Kind::Evolve;
                    arranges += s.kind == Kind::Arrange;
                    if (s.kind != Kind::Arrange)
                    {
                        bySave[s.from.label]++;
                    }
                }
                int y = 42;
                char line[96];
                auto row = [&](const char* label, const std::string& value, u32 tone)
                {
                    rect(12, y, TOP_W - 24, 17, (y / 18) % 2 ? T::LabelColumn : T::PanelFill);
                    text(18, y + 2, label, T::InkSoft);
                    text(TOP_W - 18, y + 2, value, tone, Right, 1, 220);
                    y += 18;
                };
                snprintf(line, sizeof(line), "Bank boxes %d to %d", plan->start + 1, plan->start + BOXES);
                row("Living dex", line, T::Ink);
                row("Already in place", std::to_string(plan->coveredBefore) + " / " + std::to_string(SPECIES), T::Ink);
                row("Move from saves", std::to_string(moves), moves ? S::Legal : T::SubInk);
                row("Evolve by trade", std::to_string(evolves), evolves ? S::Legal : T::SubInk);
                row("Arrange in the Bank", std::to_string(arranges), arranges ? S::Legal : T::SubInk);
                row("Still to catch", std::to_string(plan->missing), plan->missing ? S::ShinyGold : S::Legal);
                if (plan->blocked)
                {
                    row("Slot taken (left alone)", std::to_string(plan->blocked), S::Illegal);
                }
                y += 4;
                for (auto& [game, count] : bySave)
                {
                    if (y > 214)
                    {
                        break;
                    }
                    snprintf(line, sizeof(line), "%s gives %d", game.c_str(), count);
                    text(18, y, line, T::Value, Left, 1, TOP_W - 36);
                    y += 13;
                }
                text(TOP_W / 2, 214, "Only spares move: each save keeps its last copy, its party and eggs.", T::SubInk, Center);
            }

            void drawBottom() override
            {
                housing(0, 0, BOTTOM_W, H);
                widgets::titleBar(BOTTOM_W, "Dex Autopilot", plan->steps.empty() ? "Nothing to move" : "Plan");
                panel(4, 30, BOTTOM_W - 8, 182, T::PanelFill, T::FrameEdge);
                if (plan->lines.empty())
                {
                    wrap(16, 90, BOTTOM_W - 32,
                        "Nothing to move: every species you have spare copies of is already in the living dex. The rest "
                        "are still to catch.",
                        T::InkSoft);
                }
                for (int i = 0; i < VISIBLE && first + i < int(plan->lines.size()); i++)
                {
                    int y = 34 + i * 14;
                    if (i % 2 == 0)
                    {
                        rect(6, y - 1, BOTTOM_W - 20, 14, T::LabelColumn);
                    }
                    text(10, y, plan->lines[first + i], T::Value, Left, 1, BOTTOM_W - 28);
                }
                widgets::scrollbar(BOTTOM_W - 10, 34, VISIBLE * 14, first, VISIBLE, int(plan->lines.size()));
                if (plan->steps.empty())
                {
                    widgets::footer(BOTTOM_W, {{"B", "Back"}});
                }
                else
                {
                    widgets::footer(BOTTOM_W, {{"A", "Run"}, {"LR", "Page"}, {"B", "Cancel"}});
                }
            }

        private:
            static constexpr int VISIBLE = 12;
            std::unique_ptr<Plan> plan;
            int first = 0, drag = 0;
        };

        class Collecting : public Screen
        {
        public:
            explicit Collecting(std::unique_ptr<Work> w) : work(std::move(w)) {}

            bool overlay() const override { return true; }

            void update(Input&) override
            {
                if (!work->finished)
                {
                    return;
                }
                threadJoin(work->thread, U64_MAX);
                threadFree(work->thread);
                work->thread = nullptr;
                auto plan    = makePlan(std::move(work));
                app::pop();
                app::push(std::make_unique<Preview>(std::move(plan)));
            }

            void drawTop() override {}

            void drawBottom() override
            {
                panel(16, 70, 288, 80, T::Deck, T::FrameEdge);
                text(24, 76, "Dex Autopilot", T::Ink);
                char line[96];
                snprintf(line, sizeof(line), "Looking through the Bank and %d of %d saves...", work ? work->done.load() : 0,
                    work ? int(work->saves.size()) : 0);
                wrap(26, 100, 270, line, T::Value, 1, 3);
            }

        private:
            std::unique_ptr<Work> work;
        };

        void start(bool shiny)
        {
            auto w   = std::make_unique<Work>();
            w->shiny = shiny;
            w->saves = saves::found();
            s32 prio = 0x30;
            svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
            w->thread = threadCreate(collect, w.get(), 128 * 1024, prio + 1, -2, false);
            if (!w->thread)
            {
                w->finished = true;
            }
            app::push(std::make_unique<Collecting>(std::move(w)));
        }
    }

    void open()
    {
        if (saves::scanning())
        {
            app::message("Dex Autopilot", "PKForge is still looking for saves: try again when the search is done.");
            return;
        }
        if (app::bank().readOnly())
        {
            app::message("Dex Autopilot", "The Bank is read-only here, so the autopilot cannot fill it.");
            return;
        }
        std::vector<app::Option> o;
        o.push_back({"Living dex in the Bank", []() { start(false); },
            "One of every species, in Pokédex order, from the spares in your saves and the Bank. You see the plan "
            "first."});
        o.push_back({"Shiny living dex in the Bank", []() { start(true); }, "The same with shinies only."});
        o.push_back({"Show the Living Dex", []() { livingdex::open(); }, "Every species and where you have it."});
        o.push_back({"Close", []() {}});
        app::menu("Dex Autopilot", std::move(o));
    }
}
