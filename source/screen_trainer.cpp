// The trainer card: name, IDs, money, play time.
#include "gfx.hpp"
#include "pkdata.hpp"
#include "profiles.hpp"
#include "saves.hpp"
#include "screens.hpp"
#include "ui_ids.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cstdio>

using namespace gfx;

namespace
{
    constexpr int PLATE_Y = 30, LIST_Y = 51, ROW = 18;

    struct Field
    {
        std::string label, value;
        std::function<void()> act;
    };

    class Trainer : public Screen
    {
    public:
        // Trainer profiles, as the original's: this save's trainer kept, or a kept one put on this save.
        void profileMenu()
        {
            std::vector<app::Option> o;
            o.push_back({"Save as profile", [this]()
                {
                    profiles::add({sav().otName(), sav().TID(), sav().SID(), sav().gender() == pksm::Gender::Female});
                    app::toast("Profile " + sav().otName() + " kept.");
                },
                "This trainer's name, IDs and gender, to use on other saves and Pokémon."});
            o.push_back({"Load a profile", [this]()
                {
                    profiles::pick("Load a profile", [this](const profiles::Profile& p)
                        {
                            sav().otName(p.name);
                            sav().TID(p.tid);
                            sav().SID(p.sid);
                            sav().gender(p.female ? pksm::Gender::Female : pksm::Gender::Male);
                            save->dirty = true;
                            app::toast("Trainer is " + p.name + " now. Write the save to keep it.");
                        });
                },
                "Name, IDs and gender from a kept profile. Pokémon caught before keep their own trainer."});
            o.push_back({"Delete a profile", []()
                {
                    auto list = profiles::all();
                    profiles::pick("Delete a profile", [list](const profiles::Profile& p)
                        {
                            for (int i = 0; i < int(list.size()); i++)
                            {
                                if (list[i].name == p.name && list[i].tid == p.tid && list[i].sid == p.sid)
                                {
                                    profiles::remove(i);
                                    app::toast("Profile deleted.");
                                    return;
                                }
                            }
                        });
                }});
            o.push_back({"Close", []() {}});
            app::menu("Trainer profiles", std::move(o));
        }

        explicit Trainer(std::shared_ptr<OpenSave> s) : save(std::move(s)) {}

        void update(Input& in) override
        {
            auto fields = build();
            int n       = int(fields.size());
            if (in.repeat & KEY_DUP)
            {
                focus = (focus + n - 1) % n;
            }
            if (in.repeat & KEY_DDOWN)
            {
                focus = (focus + 1) % n;
            }
            for (int i = 0; i < n; i++)
            {
                if (in.tapped(6, LIST_Y + i * ROW, BOTTOM_W - 12, ROW - 2))
                {
                    focus = i;
                    if (fields[i].act)
                    {
                        fields[i].act();
                    }
                    return;
                }
            }
            if ((in.down & KEY_A) && fields[focus].act)
            {
                fields[focus].act();
            }
            if (in.down & KEY_START)
            {
                profileMenu();
                return;
            }
            if (in.down & KEY_B)
            {
                app::pop();
            }
        }

        void drawTop() override
        {
            housing(0, 0, TOP_W, H);
            // The card itself.
            panel(28, 24, 344, 192, T::CardFill, T::FrameEdge);
            vgrad(29, 25, 342, 30, T::Grid, T::CardFill);
            image(ui::trainer, 40, 28);
            text(76, 33, "TRAINER CARD", T::Ink);
            char line[64];
            snprintf(line, sizeof(line), "IDNo. %05u", unsigned(sav().displayTID()));
            text(360, 33, line, T::Value, Right);
            int y = 70;
            auto item = [&](const char* label, const std::string& value)
            {
                rect(40, y - 3, 320, 18, T::CardRow);
                text(48, y, label, T::InkSoft);
                text(352, y, value, T::Ink, Right, 1, 200);
                y += 22;
            };
            item("NAME", sav().otName());
            snprintf(line, sizeof(line), "$%u", unsigned(sav().money()));
            item("MONEY", line);
            snprintf(line, sizeof(line), "%d seen  %d caught", sav().dexSeen(), sav().dexCaught());
            item("POKéDEX", line);
            snprintf(line, sizeof(line), "%d:%02d", sav().playedHours(), sav().playedMinutes());
            item("TIME", line);
            snprintf(line, sizeof(line), "%d", sav().badges());
            item("BADGES", line);
            text(TOP_W / 2, 200, pkdata::game(save->ref.version), T::SubInk, Center);
        }

        // A window over the boxes, as the original's Trainer card popup.
        bool overlay() const override { return true; }

        void drawBottom() override
        {
            panel(2, 2, BOTTOM_W - 4, H - 4, T::Deck, T::FrameEdge);
            widgets::titleBar(BOTTOM_W, pkdata::game(save->ref.version), save->dirty ? "Trainer card - edited" : "Trainer card");
            // The original's "Trainer card" plate.
            panelGrad(6, PLATE_Y, BOTTOM_W - 12, 18, T::PartySelected, T::ChipTop, T::PartyRim);
            rect(7, PLATE_Y + 1, BOTTOM_W - 14, 1, alpha(T::Bright, 50));
            text(14, PLATE_Y + 3, "Trainer card", T::Bright);
            auto fields = build();
            for (int i = 0; i < int(fields.size()); i++)
            {
                widgets::row(6, LIST_Y + i * ROW, BOTTOM_W - 12, ROW - 2, fields[i].label, fields[i].value, i == focus,
                    bool(fields[i].act));
            }
            widgets::footer(BOTTOM_W, {{"A", "Edit"}, {"START", "Profiles"}, {"B", "Back"}});
        }

    private:
        std::shared_ptr<OpenSave> save;
        int focus = 0;

        pksm::Sav& sav() { return *save->sav; }

        int gen() const { return save->ref.gen; }

        void changed() { save->dirty = true; }

        bool number(const std::string& label, int& v, int min, int max)
        {
            if (app::numpad(label, v, min, max))
            {
                changed();
                return true;
            }
            return false;
        }

        std::vector<Field> build()
        {
            std::vector<Field> f;
            char buf[32];
            f.push_back({"Name", sav().otName(), [this]()
                {
                    std::string name = sav().otName();
                    if (app::keyboard("Trainer name", name, gen() <= 5 ? 7 : 12))
                    {
                        sav().otName(name);
                        changed();
                    }
                }});
            if (gen() >= 2)
            {
                bool female = sav().gender() == pksm::Gender::Female;
                f.push_back({"Gender", female ? "Female" : "Male", [this, female]()
                    {
                        sav().gender(female ? pksm::Gender::Male : pksm::Gender::Female);
                        changed();
                    }});
            }
            snprintf(buf, sizeof(buf), "%05u", unsigned(sav().TID()));
            f.push_back({"ID No.", buf, [this]()
                {
                    int v = sav().TID();
                    if (number("ID No.", v, 0, 65535))
                    {
                        sav().TID(u16(v));
                    }
                }});
            if (gen() >= 3)
            {
                snprintf(buf, sizeof(buf), "%05u", unsigned(sav().SID()));
                f.push_back({"Secret ID", buf, [this]()
                    {
                        int v = sav().SID();
                        if (number("Secret ID", v, 0, 65535))
                        {
                            sav().SID(u16(v));
                        }
                    }});
            }
            snprintf(buf, sizeof(buf), "%u", unsigned(sav().money()));
            f.push_back({"Money", buf, [this]()
                {
                    int v = int(sav().money());
                    if (number("Money", v, 0, gen() >= 8 ? 9999999 : 999999))
                    {
                        sav().money(u32(v));
                    }
                }});
            if (gen() >= 4)
            {
                snprintf(buf, sizeof(buf), "%u", unsigned(sav().BP()));
                f.push_back({"Battle Points", buf, [this]()
                    {
                        int v = int(sav().BP());
                        if (number("Battle Points", v, 0, 9999))
                        {
                            sav().BP(u32(v));
                        }
                    }});
            }
            snprintf(buf, sizeof(buf), "%d", sav().playedHours());
            f.push_back({"Hours played", buf, [this]()
                {
                    int v = sav().playedHours();
                    if (number("Hours", v, 0, 999))
                    {
                        sav().playedHours(u16(v));
                    }
                }});
            snprintf(buf, sizeof(buf), "%d", sav().playedMinutes());
            f.push_back({"Minutes", buf, [this]()
                {
                    int v = sav().playedMinutes();
                    if (number("Minutes", v, 0, 59))
                    {
                        sav().playedMinutes(u8(v));
                    }
                }});
            snprintf(buf, sizeof(buf), "%d", sav().badges());
            f.push_back({"Badges", buf, nullptr});
            return f;
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> trainer(std::shared_ptr<OpenSave> save) { return std::make_unique<Trainer>(std::move(save)); }
}
