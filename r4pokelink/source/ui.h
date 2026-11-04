// R4 Link's drawing: both DS screens as 16-bit bitmaps, drawn in RAM and shown on the next VBlank,
// in PKForge's colours with the NDS12 font.
#pragma once

#include <nds/ndstypes.h>

#define UI_W 256
#define UI_H 192

typedef enum
{
    UI_TOP,
    UI_BOTTOM
} UiScreen;

#define UI_RGB(r, g, b) ((u16)(((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10) | 0x8000))

// PKForge's default theme.
#define COL_VOID      UI_RGB(0x14, 0x12, 0x1D)
#define COL_DEEP      UI_RGB(0x17, 0x1B, 0x32)
#define COL_DECK      UI_RGB(0x1B, 0x24, 0x47)
#define COL_GRID      UI_RGB(0x2B, 0x4E, 0x95)
#define COL_GRIDLINE  UI_RGB(0x22, 0x37, 0x6A)
#define COL_CYAN      UI_RGB(0x42, 0xBF, 0xE8)
#define COL_INK       UI_RGB(0xF4, 0xF8, 0xFF)
#define COL_INKSOFT   UI_RGB(0xA8, 0xBA, 0xDC)
#define COL_SUBINK    UI_RGB(0x60, 0x7E, 0xBA)
#define COL_BAND      UI_RGB(0x02, 0x11, 0x2B)
#define COL_CHIPTOP   UI_RGB(0x14, 0x46, 0x82)
#define COL_FRAMEEDGE UI_RGB(0x23, 0x56, 0x9F)
#define COL_PANEL     UI_RGB(0x04, 0x24, 0x4E)
#define COL_LEGAL     UI_RGB(0x54, 0xD6, 0x8A)
#define COL_RED       UI_RGB(0xF0, 0x68, 0x68)
#define COL_GOLD      UI_RGB(0xF2, 0xC1, 0x4E)
#define COL_KEY       UI_RGB(0x6C, 0xC4, 0xEE)

void uiInit(void);
// Shows what was drawn since the last call (waits for VBlank).
void uiPresent(void);

void uiClear(UiScreen s, u16 color);
void uiHousing(UiScreen s); // the deck colour with PKForge's pixel grid
void uiRect(UiScreen s, int x, int y, int w, int h, u16 color);
void uiPanel(UiScreen s, int x, int y, int w, int h, u16 fill, u16 rim);
// Text with its cell top at y; cut with "..." past maxWidth (0: no limit). Returns the width drawn.
int uiText(UiScreen s, int x, int y, const char* utf8, u16 color, int maxWidth);
int uiTextWidth(const char* utf8);
void uiTextCentered(UiScreen s, int cx, int y, const char* utf8, u16 color);
// Word-wrapped text; returns the height used.
int uiWrap(UiScreen s, int x, int y, int width, const char* utf8, u16 color, int maxLines);
// The footer's key discs ("A", "B", "Y"...) with a label; returns the width used.
int uiKeyHint(UiScreen s, int x, int y, const char* key, const char* label);
void uiCheckbox(UiScreen s, int x, int y, bool on, bool enabled);
