// Pokémon by QR code, in the Android PKForge's PKF1 envelope so the two apps read each other's codes:
//   "PKF1", version 1, generation (1-9), u16 length N, N bytes of the .pk file, u8 M, M bytes of the
//   species name (UTF-8), all in one byte-mode QR. Codes are drawn with Project Nayuki's qrcodegen (MIT)
//   and read with quirc (ISC), from the outer camera or from a picture (stb_image, public domain).
#pragma once

#include "app.hpp"
#include "pkx/PKX.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qr
{
    std::vector<u8> envelope(const pksm::PKX& pk, const std::string& species);
    // The Pokémon a PKF1 payload carries; false (with why) when it is not one.
    bool open(const std::vector<u8>& payload, std::unique_ptr<pksm::PKX>& pk, std::string& species, std::string& error);
    // Every QR in an 8-bit grey picture; the first that reads, or false.
    bool decode(const u8* grey, int w, int h, std::vector<u8>& payload);
    // A PNG, JPEG or BMP file; false (with why) when it holds no readable QR.
    bool decodeFile(const std::string& path, std::vector<u8>& payload, std::string& error);

    std::unique_ptr<Screen> show(const pksm::PKX& pk, const std::string& where);
    // The outer camera until a QR reads; found gets its payload (the screen closes itself first).
    std::unique_ptr<Screen> scan(std::function<void(std::vector<u8>)> found);
}
