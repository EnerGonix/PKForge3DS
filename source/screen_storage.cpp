// Storage: a save's boxes and party beside the Bank, with grab-and-drop between them.
// Moves between the save and the Bank become final when the save is written: leaving without
// writing takes deposits back out of the Bank, and withdrawn Pokémon stay in the Bank until then.
#include "audio.hpp"
#include "romhack.hpp"
#include "collection.hpp"
#include "bank.hpp"
#include "encounters.hpp"
#include "dexedit.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "hax.hpp"
#include "inspector.hpp"
#include "legality.hpp"
#include "locks.hpp"
#include "preview.hpp"
#include "rng.hpp"
#include "learnsets.hpp"
#include "legality_online.hpp"
#include "livingdex.hpp"
#include "mysterygift.hpp"
#include "pkdata.hpp"
#include "qr.hpp"
#include "saves.hpp"
#include "screens.hpp"
#include "showdown.hpp"
#include "ui_ids.hpp"
#include "widgets.hpp"
#include "world.hpp"

#include "enums/Ball.hpp"
#include "enums/Move.hpp"
#include "enums/Species.hpp"
#include "utils/i18n.hpp"
#include <algorithm>
#include <map>
#include <cstdio>
#include <cstring>

using namespace gfx;

namespace
{
    constexpr int GRID_X = 8, GRID_Y = 55, CELL_W = 40, CELL_H = 32, COLS = 6, ROWS = 5;
    constexpr int SIDE_X = 254, SIDE_W = 60;

    enum class Side
    {
        Save,
        Bank
    };

    struct Cell
    {
        int species = 0, form = 0;
        bool shiny = false, egg = false, female = false;
        bool stub = false, transit = false;
    };

    struct Spot
    {
        Side side;
        int box, slot;
        bool party;
        bool operator==(const Spot& o) const
        {
            return side == o.side && slot == o.slot && party == o.party && (party || box == o.box);
        }
    };

    std::unique_ptr<pksm::PKX> clipboard;

    bool readable(const std::string& format)
    {
        return !pkdata::formatFromExtension(format).empty();
    }

    std::string transferProblem(pksm::Sav::BadTransferReason r)
    {
        switch (r)
        {
            case pksm::Sav::BadTransferReason::MOVE: return "It knows a move this game does not have.";
            case pksm::Sav::BadTransferReason::SPECIES: return "This species does not exist in this game.";
            case pksm::Sav::BadTransferReason::FORM: return "This form does not exist in this game.";
            case pksm::Sav::BadTransferReason::ABILITY: return "Its ability does not exist in this game.";
            case pksm::Sav::BadTransferReason::ITEM: return "Its held item does not exist in this game.";
            case pksm::Sav::BadTransferReason::BALL: return "Its ball does not exist in this game.";
            default: return "";
        }
    }

    class Storage : public Screen
    {
    public:
        explicit Storage(std::shared_ptr<OpenSave> s) : save(std::move(s)), bank(app::bank())
        {
            app::Timed timed("storage ctor");
            side = save ? Side::Save : Side::Bank;
            if (save)
            {
                saveBox = std::min<int>(save->sav->currentBox(), save->sav->maxBoxes() - 1);
            }
            refresh();
        }

        void resumed() override { refresh(); }

        bool eventsFirst = false; // opened from home's Events tile: show the events menu once
        bool previewed   = false; // the transfer preview was confirmed for the move under way

        // Multi-select, as the original's organizer: slots of this box marked for one action on all of them.
        bool multi = false;
        std::vector<bool> marked;

        int markedCount() const { return int(std::count(marked.begin(), marked.end(), true)); }

        void startMulti()
        {
            if (refuseLocked(current()))
            {
                return;
            }
            multi = true;
            marked.assign(cells.size(), false);
            if (cursor < int(cells.size()) && cells[cursor].species)
            {
                marked[cursor] = true;
            }
        }

        void endMulti()
        {
            multi = false;
            marked.clear();
        }

        bool markable(int slot) const
        {
            return slot >= 0 && slot < int(cells.size()) && cells[slot].species && !cells[slot].transit;
        }

        void multiUpdate(Input& in)
        {
            if (marked.size() != cells.size())
            {
                marked.assign(cells.size(), false);
            }
            int n = int(cells.size()), before = cursor;
            if (in.repeat & KEY_DLEFT)
            {
                cursor = (cursor + n - 1) % n;
            }
            if (in.repeat & KEY_DRIGHT)
            {
                cursor = (cursor + 1) % n;
            }
            if (in.repeat & KEY_DUP)
            {
                cursor = (cursor + n - COLS) % n;
            }
            if (in.repeat & KEY_DDOWN)
            {
                cursor = (cursor + COLS) % n;
            }
            bool newBox = false;
            if ((in.repeat & KEY_L) || in.tapped(GRID_X, 30, 22, 22))
            {
                changeBox(-1);
                newBox = true;
            }
            if ((in.repeat & KEY_R) || in.tapped(GRID_X + 218, 30, 22, 22))
            {
                changeBox(1);
                newBox = true;
            }
            if (newBox)
            {
                marked.assign(cells.size(), false); // marks belong to one box
                return;
            }
            int hit = in.touchUp && !in.dragged ? slotAt(in.startX, in.startY) : -1;
            if (hit >= 0)
            {
                cursor = hit;
                if (markable(hit))
                {
                    marked[hit] = !marked[hit];
                }
            }
            if ((in.down & KEY_A) && markable(cursor))
            {
                marked[cursor] = !marked[cursor];
            }
            if (in.down & KEY_Y)
            {
                // All, or none when all are marked already.
                bool all = true;
                for (int i = 0; i < n; i++)
                {
                    all &= !markable(i) || marked[i];
                }
                for (int i = 0; i < n; i++)
                {
                    marked[i] = !all && markable(i);
                }
            }
            if (in.down & KEY_X)
            {
                multiMenu();
            }
            if (in.down & KEY_B)
            {
                endMulti();
            }
            if (cursor != before)
            {
                hover();
            }
        }

        std::vector<int> markedSlots() const
        {
            std::vector<int> out;
            for (int i = 0; i < int(marked.size()); i++)
            {
                if (marked[i])
                {
                    out.push_back(i);
                }
            }
            return out;
        }

        // One action on every marked Pokémon: send or withdraw, move to a box, duplicate, export, release.
        void multiMenu()
        {
            auto slots = markedSlots();
            if (slots.empty())
            {
                app::toast("Mark Pokémon with A or a tap first.");
                return;
            }
            std::string count = std::to_string(slots.size()) + " Pokémon";
            std::vector<app::Option> o;
            if (side == Side::Save)
            {
                o.push_back({"Send to the Bank", [this, slots]()
                    {
                        for (int slot : slots)
                        {
                            cursor = slot;
                            hover();
                            quickDeposit();
                        }
                        endMulti();
                    },
                    "Each into the Bank's first empty slot. Write the save to keep it."});
                o.push_back({"Duplicate", [this, slots]()
                    {
                        if (hardcoreBlocks())
                        {
                            return;
                        }
                        for (int slot : slots)
                        {
                            cursor = slot;
                            hover();
                            cloneHovered();
                        }
                        endMulti();
                    },
                    "A copy of each into this box's empty slots."});
            }
            else if (save)
            {
                o.push_back({"Withdraw to the save", [this, slots]()
                    {
                        for (int slot : slots)
                        {
                            cursor = slot;
                            hover();
                            quickWithdraw();
                        }
                        endMulti();
                    },
                    "Into the save's current box. Write the save to keep it."});
            }
            o.push_back({"Move to another box", [this, slots]() { moveMarked(slots); }, "Into its empty slots."});
            o.push_back({"Export .pk files", [this, slots]()
                {
                    int done = 0;
                    std::string folder = std::string(files::ROOT) + "/export";
                    files::mkdirs(folder);
                    for (int slot : slots)
                    {
                        Spot s = current();
                        s.slot = slot;
                        auto pk = read(s);
                        if (!pk)
                        {
                            continue;
                        }
                        char name[112];
                        snprintf(name, sizeof(name), "%03d - %s - %s-%02d%s", int(u16(pk->species())),
                            files::sanitize(pk->nickname()).c_str(), files::stamp().c_str(), slot + 1,
                            std::string(pk->extension()).c_str());
                        auto raw = pk->rawData();
                        done += files::write(folder + "/" + name, raw.data(), raw.size());
                    }
                    app::message("Exported", std::to_string(done) + " files in " + folder);
                },
                "One .pkN file each, readable by PKForge, PKHeX and PKSM."});
            o.push_back({side == Side::Bank ? "Delete from the Bank" : "Release", [this, slots, count]()
                {
                    app::confirm((side == Side::Bank ? "Delete " : "Release ") + count + "?" +
                                     (side == Side::Bank ? "\nThey leave the Bank for good." : ""),
                        [this, slots]()
                        {
                            for (int slot : slots)
                            {
                                Spot s = current();
                                s.slot = slot;
                                std::string error;
                                if (s.side == Side::Bank)
                                {
                                    bank.remove(s.box, s.slot, error);
                                }
                                else
                                {
                                    writeSlot(s, nullptr);
                                }
                            }
                            endMulti();
                            refresh();
                        },
                        side == Side::Bank ? "Delete" : "Release", "Keep");
                }});
            o.push_back({"Clear the marks", [this]() { marked.assign(cells.size(), false); }});
            o.push_back({"Close", []() {}});
            app::menu(count, std::move(o));
        }

        // The marked Pokémon into the empty slots of a box picked from a list (this side: save or Bank).
        void moveMarked(std::vector<int> slots)
        {
            std::vector<screens::PickItem> boxes;
            for (int b = 0; b < boxCount(); b++)
            {
                if (b == box())
                {
                    continue;
                }
                int empty = 0;
                for (int i = 0; i < int(cells.size()); i++)
                {
                    empty += !read(Spot{side, b, i, false});
                }
                std::string name = side == Side::Bank ? bank.boxName(b) : sav()->boxName(b);
                boxes.push_back({b, name.empty() ? "Box " + std::to_string(b + 1) : name,
                    std::to_string(empty) + " empty"});
            }
            app::push(screens::picker("Move to", std::move(boxes), -1, [this, slots](int target)
                {
                    int moved = 0, from = box();
                    for (int slot : slots)
                    {
                        for (int i = 0; i < int(cells.size()); i++)
                        {
                            Spot to{side, target, i, false};
                            if (read(to))
                            {
                                continue;
                            }
                            Spot at{side, from, slot, false};
                            std::string error;
                            if (side == Side::Bank)
                            {
                                if (!bank.move(from, slot, target, i, error))
                                {
                                    app::toast(error);
                                    break;
                                }
                            }
                            else
                            {
                                auto pk = read(at);
                                writeSlot(to, pk.get());
                                writeSlot(at, nullptr);
                            }
                            moved++;
                            break;
                        }
                    }
                    endMulti();
                    refresh();
                    app::toast(std::to_string(moved) + " moved" + (moved < int(slots.size()) ? " (that box is full)" : ""));
                }));
        }

        void update(Input& in) override
        {
            if (eventsFirst)
            {
                eventsFirst = false;
                eventsMenu();
                return;
            }
            // ZL / ZR turn the top screen's summary pages (New 3DS).
            if (in.down & KEY_ZL)
            {
                inspector::page(inspector::page() - 1);
            }
            if (in.down & KEY_ZR)
            {
                inspector::page(inspector::page() + 1);
            }
            if (multi)
            {
                multiUpdate(in);
                return;
            }
            int n = slotCount();
            int before = cursor;
            if (party)
            {
                if (in.repeat & KEY_DLEFT)
                {
                    cursor = cursor % 2 ? cursor - 1 : cursor;
                }
                if (in.repeat & KEY_DRIGHT)
                {
                    cursor = cursor % 2 ? cursor : cursor + 1;
                }
                if (in.repeat & KEY_DUP)
                {
                    cursor = (cursor + 4) % 6;
                }
                if (in.repeat & KEY_DDOWN)
                {
                    cursor = (cursor + 2) % 6;
                }
            }
            else
            {
                if (in.repeat & KEY_DLEFT)
                {
                    if (cursor % COLS == 0)
                    {
                        changeBox(-1);
                        cursor += COLS - 1;
                    }
                    else
                    {
                        cursor--;
                    }
                }
                if (in.repeat & KEY_DRIGHT)
                {
                    if (cursor % COLS == COLS - 1)
                    {
                        changeBox(1);
                        cursor -= COLS - 1;
                    }
                    else
                    {
                        cursor++;
                    }
                }
                if (in.repeat & KEY_DUP)
                {
                    cursor = (cursor + n - COLS) % n;
                }
                if (in.repeat & KEY_DDOWN)
                {
                    cursor = (cursor + COLS) % n;
                }
            }
            if (in.repeat & KEY_L)
            {
                changeBox(-1);
            }
            if (in.repeat & KEY_R)
            {
                changeBox(1);
            }
            if (before != cursor)
            {
                hover();
            }

            touch(in);

            if (in.down & KEY_A)
            {
                if (holding)
                {
                    drop();
                }
                else
                {
                    grab();
                }
            }
            else if (in.down & KEY_B)
            {
                if (holding)
                {
                    holding = false;
                    held.reset();
                    refresh();
                }
                else
                {
                    leave();
                }
            }
            else if (in.down & KEY_X)
            {
                tools();
            }
            else if (in.down & KEY_Y)
            {
                writeSave();
            }
            else if (in.down & KEY_SELECT)
            {
                switchSide();
            }
            else if (in.down & KEY_START)
            {
                saveMenu();
            }
        }

        void drawTop() override
        {
            std::string where = whereText(current());
            if (side == Side::Bank)
            {
                const BankEntry* e = bank.at(bankBox, cursor);
                if (e && !readable(e->format))
                {
                    inspector::drawStub(e->species, e->form, e->shiny, e->nickname, e->level, e->format, where);
                    return;
                }
                if (e && hovered && std::find(withdrawn.begin(), withdrawn.end(), e->id) != withdrawn.end())
                {
                    where += "  (moving to the save)";
                }
            }
            if (side == Side::Bank)
            {
                inspector::drawCard(hovered.get(), where, version());
                return;
            }
            inspector::draw(hovered.get(), where, version());
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            std::string title = !save ? "Bank" : !save->ref.label.empty() ? save->ref.label : pkdata::game(save->ref.version);
            if (side == Side::Bank)
            {
                widgets::titleBar(BOTTOM_W, "Bank", multi ? std::to_string(markedCount()) + " marked" : "Open");
            }
            else
            {
                widgets::titleBar(BOTTOM_W, title, multi ? std::to_string(markedCount()) + " marked"
                                                     : save && save->dirty ? "Edited - write to keep" : "Connected");
            }

            // Box banner.
            u32 top = side == Side::Bank ? rgb(0x8E, 0x76, 0xB8) : T::BannerTop;
            u32 bottomColor = side == Side::Bank ? rgb(0x4A, 0x32, 0x86) : T::BannerBottom;
            panel(GRID_X, 30, 240, 22, T::Frame, T::FrameEdge);
            vgrad(GRID_X + 22, 31, 196, 20, top, bottomColor);
            panel(GRID_X + 1, 31, 20, 20, T::Frame, T::FrameEdge);
            panel(GRID_X + 219, 31, 20, 20, T::Frame, T::FrameEdge);
            chevron(GRID_X + 9, 37, T::Ink, true);
            chevron(GRID_X + 228, 37, T::Ink, false);
            if (side == Side::Bank && !party)
            {
                // The original Bank page's pager: "01 / 03" (and the box's own name when it has one).
                char pager[32];
                snprintf(pager, sizeof(pager), "%02d / %02d", bankBox + 1, bank.boxes());
                std::string name = bank.boxName(bankBox);
                char plain[16];
                snprintf(plain, sizeof(plain), "Bank %02d", bankBox + 1);
                std::string label = name.empty() || name == plain ? pager : std::string(pager) + "  " + name;
                text(GRID_X + 120, 35, label, T::Bright, Center, 1, 190);
            }
            else
            {
                text(GRID_X + 120, 35, party ? "PARTY" : boxName(), T::Ink, Center, 1, 190);
            }
            if (!party && locks::locked(ownerOf(side), box()))
            {
                // A small padlock at the banner's right end.
                rect(GRID_X + 200, 38, 10, 8, S::ShinyGold);
                outline(GRID_X + 202, 33, 6, 6, S::ShinyGold, 2);
            }

            // The well and its Pokémon (a Bank box may wear one of the storage palette's wallpapers).
            u32 well = T::Well;
            if (!party && side == Side::Bank)
            {
                std::string w = bank.wallpaper(box());
                if (w.compare(0, 6, "color:") == 0)
                {
                    well = bankPalette[std::clamp(atoi(w.c_str() + 6), 0, 7)];
                }
                else if (w.compare(0, 4, "art:") == 0)
                {
                    well = bankPalette[0]; // a game's picture chosen on Android: its first colour stands in
                }
            }
            panel(GRID_X, GRID_Y - 1, COLS * CELL_W, ROWS * CELL_H + 2, well, T::WellEdge);
            // The faint pixel grid of PKForge's box wells.
            for (int gx = GRID_X + 10; gx < GRID_X + COLS * CELL_W - 1; gx += 10)
            {
                rect(gx, GRID_Y, 1, ROWS * CELL_H, alpha(T::Grid, 34));
            }
            for (int gy = GRID_Y + 10; gy < GRID_Y + ROWS * CELL_H; gy += 10)
            {
                rect(GRID_X + 1, gy, COLS * CELL_W - 2, 1, alpha(T::Grid, 34));
            }
            if (party)
            {
                drawParty();
            }
            else
            {
                for (int i = 0; i < int(cells.size()); i++)
                {
                    int x = GRID_X + (i % COLS) * CELL_W, y = GRID_Y + (i / COLS) * CELL_H;
                    if (multi && i < int(marked.size()) && marked[i])
                    {
                        // A marked slot: the original's blue wash and a check in the corner.
                        panel(x + 1, y + 1, CELL_W - 2, CELL_H - 2, alpha(T::Cyan, 70), T::Cyan);
                    }
                    drawCell(cells[i], x + CELL_W / 2, y + CELL_H / 2, isOrigin(i));
                    if (multi && i < int(marked.size()) && marked[i])
                    {
                        image(ui::check, x + CELL_W - 15, y + CELL_H - 15);
                    }
                }
                int cx = GRID_X + (cursor % COLS) * CELL_W, cy = GRID_Y + (cursor / COLS) * CELL_H;
                int bob = (app::frame() / 20) % 2;
                widgets::cursor(cx + 1, cy + 1 - bob, CELL_W - 2, CELL_H - 2,
                    multi ? T::Cyan : holding ? S::CursorGreen : S::CursorRed);
                if (holding)
                {
                    drawHeld(cx + CELL_W / 2, cy + CELL_H / 2 - 8);
                }
            }

            // Side buttons; the Bank's are the original Bank page's (Boxes, Export, Import, Living dex).
            if (side == Side::Bank)
            {
                sideButton(0, save ? "Save" : "Bank", !!save);
                sideButton(1, "Boxes", true);
                sideButton(2, "Export", true);
                sideButton(3, "Import", true);
                sideButton(4, "Search", true);
                sideButton(5, "Dex", true);
                // The original's toolbar marks Boxes and Export with a green edge.
                rect(SIDE_X + 1, sideY(1) + 3, 2, 22, S::Legal);
                rect(SIDE_X + 1, sideY(2) + 3, 2, 22, S::Legal);
            }
            else
            {
                sideButton(0, "Bank", !!save);
                sideButton(1, party ? "Boxes" : "Party", true);
                sideButton(2, "Bag", !!save);
                sideButton(3, "Trainer", !!save);
                sideButton(4, "Write", save && save->dirty);
                sideButton(5, "Tools", true);
            }

            if (multi)
            {
                widgets::footer(BOTTOM_W, {{"A", "Mark"}, {"Y", "All"}, {"X", "Actions"}, {"LR", "Box"}, {"B", "Done"}});
            }
            else if (holding)
            {
                widgets::footer(BOTTOM_W, {{"A", "Drop"}, {"B", "Cancel"}, {"LR", "Box"}, {"SELECT", "Side"}});
            }
            else
            {
                widgets::footer(BOTTOM_W, {{"A", "Grab"}, {"X", "Tools"}, {"Y", "Save data"}, {"LR", "Box"}, {"B", "Back"}});
            }
        }

    private:
        std::shared_ptr<OpenSave> save;
        Bank& bank;
        Side side;
        int saveBox = 0, bankBox = 0, cursor = 0;
        bool party = false;
        std::vector<Cell> cells;
        std::unique_ptr<pksm::PKX> hovered;

        bool holding = false;
        Spot from{};
        std::unique_ptr<pksm::PKX> held;
        bool heldStub = false;
        Cell heldCell;

        std::vector<std::string> deposited; // Bank ids whose save slot is emptied but not yet written
        std::vector<std::string> withdrawn; // Bank ids now in the save, kept in the Bank until it is written

        pksm::Sav* sav() const { return save ? save->sav.get() : nullptr; }

        u8 version() const
        {
            if (sav())
            {
                return u8(sav()->version());
            }
            return hovered ? u8(hovered->version()) : 0;
        }

        int slotCount() const { return party ? 6 : COLS * ROWS; }

        int boxCount() const { return side == Side::Save ? sav()->maxBoxes() : bank.boxes(); }

        int& box() { return side == Side::Save ? saveBox : bankBox; }

        Spot current() const { return {side, side == Side::Save ? saveBox : bankBox, cursor, party}; }

        std::string boxName() const
        {
            if (side == Side::Bank)
            {
                return bank.boxName(bankBox);
            }
            std::string name = sav()->boxName(saveBox);
            if (name.empty())
            {
                char buf[16];
                snprintf(buf, sizeof(buf), "BOX %02d", saveBox + 1);
                name = buf;
            }
            return name;
        }

        std::string whereText(const Spot& s) const
        {
            char buf[48];
            if (s.party)
            {
                snprintf(buf, sizeof(buf), "PARTY %d", s.slot + 1);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%s %02d  SLOT %02d", s.side == Side::Bank ? "BANK" : "BOX", s.box + 1,
                    s.slot + 1);
            }
            return buf;
        }

        std::unique_ptr<pksm::PKX> read(const Spot& s) const
        {
            std::unique_ptr<pksm::PKX> pk;
            if (s.side == Side::Save)
            {
                if (s.party)
                {
                    if (s.slot < sav()->partyCount())
                    {
                        pk = sav()->pkm(u8(s.slot));
                    }
                }
                else
                {
                    pk = sav()->pkm(u8(s.box), u8(s.slot));
                }
            }
            else if (const BankEntry* e = bank.at(s.box, s.slot))
            {
                pk = bank.pkm(*e);
            }
            if (pk && u16(pk->species()) == 0)
            {
                pk.reset();
            }
            return pk;
        }

        Cell cellOf(const pksm::PKX* pk) const
        {
            Cell c;
            if (pk)
            {
                c.species = u16(pk->species());
                c.form    = pk->alternativeForm();
                c.shiny   = pk->shiny();
                c.egg     = pk->egg();
                c.female  = pk->gender() == pksm::Gender::Female;
            }
            return c;
        }

        void refresh()
        {
            app::Timed timed("storage refresh");
            cells.assign(slotCount(), Cell{});
            for (int i = 0; i < slotCount(); i++)
            {
                Spot s{side, box(), i, party};
                if (side == Side::Bank)
                {
                    if (const BankEntry* e = bank.at(bankBox, i))
                    {
                        Cell& c   = cells[i];
                        c.species = e->species;
                        c.form    = e->form;
                        c.shiny   = e->shiny;
                        c.female  = e->female;
                        c.stub    = !readable(e->format);
                        c.transit = std::find(withdrawn.begin(), withdrawn.end(), e->id) != withdrawn.end();
                    }
                }
                else
                {
                    auto pk  = read(s);
                    cells[i] = cellOf(pk.get());
                }
            }
            hover();
        }

        void hover() { hovered = read(current()); }

        void changeBox(int delta)
        {
            if (party)
            {
                return;
            }
            int n = boxCount();
            if (side == Side::Bank && delta > 0 && bankBox + 1 == n)
            {
                // The Bank grows: walking past its last box opens a new one.
                bank.addBox();
                n = bank.boxes();
            }
            box() = (box() + delta + n) % n;
            refresh();
        }

        void switchSide()
        {
            app::Timed timed("switch side");
            if (!save)
            {
                return;
            }
            side   = side == Side::Save ? Side::Bank : Side::Save;
            party  = false;
            cursor = std::min(cursor, COLS * ROWS - 1);
            refresh();
        }

        void toggleParty()
        {
            if (side != Side::Save)
            {
                return;
            }
            party  = !party;
            cursor = party ? std::min(cursor, 5) : 0;
            refresh();
        }

        bool isOrigin(int i) const
        {
            return holding && from == Spot{side, side == Side::Save ? saveBox : bankBox, i, party};
        }

        void touch(Input& in)
        {
            // Box arrows.
            if (!party && in.tapped(GRID_X, 30, 22, 22))
            {
                changeBox(-1);
            }
            if (!party && in.tapped(GRID_X + 218, 30, 22, 22))
            {
                changeBox(1);
            }
            // Side buttons.
            for (int i = 0; i < 6; i++)
            {
                if (in.tapped(SIDE_X, sideY(i), SIDE_W, 26))
                {
                    sidePress(i);
                    return;
                }
            }
            // Slots: tap to point, tap the pointed slot to grab or drop; or drag a Pokémon to its place.
            int hit = slotAt(in.tx, in.ty);
            if (in.touchDown && hit >= 0)
            {
                pressedSlot = hit;
                if (cursor != hit)
                {
                    cursor = hit;
                    hover();
                    pointedFresh = true;
                }
                else
                {
                    pointedFresh = false;
                }
            }
            if (in.touching && in.dragged && pressedSlot >= 0 && !holding && !dragGrab)
            {
                cursor = pressedSlot;
                if (grab())
                {
                    dragGrab = true;
                }
                pressedSlot = -1;
            }
            if (in.touching && dragGrab && hit >= 0 && hit != cursor)
            {
                cursor = hit;
                hover();
            }
            if (in.touchUp)
            {
                if (dragGrab)
                {
                    dragGrab = false;
                    if (slotAt(in.tx, in.ty) >= 0)
                    {
                        drop();
                    }
                    else
                    {
                        holding = false;
                        held.reset();
                        refresh();
                    }
                }
                else if (!in.dragged && pressedSlot >= 0 && pressedSlot == slotAt(in.tx, in.ty) && !pointedFresh)
                {
                    if (holding)
                    {
                        drop();
                    }
                    else
                    {
                        grab();
                    }
                }
                pressedSlot = -1;
            }
        }

        int pressedSlot = -1;
        bool pointedFresh = false, dragGrab = false;

        int slotAt(int x, int y) const
        {
            if (party)
            {
                for (int i = 0; i < 6; i++)
                {
                    int px = GRID_X + 4 + (i % 2) * 118, py = GRID_Y + 2 + (i / 2) * 52;
                    if (x >= px && x < px + 114 && y >= py && y < py + 50)
                    {
                        return i;
                    }
                }
                return -1;
            }
            if (x < GRID_X || y < GRID_Y || x >= GRID_X + COLS * CELL_W || y >= GRID_Y + ROWS * CELL_H)
            {
                return -1;
            }
            return (y - GRID_Y) / CELL_H * COLS + (x - GRID_X) / CELL_W;
        }

        int sideY(int i) const { return 30 + i * 31; }

        void sideButton(int i, const std::string& label, bool enabled)
        {
            widgets::buttonFace(SIDE_X, sideY(i), SIDE_W, 28, label, false, false, -1, enabled);
        }

        void sidePress(int i)
        {
            if (side == Side::Bank && i >= 1 && i <= 5)
            {
                switch (i)
                {
                    case 5: livingdex::open(); break;
                    case 1: boxManager(); break;
                    case 2:
                    {
                        std::string folder;
                        int n = bank.exportBox(bankBox, folder);
                        app::message("Exported", std::to_string(n) + " Pokémon written to " + folder);
                        break;
                    }
                    case 3: importFiles(); break;
                    case 4: searchBank(); break;
                }
                return;
            }
            switch (i)
            {
                case 0: switchSide(); break;
                case 1: toggleParty(); break;
                case 2:
                    if (save && !hardcoreBlocks())
                    {
                        app::push(screens::bag(save));
                    }
                    break;
                case 3:
                    if (save && !hardcoreBlocks())
                    {
                        app::push(screens::trainer(save));
                    }
                    break;
                case 4: writeSave(); break;
                case 5: tools(); break;
            }
        }

        void drawCell(const Cell& c, float cx, float cy, bool origin)
        {
            if (c.species == 0)
            {
                return;
            }
            float blend = origin ? 0.6f : c.transit ? 0.55f : 0;
            if (c.egg)
            {
                imageCentered(ui::egg, cx, cy);
            }
            else if (!icon(c.species, c.form, cx, cy, 1, T::Well, blend))
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", c.species);
                text(cx, cy - 6, buf, T::InkSoft, Center);
            }
            if (c.shiny && !c.egg)
            {
                star(cx + 15, cy - 11, 3.5f, S::ShinyGold);
            }
            if (c.stub)
            {
                rect(cx - 18, cy + 11, 5, 5, S::Illegal);
            }
        }

        void drawHeld(float cx, float cy)
        {
            if (heldCell.egg)
            {
                imageCentered(ui::egg, cx, cy);
            }
            else
            {
                icon(heldCell.species, heldCell.form, cx, cy);
            }
        }

        void drawParty()
        {
            for (int i = 0; i < 6; i++)
            {
                int x = GRID_X + 4 + (i % 2) * 118, y = GRID_Y + 2 + (i / 2) * 52;
                bool sel = i == cursor;
                panel(x, y, 114, 50, sel ? T::PartySelected : T::PartyBody, sel ? T::PartyRim : T::PartyRimDark);
                rect(x + 2, y + 2, 110, 8, sel ? T::PartySelectedBand : T::PartyBand);
                const Cell& c = cells[i];
                if (c.species)
                {
                    drawCell(c, x + 22, y + 26, isOrigin(i));
                    auto pk = read({Side::Save, 0, i, true});
                    if (pk)
                    {
                        text(x + 44, y + 12, c.egg ? "Egg" : pk->nickname(), T::Ink, Left, 1, 66);
                        char buf[16];
                        snprintf(buf, sizeof(buf), "Lv. %d", pk->level());
                        text(x + 44, y + 28, buf, T::Value);
                        int maxHp = std::max<int>(1, pk->stat(pksm::Stat::HP));
                        int hp    = pk->partyCurrHP() < 0 ? maxHp : pk->partyCurrHP();
                        rect(x + 44, y + 42, 64, 4, T::PartyRimDark);
                        rect(x + 44, y + 42, std::clamp(hp * 64 / maxHp, 0, 64), 4,
                            hp * 2 > maxHp ? S::Legal : hp * 5 > maxHp ? S::ShinyGold : S::Illegal);
                    }
                }
                else
                {
                    text(x + 57, y + 20, "-", T::SubInk, Center);
                }
                if (sel)
                {
                    widgets::cursor(x, y, 114, 50, holding ? S::CursorGreen : S::CursorRed);
                }
            }
            if (holding)
            {
                int x = GRID_X + 4 + (cursor % 2) * 118, y = GRID_Y + 2 + (cursor / 2) * 52;
                drawHeld(x + 22, y + 16);
            }
        }

        // ---- moving Pokémon

        bool grab()
        {
            Spot s = current();
            if (refuseLocked(s))
            {
                return false;
            }
            if (s.side == Side::Bank)
            {
                const BankEntry* e = bank.at(s.box, s.slot);
                if (!e)
                {
                    return false;
                }
                if (std::find(withdrawn.begin(), withdrawn.end(), e->id) != withdrawn.end())
                {
                    app::toast("It is on its way to the save: write the save first.");
                    return false;
                }
                heldStub = !readable(e->format);
                held     = heldStub ? nullptr : bank.pkm(*e);
                heldCell = cells[s.slot];
                if (!heldStub && !held)
                {
                    app::toast("This Bank entry could not be read.");
                    return false;
                }
            }
            else
            {
                held = read(s);
                if (!held)
                {
                    return false;
                }
                heldStub = false;
                heldCell = cellOf(held.get());
            }
            from    = s;
            holding = true;
            return true;
        }

        void drop()
        {
            if (!holding)
            {
                return;
            }
            Spot to = current();
            std::string error;
            bool ok = true;
            if (!(to == from) && refuseLocked(to))
            {
                return;
            }
            if (to == from)
            {
                ok = true;
            }
            else if (from.side == Side::Bank && to.side == Side::Bank)
            {
                ok = bank.move(from.box, from.slot, to.box, to.slot, error);
            }
            else if (from.side == Side::Save && to.side == Side::Save)
            {
                ok = moveInSave(to, error);
            }
            else if (from.side == Side::Save && to.side == Side::Bank)
            {
                ok = deposit(to, error);
            }
            else
            {
                // Into the save from the Bank: the transfer preview first when the format changes or the
                // game's rules would change something (the original shows every change before writing).
                if (!previewed && held && !heldStub && !read(to))
                {
                    std::string why;
                    auto converted = intoSave(*held, why);
                    if (converted && preview::needed(*held, *converted, *sav()))
                    {
                        app::push(preview::open(*held, *converted, *sav(), pkdata::game(save->ref.version), [this]()
                            {
                                previewed = true;
                                drop();
                                previewed = false;
                            }));
                        return; // still holding until the player decides
                    }
                }
                ok = withdraw(to, error);
            }
            if (!ok)
            {
                if (!error.empty())
                {
                    app::toast(error);
                }
                return; // keep holding: the user can pick another slot
            }
            holding = false;
            held.reset();
            refresh();
        }

        int partyCount() const { return sav() ? sav()->partyCount() : 0; }

        void writeSlot(const Spot& s, const pksm::PKX* pk)
        {
            auto empty = sav()->emptyPkm();
            const pksm::PKX& data = pk ? *pk : *empty;
            if (s.party)
            {
                int slot = s.slot;
                if (pk && slot >= partyCount())
                {
                    slot = partyCount();
                    sav()->partyCount(u8(slot + 1));
                }
                sav()->pkm(data, u8(slot));
                sav()->fixParty();
            }
            else
            {
                sav()->pkm(data, u8(s.box), u8(s.slot), false);
            }
            if (pk)
            {
                sav()->dex(*pk);
            }
            save->dirty = true;
        }

        bool leavesPartyEmpty(const Spot& leaving, const pksm::PKX* incoming) const
        {
            return leaving.party && !incoming && partyCount() <= 1;
        }

        bool moveInSave(const Spot& to, std::string& error)
        {
            auto target = read(to);
            if (leavesPartyEmpty(from, target.get()))
            {
                error = "Your party needs at least one Pokémon.";
                return false;
            }
            auto moving = read(from);
            if (!moving)
            {
                return true;
            }
            // Party slots stay packed: write the target first, then the origin, then close gaps.
            writeSlot(to, moving.get());
            writeSlot(from, target.get());
            return true;
        }

        bool deposit(const Spot& to, std::string& error)
        {
            if (bank.at(to.box, to.slot))
            {
                error = "Pick an empty Bank slot.";
                return false;
            }
            if (leavesPartyEmpty(from, nullptr))
            {
                error = "Your party needs at least one Pokémon.";
                return false;
            }
            auto moving = read(from);
            if (!moving)
            {
                return true;
            }
            if (!bank.put(*moving, to.box, to.slot, save->ref.label.empty() ? pkdata::game(save->ref.version) : save->ref.label, error))
            {
                return false;
            }
            deposited.push_back(bank.at(to.box, to.slot)->id);
            writeSlot(from, nullptr);
            app::toast("Sent to the Bank. Write the save to keep it.");
            return true;
        }

        std::unique_ptr<pksm::PKX> intoSave(const pksm::PKX& pk, std::string& error)
        {
            auto reason = romhack::is(*sav()) ? pksm::Sav::BadTransferReason::OKAY : sav()->invalidTransferReason(pk);
            if (reason != pksm::Sav::BadTransferReason::OKAY && !hax::on())
            {
                error = transferProblem(reason);
                return nullptr;
            }
            std::unique_ptr<pksm::PKX> converted;
            if (romhack::is(*sav()))
            {
                // A ROM hack takes any Pokémon whose species it has; its numbers come from the hack's tables.
                if (!romhack::canHold(*sav(), pk))
                {
                    error = std::string(romhack::name(romhack::hackOf(*sav()))) + " has no " + pkdata::species(u16(pk.species())) + ".";
                    return nullptr;
                }
                converted = pk.clone();
            }
            else if (romhack::is(pk))
            {
                error = "A ROM hack Pokémon only moves into a ROM hack save (its numbers are the hack's own).";
                return nullptr;
            }
            else if (pkdata::format(pk) == pkdata::format(*sav()->emptyPkm()))
            {
                converted = pk.clone();
            }
            else
            {
                converted = sav()->transfer(pk);
            }
            if (!converted)
            {
                error = "This Pokémon cannot move to this game.";
            }
            return converted;
        }

        bool withdraw(const Spot& to, std::string& error)
        {
            if (heldStub)
            {
                error = "The 3DS port cannot open this format.";
                return false;
            }
            if (read(to))
            {
                error = "Pick an empty slot in the save.";
                return false;
            }
            const BankEntry* e = bank.at(from.box, from.slot);
            if (!e || !held)
            {
                return true;
            }
            auto converted = intoSave(*held, error);
            if (!converted)
            {
                return false;
            }
            writeSlot(to, converted.get());
            withdrawn.push_back(e->id);
            app::toast("Moved to the save. Write the save to finish.");
            return true;
        }

        // ---- the save on the card

        void writeSave()
        {
            if (!save)
            {
                return;
            }
            if (!save->dirty)
            {
                app::toast("Nothing to write.");
                return;
            }
            app::confirm("Write the changes to " + files::baseName(save->ref.path) +
                             "?\nA restore point of the file is kept first.",
                [this]()
                {
                    std::string error;
                    if (!save->write(error))
                    {
                        app::message("Not written", error);
                        return;
                    }
                    finishTransfers();
                    app::toast("Save written.");
                    refresh();
                },
                "Write", "Cancel");
        }

        void finishTransfers()
        {
            std::string error;
            for (auto& id : withdrawn)
            {
                for (int b = 0; b < bank.boxes(); b++)
                {
                    for (int s = 0; s < Bank::SLOTS; s++)
                    {
                        const BankEntry* e = bank.at(b, s);
                        if (e && e->id == id)
                        {
                            bank.remove(b, s, error);
                        }
                    }
                }
            }
            withdrawn.clear();
            deposited.clear();
        }

        void undoDeposits()
        {
            std::string error;
            for (auto& id : deposited)
            {
                for (int b = 0; b < bank.boxes(); b++)
                {
                    for (int s = 0; s < Bank::SLOTS; s++)
                    {
                        const BankEntry* e = bank.at(b, s);
                        if (e && e->id == id)
                        {
                            bank.remove(b, s, error);
                        }
                    }
                }
            }
            deposited.clear();
            withdrawn.clear();
        }

        void leave()
        {
            if (save && save->dirty)
            {
                app::confirm("Leave without writing the save?\nIts changes are dropped, and Pokémon sent to the "
                             "Bank go back to it.",
                    [this]()
                    {
                        undoDeposits();
                        app::pop();
                    },
                    "Leave", "Stay");
                return;
            }
            app::pop();
        }

        // ---- menus

        // Every box at a glance (of the side on screen): how full each is and who is in it; a pick opens it.
        void allBoxes()
        {
            std::vector<screens::PickItem> items;
            int total = 0;
            for (int b = 0; b < boxCount(); b++)
            {
                int count = 0;
                std::vector<std::string> names;
                int slots = side == Side::Bank ? Bank::SLOTS : boxSlots();
                for (int i = 0; i < slots; i++)
                {
                    u16 species = 0;
                    if (side == Side::Bank)
                    {
                        if (const BankEntry* e = bank.at(b, i))
                        {
                            species = e->species;
                        }
                    }
                    else if (auto pk = read(Spot{Side::Save, b, i, false}))
                    {
                        species = pk->egg() ? 0xFFFF : u16(pk->species());
                    }
                    if (!species)
                    {
                        continue;
                    }
                    count++;
                    std::string name = species == 0xFFFF ? "Egg" : pkdata::species(species);
                    if (names.size() < 6 && std::find(names.begin(), names.end(), name) == names.end())
                    {
                        names.push_back(name);
                    }
                }
                total += count;
                std::string who;
                for (auto& n : names)
                {
                    who += (who.empty() ? "" : ", ") + n;
                }
                if (count > int(names.size()))
                {
                    who += "...";
                }
                items.push_back({b, boxLabel(side, b) + "  " + std::to_string(count) + "/" + std::to_string(slots) +
                                        (count ? "  " + who : std::string()), ""});
            }
            std::string title = (side == Side::Bank ? "Bank: " : "Boxes: ") + std::to_string(total) + " Pokémon";
            app::push(screens::picker(title, std::move(items), box(), [this](int b)
                {
                    party = false;
                    box() = b;
                    refresh();
                }));
        }

        void saveMenu()
        {
            std::vector<app::Option> o;
            o.push_back({"All boxes", [this]() { allBoxes(); }, "Every box at a glance: how full, and who is in it."});
            if (save)
            {
                o.push_back({"Search this save", [this]() { searchSave(); }, "Every Pokémon in the party and the boxes by name; picks open its box."});
                o.push_back({"Bag", [this]() { if (!hardcoreBlocks()) app::push(screens::bag(save)); }});
                o.push_back({"Trainer card", [this]() { if (!hardcoreBlocks()) app::push(screens::trainer(save)); }});
                o.push_back({"Write save", [this]() { writeSave(); }, "", save->dirty});
                o.push_back({"Undo changes", [this]() { undoChanges(); },
                    "Back to the save as it was when opened or last written; Pokémon sent to the Bank go back.",
                    save->dirty});
                o.push_back({"Restore points", [this]() { restoreMenu(); },
                    "The save as it was before an earlier write: PKForge keeps the 20 newest."});
                if (side == Side::Save && !party)
                {
                    o.push_back({"Box manager", [this]() { boxManager(); },
                        "Sort by dex, level, IVs, type or date; lock, copy, swap and rename boxes; audit the whole save."});
                    o.push_back({"Export box as Showdown team", [this]() { exportShowdown(true); },
                        "One text file with every Pokémon of this box."});
                    o.push_back({"Import Showdown sets", [this]() { importShowdown(); },
                        "From sdmc:/3ds/PKForge/showdown/import.txt into the empty slots, from this box on."});
                }
                if (side == Side::Save)
                {
                    if (world::hasDayCare(*sav()))
                    {
                        o.push_back({"Day Care", [this]() { world::dayCare(save, [this]() { refresh(); }); },
                            "Who is deposited, the EXP gained, whether an egg waits; withdraw into a box."});
                    }
                    if (world::hasAny(*sav()))
                    {
                        o.push_back({"World & events", [this]() { if (!hardcoreBlocks()) world::open(save); },
                            "This game's world editors: clock, roamers, Apricorns, Pokéwalker, honey trees."});
                    }
                    o.push_back({"Living Dex", []() { livingdex::open(); },
                        "Every species and where you have it: the Bank and every save found."});
                    o.push_back({"Mystery Gift", [this]() { mysteryGiftMenu(); },
                        "Every real distribution for this game, sent to its gift inbox."});
                    if (sav()->generation() >= pksm::Generation::FOUR && sav()->currentGiftAmount() > 0)
                    {
                        o.push_back({"Gifts in this save", [this]() { giftInbox(); },
                            "The Mystery Gift cards the save holds now (read only)."});
                    }
                    o.push_back({"Complete the Pokédex", [this]() { completeDex(); },
                        "Every species this game has, seen and caught."});
                    if (recordsOffset() >= 0)
                    {
                        o.push_back({"Trainer records", [this]() { trainerRecords(); },
                            "The game's 200 trainer counters (read only: their meaning differs by game)."});
                    }
                    if (dexedit::supported(*sav()))
                    {
                        o.push_back({"Pokédex editor", [this]() { if (!hardcoreBlocks()) dexEditor(0); },
                            "Each species unseen, seen or caught; A changes it."});
                    }
                }
                if (side == Side::Save)
                {
                    o.push_back({party ? "Legalize the party" : "Legalize this box", [this]() { legalizeAll(); },
                        "Moves it can learn here; ability, item and ball this game has; IV, EV and PP limits."});
                    std::string at = online::server();
                    o.push_back({"Legality server", []() { online::askServer(); },
                        at.empty() ? "Where local-gpss runs, for Legalize with PKHeX. Not set yet." : at});
                }
            }
            if (side == Side::Bank)
            {
                o.push_back({"Box manager", [this]() { boxManager(); }, "Sort, lock, copy, swap and rename Bank boxes."});
                o.push_back({"Export this Bank box", [this]()
                    {
                        std::string folder;
                        int n = bank.exportBox(bankBox, folder);
                        app::message("Exported", std::to_string(n) + " Pokémon written to " + folder);
                    },
                    "One .pkN file per Pokémon, readable by PKForge, PKHeX and PKSM."});
                o.push_back({"Import .pkN files", [this]() { importFiles(); },
                    "Every .pk1-.pk9/.pb7/.pa8/.pa9 file in sdmc:/3ds/PKForge/import."});
            }
            o.push_back({"Poképark", []() { app::push(screens::park()); }, "Your Bank's Pokémon living in the park."});
            o.push_back({"Collection Center", [this]() { collection::open(); refresh(); },
                "The community's event collections (RoC's PC), into the Bank. Needs Wi-Fi."});
            o.push_back({"Sound", []() { screens::soundMenu(); }, "Music and sound effects, on or off."});
            o.push_back({"Receive by QR (camera)", [this]()
                { app::push(qr::scan([this](std::vector<u8> payload) { receiveQr(payload); })); },
                "Scan a PKForge QR code with the outer camera."});
            o.push_back({"Receive from a QR picture", [this]() { qrPictures(); },
                "A screenshot or photo of a PKForge QR code in sdmc:/3ds/PKForge/qr."});
            o.push_back({hax::hardcore() ? "Hardcore mode: on" : "Hardcore mode: off", []()
                {
                    hax::hardcore(!hax::hardcore());
                    app::toast(hax::hardcore() ? "Hardcore on: editing is off, moving stays on." : "Hardcore off.");
                },
                "Data editing off for challenge runs and RetroAchievements; moving Pokémon stays possible."});
            o.push_back({hax::on() ? "HaX mode: on" : "HaX mode: off", []()
                {
                    if (hax::hardcore())
                    {
                        app::toast("Turn Hardcore mode off first.");
                        return;
                    }
                    if (hax::on())
                    {
                        hax::set(false);
                        app::toast("HaX mode off.");
                        return;
                    }
                    app::confirm("HaX mode lets the editor pick any species, move, item and ball this generation can "
                                 "store, lifts the 510 EV total, and lets Pokémon into games that lack them. Such "
                                 "Pokémon can crash a game or fail its checks.",
                        []()
                        {
                            hax::set(true);
                            app::toast("HaX mode on.");
                        },
                        "Turn on", "Cancel");
                },
                "Lift the legality limits, as PKHeX's HaX."});
            o.push_back({"Close", [this]() { leave(); }});
            app::menu(save ? pkdata::game(save->ref.version) : "Bank", std::move(o));
        }

        // One Pokémon through PKHeX's auto-legality on local-gpss; the result replaces it in the save.
        void legalizeOnline()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            if (online::server().empty() && !online::askServer())
            {
                return;
            }
            Spot s = current();
            auto pk = read(s);
            if (!pk)
            {
                return;
            }
            online::legalize(*pk, u8(sav()->version()), [this, s](online::Result& r)
                {
                    if (!r.error.empty())
                    {
                        app::message("Legalize with PKHeX", r.error + "\nSTART > Legality server changes the address.");
                        return;
                    }
                    std::string text;
                    if (!r.ran)
                    {
                        text = r.legal ? "PKHeX finds it legal already. Nothing changed." : "PKHeX did not change it.";
                    }
                    else if (r.pokemon)
                    {
                        writeSlot(s, r.pokemon.get());
                        refresh();
                        text = r.legal ? "Legalized by PKHeX. Write the save to keep it."
                                       : "PKHeX changed it but could not make it fully legal. Write the save to keep it.";
                    }
                    else
                    {
                        text = "PKHeX could not legalize this one.";
                    }
                    int shown = 0;
                    for (const auto& line : r.report)
                    {
                        if (shown++ == 5)
                        {
                            break;
                        }
                        text += "\n" + line;
                    }
                    app::message("Legalize with PKHeX", text);
                });
        }

        void legalizeAll()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            int fixed = 0, seen = 0;
            std::string example;
            int slots = party ? 6 : sav()->maxSlot() / sav()->maxBoxes();
            for (int slot = 0; slot < slots; slot++)
            {
                Spot s{Side::Save, saveBox, slot, party};
                auto pk = read(s);
                if (!pk)
                {
                    continue;
                }
                seen++;
                auto changes = legality::fix(*pk, *sav());
                if (!changes.empty())
                {
                    fixed++;
                    writeSlot(s, pk.get());
                    if (example.empty())
                    {
                        example = changes.front();
                    }
                }
            }
            refresh();
            app::message("Legalize", fixed ? std::to_string(fixed) + " of " + std::to_string(seen) +
                                                 " Pokémon fixed (for example: " + example +
                                                 "). Write the save to keep it." :
                                             (seen == 1 ? std::string("This Pokémon was") : "All " + std::to_string(seen) +
                                                                                            " Pokémon were") +
                                                 " already within the rules PKForge can check.");
        }

        void renameBox()
        {
            std::string name = boxName();
            if (!app::keyboard("Box name", name, side == Side::Bank ? 16 : 8))
            {
                return;
            }
            if (side == Side::Bank)
            {
                bank.boxName(bankBox, name);
            }
            else
            {
                sav()->boxName(u8(saveBox), name);
                save->dirty = true;
            }
        }

        void importFiles()
        {
            std::string folder = std::string(files::ROOT) + "/import";
            files::mkdirs(folder);
            std::string error;
            int n = bank.importFolder(folder, error);
            app::message("Import", n ? std::to_string(n) + " Pokémon joined the Bank." :
                                       "No .pkN files found in " + folder + (error.empty() ? "" : ("\n" + error)));
            refresh();
        }

        void tools()
        {
            app::Timed timed("tools menu");
            if (holding)
            {
                return;
            }
            Spot s = current();
            std::vector<app::Option> o;
            const BankEntry* e = s.side == Side::Bank ? bank.at(s.box, s.slot) : nullptr;
            bool transit = e && std::find(withdrawn.begin(), withdrawn.end(), e->id) != withdrawn.end();
            if (hovered)
            {
                o.push_back({"Edit", [this]() { edit(); }, "", !transit});
                if (s.side == Side::Save)
                {
                    o.push_back({"Send to the Bank", [this]() { quickDeposit(); },
                        "Into the first empty Bank slot. Write the save to keep it.", !leavesPartyEmpty(s, nullptr)});
                }
                else if (save)
                {
                    o.push_back({"Withdraw to the save", [this]() { quickWithdraw(); },
                        "Into the first empty slot of the save's current box.", !transit});
                }
                o.push_back({"Copy", [this]()
                    {
                        clipboard = hovered->clone();
                        app::toast("Copied.");
                    }});
                if (s.side == Side::Save && !s.party)
                {
                    o.push_back({"Clone", [this]() { cloneHovered(); }, "Into the next empty slot of this box."});
                }
                o.push_back({"Export Showdown set", [this]() { exportShowdown(false); },
                    "A text file in sdmc:/3ds/PKForge/showdown, for PKHeX, PKSM and Showdown."});
                if (s.side == Side::Save)
                {
                    o.push_back({"Legalize with PKHeX", [this]() { legalizeOnline(); },
                        "Through local-gpss on a computer on this Wi-Fi (START > Legality server)."});
                }
                o.push_back({"Show QR code", [this, s]() { app::push(qr::show(*hovered, whereText(s))); },
                    "For PKForge on another 3DS or on Android to scan."});
                o.push_back({"Hex editor", [this]() { hexEdit(); }, "Its raw bytes, decrypted.", !transit});
                o.push_back({"RNG / IVs", [this]() { rngTool(); }, "PID, encryption constant, IVs, nature, shiny; reroll the nature keeping shininess.", !transit});
                o.push_back({"Export .pk file", [this]() { exportOne(); }});
                o.push_back({s.side == Side::Bank ? "Delete from the Bank" : "Release", [this]() { release(); }, "",
                    !transit && !leavesPartyEmpty(s, nullptr)});
            }
            else if (!e)
            {
                o.push_back({"Paste", [this]() { paste(); }, "", clipboard != nullptr});
                if (s.side == Side::Save)
                {
                    o.push_back({"New Pokémon", [this]() { create(); }, "A level 5 Pokémon from this save's trainer."});
                    o.push_back({"Catch in the wild", [this]() { catchWild(); },
                        "Where a species can be caught or received in this game, and one made there as the game makes it.",
                        !romhack::is(*sav()) && (!encounters::of(*sav()).empty() || !encounters::statics(*sav()).empty() ||
                            !encounters::trades(*sav()).empty())});
                    o.push_back({"New egg", [this]() { newEgg(); }, "An egg of any species this game has, as from the Day Care."});
                    o.push_back({"Egg factory", [this]() { eggFactory(); }, "One egg of every species, perfect IVs or shiny if you like."});
                }
            }
            if (!party)
            {
                o.push_back({"Multi-select", [this]() { startMulti(); },
                    "Mark several Pokémon of this box, then one action for all (send, move, duplicate, export, release)."});
            }
            if (o.empty())
            {
                return;
            }
            app::menu(whereText(s), std::move(o));
        }

        void edit()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            app::Timed timed("open editor");
            Spot s = current();
            auto pk = read(s);
            if (!pk)
            {
                return;
            }
            pksm::Sav* context = s.side == Side::Save ? sav() : nullptr;
            app::push(screens::editor(std::move(pk), whereText(s), context, keeper(s),
                [this](int delta, std::unique_ptr<pksm::PKX>& next, std::string& where,
                    std::function<void(std::unique_ptr<pksm::PKX>)>& done)
                {
                    // The next occupied slot of this box (or the party) that can be edited, round the end.
                    int n = slotCount();
                    for (int k = 1; k < n; k++)
                    {
                        int slot = ((cursor + delta * k) % n + n) % n;
                        Spot s   = current();
                        s.slot   = slot;
                        if (!party && cells[slot].transit)
                        {
                            continue;
                        }
                        auto pk = read(s);
                        if (!pk || u16(pk->species()) == 0)
                        {
                            continue;
                        }
                        cursor = slot;
                        hover();
                        next  = std::move(pk);
                        where = whereText(s);
                        done  = keeper(s);
                        return true;
                    }
                    return false;
                }));
        }

        void rngTool()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            Spot s  = current();
            auto pk = read(s);
            if (!pk)
            {
                return;
            }
            auto write = keeper(s);
            app::push(rng::open(std::move(pk), whereText(s), [this, s, write](std::unique_ptr<pksm::PKX> changed)
                {
                    if (!refuseLocked(s))
                    {
                        write(std::move(changed));
                    }
                }));
        }

        // What the editor calls with the kept Pokémon of one slot.
        std::function<void(std::unique_ptr<pksm::PKX>)> keeper(Spot s)
        {
            return [this, s](std::unique_ptr<pksm::PKX> edited)
            {
                if (s.side == Side::Save)
                {
                    writeSlot(s, edited.get());
                }
                else
                {
                    std::string error;
                    if (!bank.replace(s.box, s.slot, *edited, error))
                    {
                        app::message("Not saved", error);
                    }
                }
                refresh();
            };
        }

        void qrPictures()
        {
            std::string folder = std::string(files::ROOT) + "/qr";
            files::mkdirs(folder);
            auto names = std::make_shared<std::vector<std::string>>();
            files::list(folder, [&](const std::string& name, bool dir, u64)
                {
                    std::string low = files::lower(name);
                    for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp"})
                    {
                        if (!dir && low.size() > strlen(ext) && low.compare(low.size() - strlen(ext), strlen(ext), ext) == 0)
                        {
                            names->push_back(name);
                        }
                    }
                });
            if (names->empty())
            {
                app::message("QR picture", "Put a screenshot or photo of a PKForge QR code (PNG, JPEG or BMP) in " +
                                               folder + ", then try again.");
                return;
            }
            std::sort(names->begin(), names->end());
            std::vector<screens::PickItem> items;
            for (int i = 0; i < int(names->size()); i++)
            {
                items.push_back({i, (*names)[i], ""});
            }
            app::push(screens::picker("QR picture", std::move(items), -1, [this, names, folder](int i)
                {
                    std::vector<u8> payload;
                    std::string error;
                    if (!qr::decodeFile(folder + "/" + (*names)[i], payload, error))
                    {
                        app::message("QR picture", error);
                        return;
                    }
                    receiveQr(payload);
                }));
        }

        // A PKF1 Pokémon from a QR code: into this save's first empty box slot, or the Bank's.
        void receiveQr(const std::vector<u8>& payload)
        {
            if (hardcoreBlocks())
            {
                return;
            }
            std::unique_ptr<pksm::PKX> pk;
            std::string species, error;
            if (!qr::open(payload, pk, species, error))
            {
                app::message("QR code", error);
                return;
            }
            auto shared = std::shared_ptr<pksm::PKX>(std::move(pk));
            std::string what = species + ", level " + std::to_string(shared->level()) + ", trainer " + shared->otName();
            bool toSave = side == Side::Save && save;
            app::confirm("Receive " + what + (toSave ? " into this save?" : " into the Bank?"), [this, shared, toSave]()
                {
                    std::string error;
                    if (toSave)
                    {
                        auto converted = intoSave(*shared, error);
                        if (!converted)
                        {
                            app::message("QR code", error);
                            return;
                        }
                        auto [box, slot] = emptySlot(saveBox);
                        if (box < 0)
                        {
                            app::message("QR code", "The boxes are full.");
                            return;
                        }
                        writeSlot(Spot{Side::Save, box, slot, false}, converted.get());
                        refresh();
                        app::toast("Received. Write the save to keep it.");
                        return;
                    }
                    for (int b = 0; b < bank.boxes(); b++)
                    {
                        for (int slot = 0; slot < Bank::SLOTS; slot++)
                        {
                            if (!bank.at(b, slot))
                            {
                                if (bank.put(*shared, b, slot, "QR code", error))
                                {
                                    refresh();
                                    app::toast("Received into the Bank.");
                                }
                                else
                                {
                                    app::message("QR code", error);
                                }
                                return;
                            }
                        }
                    }
                    app::message("QR code", "The Bank is full.");
                },
                "Receive", "Cancel");
        }

        void hexEdit()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            Spot s  = current();
            auto pk = read(s);
            if (!pk)
            {
                return;
            }
            app::push(screens::hex(std::move(pk), whereText(s), [this, s](std::unique_ptr<pksm::PKX> edited)
                {
                    if (s.side == Side::Save)
                    {
                        writeSlot(s, edited.get());
                    }
                    else
                    {
                        std::string error;
                        if (!bank.replace(s.box, s.slot, *edited, error))
                        {
                            app::message("Not saved", error);
                        }
                    }
                    refresh();
                }));
        }

        void quickDeposit()
        {
            if (refuseLocked(current()))
            {
                return;
            }
            for (int b = 0; b < bank.boxes() + 1; b++)
            {
                for (int i = 0; i < Bank::SLOTS; i++)
                {
                    if (!bank.at(b, i))
                    {
                        from = current();
                        std::string error;
                        if (!deposit({Side::Bank, b, i, false}, error))
                        {
                            app::toast(error);
                        }
                        refresh();
                        return;
                    }
                }
            }
        }

        void quickWithdraw()
        {
            if (refuseLocked(current()) || refuseLocked(Spot{Side::Save, saveBox, 0, false}))
            {
                return;
            }
            if (!previewed)
            {
                auto pk = read(current());
                std::string why;
                auto converted = pk ? intoSave(*pk, why) : nullptr;
                if (converted && preview::needed(*pk, *converted, *sav()))
                {
                    app::push(preview::open(*pk, *converted, *sav(), pkdata::game(save->ref.version), [this]()
                        {
                            previewed = true;
                            quickWithdraw();
                            previewed = false;
                        }));
                    return;
                }
            }
            from     = current();
            held     = read(from);
            heldStub = false;
            for (int i = 0; i < COLS * ROWS; i++)
            {
                Spot to{Side::Save, saveBox, i, false};
                if (!read(to))
                {
                    std::string error;
                    if (!withdraw(to, error))
                    {
                        app::message("Cannot move", error);
                    }
                    held.reset();
                    refresh();
                    return;
                }
            }
            held.reset();
            app::toast("The save's current box is full.");
        }

        void paste()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            if (!clipboard || refuseLocked(current()))
            {
                return;
            }
            Spot s = current();
            std::string error;
            if (s.side == Side::Bank)
            {
                if (!bank.put(*clipboard, s.box, s.slot, "Copy", error))
                {
                    app::toast(error);
                }
            }
            else
            {
                auto pk = intoSave(*clipboard, error);
                if (!pk)
                {
                    app::message("Cannot paste", error);
                    return;
                }
                writeSlot(s, pk.get());
            }
            refresh();
        }

        void release()
        {
            Spot s = current();
            if (refuseLocked(s))
            {
                return;
            }
            std::string name = hovered ? hovered->nickname() : "this Pokémon";
            app::confirm((s.side == Side::Bank ? "Delete " : "Release ") + name + "?" +
                             (s.side == Side::Bank ? "\nIt leaves the Bank for good." : ""),
                [this, s]()
                {
                    std::string error;
                    if (hovered && u16(hovered->species()))
                    {
                        std::string who = hovered->nickname();
                        std::string old;
                        files::readText("sdmc:/3ds/PKForge/wilds.txt", old);
                        files::writeText("sdmc:/3ds/PKForge/wilds.txt",
                            old + std::to_string(u16(hovered->species())) + "\t" + who + "\n");
                        app::toast(who + " heads off into the Wilds. It may visit the Poképark one day.");
                    }
                    if (s.side == Side::Bank)
                    {
                        bank.remove(s.box, s.slot, error);
                    }
                    else
                    {
                        writeSlot(s, nullptr);
                    }
                    refresh();
                },
                s.side == Side::Bank ? "Delete" : "Release", "Keep");
        }

        void exportOne()
        {
            if (!hovered)
            {
                return;
            }
            std::string folder = std::string(files::ROOT) + "/export";
            files::mkdirs(folder);
            char name[96];
            snprintf(name, sizeof(name), "%03d - %s - %s%s", int(u16(hovered->species())),
                files::sanitize(hovered->nickname()).c_str(), files::stamp().c_str(),
                std::string(hovered->extension()).c_str());
            auto raw = hovered->rawData();
            if (files::write(folder + "/" + name, raw.data(), raw.size()))
            {
                app::message("Exported", folder + "/" + name);
            }
            else
            {
                app::toast("Could not write the file.");
            }
        }

        void create()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            if (refuseLocked(current()))
            {
                return;
            }
            auto pk = freshPk();
            Spot s  = current();
            app::push(screens::editor(std::move(pk), whereText(s), sav(),
                [this, s](std::unique_ptr<pksm::PKX> made)
                {
                    writeSlot(s, made.get());
                    refresh();
                }));
        }

        // A level 5 Bulbasaur with Tackle from this save's trainer: the start of New Pokémon and imports.
        std::unique_ptr<pksm::PKX> freshPk()
        {
            auto pk = sav()->emptyPkm();
            int lang = int(sav()->language());
            pk->species(pksm::Species{u16(1)});
            pk->alternativeForm(0);
            pk->language(sav()->language());
            pk->nickname(i18n::species(pksm::Language(lang), pksm::Species{u16(1)}));
            pk->nicknamed(false);
            pk->version(sav()->version());
            pk->otName(sav()->otName());
            pk->otGender(sav()->gender());
            pk->TID(sav()->TID());
            pk->SID(sav()->SID());
            pk->nature(pksm::Nature{u8(rand() % 25)});
            pk->setAbility(0);
            pk->ball(pksm::Ball{u8(4)});
            pk->level(5);
            pk->metLevel(5);
            pk->metDate(Date::today());
            pk->otFriendship(pk->baseFriendship());
            pk->encryptionConstant(u32(rand()) ^ (u32(rand()) << 16));
            pk->PID(pksm::PKX::getRandomPID(pk->species(), pksm::Gender::Male, pk->version(), pk->nature(), 0,
                pk->abilityNumber(), false, pk->TSV(), pk->PID(), pk->generation()));
            pk->gender(pksm::PKX::genderFromRatio(pk->PID(), pk->genderType()));
            pk->move(0, pksm::Move{u16(33)});
            pk->healPP();
            pk->refreshChecksum();
            return pk;
        }

        int boxSlots() const { return sav()->maxSlot() / sav()->maxBoxes(); }

        // The first empty box slot from this box on, as {box, slot}; box -1 when the boxes are full.
        std::pair<int, int> emptySlot(int fromBox) const
        {
            for (int b = fromBox; b < sav()->maxBoxes(); b++)
            {
                for (int slot = 0; slot < boxSlots(); slot++)
                {
                    if (!read(Spot{Side::Save, b, slot, false}))
                    {
                        return {b, slot};
                    }
                }
            }
            return {-1, -1};
        }

        void cloneHovered()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            Spot from = current();
            if (refuseLocked(from))
            {
                return;
            }
            auto pk   = read(from);
            if (!pk)
            {
                return;
            }
            for (int slot = 0; slot < boxSlots(); slot++)
            {
                Spot to{Side::Save, from.box, slot, false};
                if (!read(to))
                {
                    writeSlot(to, pk.get());
                    refresh();
                    app::toast("Cloned. Write the save to keep it.");
                    return;
                }
            }
            app::message("Clone", "This box has no empty slot.");
        }

        // ---- the box manager

        std::string ownerOf(Side sd) const { return sd == Side::Bank ? std::string(locks::BANK) : save->ref.path; }

        bool spotLocked(const Spot& s) const { return !s.party && locks::locked(ownerOf(s.side), s.box); }

        std::string boxLabel(Side sd, int b) const
        {
            std::string name = sd == Side::Bank ? bank.boxName(b) : sav()->boxName(u8(b));
            return name.empty() ? "Box " + std::to_string(b + 1) : name;
        }

        bool hardcoreBlocks()
        {
            if (!hax::hardcore())
            {
                return false;
            }
            app::toast("Hardcore mode: editing is off (START > Hardcore mode). Moving stays on.");
            return true;
        }

        bool refuseLocked(const Spot& s)
        {
            if (!spotLocked(s))
            {
                return false;
            }
            app::toast(boxLabel(s.side, s.box) + " is locked (START > Box manager).");
            return true;
        }

        enum class SortBy
        {
            Dex,
            Level,
            IVs,
            Type,
            Date
        };

        static int ivTotal(const pksm::PKX& pk)
        {
            int t = 0;
            for (int i = 0; i < 6; i++)
            {
                t += pk.iv(inspector::statAt(i));
            }
            return t;
        }

        static bool sortLess(SortBy by, const pksm::PKX& a, const pksm::PKX& b)
        {
            auto dex = [](const pksm::PKX& p) { return std::make_tuple(int(u16(p.species())), int(p.alternativeForm())); };
            switch (by)
            {
                case SortBy::Level:
                    if (a.level() != b.level())
                    {
                        return a.level() > b.level();
                    }
                    break;
                case SortBy::IVs:
                    if (ivTotal(a) != ivTotal(b))
                    {
                        return ivTotal(a) > ivTotal(b);
                    }
                    break;
                case SortBy::Type:
                    if (u8(a.type1()) != u8(b.type1()))
                    {
                        return u8(a.type1()) < u8(b.type1());
                    }
                    if (u8(a.type2()) != u8(b.type2()))
                    {
                        return u8(a.type2()) < u8(b.type2());
                    }
                    break;
                case SortBy::Date:
                {
                    auto da = a.metDate(), db = b.metDate();
                    auto ka = std::make_tuple(da.year(), da.month(), da.day()), kb = std::make_tuple(db.year(), db.month(), db.day());
                    if (ka != kb)
                    {
                        return ka < kb;
                    }
                    break;
                }
                default: break;
            }
            if (dex(a) != dex(b))
            {
                return dex(a) < dex(b);
            }
            return a.level() > b.level();
        }

        // Sorts this box, or every unlocked box of the save as one run (the Pokémon fill the boxes in order).
        void sortBoxes(SortBy by, bool all)
        {
            if (side == Side::Bank)
            {
                sortBankBox(by);
                return;
            }
            std::vector<int> boxes;
            for (int b = all ? 0 : saveBox; b < (all ? sav()->maxBoxes() : saveBox + 1); b++)
            {
                if (!locks::locked(ownerOf(Side::Save), b))
                {
                    boxes.push_back(b);
                }
            }
            if (boxes.empty())
            {
                app::toast(boxLabel(Side::Save, saveBox) + " is locked (START > Box manager).");
                return;
            }
            std::vector<std::unique_ptr<pksm::PKX>> mons;
            for (int b : boxes)
            {
                for (int slot = 0; slot < boxSlots(); slot++)
                {
                    if (auto pk = read(Spot{Side::Save, b, slot, false}))
                    {
                        mons.push_back(std::move(pk));
                    }
                }
            }
            std::stable_sort(mons.begin(), mons.end(), [by](const auto& a, const auto& b) { return sortLess(by, *a, *b); });
            size_t next = 0;
            for (int b : boxes)
            {
                for (int slot = 0; slot < boxSlots(); slot++, next++)
                {
                    writeSlot(Spot{Side::Save, b, slot, false}, next < mons.size() ? mons[next].get() : nullptr);
                }
            }
            refresh();
            app::toast(std::string(all ? "Boxes sorted" : "Box sorted") + ". Write the save to keep it.");
        }

        // A Bank box in order: its entries change places (swaps), nothing is rewritten.
        void sortBankBox(SortBy by)
        {
            if (refuseLocked(Spot{Side::Bank, bankBox, 0, false}))
            {
                return;
            }
            struct Item
            {
                std::string id;
                std::unique_ptr<pksm::PKX> pk;
                int species, level;
            };
            std::vector<Item> items;
            for (int s = 0; s < Bank::SLOTS; s++)
            {
                if (const BankEntry* e = bank.at(bankBox, s))
                {
                    items.push_back({e->id, readable(e->format) ? bank.pkm(*e) : nullptr, e->species, e->level});
                }
            }
            std::stable_sort(items.begin(), items.end(), [by](const Item& a, const Item& b)
                {
                    if (a.pk && b.pk)
                    {
                        return sortLess(by, *a.pk, *b.pk);
                    }
                    if (bool(a.pk) != bool(b.pk))
                    {
                        return bool(a.pk); // formats the port cannot read go last
                    }
                    return std::make_tuple(a.species, -a.level) < std::make_tuple(b.species, -b.level);
                });
            std::string error;
            for (int i = 0; i < int(items.size()); i++)
            {
                for (int s = 0; s < Bank::SLOTS; s++)
                {
                    const BankEntry* e = bank.at(bankBox, s);
                    if (e && e->id == items[i].id)
                    {
                        if (s != i)
                        {
                            bank.move(bankBox, s, bankBox, i, error);
                        }
                        break;
                    }
                }
            }
            refresh();
            app::toast("Bank box sorted.");
        }

        void sortMenu(bool all)
        {
            std::vector<app::Option> o;
            const std::pair<const char*, SortBy> ways[] = {{"Pokédex number", SortBy::Dex}, {"Level (highest first)", SortBy::Level},
                {"IVs (best first)", SortBy::IVs}, {"Type", SortBy::Type}, {"Date met (oldest first)", SortBy::Date}};
            for (auto& [label, by] : ways)
            {
                SortBy b = by;
                o.push_back({label, [this, b, all]() { sortBoxes(b, all); }});
            }
            o.push_back({"Close", []() {}});
            app::menu(all ? "Sort every unlocked box by" : "Sort this box by", std::move(o));
        }

        // Another box of this side, picked from a list (locked ones shown but refused).
        void pickBox(const std::string& title, std::function<void(int)> chosen)
        {
            std::vector<screens::PickItem> items;
            for (int b = 0; b < boxCount(); b++)
            {
                if (b == box())
                {
                    continue;
                }
                int used = 0;
                for (int s = 0; s < int(cells.size()); s++)
                {
                    used += bool(side == Side::Bank ? bank.at(b, s) != nullptr : bool(read(Spot{side, b, s, false})));
                }
                bool lockedBox = locks::locked(ownerOf(side), b);
                items.push_back({b, boxLabel(side, b) + (lockedBox ? "  (locked)" : ""),
                    std::to_string(used) + " / " + std::to_string(cells.size())});
            }
            app::push(screens::picker(title, std::move(items), -1, [this, chosen](int b)
                {
                    if (!refuseLocked(Spot{side, b, 0, false}))
                    {
                        chosen(b);
                    }
                }));
        }

        // Copies of this box's Pokémon into another box: the same slot when free, else the first free one.
        void copyBoxTo(int target)
        {
            if (hardcoreBlocks())
            {
                return;
            }
            int copied = 0, left = 0;
            for (int s = 0; s < int(cells.size()); s++)
            {
                auto pk = side == Side::Bank ? (bank.at(bankBox, s) && readable(bank.at(bankBox, s)->format)
                                                       ? bank.pkm(*bank.at(bankBox, s)) : nullptr)
                                             : read(Spot{Side::Save, saveBox, s, false});
                if (!pk)
                {
                    continue;
                }
                int slot = -1;
                auto freeAt = [&](int i) { return side == Side::Bank ? !bank.at(target, i) : !read(Spot{Side::Save, target, i, false}); };
                if (freeAt(s))
                {
                    slot = s;
                }
                for (int i = 0; i < int(cells.size()) && slot < 0; i++)
                {
                    if (freeAt(i))
                    {
                        slot = i;
                    }
                }
                if (slot < 0)
                {
                    left++;
                    continue;
                }
                std::string error;
                if (side == Side::Bank)
                {
                    copied += bank.put(*pk, target, slot, "Box copy", error);
                }
                else
                {
                    writeSlot(Spot{Side::Save, target, slot, false}, pk.get());
                    copied++;
                }
            }
            refresh();
            app::message("Copy box", std::to_string(copied) + " copied to " + boxLabel(side, target) +
                                         (left ? ", " + std::to_string(left) + " did not fit" : "") +
                                         (side == Side::Save ? ". Write the save to keep them." : "."));
        }

        // This box and another change places, names included.
        void swapBoxWith(int target)
        {
            std::string error;
            if (side == Side::Bank)
            {
                for (int s = 0; s < Bank::SLOTS; s++)
                {
                    bank.move(bankBox, s, target, s, error);
                }
                std::string a = bank.boxName(bankBox), b = bank.boxName(target);
                bank.boxName(bankBox, b);
                bank.boxName(target, a);
            }
            else
            {
                for (int s = 0; s < boxSlots(); s++)
                {
                    auto a = read(Spot{Side::Save, saveBox, s, false});
                    auto b = read(Spot{Side::Save, target, s, false});
                    writeSlot(Spot{Side::Save, saveBox, s, false}, b.get());
                    writeSlot(Spot{Side::Save, target, s, false}, a.get());
                }
                std::string a = sav()->boxName(u8(saveBox)), b = sav()->boxName(u8(target));
                sav()->boxName(u8(saveBox), b);
                sav()->boxName(u8(target), a);
            }
            refresh();
            app::toast("Boxes swapped" + std::string(side == Side::Save ? ". Write the save to keep it." : "."));
        }

        // Every Pokémon of the save through PKForge's offline rules: the list of what they would change.
        void auditSave()
        {
            struct Finding
            {
                Spot at;
                std::string line;
            };
            auto findings = std::make_shared<std::vector<Finding>>();
            int seen = 0;
            auto check = [&](const Spot& s)
            {
                auto pk = read(s);
                if (!pk)
                {
                    return;
                }
                seen++;
                auto changes = legality::fix(*pk, *sav());
                if (!changes.empty())
                {
                    findings->push_back({s, pkdata::species(u16(pk->species())) + "  -  " + changes.front()});
                }
            };
            for (int s = 0; s < partyCount(); s++)
            {
                check(Spot{Side::Save, 0, s, true});
            }
            for (int b = 0; b < sav()->maxBoxes(); b++)
            {
                for (int s = 0; s < boxSlots(); s++)
                {
                    check(Spot{Side::Save, b, s, false});
                }
            }
            if (findings->empty())
            {
                app::message("Audit", "All " + std::to_string(seen) + " Pokémon are within the rules PKForge can check "
                                      "offline. For PKHeX's full check: X > Legalize with PKHeX.");
                return;
            }
            std::vector<screens::PickItem> items;
            items.push_back({-1, "Fix them all (" + std::to_string(findings->size()) + ")",
                "Skips locked boxes. Write the save after."});
            for (int i = 0; i < int(findings->size()); i++)
            {
                const Finding& f = (*findings)[i];
                char where[32];
                if (f.at.party)
                {
                    snprintf(where, sizeof(where), "Party %d", f.at.slot + 1);
                }
                else
                {
                    snprintf(where, sizeof(where), "Box %d slot %d", f.at.box + 1, f.at.slot + 1);
                }
                items.push_back({i, f.line, where});
            }
            char title[48];
            snprintf(title, sizeof(title), "Audit: %d of %d to fix", int(findings->size()), seen);
            app::push(screens::picker(title, std::move(items), -1, [this, findings](int i)
                {
                    if (i < 0)
                    {
                        if (hardcoreBlocks())
                        {
                            return;
                        }
                        int fixed = 0;
                        for (const Finding& f : *findings)
                        {
                            if (spotLocked(f.at))
                            {
                                continue;
                            }
                            auto pk = read(f.at);
                            if (pk && !legality::fix(*pk, *sav()).empty())
                            {
                                writeSlot(f.at, pk.get());
                                fixed++;
                            }
                        }
                        refresh();
                        app::message("Audit", std::to_string(fixed) + " Pokémon fixed. Write the save to keep it.");
                        return;
                    }
                    // Show the chosen one in its box.
                    const Finding& f = (*findings)[i];
                    party = f.at.party;
                    if (!party)
                    {
                        saveBox = f.at.box;
                    }
                    cursor = f.at.slot;
                    refresh();
                }));
        }

        // ---- wallpapers: the games' own pictures behind each box, by number

        int wallpaperCount() const
        {
            int gen = pkdata::genNumber(sav()->generation());
            return gen == 3 ? 16 : gen >= 4 && gen <= 7 && sav()->generation() != pksm::Generation::LGPE ? 24 : 0;
        }

        // The original's Bank wallpapers: the storage palette's dark tinted worlds (PKForge.Chrome Pksm.BoxWallpapers).
        static constexpr u32 bankPalette[8] = {gfx::rgb(0x1B, 0x31, 0x46), gfx::rgb(0x1B, 0x2E, 0x58), gfx::rgb(0x3B, 0x32, 0x38),
            gfx::rgb(0x42, 0x28, 0x3C), gfx::rgb(0x31, 0x29, 0x55), gfx::rgb(0x18, 0x3A, 0x4A), gfx::rgb(0x40, 0x28, 0x4D),
            gfx::rgb(0x2A, 0x3B, 0x38)};

        void pickBankWallpaper()
        {
            static const char* names[] = {"Jade navy", "Cobalt navy", "Amber dusk", "Coral dusk", "Violet navy", "Aqua navy",
                "Rose navy", "Leaf navy"};
            std::string now = bank.wallpaper(box());
            int current     = now.compare(0, 6, "color:") == 0 ? atoi(now.c_str() + 6) : -1;
            std::vector<screens::PickItem> items{{-1, "Default", ""}};
            for (int i = 0; i < 8; i++)
            {
                items.push_back({i, names[i], ""});
            }
            app::push(screens::picker("Wallpaper of " + boxLabel(Side::Bank, box()), std::move(items), current, [this](int i)
                {
                    bank.wallpaper(box(), i < 0 ? "" : "color:" + std::to_string(i));
                }));
        }

        void pickWallpaper()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            int current = sav()->boxWallpaper(u8(saveBox));
            std::vector<screens::PickItem> items;
            for (int w = 0; w < wallpaperCount(); w++)
            {
                items.push_back({w, "Wallpaper " + std::to_string(w + 1), w == current ? "now" : (w >= 16 ? "unlocked later in the game" : "")});
            }
            app::push(screens::picker("Wallpaper of " + boxLabel(Side::Save, saveBox), std::move(items), current, [this](int w)
                {
                    sav()->boxWallpaper(u8(saveBox), u8(w));
                    save->dirty = true;
                    app::toast("Wallpaper " + std::to_string(w + 1) + ". Write the save to keep it.");
                }));
        }

        // ---- the Nuzlocke report, as the original's: the first Pokémon met at each place, places that gave
        // more than one, and species held more than once (eggs and hatched Pokémon apart).

        void nuzlocke()
        {
            struct Met
            {
                Spot at;
                int species;
                std::string place;
                u32 when; // yyyymmdd from Generation 4 on, else 0
                bool hatched;
            };
            std::vector<Met> all;
            auto look = [&](const Spot& s)
            {
                auto pk = read(s);
                if (!pk || pk->egg())
                {
                    return;
                }
                bool hatched = pk->generation() >= pksm::Generation::FOUR && pk->eggLocation() != 0;
                std::string place = pkdata::location(pk->generation(), pk->metLocation());
                u32 when = 0;
                if (pk->generation() >= pksm::Generation::FOUR)
                {
                    auto d = pk->metDate();
                    when   = d.year() * 10000 + d.month() * 100 + d.day();
                }
                all.push_back({s, int(u16(pk->species())), place.empty() ? "Unknown place" : place, when, hatched});
            };
            for (int s = 0; s < partyCount(); s++)
            {
                look(Spot{Side::Save, 0, s, true});
            }
            for (int b = 0; b < sav()->maxBoxes(); b++)
            {
                for (int s = 0; s < boxSlots(); s++)
                {
                    look(Spot{Side::Save, b, s, false});
                }
            }
            if (pkdata::genNumber(sav()->generation()) < 3)
            {
                app::message("Nuzlocke report", "Generations 1 and 2 keep no place met (Crystal only for some), so there is "
                                                "nothing to report per place.");
                return;
            }
            // Per place: the first met (earliest date, else the party then box order).
            std::vector<std::string> places;
            std::map<std::string, std::vector<const Met*>> byPlace;
            std::map<int, int> perSpecies;
            int hatched = 0;
            for (const Met& m : all)
            {
                if (m.hatched)
                {
                    hatched++;
                    continue;
                }
                if (!byPlace.count(m.place))
                {
                    places.push_back(m.place);
                }
                byPlace[m.place].push_back(&m);
                perSpecies[m.species]++;
            }
            auto items = std::make_shared<std::vector<Spot>>();
            std::vector<screens::PickItem> list;
            int reused = 0;
            for (const std::string& place : places)
            {
                auto& v = byPlace[place];
                const Met* first = v.front();
                for (const Met* m : v)
                {
                    if (m->when && first->when && m->when < first->when)
                    {
                        first = m;
                    }
                }
                reused += v.size() > 1;
                std::string detail = v.size() > 1 ? "+" + std::to_string(v.size() - 1) + " more (encounter used again)" : "first encounter";
                items->push_back(first->at);
                list.push_back({int(items->size()) - 1, place + ":  " + pkdata::species(first->species), detail});
            }
            for (auto& [species, count] : perSpecies)
            {
                if (count > 1)
                {
                    items->push_back(Spot{Side::Save, 0, 0, false});
                    list.push_back({-1 - species, "Duplicate:  " + pkdata::species(species) + "  x" + std::to_string(count),
                        "the same species more than once"});
                }
            }
            char title[64];
            snprintf(title, sizeof(title), "Nuzlocke: %d places, %d reused, %d hatched", int(places.size()), reused, hatched);
            app::push(screens::picker(title, std::move(list), -1, [this, items](int i)
                {
                    if (i < 0 || i >= int(items->size()))
                    {
                        return;
                    }
                    const Spot& s = (*items)[i];
                    party = s.party;
                    if (!party)
                    {
                        saveBox = s.box;
                    }
                    cursor = s.slot;
                    refresh();
                }));
        }

        void boxManager()
        {
            if (party)
            {
                app::toast("The box manager works on boxes: leave the party first.");
                return;
            }
            bool lockedBox = locks::locked(ownerOf(side), box());
            std::vector<app::Option> o;
            o.push_back({"Sort this box", [this]() { sortMenu(false); }, "By Pokédex number, level, IVs, type or date met."});
            if (side == Side::Save)
            {
                o.push_back({"Sort every box", [this]() { sortMenu(true); }, "All unlocked boxes as one run, in order."});
            }
            o.push_back({lockedBox ? "Unlock this box" : "Lock this box", [this, lockedBox]()
                {
                    locks::set(ownerOf(side), box(), !lockedBox);
                    app::toast(boxLabel(side, box()) + (lockedBox ? " unlocked." : " locked: nothing goes in or out."));
                },
                "A locked box keeps its Pokémon: no grabbing, dropping, sorting or releasing there."});
            o.push_back({"Copy this box to...", [this]() { pickBox("Copy to", [this](int b) { copyBoxTo(b); }); },
                "Copies of every Pokémon of this box in another box."});
            o.push_back({"Swap with box...", [this]() { pickBox("Swap with", [this](int b) { swapBoxWith(b); }); },
                "The two boxes change places, names included.", !lockedBox});
            o.push_back({"Rename this box", [this]() { renameBox(); }});
            if (side == Side::Save && wallpaperCount() > 0)
            {
                o.push_back({"Wallpaper...", [this]() { pickWallpaper(); }, "The picture behind this box in the game."});
            }
            if (side == Side::Bank)
            {
                o.push_back({"Wallpaper...", [this]() { pickBankWallpaper(); }, "The colour behind this Bank box."});
            }
            if (side == Side::Save)
            {
                o.push_back({"Nuzlocke report", [this]() { nuzlocke(); },
                    "First encounter per place, the places used twice, and the duplicate species."});
            }
            if (side == Side::Save)
            {
                o.push_back({"Audit the whole save", [this]() { auditSave(); },
                    "Every Pokémon through PKForge's offline legality rules, with a fix for all."});
            }
            o.push_back({"Close", []() {}});
            app::menu("Box manager  -  " + boxLabel(side, box()) + (lockedBox ? " (locked)" : ""), std::move(o));
        }

        void sortBox()
        {
            std::vector<std::unique_ptr<pksm::PKX>> mons;
            for (int slot = 0; slot < boxSlots(); slot++)
            {
                if (auto pk = read(Spot{Side::Save, saveBox, slot, false}))
                {
                    mons.push_back(std::move(pk));
                }
            }
            std::stable_sort(mons.begin(), mons.end(), [](const auto& a, const auto& b)
                {
                    if (a->species() != b->species())
                    {
                        return u16(a->species()) < u16(b->species());
                    }
                    if (a->alternativeForm() != b->alternativeForm())
                    {
                        return a->alternativeForm() < b->alternativeForm();
                    }
                    return a->level() > b->level();
                });
            for (int slot = 0; slot < boxSlots(); slot++)
            {
                writeSlot(Spot{Side::Save, saveBox, slot, false}, slot < int(mons.size()) ? mons[slot].get() : nullptr);
            }
            refresh();
            app::toast("Box sorted. Write the save to keep it.");
        }

        void exportShowdown(bool wholeBox)
        {
            std::string text, name;
            if (wholeBox)
            {
                for (int slot = 0; slot < boxSlots(); slot++)
                {
                    if (auto pk = read(Spot{Side::Save, saveBox, slot, false}))
                    {
                        text += showdown::write(*pk) + "\n";
                    }
                }
                name = boxName();
            }
            else if (auto pk = read(current()))
            {
                text = showdown::write(*pk);
                name = i18n::species(pksm::Language::ENG, pk->species());
            }
            if (text.empty())
            {
                app::message("Showdown", "Nothing to export here.");
                return;
            }
            std::string folder = std::string(files::ROOT) + "/showdown";
            files::mkdirs(folder);
            std::string path = folder + "/" + files::sanitize(name) + "_" + files::stamp() + ".txt";
            if (!files::writeText(path, text))
            {
                app::message("Showdown", "Could not write " + path);
                return;
            }
            app::message("Showdown", "Written to " + path);
        }

        void importShowdown()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            std::string folder = std::string(files::ROOT) + "/showdown";
            files::mkdirs(folder);
            std::string path = folder + "/import.txt", text;
            if (!files::readText(path, text))
            {
                app::message("Showdown", "Put the sets in " + path + " (copied from PKHeX, PKSM or Showdown), then try again.");
                return;
            }
            int placed = 0, skipped = 0;
            std::string note;
            for (const std::string& set : showdown::split(text))
            {
                auto pk = freshPk();
                std::vector<std::string> notes;
                bool ok = showdown::read(set, *pk, *sav(), notes);
                if (ok)
                {
                    legality::fix(*pk, *sav());
                    ok = sav()->invalidTransferReason(*pk) == pksm::Sav::BadTransferReason::OKAY;
                }
                auto [box, slot] = emptySlot(saveBox);
                if (ok && box < 0)
                {
                    notes.push_back("the boxes are full");
                    ok = false;
                }
                if (ok)
                {
                    writeSlot(Spot{Side::Save, box, slot, false}, pk.get());
                    placed++;
                }
                else
                {
                    skipped++;
                }
                if (note.empty() && !notes.empty())
                {
                    note = notes.front();
                }
            }
            refresh();
            app::message("Showdown", std::to_string(placed) + " Pokémon added" +
                                         (skipped ? ", " + std::to_string(skipped) + " skipped" : std::string()) +
                                         (note.empty() ? "" : " (" + note + ")") +
                                         (placed ? ". Write the save to keep them." : "."));
        }

        // Undo: every change since the save was opened or last written goes, read again from the file (or the
        // game), and the Bank moves not written yet go back as when leaving without writing.
        void undoChanges()
        {
            app::confirm("Undo every change since the save was opened or last written?", [this]()
                {
                    std::string error;
                    auto fresh = OpenSave::open(save->ref, error);
                    if (!fresh)
                    {
                        app::message("Undo changes", error.empty() ? "The save could not be read again." : error);
                        return;
                    }
                    undoDeposits();
                    *save = std::move(*fresh);
                    save->dirty = false;
                    refresh();
                    app::toast("Changes undone.");
                },
                "Undo", "Keep them");
        }

        // Restore points: the save as it was before an earlier write. A restore keeps a restore point of the
        // save as it is now first, then PKForge goes back Home so the restored save is read afresh.
        void restoreMenu()
        {
            std::vector<std::string> names = backups::list(save->ref);
            if (names.empty())
            {
                app::message("Restore points", "No restore points yet: PKForge makes one before every write.");
                return;
            }
            std::vector<screens::PickItem> items;
            for (size_t i = 0; i < names.size(); i++)
            {
                std::string n = names[i].substr(0, names[i].size() - 4); // 2026-11-11_08-20-53
                if (n.size() == 19)
                {
                    n = n.substr(0, 10) + "  " + n.substr(11, 2) + ":" + n.substr(14, 2) + ":" + n.substr(17, 2);
                }
                items.push_back({int(i), n, i == 0 ? "The newest" : ""});
            }
            app::push(screens::picker("Restore points", std::move(items), -1, [this, names](int i)
                {
                    std::string text = "Put back the save from " + names[i].substr(0, 10) + "? What it holds now is kept as a new restore point first.";
                    if (save->dirty)
                    {
                        text += " Changes not written yet are lost.";
                    }
                    app::confirm(text, [this, name = names[i]]()
                        {
                            std::string error;
                            if (!backups::restore(save->ref, name, error))
                            {
                                app::message("Restore points", error);
                                return;
                            }
                            app::pop(); // back Home: the restored save is read again from there
                            app::message("Restore points", "Restored. Open the save again to see it.");
                        },
                        "Restore");
                }));
        }

        // Catch in the wild, as the Android PKForge's encounter gallery: a species of this game's wild tables,
        // then one of its encounters (place, kind, levels); the Pokémon is made by the game's own method.
        void catchWild()
        {
            if (hardcoreBlocks() || refuseLocked(current()))
            {
                return;
            }
            const auto& all     = encounters::of(*sav());
            const auto& statics = encounters::statics(*sav());
            if (all.empty() && statics.empty())
            {
                app::message("Catch in the wild", "PKForge has no encounter table for this game.");
                return;
            }
            std::vector<u16> species;
            for (const auto& e : all)
            {
                species.push_back(e.species);
            }
            for (const auto& e : statics)
            {
                species.push_back(e.species);
            }
            for (const auto& e : encounters::trades(*sav()))
            {
                species.push_back(e.species);
            }
            std::sort(species.begin(), species.end());
            species.erase(std::unique(species.begin(), species.end()), species.end());
            std::vector<screens::PickItem> items;
            for (u16 sp : species)
            {
                items.push_back({sp, i18n::species(sav()->language(), pksm::Species{sp}), ""});
            }
            Spot s = current();
            app::push(screens::picker("Catch in the wild", std::move(items), 1, [this, s](int species)
                {
                    const auto& all = encounters::of(*sav());
                    std::vector<screens::PickItem> spots;
                    for (size_t i = 0; i < all.size(); i++)
                    {
                        const encounters::Slot& e = all[i];
                        if (e.species != species)
                        {
                            continue;
                        }
                        std::string levels = e.max > e.min ? "Lv. " + std::to_string(e.min) + "-" + std::to_string(e.max)
                                                           : "Lv. " + std::to_string(e.min);
                        bool ok = encounters::catchable(*sav(), e);
                        spots.push_back({int(i), encounters::place(*sav(), e) + "  -  " + encounters::kind(*sav(), e) + "  -  " + levels,
                            ok ? "A catches one here." : "Shown only: PKForge can't make this kind legally yet."});
                    }
                    // Static and gift encounters first: legendaries, starters, fossils, gifts.
                    const auto& statics = encounters::statics(*sav());
                    std::vector<screens::PickItem> fixed;
                    for (size_t i = 0; i < statics.size(); i++)
                    {
                        const encounters::Static& e = statics[i];
                        if (e.species == species)
                        {
                            fixed.push_back({int(100000 + i), encounters::place(*sav(), e) + "  -  " +
                                                                  (e.gift ? "Gift" : "Static") + "  -  Lv. " +
                                                                  std::to_string(e.level), "A gets this one."});
                        }
                    }
                    const auto& trades = encounters::trades(*sav());
                    for (size_t i = 0; i < trades.size(); i++)
                    {
                        if (trades[i].species == species)
                        {
                            fixed.push_back({int(200000 + i), "In-game trade from " + encounters::tradeTrainer(*sav(), trades[i]) +
                                                                  "  -  Lv. " + std::to_string(trades[i].level), "A gets this one."});
                        }
                    }
                    spots.insert(spots.begin(), fixed.begin(), fixed.end());
                    std::vector<std::string> other = encounters::elsewhere(*sav(), u16(species));
                    if (!other.empty())
                    {
                        std::string list;
                        for (auto& g : other)
                        {
                            list += (list.empty() ? "" : ", ") + g;
                        }
                        spots.push_back({-1, "Also wild in: " + list, ""});
                    }
                    std::string title = i18n::species(sav()->language(), pksm::Species{u16(species)}) + ": where";
                    app::push(screens::picker(title, std::move(spots), -1, [this, s, species](int index)
                        {
                            if (index < 0)
                            {
                                std::string list;
                                for (auto& g : encounters::elsewhere(*sav(), u16(species)))
                                {
                                    list += (list.empty() ? "" : ", ") + g;
                                }
                                app::message("Also wild in", list + ".\nCatch it there and move it here through the Bank.");
                                return;
                            }
                            if (index >= 200000)
                            {
                                const encounters::Trade& tr = encounters::trades(*sav())[index - 200000];
                                auto pk = freshPk();
                                encounters::make(*pk, *sav(), tr);
                                writeSlot(s, pk.get());
                                refresh();
                                app::toast(pk->nickname() + " received in a trade from " + encounters::tradeTrainer(*sav(), tr) +
                                           ". Write the save to keep it.");
                                return;
                            }
                            if (index >= 100000)
                            {
                                const encounters::Static& st = encounters::statics(*sav())[index - 100000];
                                auto pk = freshPk();
                                encounters::make(*pk, *sav(), st);
                                writeSlot(s, pk.get());
                                refresh();
                                app::toast(i18n::species(sav()->language(), pk->species()) + (st.gift ? " received at " : " caught at ") +
                                           encounters::place(*sav(), st) + " (Lv. " + std::to_string(pk->level()) +
                                           "). Write the save to keep it.");
                                return;
                            }
                            const encounters::Slot& e = encounters::of(*sav())[index];
                            if (!encounters::catchable(*sav(), e))
                            {
                                std::string why = encounters::notCatchable(*sav(), e);
                                app::message("Catch in the wild", why.empty() ? "This kind of encounter is not modelled yet; "
                                                                                "pick another place." : why);
                                return;
                            }
                            auto pk = freshPk();
                            encounters::make(*pk, *sav(), e);
                            writeSlot(s, pk.get());
                            refresh();
                            app::toast(i18n::species(sav()->language(), pk->species()) + " caught at " +
                                       encounters::place(*sav(), e) + " (Lv. " + std::to_string(pk->level()) +
                                       "). Write the save to keep it.");
                        }));
                },
                screens::PickPreview::Species));
        }

        void newEgg()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            if (refuseLocked(current()))
            {
                return;
            }
            pksm::Generation gen = sav()->generation();
            u8 version           = u8(sav()->version());
            // No breeding in the first two generations PKSM-Core edits, Let's Go, Legends: Arceus or Z-A.
            if (gen < pksm::Generation::THREE || gen == pksm::Generation::LGPE || version == 47 || version == 52)
            {
                app::message("New egg", "This game has no eggs.");
                return;
            }
            std::vector<screens::PickItem> items;
            for (const pksm::Species& sp : sav()->availableSpecies())
            {
                items.push_back({u16(sp), i18n::species(sav()->language(), sp), ""});
            }
            Spot s = current();
            app::push(screens::picker("New egg", std::move(items), 1, [this, s](int species)
                {
                    auto egg = makeEgg(u16(species));
                    writeSlot(s, egg.get());
                    refresh();
                    app::toast("Egg placed. Write the save to keep it.");
                },
                screens::PickPreview::Species));
        }

        // ---- the egg factory, as the original's: one egg of every species this game can hatch

        bool eggMaxIv = false, eggShiny = false;

        bool hasEggs() const
        {
            pksm::Generation gen = sav()->generation();
            u8 version           = u8(sav()->version());
            return !(gen < pksm::Generation::THREE || gen == pksm::Generation::LGPE || version == 47 || version == 52);
        }

        void eggFactory()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            if (!hasEggs())
            {
                app::message("Egg factory", "This game has no eggs.");
                return;
            }
            std::vector<app::Option> o;
            o.push_back({"One egg of every species", [this]() { everyEgg(); },
                "Into the empty slots from this box on (locked boxes are skipped)."});
            o.push_back({eggMaxIv ? "Perfect IVs: on" : "Perfect IVs: off", [this]()
                {
                    eggMaxIv = !eggMaxIv;
                    eggFactory();
                },
                "Every IV at its maximum."});
            o.push_back({eggShiny ? "Shiny: on" : "Shiny: off", [this]()
                {
                    eggShiny = !eggShiny;
                    eggFactory();
                },
                "Shiny when they hatch."});
            o.push_back({"One egg of a species...", [this]() { newEgg(); }, "Into the slot under the cursor (must be empty)."});
            o.push_back({"Close", []() {}});
            app::menu("Egg factory", std::move(o));
        }

        void everyEgg()
        {
            std::vector<u16> species;
            std::string text;
            files::readText("romfs:/data/eggspecies.txt", text);
            auto available = sav()->availableSpecies();
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find('\n', at);
                u16 n = u16(atoi(text.c_str() + at));
                at = end == std::string::npos ? text.size() : end + 1;
                if (n && std::find(available.begin(), available.end(), pksm::Species{n}) != available.end())
                {
                    species.push_back(n);
                }
            }
            if (species.empty())
            {
                app::message("Egg factory", "No species of this game hatch from eggs.");
                return;
            }
            int free = 0;
            for (int b = saveBox; b < sav()->maxBoxes(); b++)
            {
                if (!locks::locked(ownerOf(Side::Save), b))
                {
                    for (int s = 0; s < boxSlots(); s++)
                    {
                        free += !read(Spot{Side::Save, b, s, false});
                    }
                }
            }
            char ask[200];
            snprintf(ask, sizeof(ask), "%d eggs for %s%s%s. %d empty slots from this box on%s.", int(species.size()),
                pkdata::game(save->ref.version).c_str(), eggMaxIv ? ", perfect IVs" : "", eggShiny ? ", shiny" : "", free,
                free < int(species.size()) ? ": the rest will not fit" : "");
            app::confirm(ask, [this, species]()
                {
                    size_t next = 0;
                    for (int b = saveBox; b < sav()->maxBoxes() && next < species.size(); b++)
                    {
                        if (locks::locked(ownerOf(Side::Save), b))
                        {
                            continue;
                        }
                        for (int s = 0; s < boxSlots() && next < species.size(); s++)
                        {
                            Spot at{Side::Save, b, s, false};
                            if (!read(at))
                            {
                                auto egg = makeEgg(species[next++], eggMaxIv, eggShiny);
                                writeSlot(at, egg.get());
                            }
                        }
                    }
                    refresh();
                    app::message("Egg factory", std::to_string(next) + " eggs placed" +
                                                    (next < species.size() ? " (the boxes are full)" : "") +
                                                    ". Write the save to keep them.");
                },
                "Make them", "Cancel");
        }

        // An unhatched egg as PKHeX builds one: Day Care egg location (2000 in Gen 4, the Day Care /
        // Nursery 60002 in Gen 5 to 8, a picnic 30023 in Gen 9), level 1 (5 in Gen 3),
        // its level-up moves, egg cycles left in the friendship byte, "Egg" in the save's language.
        std::unique_ptr<pksm::PKX> makeEgg(u16 species, bool maxIv = false, bool shiny = false)
        {
            auto pk              = freshPk();
            pksm::Generation gen = pk->generation();
            bool gen3            = gen == pksm::Generation::THREE;
            pk->species(pksm::Species{species});
            pk->alternativeForm(0);
            pk->nature(pksm::Nature{u8(rand() % 25)});
            pk->setAbility(gen >= pksm::Generation::SIX ? u8(1 << (rand() % 2)) : u8(rand() % 2));
            pk->PID(pksm::PKX::getRandomPID(pk->species(), pksm::Gender::Male, pk->version(), pk->nature(), 0,
                pk->abilityNumber(), shiny, pk->TSV(), pk->PID(), gen));
            pk->gender(pksm::PKX::genderFromRatio(pk->PID(), pk->genderType()));
            for (pksm::Stat st : {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD, pksm::Stat::SPATK,
                     pksm::Stat::SPDEF})
            {
                pk->iv(st, maxIv ? pk->maxIV() : u8(rand() % (pk->maxIV() + 1)));
                pk->ev(st, 0);
            }
            pk->level(gen3 ? 5 : 1);
            pk->metLevel(gen3 ? 0 : 1);
            pk->ball(pksm::Ball{u8(4)});
            pk->heldItem(u16(0));
            std::vector<u16> moves = learnsets::levelUpMoves(species, u8(sav()->version()), pk->level());
            for (u8 i = 0; i < 4; i++)
            {
                u16 m = moves.size() > i ? moves[moves.size() - 1 - i] : 0;
                pk->move(i, pksm::Move{m});
                pk->PPUp(i, 0);
            }
            pk->fixMoves();
            pk->healPP();
            // "Egg" in the save's own language: a Gen 3 egg's Japanese flag would garble its trainer's name
            // here, and the game sets the language when it hatches anyway.
            static const std::pair<pksm::Language, const char*> names[] = {{pksm::Language::JPN, "タマゴ"},
                {pksm::Language::ENG, "Egg"}, {pksm::Language::FRE, "Œuf"}, {pksm::Language::ITA, "Uovo"},
                {pksm::Language::GER, "Ei"}, {pksm::Language::SPA, "Huevo"}, {pksm::Language::KOR, "알"},
                {pksm::Language::CHS, "蛋"}, {pksm::Language::CHT, "蛋"}};
            const char* name = "Egg";
            for (auto& [lang, n] : names)
            {
                if (lang == pk->language())
                {
                    name = n;
                }
            }
            pk->nickname(name);
            if (!gen3)
            {
                pk->nicknamed(true);
                u16 eggPlace = gen == pksm::Generation::FOUR ? 2000 : gen == pksm::Generation::NINE ? 30023 : 60002;
                pk->eggLocation(eggPlace);
                pk->metLocation(0);
                pk->eggDate(Date::today());
            }
            pk->egg(true);
            u8 cycles = learnsets::hatchCycles(species);
            pk->otFriendship(cycles ? cycles : 20);
            pk->refreshChecksum();
            return pk;
        }

        // The Bank's Search: every Pokémon it holds by name; the chosen one's box opens with it pointed.
        // The save's Search, as the Bank's: every Pokémon of the party and the boxes by name, species and
        // nickname; the chosen one's box opens with the cursor on it.
        void searchSave()
        {
            std::vector<screens::PickItem> items;
            auto add = [&](const Spot& s, int id)
            {
                auto pk = read(s);
                if (!pk)
                {
                    return;
                }
                std::string name = pk->egg() ? "Egg" : pkdata::species(u16(pk->species()));
                if (!pk->egg() && files::lower(pk->nickname()) != files::lower(name))
                {
                    name += "  (" + pk->nickname() + ")";
                }
                if (pk->shiny())
                {
                    name += "  *";
                }
                char where[48];
                if (s.party)
                {
                    snprintf(where, sizeof(where), "Lv.%d  -  Party %d", pk->level(), s.slot + 1);
                }
                else
                {
                    snprintf(where, sizeof(where), "Lv.%d  -  %s slot %02d", pk->level(), boxLabel(Side::Save, s.box).c_str(), s.slot + 1);
                }
                items.push_back({id, name, where});
            };
            for (int s = 0; s < partyCount(); s++)
            {
                add(Spot{Side::Save, 0, s, true}, 100000 + s);
            }
            for (int b = 0; b < sav()->maxBoxes(); b++)
            {
                for (int s = 0; s < boxSlots(); s++)
                {
                    add(Spot{Side::Save, b, s, false}, b * 1000 + s);
                }
            }
            if (items.empty())
            {
                app::message("Search", "This save has no Pokémon yet.");
                return;
            }
            char title[48];
            snprintf(title, sizeof(title), "Search the save (%d)", int(items.size()));
            app::push(screens::picker(title, std::move(items), -1, [this](int id)
                {
                    if (side != Side::Save)
                    {
                        switchSide();
                    }
                    party = id >= 100000;
                    if (!party)
                    {
                        saveBox = id / 1000;
                    }
                    cursor = party ? id - 100000 : id % 1000;
                    refresh();
                }));
        }

        void searchBank()
        {
            std::vector<screens::PickItem> items;
            for (int b = 0; b < bank.boxes(); b++)
            {
                for (int s = 0; s < Bank::SLOTS; s++)
                {
                    const BankEntry* e = bank.at(b, s);
                    if (!e || e->species == 0)
                    {
                        continue;
                    }
                    char detail[48];
                    snprintf(detail, sizeof(detail), "Lv.%d  -  Bank %02d slot %02d", e->level, b + 1, s + 1);
                    std::string name = pkdata::species(e->species);
                    if (!e->nickname.empty() && files::lower(e->nickname) != files::lower(name))
                    {
                        name += "  (" + e->nickname + ")";
                    }
                    items.push_back({b * Bank::SLOTS + s, name, detail});
                }
            }
            if (items.empty())
            {
                app::message("Search", "The Bank is empty.");
                return;
            }
            app::push(screens::picker("Search the Bank", std::move(items), -1, [this](int at)
                {
                    bankBox = at / Bank::SLOTS;
                    cursor  = at % Bank::SLOTS;
                    refresh();
                }));
        }

        void eventsMenu()
        {
            std::vector<app::Option> o;
            o.push_back({"Mystery Gift", [this]() { mysteryGiftMenu(); },
                "Every real distribution for this game, sent to its gift inbox."});
            if (save && world::hasAny(*sav()))
            {
                o.push_back({"World & events", [this]() { if (!hardcoreBlocks()) world::open(save); },
                    "This game's world editors: clock, roamers, key item events and more."});
            }
            o.push_back({"Close", []() {}});
            app::menu("Events  -  " + pkdata::game(save->ref.version), std::move(o));
        }

        // The original's Trainer records (PKHeX RecordBlock6, X/Y to Ultra Sun/Ultra Moon): 100 u32 then
        // 100 u16 counters, read only.
        int recordsOffset()
        {
            switch (u8(sav()->version()))
            {
                case 24: case 25: return 0x1E400;
                case 26: case 27: return 0x1F400;
                case 30: case 31: return 0x69C00;
                case 32: case 33: return 0x6A200;
                default: return -1;
            }
        }

        void trainerRecords()
        {
            const u8* d = sav()->rawData().get();
            int at      = recordsOffset();
            std::vector<screens::PickItem> items;
            for (int i = 0; i < 200; i++)
            {
                u32 v = i < 100 ? u32(d[at + 4 * i] | d[at + 4 * i + 1] << 8 | d[at + 4 * i + 2] << 16 | u32(d[at + 4 * i + 3]) << 24)
                                : u32(d[at + 400 + 2 * (i - 100)] | d[at + 401 + 2 * (i - 100)] << 8);
                items.push_back({i, "Record " + std::to_string(i + 1) + ":  " + std::to_string(v), ""});
            }
            app::push(screens::picker("Trainer records", std::move(items), -1, [](int) {}));
        }

        // The original's Mystery Gift inbox: the cards physically in the save, read only (receiving, deleting
        // and the received flags stay the game's).
        void giftInbox()
        {
            std::vector<screens::PickItem> items;
            int n = sav()->currentGiftAmount();
            for (int i = 0; i < n; i++)
            {
                auto wc = sav()->mysteryGift(i);
                if (!wc)
                {
                    continue;
                }
                std::string what;
                if (wc->pokemon())
                {
                    what = pkdata::species(u16(wc->species())) + " Lv. " + std::to_string(wc->level()) + (wc->shiny() ? " (shiny)" : "");
                }
                else if (wc->item())
                {
                    what = "Item: " + pkdata::item(wc->object());
                }
                else
                {
                    what = "Other gift";
                }
                char id[16];
                snprintf(id, sizeof(id), "#%04u  ", unsigned(wc->ID()));
                items.push_back({i, std::string(id) + wc->title(), what + (wc->used() ? " - received" : " - waiting")});
            }
            app::push(screens::picker("Gifts in this save", std::move(items), -1, [](int) {}));
        }

        void mysteryGiftMenu()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            auto gifts = std::make_shared<std::vector<mysterygift::Gift>>(mysterygift::forSave(*sav()));
            if (gifts->empty())
            {
                app::message("Mystery Gift", "PKForge has no gifts for this game (Generation 4 on, without Brilliant "
                                             "Diamond, Shining Pearl and Legends: Arceus).");
                return;
            }
            std::vector<screens::PickItem> items;
            for (int i = 0; i < int(gifts->size()); i++)
            {
                items.push_back({i, (*gifts)[i].title, (*gifts)[i].detail});
            }
            app::push(screens::picker("Mystery Gift", std::move(items), -1, [this, gifts](int i)
                {
                    const mysterygift::Gift gift = (*gifts)[i];
                    app::confirm("Send \"" + gift.title + "\" to this save?\n" + gift.detail +
                                     "\nThe game hands it over like a real distribution.",
                        [this, gift]()
                        {
                            auto wc = mysterygift::load(gift);
                            if (!wc)
                            {
                                app::message("Mystery Gift", "That card could not be read.");
                                return;
                            }
                            int pos = sav()->currentGiftAmount();
                            sav()->mysteryGift(*wc, pos);
                            save->dirty = true;
                            app::message("Mystery Gift", "Sent. Write the save, then collect it in the game (from the "
                                                         "delivery person or the Mystery Gift menu).");
                        },
                        "Send", "Cancel");
                }));
        }

        // The original's Dex editor: every species of the game, A cycles unseen -> seen -> caught -> unseen.
        void dexEditor(int at)
        {
            static const char* names[] = {"[ ]  ", "[Seen]  ", "[Caught]  "};
            std::vector<screens::PickItem> items;
            for (const pksm::Species& species : sav()->availableSpecies())
            {
                u16 id = u16(species);
                char no[8];
                snprintf(no, sizeof(no), "%03u ", unsigned(id));
                items.push_back({id, names[int(dexedit::get(*sav(), id))] + std::string(no) + pkdata::species(id), ""});
            }
            std::sort(items.begin(), items.end(), [](const screens::PickItem& a, const screens::PickItem& b) { return a.id < b.id; });
            app::push(screens::picker("Pokédex editor", std::move(items), at,
                [this](int id)
                {
                    int next = (int(dexedit::get(*sav(), u16(id))) + 1) % 3;
                    dexedit::set(*sav(), u16(id), dexedit::State(next));
                    save->dirty = true;
                    refresh();
                    dexEditor(id);
                }));
        }

        void completeDex()
        {
            if (hardcoreBlocks())
            {
                return;
            }
            app::confirm("Mark every species this game has as seen and caught in its Pokédex?", [this]()
                {
                    for (const pksm::Species& species : sav()->availableSpecies())
                    {
                        auto pk = sav()->emptyPkm();
                        pk->species(species);
                        pk->alternativeForm(0);
                        pk->language(sav()->language());
                        pk->gender(pksm::PKX::genderFromRatio(pk->PID(), pk->genderType()));
                        sav()->dex(*pk);
                    }
                    save->dirty = true;
                    refresh();
                    app::message("Pokédex", std::to_string(sav()->dexSeen()) + " seen, " +
                                                std::to_string(sav()->dexCaught()) + " caught. Write the save to keep it.");
                },
                "Complete", "Cancel");
        }
    };
}

namespace screens
{
    // What a place plays, as the original's music menu: every track, one track, the menus' music, or silence.
    void placeMusic(audio::Place place, const std::string& title)
    {
        auto lib = std::make_shared<std::vector<std::pair<std::string, std::string>>>(audio::library());
        std::vector<screens::PickItem> items{{-1, "All tracks", "The whole library."}};
        if (place == audio::Place::ParkDay || place == audio::Place::ParkNight)
        {
            items.push_back({-2, "Same as menus", "Keeps the menus' music playing, without a cut."});
        }
        items.push_back({-3, "Silence", ""});
        int current = -1;
        std::string chosen = audio::choice(place);
        current = chosen == "same" ? -2 : chosen == "silence" ? -3 : -1;
        for (int i = 0; i < int(lib->size()); i++)
        {
            items.push_back({i, (*lib)[i].second, ""});
            if ((*lib)[i].first == chosen)
            {
                current = i;
            }
        }
        app::push(screens::picker(title, std::move(items), current, [place, lib](int id)
            {
                audio::choice(place, id == -1 ? "" : id == -2 ? "same" : id == -3 ? "silence" : (*lib)[id].first);
                soundMenu();
            }));
    }

    void soundMenu()
    {
        std::vector<app::Option> s;
        s.push_back({audio::musicOn() ? "Music: on" : "Music: off", []() { audio::musicOn(!audio::musicOn()); soundMenu(); },
            audio::status()});
        if (audio::musicOn())
        {
            s.push_back({audio::paused() ? "Play" : "Pause", []() { audio::pause(!audio::paused()); soundMenu(); }, audio::status()});
            s.push_back({"Skip to next track", []() { audio::skip(); soundMenu(); }, ""});
        }
        auto name = [](audio::Place p)
        {
            std::string c = audio::choice(p);
            if (c.empty())
            {
                return std::string("All tracks");
            }
            if (c == "same")
            {
                return std::string("Same as menus");
            }
            if (c == "silence")
            {
                return std::string("Silence");
            }
            for (auto& [path, title] : audio::library())
            {
                if (path == c)
                {
                    return title;
                }
            }
            return files::baseName(c);
        };
        s.push_back({"Menus: " + name(audio::Place::Menu), []() { placeMusic(audio::Place::Menu, "Menus"); },
            "Plays everywhere in PKForge outside the Poképark."});
        s.push_back({"Park by day: " + name(audio::Place::ParkDay), []() { placeMusic(audio::Place::ParkDay, "Park by day"); },
            "Plays in the Poképark in the morning and the afternoon."});
        s.push_back({"Park by night: " + name(audio::Place::ParkNight), []() { placeMusic(audio::Place::ParkNight, "Park by night"); },
            "Plays in the Poképark in the evening and at night."});
        s.push_back({std::string("Order: ") + (audio::shuffle() ? "Shuffle" : "In order"),
            []() { audio::shuffle(!audio::shuffle()); soundMenu(); }, "Play the tracks in order or shuffled."});
        s.push_back({std::string("Autostart: ") + (audio::autostart() ? "ON" : "OFF"),
            []() { audio::autostart(!audio::autostart()); soundMenu(); }, "ON starts the music when PKForge opens."});
        s.push_back({"Library: " + std::to_string(audio::library().size()) + " track(s)", []()
            {
                app::message("Music library", "Every track PKForge has, and your own: copy MP3, OGG or WAV files into " +
                                                  audio::libraryFolder().substr(5) + " on the SD card (or a place's own folder "
                                                  "in 3ds/PKForge/music) and they join the library.");
            },
            "Add your own files on the SD card."});
        s.push_back({audio::effectsOn() ? "Sound effects and cries: on" : "Sound effects and cries: off",
            []() { audio::effectsOn(!audio::effectsOn()); },
            "Your own music in 3ds/PKForge/music/{title,menu,park-day,park-night}; your own cries in "
            "3ds/PKForge/cries/<number>.wav, .ogg or .mp3."});
        s.push_back({"Test sound", []() { audio::test(); },
            "Four notes now. Nothing heard? Check the volume slider, then send me 3ds/PKForge/audio.log."});
        s.push_back({"Close", []() {}});
        app::menu("Sound", std::move(s));
    }

    std::unique_ptr<Screen> storage(std::shared_ptr<OpenSave> save)
    {
        return std::make_unique<Storage>(std::move(save));
    }

    std::unique_ptr<Screen> storageEvents(std::shared_ptr<OpenSave> save)
    {
        auto s         = std::make_unique<Storage>(std::move(save));
        s->eventsFirst = true;
        return s;
    }
}
