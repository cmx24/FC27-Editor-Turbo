#include "core/kit_fonts.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <set>

#include "core/kit_fonts_data.h"

namespace turbo {
namespace kitfonts {

const char* field_of(Kind k) {
    switch (k) {
        case Kind::NameFont: return "jerseynamefonttype";
        case Kind::NumberFont: return "numberfonttype";
        case Kind::ShortsNumberFont: return "shortsnumberfonttype";
        case Kind::NameLayout: return "jerseynamelayouttype";
    }
    return "";
}

const char* title_of(Kind k) {
    switch (k) {
        case Kind::NameFont: return "Name font";
        case Kind::NumberFont: return "Number font";
        case Kind::ShortsNumberFont: return "Shorts number font";
        case Kind::NameLayout: return "Name layout";
    }
    return "";
}

int64_t schema_max(Kind k) { return k == Kind::NameLayout ? 255 : 511; }

template <size_t N>
static const char* lookup(const kitfonts_data::Named (&table)[N], int64_t id) {
    const auto* end = table + N;
    const auto* it = std::lower_bound(table, end, id, [](const kitfonts_data::Named& n, int64_t v) { return n.id < v; });
    return it != end && it->id == id ? it->name : nullptr;
}

std::string game_name(Kind k, int64_t id) {
    const char* n = nullptr;
    switch (k) {
        case Kind::NameFont: n = lookup(kitfonts_data::kNameFonts, id); break;
        case Kind::NumberFont:
        case Kind::ShortsNumberFont: n = lookup(kitfonts_data::kNumberFonts, id); break;
        case Kind::NameLayout:
            if (id == 0) return "straight";
            if (id == 1) return "arched";
            return "";
    }
    return n ? n : "";
}

std::string pretty_name(const std::string& raw) {
    std::string out;
    bool start = true;
    for (char c : raw) {
        if (c == '_') {
            out += ' ';
            start = true;
            continue;
        }
        out += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        start = false;
    }
    return out;
}

void Index::build(const std::vector<KitRow>& rows, const NameFn& team_name) {
    for (auto& v : per_) v.clear();
    kits_ = 0;
    std::map<int64_t, std::string> names;  // club id -> name, asked once
    auto club_of = [&](int64_t team) -> const std::string& {
        auto it = names.find(team);
        if (it == names.end()) {
            std::string n = team_name ? team_name(team) : std::string();
            if (n.empty()) n = "team " + std::to_string(team);
            it = names.emplace(team, std::move(n)).first;
        }
        return it->second;
    };
    // per kind: id -> club name -> kits
    std::map<int64_t, std::map<std::string, int>> acc[kKinds];
    for (const KitRow& r : rows) {
        if (r.type < 0 || r.type > kLastCountedKitType) continue;
        ++kits_;
        for (int k = 0; k < kKinds; ++k)
            if (r.value[k] >= 0) ++acc[k][r.value[k]][club_of(r.team)];
    }
    for (int k = 0; k < kKinds; ++k) {
        for (const auto& id : acc[k]) {
            Entry e;
            e.id = id.first;
            for (const auto& c : id.second) {
                e.clubs.push_back(Wearer{c.first, c.second});
                e.kits += c.second;
            }
            std::sort(e.clubs.begin(), e.clubs.end(), [](const Wearer& a, const Wearer& b) { return a.kits != b.kits ? a.kits > b.kits : a.club < b.club; });
            per_[k].push_back(std::move(e));  // std::map order: ascending id
        }
    }
}

const Entry* Index::find(Kind k, int64_t id) const {
    const auto& v = per_[static_cast<int>(k)];
    auto it = std::lower_bound(v.begin(), v.end(), id, [](const Entry& e, int64_t x) { return e.id < x; });
    return it != v.end() && it->id == id ? &*it : nullptr;
}

std::vector<int64_t> choice_ids(Kind k, const Index& idx) {
    std::set<int64_t> ids;
    for (const Entry& e : idx.entries(k)) ids.insert(e.id);
    switch (k) {
        case Kind::NameFont:
            for (const auto& n : kitfonts_data::kNameFonts) ids.insert(n.id);
            break;
        case Kind::NumberFont:
        case Kind::ShortsNumberFont:
            for (const auto& n : kitfonts_data::kNumberFonts) ids.insert(n.id);
            break;
        case Kind::NameLayout:
            ids.insert(0);
            ids.insert(1);
            break;
    }
    return std::vector<int64_t>(ids.begin(), ids.end());
}

std::string worn_by(const Entry* e, size_t max_names) {
    if (!e || e->clubs.empty()) return "worn by no club in this database";
    std::string s = "worn by " + std::to_string(e->clubs.size()) + (e->clubs.size() == 1 ? " club: " : " clubs: ");
    const size_t shown = std::min(max_names, e->clubs.size());
    for (size_t i = 0; i < shown; ++i) s += (i ? ", " : "") + e->clubs[i].club;
    if (shown < e->clubs.size()) s += " and " + std::to_string(e->clubs.size() - shown) + " more";
    return s;
}

std::string short_label(Kind k, int64_t id) {
    const std::string n = game_name(k, id);
    return std::to_string(id) + (n.empty() ? std::string() : "  " + pretty_name(n));
}

std::string choice_label(Kind k, int64_t id, const Entry* e) {
    const std::string n = game_name(k, id);
    std::string s = std::to_string(id) + "  " + (n.empty() ? std::string("(not named by the game)") : pretty_name(n)) + "  -  ";
    if (!e || e->clubs.empty()) return s + "worn by no club";
    s += "worn by ";
    const size_t shown = std::min<size_t>(3, e->clubs.size());
    for (size_t i = 0; i < shown; ++i) s += (i ? ", " : "") + e->clubs[i].club;
    if (shown < e->clubs.size()) s += " and " + std::to_string(e->clubs.size() - shown) + " more";
    return s;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matches(Kind k, int64_t id, const Entry* e, const std::string& filter) {
    std::string hay = std::to_string(id) + " " + lower(pretty_name(game_name(k, id))) + " " + lower(game_name(k, id));
    if (e)
        for (const Wearer& w : e->clubs) hay += " " + lower(w.club);
    std::string word;
    auto ok = [&](const std::string& w) { return w.empty() || hay.find(w) != std::string::npos; };
    for (char c : filter) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!ok(word)) return false;
            word.clear();
        } else {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return ok(word);
}

}  // namespace kitfonts
}  // namespace turbo
