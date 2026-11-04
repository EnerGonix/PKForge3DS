// Full legality through PKHeX: FlagBrew's local-gpss (github.com/FlagBrew/local-gpss) running on a
// computer on the same network, as PKSM uses it since FlagBrew's own server closed.
// POST <server>api/v2/pksm/legalize with the Pokémon as the multipart field "pkmn" and the
// "generation" and "version" headers; the reply is JSON with legal, ran, report and the Pokémon in base64.
#pragma once

#include "pkx/PKX.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace online
{
    struct Result
    {
        std::string error;  // empty when the server answered
        bool legal = false; // after its fixes
        bool ran   = false; // false when it was legal already
        std::vector<std::string> report;
        std::unique_ptr<pksm::PKX> pokemon; // the legalized one, when it made one
    };

    // "http://192.168.1.20:8080/", kept in sdmc:/3ds/PKForge/legality-server.txt; empty until set.
    std::string server();
    void setServer(const std::string& url);
    // Asks for the server's address with the keyboard; false when cancelled.
    bool askServer();

    // Sends pk on a worker thread behind a waiting overlay, then calls done on the main thread.
    void legalize(const pksm::PKX& pk, u8 version, std::function<void(Result&)> done);
}
