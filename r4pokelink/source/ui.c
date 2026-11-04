#include "ui.h"
#include "font.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

static u16 backTop[UI_W * UI_H] __attribute__((aligned(32)));
static u16 backBottom[UI_W * UI_H] __attribute__((aligned(32)));
static u16* vramTop;
static u16* vramBottom;

static u16* buffer(UiScreen s) { return s == UI_TOP ? backTop : backBottom; }

void uiInit(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    vramTop    = bgGetGfxPtr(bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0));
    vramBottom = bgGetGfxPtr(bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0));
}

void uiPresent(void)
{
    DC_FlushRange(backTop, sizeof(backTop));
    DC_FlushRange(backBottom, sizeof(backBottom));
    swiWaitForVBlank();
    // Each screen's bitmap is 256 wide, so the 192 rows sit together in VRAM.
    dmaCopy(backTop, vramTop, sizeof(backTop));
    dmaCopy(backBottom, vramBottom, sizeof(backBottom));
}

void uiClear(UiScreen s, u16 color)
{
    u16* p = buffer(s);
    for (int i = 0; i < UI_W * UI_H; i++)
    {
        p[i] = color;
    }
}

void uiRect(UiScreen s, int x, int y, int w, int h, u16 color)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > UI_W)
    {
        w = UI_W - x;
    }
    if (y + h > UI_H)
    {
        h = UI_H - y;
    }
    if (w <= 0 || h <= 0)
    {
        return;
    }
    u16* p = buffer(s) + y * UI_W + x;
    for (int row = 0; row < h; row++, p += UI_W)
    {
        for (int col = 0; col < w; col++)
        {
            p[col] = color;
        }
    }
}

void uiHousing(UiScreen s)
{
    uiClear(s, COL_DECK);
    for (int x = 11; x < UI_W; x += 12)
    {
        uiRect(s, x, 0, 1, UI_H, COL_GRIDLINE);
    }
    for (int y = 11; y < UI_H; y += 12)
    {
        uiRect(s, 0, y, UI_W, 1, COL_GRIDLINE);
    }
}

void uiPanel(UiScreen s, int x, int y, int w, int h, u16 fill, u16 rim)
{
    // Rim with the corner pixels cut, as PKForge's pixel panels.
    uiRect(s, x + 2, y, w - 4, 1, rim);
    uiRect(s, x + 2, y + h - 1, w - 4, 1, rim);
    uiRect(s, x, y + 2, 1, h - 4, rim);
    uiRect(s, x + w - 1, y + 2, 1, h - 4, rim);
    uiRect(s, x + 1, y + 1, 1, 1, rim);
    uiRect(s, x + w - 2, y + 1, 1, 1, rim);
    uiRect(s, x + 1, y + h - 2, 1, 1, rim);
    uiRect(s, x + w - 2, y + h - 2, 1, 1, rim);
    uiRect(s, x + 2, y + 1, w - 4, h - 2, fill);
    uiRect(s, x + 1, y + 2, 1, h - 4, fill);
    uiRect(s, x + w - 2, y + 2, 1, h - 4, fill);
}

static u32 nextCode(const char** p)
{
    const u8* s = (const u8*)*p;
    u32 c       = *s++;
    if (c >= 0xC0 && c < 0xE0 && (*s & 0xC0) == 0x80)
    {
        c = ((c & 0x1F) << 6) | (*s++ & 0x3F);
    }
    else if (c >= 0xE0 && c < 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80)
    {
        c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        s += 2;
    }
    *p = (const char*)s;
    return c;
}

static const FontGlyph* glyph(u32 code)
{
    for (unsigned i = 0; i < FONT_GLYPHS; i++)
    {
        if (fontGlyphs[i].code == code)
        {
            return &fontGlyphs[i];
        }
    }
    // Letters with accents the table lacks fall back to their base letter, then to '?'.
    if (code >= 0xC0 && code <= 0xFF)
    {
        static const char base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        return glyph((u8)base[code - 0xC0]);
    }
    return code == '?' ? &fontGlyphs[0] : glyph('?');
}

int uiTextWidth(const char* utf8)
{
    int w = 0;
    while (*utf8)
    {
        w += glyph(nextCode(&utf8))->advance;
    }
    return w;
}

static int drawGlyph(UiScreen s, int x, int y, const FontGlyph* g, u16 color)
{
    u16* p = buffer(s);
    for (int row = 0; row < FONT_HEIGHT; row++)
    {
        int py = y + row;
        if (py < 0 || py >= UI_H || !g->rows[row])
        {
            continue;
        }
        for (int col = 0; col < 16; col++)
        {
            if ((g->rows[row] >> col) & 1)
            {
                int px = x + g->left + col;
                if (px >= 0 && px < UI_W)
                {
                    p[py * UI_W + px] = color;
                }
            }
        }
    }
    return g->advance;
}

int uiText(UiScreen s, int x, int y, const char* utf8, u16 color, int maxWidth)
{
    int full = uiTextWidth(utf8);
    bool cut = maxWidth > 0 && full > maxWidth;
    int dots = cut ? uiTextWidth("...") : 0;
    int cx   = x;
    while (*utf8)
    {
        const FontGlyph* g = glyph(nextCode(&utf8));
        if (cut && cx + g->advance + dots > x + maxWidth)
        {
            break;
        }
        cx += drawGlyph(s, cx, y, g, color);
    }
    if (cut)
    {
        for (const char* d = "..."; *d; d++)
        {
            cx += drawGlyph(s, cx, y, glyph(*d), color);
        }
    }
    return cx - x;
}

void uiTextCentered(UiScreen s, int cx, int y, const char* utf8, u16 color)
{
    uiText(s, cx - uiTextWidth(utf8) / 2, y, utf8, color, 0);
}

int uiWrap(UiScreen s, int x, int y, int width, const char* utf8, u16 color, int maxLines)
{
    char line[256] = "";
    int lines = 0, cy = y;
    const char* p = utf8;
    while (*p)
    {
        // The next word, newline included as a break.
        const char* end = p;
        while (*end && *end != ' ' && *end != '\n')
        {
            end++;
        }
        char word[96];
        int len = end - p < 95 ? end - p : 95;
        memcpy(word, p, len);
        word[len] = 0;
        char candidate[256];
        snprintf(candidate, sizeof(candidate), "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && uiTextWidth(candidate) > width)
        {
            uiText(s, x, cy, line, color, width);
            cy += 12;
            if (maxLines && ++lines >= maxLines)
            {
                return cy - y;
            }
            snprintf(line, sizeof(line), "%s", word);
        }
        else
        {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        if (*end == '\n')
        {
            uiText(s, x, cy, line, color, width);
            cy += 12;
            line[0] = 0;
            if (maxLines && ++lines >= maxLines)
            {
                return cy - y;
            }
        }
        p = *end ? end + 1 : end;
    }
    if (line[0])
    {
        uiText(s, x, cy, line, color, width);
        cy += 12;
    }
    return cy - y;
}

int uiKeyHint(UiScreen s, int x, int y, const char* key, const char* label)
{
    int kw = uiTextWidth(key) + 7;
    if (kw < 13)
    {
        kw = 13;
    }
    // A pill of rows, rounder at the ends.
    static const u8 inset[13] = {4, 2, 1, 1, 0, 0, 0, 0, 0, 1, 1, 2, 4};
    for (int row = 0; row < 13; row++)
    {
        uiRect(s, x + inset[row], y + row, kw - 2 * inset[row], 1, COL_KEY);
    }
    uiText(s, x + (kw - uiTextWidth(key)) / 2, y - 1, key, COL_VOID, 0);
    int lw = uiText(s, x + kw + 3, y - 1, label, COL_INK, 0);
    return kw + 3 + lw;
}

void uiCheckbox(UiScreen s, int x, int y, bool on, bool enabled)
{
    u16 rim = enabled ? COL_CYAN : COL_SUBINK;
    uiPanel(s, x, y, 12, 12, on ? COL_GRID : COL_BAND, rim);
    if (on)
    {
        // A tick.
        static const u8 tick[][2] = {{2, 6}, {3, 7}, {4, 8}, {5, 7}, {6, 6}, {7, 5}, {8, 4}, {9, 3}};
        for (unsigned i = 0; i < sizeof(tick) / sizeof(tick[0]); i++)
        {
            uiRect(s, x + tick[i][0], y + tick[i][1], 1, 2, COL_INK);
        }
    }
}
