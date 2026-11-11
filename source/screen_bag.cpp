// The bag editor: any pouch, any item, any count.
#include "gfx.hpp"
#include "pkdata.hpp"
#include "saves.hpp"
#include "screens.hpp"
#include "ui_ids.hpp"
#include "widgets.hpp"

#include "sav/Item.hpp"
#include <algorithm>
#include <cstdio>

using namespace gfx;

namespace
{
    constexpr int LIST_Y = 52, ROW = 19, VISIBLE = 8;

    const char* pouchName(pksm::Sav::Pouch p)
    {
        using P = pksm::Sav::Pouch;
        switch (p)
        {
            case P::NormalItem: return "Items";
            case P::KeyItem: return "Key items";
            case P::TM: return "TMs & HMs";
            case P::Mail: return "Mail";
            case P::Medicine: return "Medicine";
            case P::Berry: return "Berries";
            case P::Ball: return "Poké Balls";
            case P::Battle: return "Battle items";
            case P::Candy: return "Candy";
            case P::ZCrystals: return "Z-Crystals";
            case P::Treasure: return "Treasures";
            case P::Ingredient: return "Ingredients";
            case P::PCItem: return "PC items";
            case P::RotomPower: return "Rotom Powers";
            case P::CatchingItem: return "Catching items";
            case P::MegaStones: return "Mega Stones";
            case P::Recipe: return "Recipes";
            case P::Donut: return "Donuts";
        }
        return "Pouch";
    }

    struct Entry
    {
        int slot, id, count;
    };

    class BagScreen : public Screen
    {
    public:
        explicit BagScreen(std::shared_ptr<OpenSave> s) : save(std::move(s))
        {
            for (auto& p : save->sav->pouches())
            {
                if (p.first != pksm::Sav::Pouch::Donut)
                {
                    pouches.push_back(p);
                }
            }
            load();
        }

        void update(Input& in) override
        {
            if (pouches.empty())
            {
                if (in.down & KEY_B)
                {
                    app::pop();
                }
                return;
            }
            if ((in.repeat & KEY_L) || in.tapped(8, 29, 22, 20))
            {
                pouch = (pouch + int(pouches.size()) - 1) % int(pouches.size());
                load();
            }
            if ((in.repeat & KEY_R) || in.tapped(BOTTOM_W - 30, 29, 22, 20))
            {
                pouch = (pouch + 1) % int(pouches.size());
                load();
            }
            int n = int(items.size());
            if (n)
            {
                if (in.repeat & KEY_DUP)
                {
                    focus = (focus + n - 1) % n;
                }
                if (in.repeat & KEY_DDOWN)
                {
                    focus = (focus + 1) % n;
                }
                if (in.repeat & KEY_DLEFT)
                {
                    setCount(focus, items[focus].count - 1);
                }
                if (in.repeat & KEY_DRIGHT)
                {
                    setCount(focus, items[focus].count + 1);
                }
            }
            if (focus < first)
            {
                first = focus;
            }
            if (focus >= first + VISIBLE)
            {
                first = focus - VISIBLE + 1;
            }
            if (in.touching && in.dragged && in.inside(0, LIST_Y, BOTTOM_W, VISIBLE * ROW))
            {
                drag += in.dy;
                while (drag <= -ROW && first + VISIBLE < n)
                {
                    drag += ROW;
                    first++;
                }
                while (drag >= ROW && first > 0)
                {
                    drag -= ROW;
                    first--;
                }
                focus = std::clamp(focus, first, std::max(first, std::min(n - 1, first + VISIBLE - 1)));
            }
            else if (!in.touching)
            {
                drag = 0;
            }
            for (int i = 0; i < std::min(VISIBLE, n - first); i++)
            {
                int y = LIST_Y + i * ROW, idx = first + i;
                if (in.tapped(BOTTOM_W - 58, y, 22, ROW - 1))
                {
                    focus = idx;
                    setCount(idx, items[idx].count - 1);
                    return;
                }
                if (in.tapped(BOTTOM_W - 32, y, 22, ROW - 1))
                {
                    focus = idx;
                    setCount(idx, items[idx].count + 1);
                    return;
                }
                if (in.tapped(8, y, BOTTOM_W - 70, ROW - 1))
                {
                    focus = idx;
                    itemMenu();
                    return;
                }
            }
            if ((in.down & KEY_A) && n)
            {
                itemMenu();
            }
            else if (in.down & KEY_Y)
            {
                addItem();
            }
            else if (in.down & KEY_START)
            {
                presets();
            }
            else if (in.down & KEY_B)
            {
                app::pop();
            }
        }

        void drawTop() override
        {
            housing(0, 0, TOP_W, H);
            image(ui::bag, 12, 10);
            text(46, 16, "Bag", T::Ink, Left, 2);
            text(388, 20, (save->ref.label.empty() ? pkdata::game(save->ref.version) : save->ref.label), T::InkSoft, Right, 1, 200);
            panel(12, 50, 376, 150, T::PanelFill, T::FrameEdge);
            if (!items.empty())
            {
                const Entry& e = items[focus];
                text(28, 66, pkdata::item(e.id), T::Ink, Left, 2, 340);
                char line[64];
                snprintf(line, sizeof(line), "x%d   (holds up to %d)", e.count, maxCount());
                text(28, 100, line, T::Value);
                snprintf(line, sizeof(line), "Item No. %d  -  slot %d", e.id, e.slot + 1);
                text(28, 118, line, T::SubInk);
            }
            else
            {
                text(200, 116, "This pouch is empty", T::InkSoft, Center);
            }
            wrap(16, 208, 368, "Left / right adjust - L / R change pouch - A opens the item - Y adds one",
                T::SubInk, 1, 2);
        }

        // A window over the boxes, as the original's Bag popup.
        bool overlay() const override { return true; }

        void drawBottom() override
        {
            panel(2, 2, BOTTOM_W - 4, H - 4, T::Deck, T::FrameEdge);
            widgets::titleBar(BOTTOM_W, (save->ref.label.empty() ? pkdata::game(save->ref.version) : save->ref.label), save->dirty ? "Bag - edited" : "Bag");
            if (pouches.empty())
            {
                text(BOTTOM_W / 2, 110, "This game has no bag to edit", T::InkSoft, Center);
                widgets::footer(BOTTOM_W, {{"B", "Back"}});
                return;
            }
            panel(8, 29, 22, 20, T::Void, T::Ink);
            chevron(17, 35, T::Ink, true);
            panel(BOTTOM_W - 30, 29, 22, 20, T::Void, T::Ink);
            chevron(BOTTOM_W - 21, 35, T::Ink, false);
            char title[64];
            snprintf(title, sizeof(title), "%s (%d)", pouchName(pouches[pouch].first), int(items.size()));
            text(BOTTOM_W / 2, 33, title, T::Ink, Center, 1, 240);
            for (int i = 0; i < std::min(VISIBLE, int(items.size()) - first); i++)
            {
                const Entry& e = items[first + i];
                int y          = LIST_Y + i * ROW;
                bool f         = first + i == focus;
                if (f)
                {
                    panelGrad(8, y, BOTTOM_W - 16, ROW - 1, T::PartySelected, T::ChipTop, T::Cyan);
                }
                else
                {
                    panel(8, y, BOTTOM_W - 16, ROW - 1, T::CardRow, T::CardRow);
                }
                text(14, y + 3, pkdata::item(e.id), f ? T::Bright : T::Ink, Left, 1, 180);
                char count[16];
                snprintf(count, sizeof(count), "x%d", e.count);
                text(BOTTOM_W - 62, y + 3, count, f ? T::ChipInk : T::InkSoft, Right);
                for (int b = 0; b < 2; b++)
                {
                    float cx = BOTTOM_W - (b ? 21 : 47), cy = y + 9;
                    circle(cx, cy, 8, T::Cyan);
                    circle(cx, cy, 7, rgb(0x0C, 0x10, 0x20));
                    rect(cx - 4, cy - 1, 8, 2, T::Bright);
                    if (b)
                    {
                        rect(cx - 1, cy - 4, 2, 8, T::Bright);
                    }
                }
            }
            widgets::scrollbar(BOTTOM_W - 6, LIST_Y, VISIBLE * ROW, first, VISIBLE, int(items.size()));
            widgets::footer(BOTTOM_W, {{"A", "Item"}, {"Y", "Add"}, {"LR", "Pouch"}, {"START", "Presets"}, {"B", "Back"}});
        }

    private:
        std::shared_ptr<OpenSave> save;
        std::vector<std::pair<pksm::Sav::Pouch, int>> pouches;
        std::vector<Entry> items;
        int pouch = 0, focus = 0, first = 0, drag = 0;

        pksm::Sav& sav() { return *save->sav; }

        pksm::Sav::Pouch current() const { return pouches[pouch].first; }

        int maxCount() { return sav().maxCount(current()); }

        void load()
        {
            items.clear();
            focus = first = 0;
            if (pouches.empty())
            {
                return;
            }
            for (int slot = 0; slot < pouches[pouch].second; slot++)
            {
                auto it = sav().item(current(), u16(slot));
                if (it && it->id() != 0)
                {
                    items.push_back({slot, it->id(), it->count()});
                }
            }
        }

        void write(int slot, int id, int count)
        {
            auto it = sav().item(current(), u16(slot));
            if (!it)
            {
                return;
            }
            it->id(u16(id));
            it->count(u16(count));
            sav().item(*it, current(), u16(slot));
            save->dirty = true;
        }

        void setCount(int index, int count)
        {
            if (index < 0 || index >= int(items.size()))
            {
                return;
            }
            count = std::clamp(count, 1, maxCount());
            write(items[index].slot, items[index].id, count);
            items[index].count = count;
        }

        std::vector<screens::PickItem> validItems()
        {
            std::vector<screens::PickItem> out;
            for (auto& [p, ids] : sav().validItems())
            {
                if (p != current())
                {
                    continue;
                }
                for (int id : ids)
                {
                    if (id && !pkdata::item(id).empty())
                    {
                        out.push_back({id, pkdata::item(id), ""});
                    }
                }
            }
            return out;
        }

        void itemMenu()
        {
            if (items.empty())
            {
                return;
            }
            std::vector<app::Option> o;
            o.push_back({"Set the count", [this]()
                {
                    int v = items[focus].count;
                    if (app::numpad("Count", v, 1, maxCount()))
                    {
                        setCount(focus, v);
                    }
                }});
            o.push_back({"Change the item", [this]()
                {
                    int slot = items[focus].slot, count = items[focus].count;
                    app::push(screens::picker("Item", validItems(), items[focus].id,
                        [this, slot, count](int id)
                        {
                            write(slot, id, count);
                            int keep = focus;
                            load();
                            focus = std::min(keep, std::max(0, int(items.size()) - 1));
                        },
                        screens::PickPreview::Item));
                }});
            o.push_back({"Remove it", [this]() { removeItem(focus); }});
            app::menu(pkdata::item(items[focus].id), std::move(o));
        }

        // ---- presets, as the original's one-tap refills

        // Every item this game allows in pouch p, at its largest count; key items once each.
        int fillPouch(int p)
        {
            pksm::Sav::Pouch kind = pouches[p].first;
            int capacity          = pouches[p].second, most = sav().maxCount(kind), done = 0;
            if (sav().pouchIndexedByItem(kind))
            {
                // A place per item: every item already listed there gets the largest count.
                for (int slot = 0; slot < capacity; slot++)
                {
                    auto it = sav().item(kind, u16(slot));
                    if (it && it->id())
                    {
                        it->count(u16(most));
                        sav().item(*it, kind, u16(slot));
                        done++;
                    }
                }
            }
            else
            {
                std::vector<int> ids;
                for (auto& [pk, list] : sav().validItems())
                {
                    if (pk == kind)
                    {
                        for (int id : list)
                        {
                            if (id && !pkdata::item(id).empty())
                            {
                                ids.push_back(id);
                            }
                        }
                    }
                }
                for (int slot = 0; slot < capacity; slot++)
                {
                    auto it = sav().item(kind, u16(slot));
                    if (!it)
                    {
                        continue;
                    }
                    bool has = slot < int(ids.size());
                    it->id(u16(has ? ids[slot] : 0));
                    it->count(u16(has ? most : 0));
                    sav().item(*it, kind, u16(slot));
                    done += has;
                }
            }
            save->dirty = true;
            return done;
        }

        void maxCounts()
        {
            for (auto& e : items)
            {
                write(e.slot, e.id, maxCount());
            }
            load();
            app::toast("Every item here at " + std::to_string(maxCount()) + ". Write the save to keep it.");
        }

        void presets()
        {
            if (pouches.empty())
            {
                return;
            }
            std::string name = pouchName(current());
            bool keys        = current() == pksm::Sav::Pouch::KeyItem;
            std::vector<app::Option> o;
            o.push_back({"Fill " + name, [this, keys, name]()
                {
                    app::confirm(std::string(keys ? "Key items can start events early or skip them. " : "") + "Fill " + name +
                                     " with every item this game has, at the largest count? What is there now is replaced.",
                        [this, name]()
                        {
                            int n = fillPouch(pouch);
                            load();
                            app::toast(std::to_string(n) + " items in " + name + ". Write the save to keep it.");
                        },
                        "Fill it", "Cancel");
                },
                "Every item the pouch takes, at the largest count."});
            o.push_back({"Max the counts here", [this]() { maxCounts(); }, "The items already here, each at the largest count."});
            o.push_back({"Fill every pouch", [this]()
                {
                    app::confirm("Fill every pouch except the key items with every item this game has, at the largest count? "
                                 "What is there now is replaced.",
                        [this]()
                        {
                            int n = 0;
                            for (int p = 0; p < int(pouches.size()); p++)
                            {
                                if (pouches[p].first != pksm::Sav::Pouch::KeyItem)
                                {
                                    n += fillPouch(p);
                                }
                            }
                            load();
                            app::toast(std::to_string(n) + " items in the bag. Write the save to keep it.");
                        },
                        "Fill them", "Cancel");
                },
                "Not the key items: they drive the story."});
            o.push_back({"Empty " + name, [this, name]()
                {
                    app::confirm("Take everything out of " + name + "?", [this]()
                        {
                            pksm::Sav::Pouch kind = current();
                            for (int slot = 0; slot < pouches[pouch].second; slot++)
                            {
                                if (auto it = sav().item(kind, u16(slot)))
                                {
                                    if (!sav().pouchIndexedByItem(kind))
                                    {
                                        it->id(0);
                                    }
                                    it->count(0);
                                    sav().item(*it, kind, u16(slot));
                                }
                            }
                            save->dirty = true;
                            load();
                            app::toast("Emptied. Write the save to keep it.");
                        },
                        "Empty it", "Cancel");
                }});
            o.push_back({"Close", []() {}});
            app::menu("Bag presets", std::move(o));
        }

        void removeItem(int index)
        {
            int slot = items[index].slot;
            if (sav().pouchIndexedByItem(current()))
            {
                write(slot, items[index].id, 0);
            }
            else
            {
                // Packed pouches close the gap, as the games keep them.
                int capacity = pouches[pouch].second;
                for (int s = slot; s + 1 < capacity; s++)
                {
                    auto next = sav().item(current(), u16(s + 1));
                    write(s, next ? next->id() : 0, next ? next->count() : 0);
                }
                write(capacity - 1, 0, 0);
            }
            load();
            focus = std::min(index, std::max(0, int(items.size()) - 1));
        }

        void addItem()
        {
            if (pouches.empty())
            {
                return;
            }
            if (sav().pouchIndexedByItem(current()))
            {
                app::toast("This pouch keeps a place for every item; change an item instead.");
                return;
            }
            int freeSlot = -1;
            for (int slot = 0; slot < pouches[pouch].second; slot++)
            {
                auto it = sav().item(current(), u16(slot));
                if (it && it->id() == 0)
                {
                    freeSlot = slot;
                    break;
                }
            }
            if (freeSlot < 0)
            {
                app::toast("This pouch is full.");
                return;
            }
            app::push(screens::picker("Add an item", validItems(), 0,
                [this, freeSlot](int id)
                {
                    write(freeSlot, id, 1);
                    load();
                    focus = std::max(0, int(items.size()) - 1);
                },
                screens::PickPreview::Item));
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> bag(std::shared_ptr<OpenSave> save) { return std::make_unique<BagScreen>(std::move(save)); }
}
