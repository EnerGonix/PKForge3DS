#include "app.hpp"
#include "gfx.hpp"
#include "ui_ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfx
{
    namespace
    {
        C3D_RenderTarget* topTarget;
        C3D_RenderTarget* bottomTarget;
        C3D_RenderTarget* current;
        int currentWidth = TOP_W;
        C2D_Font font;
        float nativeScale = 1;
        int missingGlyph   = -1;
        C2D_TextBuf textBuf;
        C2D_SpriteSheet uiSheet;

        // Showdown's icon sheet, cut in pages by tools/assets.py.
        int iconPages = 0, iconPageRows = 34, iconColumns = 12, iconW = 40, iconH = 30;
        std::vector<C2D_SpriteSheet> iconSheets;
        std::vector<bool> iconTried;

        struct Showdown
        {
            std::string stem;
            bool front, shinyFront;
            int icon;
        };
        std::unordered_map<u32, Showdown> showdown;

        struct Cached
        {
            std::string key;
            C2D_SpriteSheet sheet;
        };
        std::list<Cached> frontCache;
        std::unordered_map<std::string, bool> exists;

        std::string shotPath;
        u8* shotTop    = nullptr;
        u8* shotBottom = nullptr;
        int shotState  = 0; // 1: frame queued, read it at the next frame start

        u32 key(int species, int form) { return (u32(species) << 8) | (u32(form) & 0xFF); }

        bool fileExists(const std::string& path)
        {
            auto found = exists.find(path);
            if (found != exists.end())
            {
                return found->second;
            }
            FILE* f   = fopen(path.c_str(), "rb");
            bool here = f != nullptr;
            if (f)
            {
                fclose(f);
            }
            exists.emplace(path, here);
            return here;
        }

        void loadShowdownIndex()
        {
            FILE* f = fopen("romfs:/data/showdownsprites.tsv", "r");
            if (!f)
            {
                return;
            }
            char line[256];
            while (fgets(line, sizeof(line), f))
            {
                if (line[0] == '#' || line[0] == '\n')
                {
                    continue;
                }
                int species, form, flags, icon;
                char stem[128];
                if (sscanf(line, "%d-%d\t%127s\t%d\t%d", &species, &form, stem, &flags, &icon) == 5)
                {
                    showdown[key(species, form)] = {stem, (flags & 1) != 0, (flags & 2) != 0, icon};
                }
            }
            fclose(f);

            f = fopen("romfs:/data/icons.txt", "r");
            if (f)
            {
                if (fscanf(f, "%d %d %d %d %d", &iconPages, &iconPageRows, &iconColumns, &iconW, &iconH) != 5)
                {
                    iconPages = 0;
                }
                fclose(f);
            }
            iconSheets.assign(iconPages, nullptr);
            iconTried.assign(iconPages, false);
        }

        const Showdown* lookup(int species, int form)
        {
            auto found = showdown.find(key(species, form));
            if (found == showdown.end() && form != 0)
            {
                found = showdown.find(key(species, 0));
            }
            return found == showdown.end() ? nullptr : &found->second;
        }

        void nearest(C2D_SpriteSheet sheet)
        {
            if (sheet && C2D_SpriteSheetCount(sheet) > 0)
            {
                C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
                C3D_TexSetFilter((C3D_Tex*)img.tex, GPU_NEAREST, GPU_NEAREST);
            }
        }

        C2D_SpriteSheet frontSheet(const std::string& path)
        {
            for (auto it = frontCache.begin(); it != frontCache.end(); ++it)
            {
                if (it->key == path)
                {
                    frontCache.splice(frontCache.begin(), frontCache, it);
                    return it->sheet;
                }
            }
            if (!fileExists(path))
            {
                return nullptr;
            }
            C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(path.c_str());
            if (!sheet)
            {
                return nullptr;
            }
            nearest(sheet);
            frontCache.push_front({path, sheet});
            while (frontCache.size() > 24)
            {
                C2D_SpriteSheetFree(frontCache.back().sheet);
                frontCache.pop_back();
            }
            return sheet;
        }

        // UTF-8 to code points, one at a time.
        u32 nextCodepoint(std::string_view s, size_t& i)
        {
            u8 c = s[i++];
            if (c < 0x80)
            {
                return c;
            }
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            u32 cp    = c & (0x3F >> extra);
            while (extra-- > 0 && i < s.size())
            {
                cp = (cp << 6) | (u8(s[i++]) & 0x3F);
            }
            return cp;
        }

        bool supported(std::string_view s)
        {
            for (size_t i = 0; i < s.size();)
            {
                u32 cp = nextCodepoint(s, i);
                if (cp >= 0x20 && C2D_FontGlyphIndexFromCodePoint(font, cp) == missingGlyph)
                {
                    return false;
                }
            }
            return true;
        }

        float glyphAdvance(C2D_Font f, u32 cp)
        {
            int glyph = C2D_FontGlyphIndexFromCodePoint(f, cp);
            charWidthInfo_s* info = C2D_FontGetCharWidthInfo(f, glyph);
            return info ? info->charWidth : 0;
        }

        // The system font stands in for code points NDS12 lacks (Japanese, Korean, Chinese).
        constexpr float SYSTEM_SCALE = 0.5f;
        float systemWidth(std::string_view s)
        {
            float w = 0;
            for (size_t i = 0; i < s.size();)
            {
                w += glyphAdvance(nullptr, nextCodepoint(s, i));
            }
            return w * SYSTEM_SCALE;
        }

        void writeShot()
        {
            const int W = TOP_W, Hh = H * 2;
            std::vector<u8> pixels(W * Hh * 3, 0);
            auto copy = [&](const u8* fb, int width, int ox, int oy)
            {
                for (int y = 0; y < H; y++)
                {
                    for (int x = 0; x < width; x++)
                    {
                        const u8* src = fb + ((x * H) + (H - 1 - y)) * 3;
                        u8* dst       = &pixels[((oy + y) * W + ox + x) * 3];
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                    }
                }
            };
            GSPGPU_InvalidateDataCache(shotTop, TOP_W * H * 3);
            GSPGPU_InvalidateDataCache(shotBottom, BOTTOM_W * H * 3);
            copy(shotTop, TOP_W, 0, 0);
            copy(shotBottom, BOTTOM_W, (TOP_W - BOTTOM_W) / 2, H);

            FILE* f = fopen(shotPath.c_str(), "wb");
            if (!f)
            {
                return;
            }
            u32 rowSize = W * 3, dataSize = rowSize * Hh;
            u8 header[54] = {'B', 'M'};
            auto put32 = [&](int at, u32 v) { memcpy(header + at, &v, 4); };
            put32(2, 54 + dataSize);
            put32(10, 54);
            put32(14, 40);
            put32(18, W);
            put32(22, Hh);
            header[26] = 1;
            header[28] = 24;
            put32(34, dataSize);
            fwrite(header, 1, 54, f);
            for (int y = Hh - 1; y >= 0; y--)
            {
                fwrite(&pixels[y * rowSize], 1, rowSize, f);
            }
            fclose(f);
        }
    }

    u32 alpha(u32 color, u8 a) { return (color & 0x00FFFFFF) | (u32(a) << 24); }

    u32 mix(u32 a, u32 b, float t)
    {
        auto ch = [&](int shift)
        {
            float x = ((a >> shift) & 0xFF) * (1 - t) + ((b >> shift) & 0xFF) * t;
            return u32(std::clamp(int(x + 0.5f), 0, 255)) << shift;
        };
        return ch(0) | ch(8) | ch(16) | ch(24);
    }

    u32 typeColor(int type)
    {
        static constexpr u32 colors[] = {rgb(0xA8, 0xA7, 0x7A), rgb(0xC2, 0x2E, 0x28), rgb(0xA9, 0x8F, 0xF3),
            rgb(0xA3, 0x3E, 0xA1), rgb(0xE2, 0xBF, 0x65), rgb(0xB6, 0xA1, 0x36), rgb(0xA6, 0xB9, 0x1A),
            rgb(0x73, 0x57, 0x97), rgb(0xB7, 0xB7, 0xCE), rgb(0xEE, 0x81, 0x30), rgb(0x63, 0x90, 0xF0),
            rgb(0x7A, 0xC7, 0x4C), rgb(0xF7, 0xD0, 0x2C), rgb(0xF9, 0x55, 0x87), rgb(0x96, 0xD9, 0xD6),
            rgb(0x6F, 0x35, 0xFC), rgb(0x70, 0x57, 0x46), rgb(0xD6, 0x85, 0xAD)};
        return type >= 0 && type < 18 ? colors[type] : T::Blue;
    }

    u32 inkOn(u32 fill)
    {
        float lum = (0.299f * (fill & 0xFF) + 0.587f * ((fill >> 8) & 0xFF) + 0.114f * ((fill >> 16) & 0xFF)) / 255;
        return lum >= 0.62f ? T::Void : T::Bright;
    }

    void loadSpriteIndex() { loadShowdownIndex(); }

    bool init()
    {
        C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
        C2D_Init(8192);
        C2D_Prepare();
        topTarget    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

        font = C2D_FontLoad("romfs:/font/nds12.bcfnt");
        if (!font)
        {
            return false;
        }
        C2D_FontSetFilter(font, GPU_NEAREST, GPU_NEAREST);
        FINF_s* info = C2D_FontGetInfo(font);
        nativeScale  = info->tglp->cellHeight / 30.0f;
        missingGlyph = info->alterCharIndex;
        textBuf      = C2D_TextBufNew(12000);

        uiSheet = C2D_SpriteSheetLoad("romfs:/gfx/ui.t3x");
        if (!uiSheet)
        {
            return false;
        }
        nearest(uiSheet);
        return true;
    }

    void exit()
    {
        for (auto& c : frontCache)
        {
            C2D_SpriteSheetFree(c.sheet);
        }
        frontCache.clear();
        for (auto sheet : iconSheets)
        {
            if (sheet)
            {
                C2D_SpriteSheetFree(sheet);
            }
        }
        C2D_SpriteSheetFree(uiSheet);
        C2D_TextBufDelete(textBuf);
        C2D_FontFree(font);
        C2D_Fini();
        C3D_Fini();
    }

    void beginFrame()
    {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        if (shotState == 1)
        {
            // The frame queued last time is finished now: save what it drew.
            writeShot();
            shotState = 0;
            shotPath.clear();
        }
        C2D_TextBufClear(textBuf);
        C2D_TargetClear(topTarget, T::Void);
        C2D_TargetClear(bottomTarget, T::Void);
    }

    void endFrame()
    {
        bool shooting = !shotPath.empty() && shotState == 0;
        if (shooting)
        {
            // The framebuffers this frame's display transfer writes into.
            shotTop    = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, nullptr, nullptr);
            shotBottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr);
        }
        C3D_FrameEnd(0);
        if (shooting)
        {
            shotState = 1;
        }
    }

    void requestShot(const std::string& path) { shotPath = path; }

    void top()
    {
        C2D_SceneBegin(topTarget);
        current      = topTarget;
        currentWidth = TOP_W;
    }

    void bottom()
    {
        C2D_SceneBegin(bottomTarget);
        current      = bottomTarget;
        currentWidth = BOTTOM_W;
    }

    void clip(int x, int y, int w, int h)
    {
        C2D_Flush();
        C3D_SetScissor(GPU_SCISSOR_NORMAL, H - (y + h), currentWidth - (x + w), H - y, currentWidth - x);
    }

    void unclip()
    {
        C2D_Flush();
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    }

    void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); }

    void vgrad(float x, float y, float w, float h, u32 a, u32 b) { C2D_DrawRectangle(x, y, 0.5f, w, h, a, a, b, b); }

    void hgrad(float x, float y, float w, float h, u32 a, u32 b) { C2D_DrawRectangle(x, y, 0.5f, w, h, a, b, a, b); }

    void outline(float x, float y, float w, float h, u32 c, float t)
    {
        rect(x, y, w, t, c);
        rect(x, y + h - t, w, t, c);
        rect(x, y + t, t, h - 2 * t, c);
        rect(x + w - t, y + t, t, h - 2 * t, c);
    }

    void panel(float x, float y, float w, float h, u32 fill, u32 rim) { panelGrad(x, y, w, h, fill, fill, rim); }

    void panelGrad(float x, float y, float w, float h, u32 a, u32 b, u32 rim)
    {
        // Rim with its corner pixels cut, then the body one pixel in.
        rect(x + 2, y, w - 4, 1, rim);
        rect(x + 2, y + h - 1, w - 4, 1, rim);
        rect(x, y + 2, 1, h - 4, rim);
        rect(x + w - 1, y + 2, 1, h - 4, rim);
        rect(x + 1, y + 1, 1, 1, rim);
        rect(x + w - 2, y + 1, 1, 1, rim);
        rect(x + 1, y + h - 2, 1, 1, rim);
        rect(x + w - 2, y + h - 2, 1, 1, rim);
        if (a == b)
        {
            rect(x + 2, y + 1, w - 4, h - 2, a);
            rect(x + 1, y + 2, 1, h - 4, a);
            rect(x + w - 2, y + 2, 1, h - 4, a);
        }
        else
        {
            vgrad(x + 1, y + 1, w - 2, h - 2, a, b);
            rect(x + 1, y + 1, 1, 1, rim);
            rect(x + w - 2, y + 1, 1, 1, rim);
            rect(x + 1, y + h - 2, 1, 1, rim);
            rect(x + w - 2, y + h - 2, 1, 1, rim);
        }
    }

    void circle(float x, float y, float r, u32 c)
    {
        // Pixel spans: crisp at these small sizes, and independent of citro2d's ellipse mode.
        int rows = int(std::ceil(r * 2));
        float top = std::floor(y - r);
        for (int i = 0; i < rows; i++)
        {
            float dy   = top + i + 0.5f - y;
            float half = std::sqrt(std::max(0.0f, r * r - dy * dy));
            float x0   = std::round(x - half), x1 = std::round(x + half);
            if (x1 > x0)
            {
                rect(x0, top + i, x1 - x0, 1, c);
            }
        }
    }

    void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
    {
        C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0.5f);
    }

    void housing(float x, float y, float w, float h)
    {
        rect(x, y, w, h, T::Deck);
        u32 line = mix(T::Deck, T::Grid, 0.42f);
        int step = 12;
        for (int gx = int(x) + step - 1; gx < x + w; gx += step)
        {
            rect(gx, y, 1, h, line);
        }
        for (int gy = int(y) + step - 1; gy < y + h; gy += step)
        {
            rect(x, gy, w, 1, line);
        }
    }

    float textWidth(std::string_view s, float scale)
    {
        if (!supported(s))
        {
            return systemWidth(s) * scale;
        }
        float w = 0;
        for (size_t i = 0; i < s.size();)
        {
            u32 cp = nextCodepoint(s, i);
            if (cp == '\n')
            {
                break;
            }
            w += glyphAdvance(font, cp);
        }
        return w * scale;
    }

    std::string ellipsize(std::string_view s, float maxWidth, float scale)
    {
        if (maxWidth <= 0 || textWidth(s, scale) <= maxWidth)
        {
            return std::string(s);
        }
        const float dots = textWidth("...", scale);
        bool own         = supported(s);
        float w          = 0;
        size_t cut       = 0;
        for (size_t i = 0; i < s.size();)
        {
            size_t start = i;
            u32 cp       = nextCodepoint(s, i);
            float adv    = own ? glyphAdvance(font, cp) * scale : glyphAdvance(nullptr, cp) * SYSTEM_SCALE * scale;
            if (w + adv + dots > maxWidth)
            {
                cut = start;
                break;
            }
            w  += adv;
            cut = i;
        }
        return std::string(s.substr(0, cut)) + "...";
    }

    float text(float x, float y, std::string_view s, u32 color, Align align, float scale, float maxWidth)
    {
        if (s.empty())
        {
            return 0;
        }
        std::string str = maxWidth > 0 ? ellipsize(s, maxWidth, scale) : std::string(s);
        bool own        = supported(str);
        float width     = textWidth(str, scale);
        float dx        = align == Center ? std::floor(width / 2) : align == Right ? width : 0;
        C2D_Text t;
        if (own)
        {
            C2D_TextFontParse(&t, font, textBuf, str.c_str());
            C2D_DrawText(&t, C2D_WithColor, std::floor(x - dx), std::floor(y), 0.5f, nativeScale * scale,
                nativeScale * scale, color);
        }
        else
        {
            C2D_TextFontParse(&t, nullptr, textBuf, str.c_str());
            C2D_DrawText(&t, C2D_WithColor, std::floor(x - dx), std::floor(y) - 1, 0.5f, SYSTEM_SCALE * scale,
                SYSTEM_SCALE * scale, color);
        }
        return width;
    }

    float wrap(float x, float y, float width, std::string_view s, u32 color, float scale, int maxLines)
    {
        std::string line;
        float cy  = y;
        int lines = 0;
        size_t i  = 0;
        auto flush = [&]()
        {
            text(x, cy, line, color, Left, scale);
            cy += LINE * scale;
            lines++;
            line.clear();
        };
        while (i < s.size())
        {
            size_t end = s.find_first_of(" \n", i);
            if (end == std::string_view::npos)
            {
                end = s.size();
            }
            std::string word(s.substr(i, end - i));
            std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && textWidth(candidate, scale) > width)
            {
                if (maxLines && lines + 1 >= maxLines)
                {
                    line = ellipsize(line + " " + word, width, scale);
                    break;
                }
                flush();
                line = word;
            }
            else
            {
                line = candidate;
            }
            if (end < s.size() && s[end] == '\n')
            {
                flush();
            }
            i = end + 1;
        }
        if (!line.empty())
        {
            flush();
        }
        return cy - y;
    }

    C2D_Image ui(int index) { return C2D_SpriteSheetGetImage(uiSheet, index); }

    void cameraImage(const u16* pixels, int w, int h, float x, float y)
    {
        static C3D_Tex tex;
        static bool ready = false;
        static Tex3DS_SubTexture sub;
        constexpr int TW = 512, TH = 256;
        if (!ready)
        {
            if (!C3D_TexInit(&tex, TW, TH, GPU_RGB565))
            {
                return;
            }
            C3D_TexSetFilter(&tex, GPU_LINEAR, GPU_LINEAR);
            ready = true;
        }
        // The GPU's textures are 8x8 tiles in Morton order; with the sub-texture below, row 0 is the top.
        u16* dst = static_cast<u16*>(tex.data);
        for (int py = 0; py < h; py++)
        {
            int ty = py;
            for (int px = 0; px < w; px++)
            {
                u32 morton = (px & 1) | ((ty & 1) << 1) | ((px & 2) << 1) | ((ty & 2) << 2) | ((px & 4) << 2) |
                             ((ty & 4) << 3);
                dst[((((ty >> 3) * (TW >> 3)) + (px >> 3)) << 6) + morton] = pixels[py * w + px];
            }
        }
        C3D_TexFlush(&tex);
        sub = {u16(w), u16(h), 0.0f, 1.0f, float(w) / TW, 1.0f - float(h) / TH};
        C2D_Image img{&tex, &sub};
        C2D_DrawImageAt(img, x, y, 0.5f);
    }

    void image(int index, float x, float y, float scale)
    {
        C2D_DrawImageAt(ui(index), std::floor(x), std::floor(y), 0.5f, nullptr, scale, scale);
    }

    void imageTinted(int index, float x, float y, u32 color, float blend, float scale)
    {
        C2D_ImageTint tint;
        C2D_PlainImageTint(&tint, color, blend);
        C2D_DrawImageAt(ui(index), std::floor(x), std::floor(y), 0.5f, &tint, scale, scale);
    }

    void imageCentered(int index, float cx, float cy, float scale)
    {
        C2D_Image img = ui(index);
        image(index, cx - img.subtex->width * scale / 2, cy - img.subtex->height * scale / 2, scale);
    }

    bool icon(int species, int form, float cx, float cy, float scale, u32 tint, float blend)
    {
        const Showdown* sd = lookup(species, form);
        if (!sd || iconPages == 0)
        {
            return false;
        }
        int perPage = iconPageRows * iconColumns;
        int page    = sd->icon / perPage;
        int local   = sd->icon % perPage;
        if (page >= iconPages)
        {
            return false;
        }
        if (!iconSheets[page] && !iconTried[page])
        {
            iconTried[page] = true;
            char path[64];
            snprintf(path, sizeof(path), "romfs:/gfx/icons%d.t3x", page);
            iconSheets[page] = C2D_SpriteSheetLoad(path);
            nearest(iconSheets[page]);
        }
        if (!iconSheets[page])
        {
            return false;
        }
        C2D_Image full = C2D_SpriteSheetGetImage(iconSheets[page], 0);
        float tw = full.tex->width, th = full.tex->height;
        int px = (local % iconColumns) * iconW, py = (local / iconColumns) * iconH;
        Tex3DS_SubTexture sub;
        sub.width  = iconW;
        sub.height = iconH;
        sub.left   = full.subtex->left + px / tw;
        sub.right  = sub.left + iconW / tw;
        sub.top    = full.subtex->top - py / th;
        sub.bottom = sub.top - iconH / th;
        C2D_Image img{full.tex, &sub};
        C2D_ImageTint t;
        if (blend > 0)
        {
            C2D_PlainImageTint(&t, tint, blend);
        }
        C2D_DrawImageAt(img, std::floor(cx - iconW * scale / 2), std::floor(cy - iconH * scale / 2), 0.5f,
            blend > 0 ? &t : nullptr, scale, scale);
        return true;
    }

    bool front(int species, int form, bool shiny, bool female, float cx, float cy, float scale)
    {
        const Showdown* sd = lookup(species, form);
        if (!sd)
        {
            return false;
        }
        bool useShiny = shiny && sd->shinyFront;
        if (!useShiny && !sd->front)
        {
            return false;
        }
        std::string dir = useShiny ? "romfs:/shiny/" : "romfs:/front/";
        C2D_SpriteSheet sheet = nullptr;
        if (female)
        {
            sheet = frontSheet(dir + sd->stem + "-f.t3x");
        }
        if (!sheet)
        {
            sheet = frontSheet(dir + sd->stem + ".t3x");
        }
        if (!sheet)
        {
            return false;
        }
        C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
        C2D_DrawImageAt(img, std::floor(cx - img.subtex->width * scale / 2),
            std::floor(cy - img.subtex->height * scale / 2), 0.5f, nullptr, scale, scale);
        return true;
    }

    bool cartridge(std::string_view artName, float cx, float cy, float scale)
    {
        for (const auto& art : ui::gameArt)
        {
            if (artName == art.name)
            {
                imageCentered(art.image, cx, cy, scale);
                return true;
            }
        }
        return false;
    }

    float keyDiscWidth(std::string_view key) { return std::max(13.0f, std::floor(textWidth(key)) + 7); }

    void keyDisc(float x, float y, std::string_view key)
    {
        constexpr u32 disc = rgb(0x6C, 0xC4, 0xEE);
        float w = keyDiscWidth(key);
        circle(x + 6.5f, y + 6.5f, 6.5f, disc);
        if (w > 13)
        {
            // A pill for the longer names (LR, START, SELECT).
            circle(x + w - 6.5f, y + 6.5f, 6.5f, disc);
            rect(x + 6, y, w - 12, 13, disc);
        }
        text(x + w / 2, y + 1, key, T::Void, Center);
    }

    void chevron(float x, float y, u32 c, bool left)
    {
        if (left)
        {
            tri(x + 4, y, x + 4, y + 8, x, y + 4, c);
        }
        else
        {
            tri(x, y, x, y + 8, x + 4, y + 4, c);
        }
    }

    void star(float cx, float cy, float r, u32 c)
    {
        tri(cx, cy - r, cx - r * 0.3f, cy, cx + r * 0.3f, cy, c);
        tri(cx, cy + r, cx - r * 0.3f, cy, cx + r * 0.3f, cy, c);
        tri(cx - r, cy, cx, cy - r * 0.3f, cx, cy + r * 0.3f, c);
        tri(cx + r, cy, cx, cy - r * 0.3f, cx, cy + r * 0.3f, c);
    }

    void genderMark(float x, float y, int gender)
    {
        if (gender == 0)
        {
            imageTinted(ui::male, x, y, S::Male, 1);
        }
        else if (gender == 1)
        {
            imageTinted(ui::female, x, y, S::Female, 1);
        }
    }
}
