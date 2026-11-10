// Drawing for PKForge 3DS: the PKForge chrome (theme tokens from PKForge.Chrome's default
// theme), the NDS12 pixel font, the UI atlas, Showdown box icons and front sprites.
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <string_view>

namespace gfx
{
    constexpr int TOP_W = 400, BOTTOM_W = 320, H = 240;

    constexpr u32 rgb(u8 r, u8 g, u8 b, u8 a = 255) { return r | (g << 8) | (b << 16) | (u32(a) << 24); }
    u32 alpha(u32 color, u8 a);
    u32 mix(u32 a, u32 b, float t);

    // The palette: PKForge's default theme (PKForge.Chrome/ColorThemes.g.cs, "default") until gfx::theme()
    // switches to another of the original's colour themes.
    namespace T
    {
        inline u32 Void = rgb(0x14, 0x12, 0x1D), Deep = rgb(0x17, 0x1B, 0x32), Deck = rgb(0x1B, 0x24, 0x47),
                      Grid = rgb(0x2B, 0x4E, 0x95), Blue = rgb(0x27, 0x89, 0xCD), Cyan = rgb(0x42, 0xBF, 0xE8),
                      Ink = rgb(0xF4, 0xF8, 0xFF), InkSoft = rgb(0xA8, 0xBA, 0xDC), Bright = rgb(0xFF, 0xFF, 0xFF),
                      Well = rgb(0x04, 0x1F, 0x46), WellEdge = rgb(0x08, 0x37, 0x6E), Frame = rgb(0x1B, 0x23, 0x46),
                      FrameEdge = rgb(0x23, 0x56, 0x9F), BannerTop = rgb(0x73, 0x8A, 0xB8),
                      BannerBottom = rgb(0x25, 0x53, 0x96), PoolLight = rgb(0x78, 0xC8, 0xFF),
                      WallMid = rgb(0x0E, 0x33, 0x68), WallLight = rgb(0x1E, 0x58, 0x92),
                      Label = rgb(0x23, 0x60, 0xB0), Value = rgb(0xC6, 0xD2, 0xEE), Rim = rgb(0x16, 0xB6, 0xDC),
                      RimFill = rgb(0x04, 0x28, 0x56), SubInk = rgb(0x60, 0x7E, 0xBA), ChipInk = rgb(0xD2, 0xF0, 0xFF),
                      ChipTop = rgb(0x14, 0x46, 0x82), ChipBottom = rgb(0x08, 0x28, 0x5A), Band = rgb(0x02, 0x11, 0x2B),
                      ToolFill = rgb(0x25, 0x53, 0x9A), ToolEdge = rgb(0x4C, 0x7C, 0xC4),
                      PanelFill = rgb(0x04, 0x24, 0x4E), LabelColumn = rgb(0x06, 0x19, 0x39),
                      LabelInk = rgb(0x28, 0x46, 0x82), ValueInk = rgb(0x96, 0xA8, 0xD2), CardFill = rgb(0x0C, 0x14, 0x2C),
                      CardRow = rgb(0x19, 0x24, 0x47), SpeciesFill = rgb(0x10, 0x89, 0xB6),
                      SpeciesEdge = rgb(0x5A, 0xD2, 0xF0), SpeciesInk = rgb(0xC8, 0xF0, 0xFF),
                      NameFrame = rgb(0x1D, 0x22, 0x44), Shadow = rgb(0x06, 0x08, 0x14), TabTop = rgb(0x1E, 0x48, 0x86),
                      TabBottom = rgb(0x10, 0x30, 0x64), TabActiveTop = rgb(0x7E, 0x9C, 0xD6),
                      TabActiveBottom = rgb(0x2C, 0x5A, 0xA6), PartyRim = rgb(0x6C, 0xDE, 0xF6),
                      PartyRimDark = rgb(0x0C, 0x1A, 0x34), PartyBody = rgb(0x2A, 0x4C, 0x7E),
                      PartyBand = rgb(0x36, 0x5E, 0x96), PartySelected = rgb(0x2E, 0x6C, 0xB8),
                      PartySelectedBand = rgb(0x3C, 0x82, 0xCC);
    }

    // The Android PKForge's colour themes (Default, then one per type), kept in sdmc:/3ds/PKForge/theme.txt.
    int themeCount();
    const char* themeName(int i);
    int theme();
    void theme(int i);
    void loadTheme();

    // Signals with a meaning in the games: never themed (PKForge.Chrome/Pksm.cs).
    namespace S
    {
        constexpr u32 Legal = rgb(0x54, 0xD6, 0x8A), Illegal = rgb(0xF0, 0x68, 0x68),
                      ShinyGold = rgb(0xF2, 0xC1, 0x4E), CursorRed = rgb(0xF0, 0x68, 0x68),
                      CursorGreen = rgb(0x54, 0xD6, 0x8A), Male = rgb(0x42, 0xBF, 0xE8), Female = rgb(0xF0, 0x7A, 0x9B),
                      BandInfo = rgb(0xB0, 0x4E, 0x5C), BandStats = rgb(0x2E, 0x72, 0xC2),
                      BandMoves = rgb(0xB8, 0x74, 0x34), BandOrigin = rgb(0x26, 0x86, 0x88),
                      BandLegal = rgb(0x38, 0x8A, 0x58), GiftPink = rgb(0x45, 0x24, 0x46);
    }

    u32 typeColor(int type);
    u32 inkOn(u32 fill);

    bool init();
    // Which Showdown sprite and icon each species/form uses; start-up loads it on its loader thread.
    void loadSpriteIndex();
    void exit();
    void beginFrame();
    void endFrame();
    void top();
    void bottom();
    // Clips drawing to a rectangle on the current screen (screen coordinates); none() resets.
    void clip(int x, int y, int w, int h);
    void unclip();

    // ---- primitives
    void rect(float x, float y, float w, float h, u32 c);
    void vgrad(float x, float y, float w, float h, u32 topColor, u32 bottomColor);
    void hgrad(float x, float y, float w, float h, u32 leftColor, u32 rightColor);
    void outline(float x, float y, float w, float h, u32 c, float t = 1);
    // A pixel-art panel: chamfered corners, fill, 1px rim.
    void panel(float x, float y, float w, float h, u32 fill, u32 rim);
    void panelGrad(float x, float y, float w, float h, u32 fillTop, u32 fillBottom, u32 rim);
    void circle(float x, float y, float r, u32 c);
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c);
    // The housing: Deck with PKForge's pixel grid.
    void housing(float x, float y, float w, float h);

    // ---- text (NDS12 at its native pixel size; scale 2 for headings)
    enum Align
    {
        Left,
        Center,
        Right
    };
    constexpr float LINE = 13;
    float textWidth(std::string_view s, float scale = 1);
    // Draws text with its top at y. maxWidth > 0 shortens it with an ellipsis. Returns the width drawn.
    float text(float x, float y, std::string_view s, u32 color, Align align = Left, float scale = 1, float maxWidth = 0);
    // Wrapped text; returns the height used.
    float wrap(float x, float y, float width, std::string_view s, u32 color, float scale = 1, int maxLines = 0);
    std::string ellipsize(std::string_view s, float maxWidth, float scale = 1);

    // ---- images
    C2D_Image ui(int index);
    void image(int uiIndex, float x, float y, float scale = 1);
    void imageTinted(int uiIndex, float x, float y, u32 color, float blend = 1, float scale = 1);
    void imageCentered(int uiIndex, float cx, float cy, float scale = 1);

    // Box icon from Showdown's sheet; false when this species/form has none.
    // Drawn with its 40x30 cell centred at (cx, cy).
    bool icon(int species, int form, float cx, float cy, float scale = 1, u32 tint = 0, float blend = 0);
    // Front sprite (96x96) centred at (cx, cy); false when missing.
    bool front(int species, int form, bool shiny, bool female, float cx, float cy, float scale = 1);
    // Cartridge art for a PKForge game-art name ("firered"); false when none.
    bool cartridge(std::string_view artName, float cx, float cy, float scale = 1);
    // A live RGB565 picture up to 400x240 (the camera's), uploaded each call and drawn at (x, y).
    void cameraImage(const u16* pixels, int w, int h, float x, float y);

    // Pixel-art glyphs drawn from primitives.
    void keyDisc(float x, float y, std::string_view key); // the footer's key discs (A, B, X, Y, L, R, +, -)
    float keyDiscWidth(std::string_view key);
    void chevron(float x, float y, u32 c, bool left);
    void star(float cx, float cy, float r, u32 c);
    void genderMark(float x, float y, int gender); // 0 male, 1 female, 2 none

    // Saves both screens of the last presented frame as one BMP (testing aid).
    void requestShot(const std::string& path);
}
