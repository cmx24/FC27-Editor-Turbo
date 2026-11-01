#include "preview/page_ids.h"

namespace preview {

using turbo::shell::Area;
using turbo::shell::Page;
using turbo::shell::Place;

namespace {
// lower case ASCII letters/digits, every other run of characters becomes one '-' ("Name, colours & crest" -> "name-colours-crest")
std::string norm(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/';
        if (c == '/' && !r.empty() && r.back() == '-') r.pop_back();
        if (ok) r += c;
        else if (!r.empty() && r.back() != '-' && r.back() != '/') r += '-';
    }
    while (!r.empty() && r.back() == '-') r.pop_back();
    return r;
}
std::string slug(const char* s) { return norm(s); }
std::string all_ids() {
    std::string r;
    for (const Place& p : all_places()) r += (r.empty() ? "" : ", ") + id_of(p);
    return r;
}
}  // namespace

std::string id_of(const Place& p) { return slug(turbo::shell::area_label(p.area)) + "/" + slug(turbo::shell::page_label(p.page)); }

std::string file_stem(const Place& p) {
    std::string s = id_of(p);
    const size_t k = s.find('/');
    return s.substr(0, k) + "__" + s.substr(k + 1);
}

std::vector<Place> all_places() {
    std::vector<Place> v;
    for (int ai = 0; ai < turbo::shell::kAreaCount; ++ai)
        for (Page p : turbo::shell::pages_of(static_cast<Area>(ai))) v.push_back(Place{static_cast<Area>(ai), p});
    return v;
}

bool resolve_page_id(const std::string& id, Place& out, std::string& error) {
    out = Place{};
    const std::string want = norm(id);
    if (want.empty()) {
        error = "empty page id (valid ids: " + all_ids() + ")";
        return false;
    }
    const size_t slash = want.find('/');
    std::vector<Place> hit;
    for (const Place& p : all_places()) {
        const std::string full = id_of(p);
        if (slash != std::string::npos ? full == want : full.substr(full.find('/') + 1) == want) hit.push_back(p);
    }
    if (hit.size() == 1) {
        out = hit[0];
        return true;
    }
    if (hit.size() > 1) {
        error = "ambiguous page id '" + id + "': write it as <area>/<page> (";
        for (size_t i = 0; i < hit.size(); ++i) error += (i ? ", " : "") + id_of(hit[i]);
        error += ")";
        return false;
    }
    error = "unknown page id '" + id + "' (valid ids: " + all_ids() + ")";
    return false;
}

}  // namespace preview
