#include "audio.hpp"
#include "app.hpp"
#include "bank.hpp"
#include "gfx.hpp"
#include "widgets.hpp"

#include <3ds.h>
#include <algorithm>
#include <citro3d.h>
#include <cstdio>
#include <cstdlib>

using namespace gfx;

namespace
{
    std::vector<std::unique_ptr<Screen>> stack;
    std::vector<std::unique_ptr<Screen>> incoming;
    int pops      = 0;
    bool quitting = false;
    int frames    = 0;
    std::string toastText;
    int toastUntil = 0;
    Bank theBank;

    void applyChanges()
    {
        bool popped = pops > 0;
        while (pops > 0 && !stack.empty())
        {
            stack.pop_back();
            pops--;
        }
        pops = 0;
        for (auto& s : incoming)
        {
            stack.push_back(std::move(s));
        }
        bool pushed = !incoming.empty();
        incoming.clear();
        if (popped && !pushed && !stack.empty())
        {
            stack.back()->resumed();
        }
    }

    // A list of choices over the current screen.
    class Menu : public Screen
    {
    public:
        Menu(std::string title, std::vector<app::Option> options) : title(std::move(title)), options(std::move(options))
        {
            while (focus < int(this->options.size()) && !this->options[focus].enabled)
            {
                focus++;
            }
            if (focus >= int(this->options.size()))
            {
                focus = 0;
            }
        }

        bool overlay() const override { return true; }

        void update(Input& in) override
        {
            int n = int(options.size());
            if (in.repeat & KEY_DUP)
            {
                focus = (focus + n - 1) % n;
            }
            if (in.repeat & KEY_DDOWN)
            {
                focus = (focus + 1) % n;
            }
            keepVisible();
            if (in.down & KEY_B)
            {
                app::pop();
                return;
            }
            for (int i = 0; i < std::min(n - first, VISIBLE); i++)
            {
                if (in.tapped(X + 4, rowY(i), W - 8, ROW - 2))
                {
                    focus = first + i;
                    choose();
                    return;
                }
            }
            if (in.touching && in.dragged && in.inside(X, Y, W, panelH()))
            {
                scroll += in.dy;
                while (scroll <= -ROW && first + VISIBLE < n)
                {
                    scroll += ROW;
                    first++;
                }
                while (scroll >= ROW && first > 0)
                {
                    scroll -= ROW;
                    first--;
                }
            }
            if (!in.touching)
            {
                scroll = 0;
            }
            if (in.down & KEY_A)
            {
                choose();
            }
        }

        void drawTop() override {}

        void drawBottom() override
        {
            int h = panelH();
            panel(X, Y, W, h, T::Deck, T::FrameEdge);
            vgrad(X + 1, Y + 1, W - 2, 18, T::Grid, T::Deck);
            text(X + 8, Y + 4, title, T::Ink, Left, 1, W - 16);
            int n = int(options.size());
            for (int i = 0; i < std::min(n - first, VISIBLE); i++)
            {
                const auto& o = options[first + i];
                bool f        = first + i == focus;
                int y         = rowY(i);
                if (f)
                {
                    panelGrad(X + 4, y, W - 8, ROW - 2, T::ChipTop, T::ChipBottom, T::Cyan);
                }
                else
                {
                    rect(X + 4, y, W - 8, ROW - 2, T::Band);
                }
                text(X + 10, y + 3, o.label, o.enabled ? (f ? T::Ink : T::Value) : T::SubInk, Left, 1, W - 24);
            }
            widgets::scrollbar(X + W - 5, Y + 22, VISIBLE * ROW, first, VISIBLE, n);
            if (focus < n && !options[focus].detail.empty())
            {
                wrap(X + 8, Y + h - 28, W - 16, options[focus].detail, T::InkSoft, 1, 2);
            }
            widgets::footer(BOTTOM_W, {{"A", "Choose"}, {"B", "Back"}});
        }

    private:
        static constexpr int X = 24, Y = 22, W = 272, ROW = 20, VISIBLE = 8;
        std::string title;
        std::vector<app::Option> options;
        int focus = 0, first = 0, scroll = 0;

        bool anyDetail() const
        {
            return std::any_of(options.begin(), options.end(), [](const app::Option& o) { return !o.detail.empty(); });
        }

        int panelH() const
        {
            int rows = std::min(int(options.size()), VISIBLE);
            return 24 + rows * ROW + (anyDetail() ? 32 : 4);
        }

        int rowY(int i) const { return Y + 22 + i * ROW; }

        void keepVisible()
        {
            if (focus < first)
            {
                first = focus;
            }
            if (focus >= first + VISIBLE)
            {
                first = focus - VISIBLE + 1;
            }
        }

        void choose()
        {
            if (focus >= int(options.size()) || !options[focus].enabled)
            {
                return;
            }
            auto action = options[focus].action;
            app::pop();
            if (action)
            {
                action();
            }
        }
    };

    // A question with two answers.
    class Confirm : public Screen
    {
    public:
        Confirm(std::string message, std::function<void()> yes, std::string yesLabel, std::string noLabel)
            : message(std::move(message)), yes(std::move(yes)), yesLabel(std::move(yesLabel)), noLabel(std::move(noLabel))
        {
        }

        bool overlay() const override { return true; }

        void update(Input& in) override
        {
            if (in.down & (KEY_DLEFT | KEY_DRIGHT))
            {
                focusYes = !focusYes;
            }
            if (in.down & KEY_B)
            {
                app::pop();
                return;
            }
            bool tapYes = in.tapped(X + 12, buttonY(), 110, 26);
            bool tapNo  = in.tapped(X + W - 122, buttonY(), 110, 26);
            if (tapYes || ((in.down & KEY_A) && focusYes))
            {
                auto action = yes;
                app::pop();
                if (action)
                {
                    action();
                }
            }
            else if (tapNo || ((in.down & KEY_A) && !focusYes))
            {
                app::pop();
            }
        }

        void drawTop() override {}

        void drawBottom() override
        {
            panel(X, Y, W, H, T::Deck, T::FrameEdge);
            wrap(X + 12, Y + 12, W - 24, message, T::Ink, 1, 6);
            widgets::buttonFace(X + 12, buttonY(), 110, 26, yesLabel, focusYes, false);
            widgets::buttonFace(X + W - 122, buttonY(), 110, 26, noLabel, !focusYes, false);
            widgets::footer(BOTTOM_W, {{"A", "Choose"}, {"B", "Cancel"}});
        }

    private:
        static constexpr int X = 24, Y = 50, W = 272, H = 130;
        std::string message;
        std::function<void()> yes;
        std::string yesLabel, noLabel;
        bool focusYes = true;

        int buttonY() const { return Y + H - 36; }
    };

    class Message : public Screen
    {
    public:
        Message(std::string title, std::string body) : title(std::move(title)), body(std::move(body)) {}

        bool overlay() const override { return true; }

        void update(Input& in) override
        {
            if ((in.down & (KEY_A | KEY_B)) || in.tapped(X + W / 2 - 50, Y + H - 34, 100, 24))
            {
                app::pop();
            }
        }

        void drawTop() override {}

        void drawBottom() override
        {
            panel(X, Y, W, H, T::Deck, T::FrameEdge);
            vgrad(X + 1, Y + 1, W - 2, 18, T::Grid, T::Deck);
            text(X + 8, Y + 4, title, T::Ink, Left, 1, W - 16);
            wrap(X + 10, Y + 26, W - 20, body, T::Value, 1, 8);
            widgets::buttonFace(X + W / 2 - 50, Y + H - 34, 100, 24, "OK", true, false);
        }

    private:
        static constexpr int X = 16, Y = 30, W = 288, H = 170;
        std::string title, body;
    };
}

namespace app
{
    void push(std::unique_ptr<Screen> s)
    {
        audio::sfx(audio::Sfx::Open);
        incoming.push_back(std::move(s));
    }

    void pop()
    {
        audio::sfx(audio::Sfx::Back);
        pops++;
    }

    void toast(const std::string& message)
    {
        toastText  = message;
        toastUntil = frames + 150;
    }

    void quit() { quitting = true; }

    int frame() { return frames; }

    Bank& bank() { return theBank; }

    FILE* perfLog = nullptr;

    void perfBegin()
    {
        if (FILE* f = fopen("sdmc:/3ds/PKForge/perf.on", "r"))
        {
            fclose(f);
            perfLog = fopen("sdmc:/3ds/PKForge/perf.log", "w");
        }
    }

    Timed::Timed(const char* w) : what(w), start(svcGetSystemTick()) {}

    Timed::~Timed()
    {
        double ms = double(svcGetSystemTick() - start) / (SYSCLOCK_ARM11 / 1000.0);
        if (perfLog && ms > 4)
        {
            fprintf(perfLog, "  slow: %s %.1f ms\n", what, ms);
        }
    }

    // Frame timings, written once a second to sdmc:/3ds/PKForge/perf.log while sdmc:/3ds/PKForge/perf.on
    // exists (on the console too): the logic, the CPU side of drawing, the GPU's drawing time.
    struct Perf
    {
        bool on = false;
        FILE* log = nullptr;
        double update = 0, draw = 0, gpu = 0, worst = 0;
        int count = 0;

        static double ms(u64 ticks) { return double(ticks) / (SYSCLOCK_ARM11 / 1000.0); }

        void add(u64 u, u64 d)
        {
            double a = ms(u), b = ms(d), g = C3D_GetDrawingTime();
            update += a;
            draw += b;
            gpu += g;
            worst = std::max(worst, a + b);
            if (++count == 60)
            {
                fprintf(log, "frame %d  screens %d  update %.2f  draw %.2f  gpu %.2f  worst cpu %.2f ms\n", frames,
                    int(stack.size()), update / count, draw / count, gpu / count, worst);
                fflush(log);
                update = draw = gpu = worst = 0;
                count  = 0;
            }
        }
    };

    void run()
    {
        applyChanges();
        Input in;
        Perf perf;
        perf.log = perfLog;
        perf.on  = perf.log != nullptr;
        while (aptMainLoop() && !quitting && !stack.empty())
        {
            u64 t0 = svcGetSystemTick();
            in.scan();
            if (autotest::wantsQuit())
            {
                break;
            }
            stack.back()->update(in);
            applyChanges();
            if (stack.empty())
            {
                break;
            }
            u64 t1 = svcGetSystemTick();

            gfx::beginFrame();
            u64 t2 = svcGetSystemTick();
            int base = int(stack.size()) - 1;
            while (base > 0 && stack[base]->overlay())
            {
                base--;
            }
            gfx::top();
            for (int i = base; i < int(stack.size()); i++)
            {
                stack[i]->drawTop();
            }
            gfx::bottom();
            for (int i = base; i < int(stack.size()); i++)
            {
                if (i > base)
                {
                    widgets::dim(BOTTOM_W);
                }
                stack[i]->drawBottom();
            }
            if (frames < toastUntil && !toastText.empty())
            {
                float w = std::min(300.0f, textWidth(toastText) + 20);
                panel((BOTTOM_W - w) / 2, 190, w, 22, T::Void, T::Cyan);
                text(BOTTOM_W / 2, 195, toastText, T::Ink, Center, 1, w - 12);
            }
            u64 t3 = svcGetSystemTick();
            gfx::endFrame();
            if (perf.on)
            {
                perf.add(t1 - t0, t3 - t2);
            }
            frames++;
        }
        if (perf.log)
        {
            perfLog = nullptr;
            fclose(perf.log);
        }
        stack.clear();
    }

    bool keyboard(const std::string& hint, std::string& value, int maxLength)
    {
        if (autotest::active())
        {
            std::string scripted;
            if (!autotest::takeText(scripted))
            {
                return false;
            }
            value = scripted;
            return true;
        }
        SwkbdState kb;
        swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, maxLength);
        swkbdSetHintText(&kb, hint.c_str());
        swkbdSetInitialText(&kb, value.c_str());
        swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        std::vector<char> buf(maxLength * 4 + 1, 0);
        if (swkbdInputText(&kb, buf.data(), buf.size()) != SWKBD_BUTTON_CONFIRM)
        {
            return false;
        }
        value = buf.data();
        return true;
    }

    bool numpad(const std::string& hint, int& value, int min, int max)
    {
        std::string entered;
        if (autotest::active())
        {
            if (!autotest::takeText(entered))
            {
                return false;
            }
        }
        else
        {
            int digits = std::max(1, int(std::to_string(std::max(std::abs(min), std::abs(max))).size()));
            SwkbdState kb;
            swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, digits);
            swkbdSetHintText(&kb, hint.c_str());
            swkbdSetInitialText(&kb, std::to_string(value).c_str());
            swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
            char buf[16] = {};
            if (swkbdInputText(&kb, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
            {
                return false;
            }
            entered = buf;
        }
        if (entered.empty())
        {
            return false;
        }
        value = std::clamp(atoi(entered.c_str()), min, max);
        return true;
    }

    void menu(const std::string& title, std::vector<Option> options)
    {
        if (!options.empty())
        {
            push(std::make_unique<Menu>(title, std::move(options)));
        }
    }

    void confirm(const std::string& message, std::function<void()> yes, const std::string& yesLabel,
        const std::string& noLabel)
    {
        push(std::make_unique<Confirm>(message, std::move(yes), yesLabel, noLabel));
    }

    void message(const std::string& title, const std::string& text) { push(std::make_unique<Message>(title, text)); }
}
