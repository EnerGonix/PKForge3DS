#include "qr.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "widgets.hpp"

#include "qrcodegen.h"
#include "quirc.h"
#include <3ds.h>
#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include "files.hpp"
#include "stb_image.h"

using namespace gfx;

namespace qr
{
    namespace
    {
        int generationNumber(pksm::Generation g)
        {
            switch (g)
            {
                case pksm::Generation::ONE: return 1;
                case pksm::Generation::TWO: return 2;
                case pksm::Generation::THREE: return 3;
                case pksm::Generation::FOUR: return 4;
                case pksm::Generation::FIVE: return 5;
                case pksm::Generation::SIX: return 6;
                case pksm::Generation::SEVEN:
                case pksm::Generation::LGPE: return 7;
                case pksm::Generation::EIGHT: return 8;
                case pksm::Generation::NINE: return 9;
                default: return 0;
            }
        }

        pksm::Generation generationOf(int n, size_t length)
        {
            switch (n)
            {
                case 1: return pksm::Generation::ONE;
                case 2: return pksm::Generation::TWO;
                case 3: return pksm::Generation::THREE;
                case 4: return pksm::Generation::FOUR;
                case 5: return pksm::Generation::FIVE;
                case 6: return pksm::Generation::SIX;
                // Let's Go's PB7 is 0x104 bytes; a Sun/Moon PK7 travels in its 0xE8 box form (envelope()).
                case 7: return length == 0x104 ? pksm::Generation::LGPE : pksm::Generation::SEVEN;
                case 8: return pksm::Generation::EIGHT;
                case 9: return pksm::Generation::NINE;
                default: return pksm::Generation::UNUSED;
            }
        }
    }

    // A party Pokémon's stored part, the form PKHeX writes .pk files in (Gen 3 on; Let's Go has one form).
    size_t storedLength(const pksm::PKX& pk)
    {
        size_t n = pk.getLength();
        if (!pk.isParty())
        {
            return n;
        }
        switch (pk.generation())
        {
            case pksm::Generation::THREE: return 80;
            case pksm::Generation::FOUR:
            case pksm::Generation::FIVE: return 136;
            case pksm::Generation::SIX:
            case pksm::Generation::SEVEN: return 232;
            case pksm::Generation::EIGHT:
            case pksm::Generation::NINE: return 0x148;
            default: return n;
        }
    }

    std::vector<u8> envelope(const pksm::PKX& pk, const std::string& species)
    {
        auto all = pk.rawData();
        std::span<const u8> raw = all.first(std::min(all.size(), storedLength(pk)));
        std::vector<u8> out = {'P', 'K', 'F', '1', 1, u8(generationNumber(pk.generation())), u8(raw.size()),
            u8(raw.size() >> 8)};
        out.insert(out.end(), raw.begin(), raw.end());
        std::string name = species.substr(0, 255);
        out.push_back(u8(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        return out;
    }

    bool open(const std::vector<u8>& p, std::unique_ptr<pksm::PKX>& pk, std::string& species, std::string& error)
    {
        if (p.size() < 9 || memcmp(p.data(), "PKF1", 4) != 0)
        {
            error = "This QR code is not a PKForge Pokémon (PKF1).";
            return false;
        }
        if (p[4] != 1)
        {
            error = "This QR code comes from a newer PKForge.";
            return false;
        }
        size_t n = p[6] | p[7] << 8;
        if (8 + n + 1 > p.size() || 8 + n + 1 + p[8 + n] != p.size())
        {
            error = "This QR code is cut short.";
            return false;
        }
        species.assign(p.begin() + 9 + n, p.end());
        std::vector<u8> bytes(p.begin() + 8, p.begin() + 8 + n);
        pk = pksm::PKX::getPKM(generationOf(p[5], n), bytes.data(), bytes.size());
        if (!pk)
        {
            error = "PKForge 3DS cannot read that Pokémon's format.";
            return false;
        }
        return true;
    }

    bool decode(const u8* grey, int w, int h, std::vector<u8>& payload)
    {
        quirc* q = quirc_new();
        if (!q || quirc_resize(q, w, h) < 0)
        {
            if (q)
            {
                quirc_destroy(q);
            }
            return false;
        }
        memcpy(quirc_begin(q, nullptr, nullptr), grey, size_t(w) * h);
        quirc_end(q);
        bool ok = false;
        for (int i = 0; i < quirc_count(q) && !ok; i++)
        {
            quirc_code code;
            quirc_data data;
            quirc_extract(q, i, &code);
            quirc_decode_error_t err = quirc_decode(&code, &data);
            if (err != QUIRC_SUCCESS)
            {
                quirc_flip(&code); // a mirrored picture (a front camera, a flipped feed)
                err = quirc_decode(&code, &data);
            }
            if (err == QUIRC_SUCCESS)
            {
                payload.assign(data.payload, data.payload + data.payload_len);
                ok = true;
            }
        }
        quirc_destroy(q);
        return ok;
    }

    bool decodeFile(const std::string& path, std::vector<u8>& payload, std::string& error)
    {
        std::vector<u8> file;
        if (!files::read(path, file))
        {
            error = "Could not read " + path;
            return false;
        }
        int w = 0, h = 0, channels = 0;
        u8* grey = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &channels, 1);
        if (!grey)
        {
            error = "Not a picture PKForge can open (PNG, JPEG or BMP).";
            return false;
        }
        // Big phone screenshots: halve until quirc's work stays small; QR modules survive it.
        while (w > 1600 || h > 1600)
        {
            int nw = w / 2, nh = h / 2;
            for (int y = 0; y < nh; y++)
            {
                for (int x = 0; x < nw; x++)
                {
                    int s = grey[(2 * y) * w + 2 * x] + grey[(2 * y) * w + 2 * x + 1] + grey[(2 * y + 1) * w + 2 * x] +
                            grey[(2 * y + 1) * w + 2 * x + 1];
                    grey[y * nw + x] = u8(s / 4);
                }
            }
            w = nw;
            h = nh;
        }
        bool ok = decode(grey, w, h, payload);
        stbi_image_free(grey);
        if (!ok)
        {
            error = "No QR code could be read in that picture.";
        }
        return ok;
    }

    // ---------------------------------------------------------------- showing a code

    namespace
    {
        class Show : public Screen
        {
        public:
            Show(const pksm::PKX& pk, std::string where) : where(std::move(where))
            {
                species = pkdata::species(u16(pk.species()));
                speciesId = u16(pk.species());
                level     = pk.level();
                std::vector<u8> data = envelope(pk, species);
                std::vector<u8> temp(qrcodegen_BUFFER_LEN_MAX);
                code.resize(qrcodegen_BUFFER_LEN_MAX);
                std::copy(data.begin(), data.end(), temp.begin());
                ok = qrcodegen_encodeBinary(temp.data(), data.size(), code.data(), qrcodegen_Ecc_MEDIUM,
                    qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
                bytes = int(data.size());
            }

            void update(Input& in) override
            {
                if (in.down & (KEY_B | KEY_A))
                {
                    app::pop();
                }
            }

            void drawTop() override
            {
                rect(0, 0, TOP_W, H, rgb(255, 255, 255));
                if (!ok)
                {
                    text(TOP_W / 2, 110, "Too much data for one QR code.", rgb(0, 0, 0), Center);
                    return;
                }
                int size  = qrcodegen_getSize(code.data());
                int scale = std::max(1, (H - 16) / (size + 8));
                int side  = size * scale;
                int x0 = (TOP_W - side) / 2, y0 = (H - side) / 2;
                for (int y = 0; y < size; y++)
                {
                    for (int x = 0; x < size; x++)
                    {
                        if (qrcodegen_getModule(code.data(), x, y))
                        {
                            rect(float(x0 + x * scale), float(y0 + y * scale), float(scale), float(scale), rgb(0, 0, 0));
                        }
                    }
                }
            }

            void drawBottom() override
            {
                housing(0, 0, BOTTOM_W, H);
                widgets::header(BOTTOM_W, "QR code", where);
                panel(8, 26, BOTTOM_W - 16, 150, T::PanelFill, T::FrameEdge);
                front(speciesId, 0, false, false, 60, 90, 1);
                text(118, 44, species, T::Ink, Left, 1, 190);
                text(118, 62, "Level " + std::to_string(level), T::Value);
                text(118, 80, std::to_string(bytes) + " bytes, PKF1", T::SubInk);
                wrap(118, 100, 190, "Scan it with PKForge on another 3DS or on Android to receive this Pokémon.",
                    T::InkSoft, 1, 4);
                widgets::footer(BOTTOM_W, {{"B", "Back"}});
            }

        private:
            std::string where, species;
            int speciesId = 0, level = 0, bytes = 0;
            std::vector<u8> code;
            bool ok = false;
        };

        // ---------------------------------------------------------------- the camera

        constexpr int CW = 400, CH = 240;

        class Scan : public Screen
        {
        public:
            explicit Scan(std::function<void(std::vector<u8>)> f) : found(std::move(f))
            {
                pixels.resize(CW * CH);
                grey.resize(CW * CH);
                started = R_SUCCEEDED(camInit());
                if (!started)
                {
                    return;
                }
                CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
                CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
                CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
                CAMU_SetNoiseFilter(SELECT_OUT1, true);
                CAMU_SetAutoExposure(SELECT_OUT1, true);
                CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
                CAMU_SetTrimming(PORT_CAM1, false);
                CAMU_GetMaxBytes(&unit, CW, CH);
                CAMU_SetTransferBytes(PORT_CAM1, unit, CW, CH);
                CAMU_Activate(SELECT_OUT1);
                CAMU_ClearBuffer(PORT_CAM1);
                CAMU_StartCapture(PORT_CAM1);
            }

            ~Scan() override
            {
                if (started)
                {
                    CAMU_StopCapture(PORT_CAM1);
                    CAMU_Activate(SELECT_NONE);
                    camExit();
                }
            }

            void update(Input& in) override
            {
                if (in.down & KEY_B)
                {
                    app::pop();
                    return;
                }
                if (!started)
                {
                    return;
                }
                Handle event = 0;
                if (R_SUCCEEDED(CAMU_SetReceiving(&event, pixels.data(), PORT_CAM1, CW * CH * 2, s16(unit))))
                {
                    svcWaitSynchronization(event, 100000000LL);
                    svcCloseHandle(event);
                    frames++;
                }
                // quirc is not free on an Old 3DS: one look every few frames is plenty.
                if (frames % 8 == 0 && frames != lastTry)
                {
                    lastTry = frames;
                    for (int i = 0; i < CW * CH; i++)
                    {
                        u16 c = pixels[i];
                        grey[i] = u8(((c >> 11) * 8 * 77 + ((c >> 5) & 63) * 4 * 150 + (c & 31) * 8 * 29) >> 8);
                    }
                    std::vector<u8> payload;
                    if (decode(grey.data(), CW, CH, payload))
                    {
                        auto cb = std::move(found);
                        app::pop();
                        cb(std::move(payload));
                    }
                }
            }

            void drawTop() override
            {
                if (started)
                {
                    cameraImage(pixels.data(), CW, CH, 0, 0);
                    // Aiming square.
                    outline(TOP_W / 2 - 90, H / 2 - 90, 180, 180, T::Cyan, 2);
                }
                else
                {
                    housing(0, 0, TOP_W, H);
                    text(TOP_W / 2, 110, "The camera could not start.", T::Ink, Center);
                }
            }

            void drawBottom() override
            {
                housing(0, 0, BOTTOM_W, H);
                widgets::header(BOTTOM_W, "Scan a QR code");
                panel(8, 30, BOTTOM_W - 16, 120, T::PanelFill, T::FrameEdge);
                wrap(18, 40, BOTTOM_W - 36,
                    "Point the outer camera at a PKForge QR code (from this 3DS or the Android app). Keep it inside "
                    "the square, steady and well lit.",
                    T::Ink, 1, 6);
                widgets::footer(BOTTOM_W, {{"B", "Cancel"}});
            }

        private:
            std::function<void(std::vector<u8>)> found;
            std::vector<u16> pixels;
            std::vector<u8> grey;
            u32 unit    = 0;
            bool started = false;
            int frames = 0, lastTry = -1;
        };
    }

    std::unique_ptr<Screen> show(const pksm::PKX& pk, const std::string& where) { return std::make_unique<Show>(pk, where); }

    std::unique_ptr<Screen> scan(std::function<void(std::vector<u8>)> found)
    {
        return std::make_unique<Scan>(std::move(found));
    }
}
