// FC 27 LE Turbo GUI - the pure part of the paged asset browser (ui/ui_asset_browser.h): the browser's items, the
// search and All / In this save / Hidden filter, the 200-tile pages, and the item lists of the big picture families
// (game minifaces, youth faces, staff faces, legend pictures, other clubs' crests) built from the asset catalog.
//
// Header only and free of ImGui and Windows calls, so tests/catalog/run_pages.sh tests it with g++ in seconds
// (docs/asset-catalog.md). The drawing is in ui/ui_asset_browser.cpp.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/asset_catalog.h"

namespace turbo {

using AssetScope = assets::Scope;

constexpr int kAssetBrowserPage = 200;  // tiles per page: only what is on screen asks for its picture

struct AssetBrowserItem {
    int64_t id = 0;
    std::string name;         // display name (DB > catalog > fallback)
    std::string name_source;  // assets::Name::source ("" = DB name or fallback)
    std::string path;         // picture path (legacy file), "" = placeholder
    bool hidden = false;
};

struct AssetBrowserState {
    AssetScope scope = AssetScope::All;
    int page = 0;
    char search[64] = "";
};

// ---------------------------------------------------------------- filter and pages
inline std::string asset_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Does the item match the search text? Empty text: yes. Digits only: the id contains them, or the name does.
// Otherwise: the name contains the text (any letter case). `q_lower` must already be lower-case and trimmed.
inline bool asset_search_match(const AssetBrowserItem& it, const std::string& q_lower) {
    if (q_lower.empty()) return true;
    const bool digits = std::all_of(q_lower.begin(), q_lower.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (digits && std::to_string(it.id).find(q_lower) != std::string::npos) return true;
    return asset_lower(it.name).find(q_lower) != std::string::npos;
}

inline std::string asset_trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Indices (into items) of the items that pass the scope filter and the search, in list order
inline std::vector<size_t> asset_filter(const std::vector<AssetBrowserItem>& items, AssetScope scope, const std::string& search) {
    const std::string q = asset_lower(asset_trim(search));
    std::vector<size_t> out;
    out.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
        if (assets::scope_shows(scope, items[i].hidden) && asset_search_match(items[i], q)) out.push_back(i);
    return out;
}

// Number of pages for n items (at least 1, so "page 1 of 1" shows for an empty list)
inline int asset_page_count(size_t n, int page_size = kAssetBrowserPage) {
    if (page_size <= 0) page_size = kAssetBrowserPage;
    const size_t pages = (n + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size);
    return pages == 0 ? 1 : static_cast<int>(pages);
}

// Clamps `page` into [0, page_count) and returns the [first, last) positions it covers
inline std::pair<size_t, size_t> asset_page_range(size_t n, int& page, int page_size = kAssetBrowserPage) {
    if (page_size <= 0) page_size = kAssetBrowserPage;
    const int pages = asset_page_count(n, page_size);
    page = std::max(0, std::min(page, pages - 1));
    const size_t first = static_cast<size_t>(page) * static_cast<size_t>(page_size);
    const size_t last = std::min(n, first + static_cast<size_t>(page_size));
    return {std::min(first, n), last};
}

// The pictures one page may ask the game for: the page's non-empty paths, each once, at most page_size of them.
// The browser asks only for the tiles on screen, which are a subset of these.
inline std::vector<std::string> asset_page_wants(const std::vector<AssetBrowserItem>& items, const std::vector<size_t>& filtered,
                                                 int page, int page_size = kAssetBrowserPage) {
    std::vector<std::string> out;
    const auto r = asset_page_range(filtered.size(), page, page_size);
    std::unordered_set<std::string> seen;
    for (size_t k = r.first; k < r.second; ++k) {
        const std::string& p = items[filtered[k]].path;
        if (!p.empty() && seen.insert(p).second) out.push_back(p);
    }
    return out;
}

// ---------------------------------------------------------------- names and paths
// Display name: the live DB name first, then the catalog's name (its source kept for the tooltip), then the fallback
// ("Youth face #91001"). A catalog name taken from a game file name ("slug") gets the id, because slugs are ambiguous
// (two different heads can share one).
inline void asset_pick_name(AssetBrowserItem& it, assets::Family f, const std::string& db_name, const assets::Name& cat_name) {
    if (!db_name.empty()) {
        it.name = db_name;
        it.name_source.clear();
    } else if (!cat_name.text.empty()) {
        it.name = cat_name.text;
        it.name_source = cat_name.source;
        if (cat_name.source == "slug" || cat_name.source == "kit_slug") it.name += " (" + std::to_string(it.id) + ")";
    } else {
        it.name = assets::fallback_name(f, it.id);
        it.name_source.clear();
    }
}

// The game's picture path of an id when the catalog does not give an exact one (the legacy file naming rules;
// crests: the big light crest)
inline std::string asset_default_path(assets::Family f, int64_t id) {
    const std::string n = std::to_string(id);
    switch (f) {
        case assets::Family::Miniface: return "data/ui/imgAssets/heads/p" + n + ".dds";
        case assets::Family::Youthface: return "data/ui/imgAssets/youthheads/p" + n + ".dds";
        case assets::Family::Staffface: return "data/ui/imgAssets/heads_staff/heads_staff_" + n + ".dds";
        case assets::Family::Legendface: return "data/ui/imgAssets/cmLegendaryPlayers/legend_" + n + ".dds";
        case assets::Family::Crest: return "data/ui/imgAssets/crest/light/l" + n + ".dds";
        default: return "";
    }
}

// Which name table names the ids of a picture family (crests: teams; the faces: people)
inline assets::NameKind asset_name_kind(assets::Family f) {
    return f == assets::Family::Crest || f == assets::Family::Kit ? assets::NameKind::Team : assets::NameKind::Player;
}

// What one picture family needs to become browser items. Every callable may be empty (no DB name, no catalog name,
// default path, not hidden).
struct AssetItemSource {
    assets::Family family = assets::Family::Miniface;
    std::vector<int64_t> ids;                              // the catalog's ids of the family
    std::function<std::string(int64_t)> db_name;           // live DB name, "" when no row names the id
    std::function<assets::Name(int64_t)> catalog_name;     // the builder's name
    std::function<std::string(int64_t)> path;              // exact path from the catalog, "" = default rule
    std::function<bool(int64_t)> hidden;                   // shipped, and no row of the save uses it
    bool hidden_off = false;                               // asset_hidden_off.txt: hidden ids are not listed at all
};

inline std::vector<AssetBrowserItem> asset_items(const AssetItemSource& src) {
    std::vector<AssetBrowserItem> out;
    out.reserve(src.ids.size());
    for (int64_t id : src.ids) {
        AssetBrowserItem it;
        it.id = id;
        it.hidden = src.hidden ? src.hidden(id) : false;
        if (src.hidden_off && it.hidden) continue;
        asset_pick_name(it, src.family, src.db_name ? src.db_name(id) : std::string(),
                        src.catalog_name ? src.catalog_name(id) : assets::Name{});
        it.path = src.path ? src.path(id) : std::string();
        if (it.path.empty()) it.path = asset_default_path(src.family, id);
        out.push_back(std::move(it));
    }
    return out;
}

// The same, read from a loaded catalog. `use` = what the save uses (null: nothing is badged hidden). With
// asset_hidden_off.txt the catalog reports nothing hidden, so the ids no row uses are dropped here from `use`.
inline std::vector<AssetBrowserItem> asset_items_from_catalog(const assets::Catalog& cat, assets::Family f, const assets::DbUse* use,
                                                              const std::function<std::string(int64_t)>& db_name) {
    if (!cat.loaded()) return {};
    AssetItemSource src;
    src.family = f;
    src.ids = cat.ids(f);
    src.db_name = db_name;
    const assets::NameKind nk = asset_name_kind(f);
    src.catalog_name = [&cat, nk](int64_t id) { return cat.name(nk, id); };
    src.path = [&cat, f](int64_t id) { return cat.preview_path(f, id); };
    if (cat.hidden_off()) {
        const std::unordered_set<int64_t>* used = use ? &use->used[static_cast<size_t>(f)] : nullptr;
        src.hidden = [used](int64_t id) { return !used || used->count(id) == 0; };
        src.hidden_off = true;
    } else if (use) {
        src.hidden = [&cat, f, use](int64_t id) { return cat.hidden(f, id, *use); };
    }
    return asset_items(src);
}

// ---------------------------------------------------------------- the picture sources the editors offer
struct AssetPictureSource {
    assets::Family family;
    const char* tab;   // tab label in the miniface / crest editor
    const char* note;  // one line under the tab
};

// The catalog sources of the miniface editor (player or manager) or of the crest editor. Empty when there is no
// loaded catalog (asset_catalog_off.txt, no file, refused file): the editors then look exactly as in 2.0.
inline std::vector<AssetPictureSource> asset_miniface_sources(bool manager, bool catalog_loaded) {
    if (!catalog_loaded) return {};
    std::vector<AssetPictureSource> out;
    out.push_back({assets::Family::Miniface, "Game minifaces",
                   "Every player miniface the game ships, also of players no longer in the database"});
    if (manager)
        out.push_back({assets::Family::Staffface, "Staff faces", "Every manager / staff picture the game ships"});
    out.push_back({assets::Family::Youthface, "Youth faces",
                   "Generated youth player faces. Only the picture is copied: the 3D head stays as it is"});
    out.push_back({assets::Family::Legendface, "Legend pictures", "Career mode legend pictures"});
    return out;
}
inline std::vector<AssetPictureSource> asset_crest_sources(bool catalog_loaded) {
    if (!catalog_loaded) return {};
    return {{assets::Family::Crest, "Crests in the game files",
             "Every club crest the game ships, also of clubs no longer in the database. Names: the database, else the game's "
             "team names or kit folders when the catalog has them, else \"Team #id\" (search by id)"}};
}

}  // namespace turbo
