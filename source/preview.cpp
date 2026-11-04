#include "preview.hpp"
#include "gfx.hpp"
#include "inspector.hpp"
#include "legality.hpp"
#include "pkdata.hpp"
#include "widgets.hpp"

#include <vector>

using namespace gfx;

namespace preview
{
    namespace
    {
        struct Change
        {
            std::string what;
            bool bad; // a loss or a problem (red), else a plain change
        };

        std::string moveName(int m) { return m ? pkdata::move(m) : "-"; }

        std::vector<Change> changes(const pksm::PKX& a, const pksm::PKX& b)
        {
            std::vector<Change> out;
            std::string fa = pkdata::format(a), fb = pkdata::format(b);
            if (fa != fb)
            {
                out.push_back({"Format: " + fa + " becomes " + fb, false});
            }
            if (u16(a.species()) != u16(b.species()) || a.alternativeForm() != b.alternativeForm())
            {
                out.push_back({"Species: " + pkdata::species(u16(a.species())) + " becomes " + pkdata::species(u16(b.species())), true});
            }
            if (a.level() != b.level())
            {
                out.push_back({"Level: " + std::to_string(a.level()) + " becomes " + std::to_string(b.level()), true});
            }
            if (a.nickname() != b.nickname())
            {
                out.push_back({"Nickname: " + a.nickname() + " becomes " + b.nickname(), false});
            }
            for (int i = 0; i < 4; i++)
            {
                int ma = u16(a.move(i)), mb = u16(b.move(i));
                if (ma != mb)
                {
                    out.push_back({"Move " + std::to_string(i + 1) + ": " + moveName(ma) + (mb ? " becomes " + moveName(mb) : " is lost"), ma && !mb});
                }
            }
            if (a.generation() >= pksm::Generation::THREE && b.generation() >= pksm::Generation::THREE &&
                u16(a.ability()) != u16(b.ability()))
            {
                out.push_back({"Ability: " + pkdata::ability(u16(a.ability())) + " becomes " + pkdata::ability(u16(b.ability())), false});
            }
            if (a.heldItem() != b.heldItem())
            {
                out.push_back({"Item: " + (a.heldItem() ? pkdata::item(a.heldItem()) : std::string("none")) +
                                   (b.heldItem() ? " becomes " + pkdata::item(b.heldItem()) : " is lost"),
                    a.heldItem() && !b.heldItem()});
            }
            if (u8(a.ball()) != u8(b.ball()))
            {
                out.push_back({"Ball: " + pkdata::ball(u8(a.ball())) + " becomes " + pkdata::ball(u8(b.ball())), false});
            }
            if (a.shiny() != b.shiny())
            {
                out.push_back({b.shiny() ? "Becomes shiny" : "Stops being shiny", true});
            }
            return out;
        }

        // What PKForge's offline rules would change in the destination game: empty means legal there.
        std::vector<std::string> issues(const pksm::PKX& after, const pksm::Sav& sav)
        {
            auto copy = after.clone();
            return legality::fix(*copy, sav);
        }

        class Preview : public Screen
        {
        public:
            Preview(const pksm::PKX& before, const pksm::PKX& after, const pksm::Sav& sav, std::string g, std::function<void()> c)
                : shown(after.clone()), game(std::move(g)), confirm(std::move(c))
            {
                list = changes(before, after);
                problems = issues(after, sav);
                for (auto& p : problems)
                {
                    list.push_back({"Not legal here: " + p, true});
                }
                if (list.empty())
                {
                    list.push_back({"Nothing changes.", false});
                }
            }

            bool overlay() const override { return true; }

            void update(Input& in) override
            {
                int n = int(list.size());
                if (in.repeat & KEY_DDOWN)
                {
                    first = std::min(std::max(0, n - VISIBLE), first + 1);
                }
                if (in.repeat & KEY_DUP)
                {
                    first = std::max(0, first - 1);
                }
                if ((in.down & KEY_A) || in.tapped(BUTTONS_X, BUTTONS_Y, 140, 24))
                {
                    auto go = confirm;
                    app::pop();
                    if (go)
                    {
                        go();
                    }
                    return;
                }
                if ((in.down & KEY_B) || in.tapped(BUTTONS_X + 150, BUTTONS_Y, 128, 24))
                {
                    app::pop();
                }
            }

            void drawTop() override { inspector::draw(shown.get(), "After the move to " + game, 0); }

            void drawBottom() override
            {
                panel(8, 24, BOTTOM_W - 16, 188, T::Deck, T::FrameEdge);
                panelGrad(14, 28, BOTTOM_W - 28, 18, T::PartySelected, T::ChipTop, T::PartyRim);
                text(22, 31, "Transfer preview  -  to " + game, T::Bright, Left, 1, BOTTOM_W - 50);
                for (int i = 0; i < VISIBLE && first + i < int(list.size()); i++)
                {
                    const Change& c = list[first + i];
                    int y = 50 + i * 14;
                    if (i % 2 == 0)
                    {
                        rect(14, y - 1, BOTTOM_W - 34, 14, T::LabelColumn);
                    }
                    text(18, y, c.what, c.bad ? S::Illegal : T::Value, Left, 1, BOTTOM_W - 42);
                }
                widgets::scrollbar(BOTTOM_W - 18, 50, VISIBLE * 14, first, VISIBLE, int(list.size()));
                bool legal = problems.empty();
                text(BOTTOM_W / 2, 154, legal ? "Legal there by PKForge's offline rules" : "PKForge's rules find problems there",
                    legal ? S::Legal : S::Illegal, Center);
                widgets::buttonFace(BUTTONS_X, BUTTONS_Y, 140, 24, "A  Move it", true, false);
                widgets::buttonFace(BUTTONS_X + 150, BUTTONS_Y, 128, 24, "B  Cancel", false, false);
            }

        private:
            static constexpr int VISIBLE = 7, BUTTONS_X = 18, BUTTONS_Y = 176;
            std::unique_ptr<pksm::PKX> shown;
            std::string game;
            std::function<void()> confirm;
            std::vector<Change> list;
            std::vector<std::string> problems;
            int first = 0;
        };
    }

    bool needed(const pksm::PKX& before, const pksm::PKX& after, const pksm::Sav& sav)
    {
        return pkdata::format(before) != pkdata::format(after) || !issues(after, sav).empty();
    }

    std::unique_ptr<Screen> open(const pksm::PKX& before, const pksm::PKX& after, const pksm::Sav& sav, const std::string& game,
        std::function<void()> confirm)
    {
        return std::make_unique<Preview>(before, after, sav, game, std::move(confirm));
    }
}
