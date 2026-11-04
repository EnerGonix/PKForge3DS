// A searchable list over the current screen: species, moves, items, abilities...
#include "facts.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cstdio>

using namespace gfx;

namespace
{
    constexpr int LIST_Y = 44, ROW = 17, VISIBLE = 10;

    class Picker : public Screen
    {
    public:
        Picker(std::string title, std::vector<screens::PickItem> items, int current, std::function<void(int)> chosen,
            screens::PickPreview preview)
            : title(std::move(title)), all(std::move(items)), chosen(std::move(chosen)), preview(preview)
        {
            filter();
            for (int i = 0; i < int(view.size()); i++)
            {
                if (all[view[i]].id == current)
                {
                    focus = i;
                }
            }
            first = std::max(0, focus - VISIBLE / 2);
        }

        bool overlay() const override { return true; }

        void update(Input& in) override
        {
            int n = int(view.size());
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
                if (in.repeat & (KEY_L | KEY_DLEFT))
                {
                    focus = std::max(0, focus - VISIBLE);
                }
                if (in.repeat & (KEY_R | KEY_DRIGHT))
                {
                    focus = std::min(n - 1, focus + VISIBLE);
                }
            }
            if (in.down & KEY_B)
            {
                app::pop();
                return;
            }
            if ((in.down & KEY_Y) || in.tapped(8, 22, BOTTOM_W - 16, 18))
            {
                std::string q = query;
                if (app::keyboard("Search", q, 20) || q.empty())
                {
                    query = q;
                    filter();
                    focus = first = 0;
                }
                return;
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
                focus = std::clamp(focus, first, std::min(n - 1, first + VISIBLE - 1));
            }
            else
            {
                drag = 0;
                if (focus < first)
                {
                    first = focus;
                }
                if (focus >= first + VISIBLE)
                {
                    first = focus - VISIBLE + 1;
                }
            }
            for (int i = 0; i < std::min(VISIBLE, n - first); i++)
            {
                if (in.tapped(8, LIST_Y + i * ROW, BOTTOM_W - 20, ROW - 1))
                {
                    focus = first + i;
                    pick();
                    return;
                }
            }
            if ((in.down & KEY_A) && n)
            {
                pick();
            }
        }

        void drawTop() override
        {
            if (preview == screens::PickPreview::None || view.empty())
            {
                return;
            }
            const screens::PickItem& item = all[view[focus]];
            housing(0, 0, TOP_W, H);
            if (preview == screens::PickPreview::Species)
            {
                panel(16, 24, 136, 136, T::Well, T::FrameEdge);
                if (!front(item.id, 0, false, false, 84, 92, 1))
                {
                    icon(item.id, 0, 84, 92, 2);
                }
                char line[64];
                snprintf(line, sizeof(line), "No. %03d", item.id);
                text(168, 28, line, T::SubInk);
                text(168, 44, item.name, T::Ink, Left, 2, 220);
                if (const facts::Entry* e = facts::species(item.id))
                {
                    text(168, 76, e->genus, T::Value, Left, 1, 220);
                    wrap(168, 100, 220, e->text, T::InkSoft, 1, 6);
                    if (!e->game.empty())
                    {
                        text(388, 196, "Pokémon " + e->game, T::SubInk, Right);
                    }
                }
            }
            else if (preview == screens::PickPreview::Move)
            {
                panel(16, 24, 368, 180, T::PanelFill, T::FrameEdge);
                text(30, 34, item.name, T::Ink, Left, 2, 340);
                if (const facts::Move* m = facts::move(item.id))
                {
                    if (m->type >= 0)
                    {
                        widgets::typeChip(30, 68, 70, m->type);
                    }
                    static const char* categories[] = {"Status", "Physical", "Special"};
                    char line[96];
                    snprintf(line, sizeof(line), "%s   Power %s   Accuracy %s", categories[std::clamp(m->category, 0, 2)],
                        m->power ? std::to_string(m->power).c_str() : "-",
                        m->accuracy ? std::to_string(m->accuracy).c_str() : "-");
                    text(108, 68, line, T::Value);
                    wrap(30, 94, 340, m->text, T::InkSoft, 1, 7);
                }
            }
            else
            {
                panel(16, 24, 368, 120, T::PanelFill, T::FrameEdge);
                text(30, 34, item.name, T::Ink, Left, 2, 340);
                if (!item.detail.empty())
                {
                    wrap(30, 70, 340, item.detail, T::InkSoft, 1, 4);
                }
            }
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::header(BOTTOM_W, title, std::to_string(view.size()));
            panel(8, 22, BOTTOM_W - 16, 18, T::Deep, T::FrameEdge);
            text(14, 25, query.empty() ? "Search (Y)" : "Search: " + query, query.empty() ? T::SubInk : T::Ink, Left,
                1, BOTTOM_W - 30);
            int n = int(view.size());
            for (int i = 0; i < std::min(VISIBLE, n - first); i++)
            {
                int idx = first + i;
                int y   = LIST_Y + i * ROW;
                bool f  = idx == focus;
                if (f)
                {
                    panelGrad(8, y, BOTTOM_W - 20, ROW - 1, T::ChipTop, T::ChipBottom, T::Cyan);
                }
                else
                {
                    rect(8, y, BOTTOM_W - 20, ROW - 1, (idx % 2) ? T::Band : T::LabelColumn);
                }
                const screens::PickItem& item = all[view[idx]];
                if (preview == screens::PickPreview::Species)
                {
                    char num[8];
                    snprintf(num, sizeof(num), "%03d", item.id);
                    text(14, y + 2, num, T::SubInk);
                    text(44, y + 2, item.name, f ? T::Ink : T::Value, Left, 1, 250);
                }
                else
                {
                    text(14, y + 2, item.name, f ? T::Ink : T::Value, Left, 1, 280);
                }
            }
            if (n == 0)
            {
                text(BOTTOM_W / 2, 110, "Nothing matches", T::InkSoft, Center);
            }
            widgets::scrollbar(BOTTOM_W - 9, LIST_Y, VISIBLE * ROW, first, VISIBLE, n);
            widgets::footer(BOTTOM_W, {{"A", "Pick"}, {"Y", "Search"}, {"LR", "Page"}, {"B", "Back"}});
        }

    private:
        std::string title;
        std::vector<screens::PickItem> all;
        std::vector<int> view;
        std::function<void(int)> chosen;
        screens::PickPreview preview;
        std::string query;
        int focus = 0, first = 0, drag = 0;

        void filter()
        {
            view.clear();
            std::string q = files::lower(query);
            for (int i = 0; i < int(all.size()); i++)
            {
                if (q.empty() || files::lower(all[i].name).find(q) != std::string::npos ||
                    std::to_string(all[i].id) == q)
                {
                    view.push_back(i);
                }
            }
        }

        void pick()
        {
            if (view.empty())
            {
                return;
            }
            int id   = all[view[focus]].id;
            auto act = chosen;
            app::pop();
            if (act)
            {
                act(id);
            }
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> picker(std::string title, std::vector<PickItem> items, int current,
        std::function<void(int)> chosen, PickPreview preview)
    {
        return std::make_unique<Picker>(std::move(title), std::move(items), current, std::move(chosen), preview);
    }
}
