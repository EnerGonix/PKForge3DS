// The title screen: PKForge's logo floating over the Android app's splash colour, a field of drifting
// pixel stars, and "touch to start". Any touch, A or START goes on to the save list beneath it.
#include "audio.hpp"
#include "gfx.hpp"
#include "screens.hpp"
#include "version.hpp"

#include <citro2d.h>
#include <cmath>

using namespace gfx;

namespace
{
    constexpr int STARS = 48, FADE = 16;

    struct Star
    {
        float x, y, speed;
        int phase;
    };

    class Title : public Screen
    {
    public:
        explicit Title(std::function<void()> r) : ready(std::move(r))
        {
            sheet = C2D_SpriteSheetLoad("romfs:/gfx/title.t3x");
            if (sheet)
            {
                logo = C2D_SpriteSheetGetImage(sheet, 0);
                C3D_TexSetFilter((C3D_Tex*)logo.tex, GPU_NEAREST, GPU_NEAREST);
            }
            audio::place(audio::Place::Title);
            for (int i = 0; i < STARS; i++)
            {
                stars[i] = {float(rand() % 720), float(rand() % 480), 0.08f + (rand() % 100) / 400.0f, rand() % 120};
            }
        }

        ~Title() override
        {
            if (sheet)
            {
                C2D_SpriteSheetFree(sheet);
            }
        }

        void update(Input& in) override
        {
            t++;
            for (auto& s : stars)
            {
                s.x -= s.speed;
                if (s.x < 0)
                {
                    s.x += 720;
                }
            }
            if (leaving > 0 && ++leaving > FADE)
            {
                app::pop();
                return;
            }
            if (leaving == 0 && ((in.down & (KEY_A | KEY_START)) || in.touchUp))
            {
                audio::sfx(audio::Sfx::Open);
                audio::place(audio::Place::Menu);
                leaving = 1;
                if (ready)
                {
                    ready(); // the start-up loads behind the title finish here (usually long done)
                    ready = nullptr;
                }
            }
        }

        void drawTop() override
        {
            sky(0, TOP_W);
            float bob = std::sin(t / 30.0f) * 4;
            // The logo's shadow on the ground, shrinking as it rises.
            float shade = 52 - bob * 2;
            for (int i = 0; i < 4; i++)
            {
                rect(110 - shade / 2 + i * 4, 226 + i, shade - i * 8, 2, rgb(0x04, 0x06, 0x10, 0x60));
            }
            if (sheet)
            {
                C2D_DrawImageAt(logo, 30, 10 + bob, 0.5f, nullptr, 0.8f, 0.8f);
            }
            // The name, with a cyan edge glowing slowly.
            float glow = 0.5f + 0.5f * std::sin(t / 40.0f);
            u8 a       = u8(90 + 120 * glow);
            text(292, 70, "PKForge", rgb(0x42, 0xBF, 0xE8, a), Center, 3);
            text(290, 68, "PKForge", T::Ink, Center, 3);
            text(290, 112, "for Nintendo 3DS", T::InkSoft, Center);
            rect(220, 132, 140, 1, T::Grid);
            text(290, 140, "Save editor  -  Bank  -  Poképark", T::SubInk, Center);
            text(290, 158, "3DS port by En3rGonix", T::InkSoft, Center);
            text(TOP_W - 6, 226, PKFORGE3DS_VERSION, T::SubInk, Right);
            fade(TOP_W);
        }

        void drawBottom() override
        {
            sky(400, BOTTOM_W);
            // A soft pulse behind the prompt.
            float pulse = 0.5f + 0.5f * std::sin(t / 18.0f);
            panel(40, 92, BOTTOM_W - 80, 44, rgb(0x04, 0x24, 0x4E, u8(140 + 80 * pulse)), T::Rim);
            text(BOTTOM_W / 2, 100, "Touch the screen", T::Ink, Center);
            text(BOTTOM_W / 2, 116, "or press A", rgb(0xA8, 0xBA, 0xDC, u8(120 + 135 * pulse)), Center);
            text(BOTTOM_W / 2, 218, "Not affiliated with Nintendo or Game Freak.", T::SubInk, Center);
            fade(BOTTOM_W);
        }

    private:
        std::function<void()> ready;
        C2D_SpriteSheet sheet = nullptr;
        C2D_Image logo{};
        Star stars[STARS];
        int t = 0, leaving = 0;

        // The splash's dark violet sliding into the deck blue, with the stars over both screens:
        // offset is where this screen starts in one 720-wide strip (the top 400, then the bottom 320).
        void sky(int offset, int width)
        {
            vgrad(0, 0, float(width), H, T::Void, T::Deep);
            for (auto& s : stars)
            {
                float x = s.x - offset, y = s.y * 0.5f;
                if (x < 0 || x >= width)
                {
                    continue;
                }
                int twinkle = (t + s.phase) % 120;
                u8 a        = u8(twinkle < 60 ? 80 + twinkle * 2 : 200 - (twinkle - 60) * 2);
                float size  = s.speed > 0.2f ? 2 : 1;
                rect(std::floor(x), std::floor(y), size, size, rgb(0xC6, 0xD2, 0xEE, a));
            }
        }

        void fade(int width)
        {
            if (leaving > 0)
            {
                rect(0, 0, float(width), H, rgb(0x14, 0x12, 0x1D, u8(255 * leaving / FADE)));
            }
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> title(std::function<void()> ready) { return std::make_unique<Title>(std::move(ready)); }
}
