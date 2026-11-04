#include "rng.hpp"
#include "gfx.hpp"
#include "inspector.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "widgets.hpp"

#include <cstdio>

using namespace gfx;

namespace rng
{
    namespace
    {
        constexpr int PANEL_X = 10, PANEL_Y = 30, PANEL_W = 300, ROW = 17;

        class Rng : public Screen
        {
        public:
            Rng(std::unique_ptr<pksm::PKX> p, std::string w, std::function<void(std::unique_ptr<pksm::PKX>)> k)
                : pk(std::move(p)), where(std::move(w)), keep(std::move(k))
            {
            }

            bool overlay() const override { return true; }

            void update(Input& in) override
            {
                if (in.repeat & (KEY_DUP | KEY_DDOWN))
                {
                    focus = 1 - focus;
                }
                if (in.tapped(PANEL_X + 6, BUTTON_Y, PANEL_W - 12, 24))
                {
                    focus = 0;
                    choose();
                    return;
                }
                if (in.tapped(PANEL_X + 6, BUTTON_Y + 30, PANEL_W - 12, 24))
                {
                    app::pop();
                    return;
                }
                if (in.down & KEY_A)
                {
                    choose();
                }
                else if (in.down & KEY_B)
                {
                    app::pop();
                }
            }

            void drawTop() override { inspector::draw(pk.get(), where, 0); }

            void drawBottom() override
            {
                panel(PANEL_X, PANEL_Y - 4, PANEL_W, 184, T::Deck, T::FrameEdge);
                panelGrad(PANEL_X + 6, PANEL_Y, PANEL_W - 12, 18, T::PartySelected, T::ChipTop, T::PartyRim);
                text(PANEL_X + 14, PANEL_Y + 3, "RNG / IVs", T::Bright);
                int y = PANEL_Y + 24;
                char line[64];
                auto row = [&](const char* label, const std::string& value, u32 tone = T::Ink)
                {
                    text(PANEL_X + 10, y, label, T::InkSoft);
                    text(PANEL_X + 76, y, value, tone, Left, 1, PANEL_W - 86);
                    y += ROW;
                };
                snprintf(line, sizeof(line), "%08lX", (unsigned long)pk->PID());
                row("PID", line);
                if (pk->generation() >= pksm::Generation::SIX)
                {
                    snprintf(line, sizeof(line), "%08lX", (unsigned long)pk->encryptionConstant());
                    row("EC", line);
                }
                else
                {
                    row("EC", "(not in this generation)", T::SubInk);
                }
                std::string ivs;
                for (int i = 0; i < 6; i++)
                {
                    ivs += (i ? "/" : "") + std::to_string(pk->iv(inspector::statAt(i)));
                }
                row("IVs", ivs);
                row("Nature", natureText());
                row("Shiny", pk->shiny() ? "YES" : "NO", pk->shiny() ? S::ShinyGold : T::Ink);
                bool can = rerollable();
                widgets::buttonFace(PANEL_X + 6, BUTTON_Y, PANEL_W - 12, 24,
                    can ? "Reroll nature (keeps shiny)" : "No nature in this generation", focus == 0, false, -1, can);
                rect(PANEL_X + 7, BUTTON_Y + 3, 2, 18, can ? S::Legal : T::SubInk);
                widgets::buttonFace(PANEL_X + 6, BUTTON_Y + 30, PANEL_W - 12, 24, "Close", focus == 1, false);
            }

        private:
            static constexpr int BUTTON_Y = PANEL_Y + 118;
            std::unique_ptr<pksm::PKX> pk;
            std::string where;
            std::function<void(std::unique_ptr<pksm::PKX>)> keep;
            int focus = 0;

            bool rerollable() const { return pk->generation() >= pksm::Generation::THREE && !pk->egg(); }

            std::string natureText() const
            {
                int up, down;
                pkdata::natureEffect(u8(pk->nature()), up, down);
                static const char* shortStat[] = {"Atk", "Def", "Spe", "SpA", "SpD"};
                std::string name = pkdata::nature(u8(pk->nature()));
                return up >= 0 ? name + "  (+" + shortStat[up] + " -" + shortStat[down] + ")" : name;
            }

            void choose()
            {
                if (focus == 1)
                {
                    app::pop();
                    return;
                }
                if (!rerollable())
                {
                    app::toast("Generations 1 and 2 have no natures.");
                    return;
                }
                std::vector<screens::PickItem> items;
                for (int n = 0; n < 25; n++)
                {
                    int up, down;
                    pkdata::natureEffect(u8(n), up, down);
                    static const char* shortStat[] = {"Atk", "Def", "Spe", "SpA", "SpD"};
                    items.push_back({n, pkdata::nature(u8(n)), up >= 0 ? std::string("+") + shortStat[up] + " -" + shortStat[down] : "neutral"});
                }
                app::push(screens::picker("Reroll nature to", std::move(items), int(u8(pk->nature())), [this](int n) { reroll(n); }));
            }

            void reroll(int nature)
            {
                bool shiny  = pk->shiny();
                auto before = pk->clone();
                // Generation 3 and 4: PKSM-Core finds a PID with this nature that keeps the shininess, gender,
                // form and ability; Generation 5 on: the nature byte.
                pk->nature(pksm::Nature{u8(nature)});
                if (u8(pk->nature()) != nature || pk->shiny() != shiny)
                {
                    pk = std::move(before);
                    app::toast("No PID keeps this Pokémon as it is with that nature.");
                    return;
                }
                pk->refreshChecksum();
                if (pk->isParty())
                {
                    pk->updatePartyData();
                }
                if (keep)
                {
                    keep(pk->clone());
                }
                app::toast("Nature now " + pkdata::nature(u8(nature)) + ". Write the save to keep it.");
            }
        };
    }

    std::unique_ptr<Screen> open(std::unique_ptr<pksm::PKX> pk, std::string where,
        std::function<void(std::unique_ptr<pksm::PKX>)> keep)
    {
        return std::make_unique<Rng>(std::move(pk), std::move(where), std::move(keep));
    }
}
