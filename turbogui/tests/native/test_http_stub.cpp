// Native tests have no network: http_get answers "no network" (the Windows build uses src/win/http_win.cpp).
#include "core/http.h"

namespace turbo {
HttpResult http_get(const std::string&, size_t) {
    HttpResult r;
    r.error = "no network in the tests";
    return r;
}
}  // namespace turbo
