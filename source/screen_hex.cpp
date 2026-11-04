// Hex editor: a Pokémon's decrypted bytes, 8 to a row. A edits the byte under the cursor; Y keeps the
// result (with its checksum redone, or the game would call it a Bad Egg); B leaves it as it was.
#include "gfx.hpp"
#include "screens.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace gfx;

namespace
{
    constexpr int COLS = 8, ROWS = 11, LIST_Y = 26, ROW_H = 16;

    class Hex : public Screen
    {
    public:
        Hex(std::unique_ptr<pksm::PKX> pk, std::string where, std::function<void(std::unique_ptr<pksm::PKX>)> done)
            : pk(std::move(pk)), where(std::move(where)), done(std::move(done))
        {
            auto raw = this->pk->rawData();
            bytes.assign(raw.begin(), raw.end());
            original = bytes;
        }

        void update(Input& in) override
        {
            int n = int(bytes.size());
            if (in.repeat & KEY_DLEFT)
            {
                cursor = std::max(0, cursor - 1);
            }
            if (in.repeat & KEY_DRIGHT)
            {
                cursor = std::min(n - 1, cursor + 1);
            }
            if (in.repeat & KEY_DUP)
            {
                cursor = std::max(0, cursor - COLS);
            }
            if (in.repeat & KEY_DDOWN)
            {
                cursor = std::min(n - 1, cursor + COLS);
            }
            if (in.repeat & KEY_L)
            {
                cursor = std::max(0, cursor - COLS * ROWS);
            }
            if (in.repeat & KEY_R)
            {
                cursor = std::min(n - 1, cursor + COLS * ROWS);
            }
            for (int r = 0; r < ROWS; r++)
            {
                for (int c = 0; c < COLS; c++)
                {
                    int i = (first + r) * COLS + c;
                    if (i < n && in.tapped(cellX(c) - 2, LIST_Y + r * ROW_H, 24, ROW_H - 1))
                    {
                        if (cursor == i)
                        {
                            editByte();
                        }
                        cursor = i;
                    }
                }
            }
            int row = cursor / COLS;
            if (row < first)
            {
                first = row;
            }
            if (row >= first + ROWS)
            {
                first = row - ROWS + 1;
            }
            if (in.down & KEY_A)
            {
                editByte();
            }
            if (in.down & KEY_Y)
            {
                finish();
                return;
            }
            if (in.down & KEY_B)
            {
                if (bytes == original)
                {
                    app::pop();
                    return;
                }
                app::confirm("Leave the hex editor without keeping the changes?", [] { app::pop(); }, "Leave", "Stay");
            }
        }

        void drawTop() override
        {
            housing(0, 0, TOP_W, H);
            text(12, 8, "Hex editor", T::Ink);
            text(TOP_W - 12, 8, where, T::SubInk, Right, 1, 240);
            panel(12, 30, TOP_W - 24, 120, T::PanelFill, T::FrameEdge);
            u8 v = bytes[cursor];
            char line[96];
            snprintf(line, sizeof(line), "Offset 0x%03X (%d) of %d bytes", cursor, cursor, int(bytes.size()));
            text(24, 40, line, T::Value);
            char bits[9];
            for (int b = 0; b < 8; b++)
            {
                bits[b] = (v >> (7 - b)) & 1 ? '1' : '0';
            }
            bits[8] = 0;
            snprintf(line, sizeof(line), "0x%02X   %u   %s", v, v, bits);
            text(24, 64, line, T::Ink, Left, 2);
            if (cursor + 1 < int(bytes.size()))
            {
                u16 word = u16(bytes[cursor] | bytes[cursor + 1] << 8);
                snprintf(line, sizeof(line), "u16 here: %u (0x%04X)", word, word);
                text(24, 100, line, T::InkSoft);
            }
            if (cursor + 3 < int(bytes.size()))
            {
                u32 dword = u32(bytes[cursor] | bytes[cursor + 1] << 8 | bytes[cursor + 2] << 16 | u32(bytes[cursor + 3]) << 24);
                snprintf(line, sizeof(line), "u32 here: %lu (0x%08lX)", (unsigned long)dword, (unsigned long)dword);
                text(24, 118, line, T::InkSoft);
            }
            int changed = 0;
            for (size_t i = 0; i < bytes.size(); i++)
            {
                changed += bytes[i] != original[i];
            }
            snprintf(line, sizeof(line), "%d byte%s changed", changed, changed == 1 ? "" : "s");
            text(24, 170, line, changed ? T::Value : T::SubInk);
            wrap(24, 192, TOP_W - 48, "Y keeps it with a fresh checksum. Wrong bytes can make a Bad Egg: the "
                                      "save's restore point is kept anyway.",
                T::SubInk, 1, 3);
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::header(BOTTOM_W, "Hex editor", std::to_string(bytes.size()) + " bytes");
            int n = int(bytes.size());
            for (int r = 0; r < ROWS && (first + r) * COLS < n; r++)
            {
                int y = LIST_Y + r * ROW_H;
                char off[8];
                snprintf(off, sizeof(off), "%03X", (first + r) * COLS);
                text(6, y + 2, off, T::SubInk);
                for (int c = 0; c < COLS; c++)
                {
                    int i = (first + r) * COLS + c;
                    if (i >= n)
                    {
                        break;
                    }
                    if (i == cursor)
                    {
                        rect(cellX(c) - 3, y, 24, ROW_H - 1, T::Cyan);
                    }
                    char cell[4];
                    snprintf(cell, sizeof(cell), "%02X", bytes[i]);
                    text(cellX(c), y + 2, cell, i == cursor ? T::Void : bytes[i] != original[i] ? T::Value : T::Ink);
                }
            }
            widgets::scrollbar(BOTTOM_W - 6, LIST_Y, ROWS * ROW_H, first, ROWS, (n + COLS - 1) / COLS);
            widgets::footer(BOTTOM_W, {{"A", "Edit"}, {"Y", "Keep"}, {"LR", "Page"}, {"B", "Back"}});
        }

    private:
        std::unique_ptr<pksm::PKX> pk;
        std::string where;
        std::function<void(std::unique_ptr<pksm::PKX>)> done;
        std::vector<u8> bytes, original;
        int cursor = 0, first = 0;

        static int cellX(int c) { return 40 + c * 34; }

        void editByte()
        {
            char hex[4];
            snprintf(hex, sizeof(hex), "%02X", bytes[cursor]);
            std::string value = hex;
            char label[48];
            snprintf(label, sizeof(label), "Byte 0x%03X in hex (00-FF)", cursor);
            if (!app::keyboard(label, value, 2) || value.empty())
            {
                return;
            }
            char* end = nullptr;
            long v    = strtol(value.c_str(), &end, 16);
            if (*end || v < 0 || v > 255)
            {
                app::toast("Not a hex byte: " + value);
                return;
            }
            bytes[cursor] = u8(v);
        }

        void finish()
        {
            auto edited = pksm::PKX::getPKM(pk->generation(), bytes.data(), bytes.size());
            if (!edited)
            {
                app::message("Hex editor", "Those bytes are not a Pokémon of this format.");
                return;
            }
            edited->refreshChecksum();
            auto cb = std::move(done);
            app::pop();
            cb(std::move(edited));
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> hex(std::unique_ptr<pksm::PKX> pk, std::string where,
        std::function<void(std::unique_ptr<pksm::PKX>)> done)
    {
        return std::make_unique<Hex>(std::move(pk), std::move(where), std::move(done));
    }
}
