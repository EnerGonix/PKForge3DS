// In-app updates, as the original's: the latest release of github.com/EnerGonix/PKForge3DS, its PKForge.cia
// streamed into the 3DS's own installer (AM) and checked against the SHA-256 digest GitHub publishes for
// it before the install is finished; then PKForge restarts into the new version. Nothing on the SD card
// changes: settings, Bank, backups and music stay in sdmc:/3ds/PKForge.
#pragma once

#include "app.hpp"
#include <memory>
#include <string>
#include <vector>

namespace updater
{
    // Once a day at most, on a background thread: whether a newer release is out.
    void checkInBackground();
    // The newer release found by checkInBackground ("" when none or not checked yet).
    std::string available();
    // The update screen: check, show what is new, download and install on A.
    std::unique_ptr<Screen> open();
    // One verified HTTPS GET (the updater's curl + mbedtls, its root certificates), the whole body into out.
    bool fetch(const std::string& url, std::vector<u8>& out, std::string& error);
    // Bytes from..to (inclusive) of a file, by an HTTP range request.
    bool fetchRange(const std::string& url, u64 from, u64 to, std::vector<u8>& out, std::string& error);
}
