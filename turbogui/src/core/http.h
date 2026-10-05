// FC 27 LE Turbo GUI - one HTTPS GET (WinHTTP in the game build, a stub in the tests). Used only for the CMTracker miniface
// picture of a player the user picked (core/cmtracker.h builds the URL); Turbo makes no other network request.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace turbo {

struct HttpResult {
    int status = 0;                // HTTP status, 0 = no response
    std::vector<uint8_t> body;
    std::string error;             // set when status is 0
};

// GET an https:// URL. Refuses other schemes, bodies larger than max_bytes and waits at most ~15 s. Blocking: call it from a
// worker thread, never from the render loop.
HttpResult http_get(const std::string& url, size_t max_bytes);

}  // namespace turbo
