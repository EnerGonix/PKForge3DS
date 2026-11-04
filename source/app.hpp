// The screen stack, the main loop and the things every screen shares.
#pragma once

#include "input.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Bank;

class Screen
{
public:
    virtual ~Screen() = default;
    virtual void update(Input& in) = 0;
    virtual void drawTop()         = 0;
    virtual void drawBottom()      = 0;
    // Overlays are drawn over the screen beneath them, which stays visible (dimmed).
    virtual bool overlay() const { return false; }
    // Called when the screen comes back to the top of the stack.
    virtual void resumed() {}
};

namespace app
{
    // Times its scope; with perf.on, calls slower than 4 ms are written to perf.log.
    // Opens perf.log when sdmc:/3ds/PKForge/perf.on exists (first thing in main, so start-up is timed too).
    void perfBegin();
    struct Timed
    {
        const char* what;
        u64 start;
        explicit Timed(const char* w);
        ~Timed();
    };

    void push(std::unique_ptr<Screen> s);
    // Removes the top screen once the current update ends.
    void pop();
    void toast(const std::string& message);
    void quit();
    void run();
    int frame();
    Bank& bank();

    // The system keyboard. False when cancelled.
    bool keyboard(const std::string& hint, std::string& value, int maxLength);
    bool numpad(const std::string& hint, int& value, int min, int max);

    // Common overlays.
    struct Option
    {
        std::string label;
        std::function<void()> action;
        std::string detail;
        bool enabled = true;
    };
    void menu(const std::string& title, std::vector<Option> options);
    void confirm(const std::string& message, std::function<void()> yes, const std::string& yesLabel = "Yes",
        const std::string& noLabel = "No");
    void message(const std::string& title, const std::string& text);
}
