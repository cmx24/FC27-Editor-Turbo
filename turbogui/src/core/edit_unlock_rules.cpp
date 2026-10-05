// FC 27 LE Turbo GUI - unlocking the game's own editors: the shared rule table (see edit_unlock_rules.h)
#include "edit_unlock_rules.h"

#include <cstring>

namespace turbo {
namespace edit_unlock {

namespace {

const Context kContexts[kContextCount] = {
    {"managercareer_editplayers", Group::CareerPlayers, false, "Career > Squad > Edit Player"},
    {"managercareer_edit_custom_player", Group::CreateClubPlayers, false, "Create a Club > squad > edit player"},
    {"managercareer_edit", Group::Manager, true, "Edit Manager (your created manager)"},
    {"managercareer_edit_retiredreal", Group::Manager, true, "Edit Manager (real or licensed manager)"},
    {"managercareer_create_real", Group::Manager, true, "Career start with a real manager"},
    {"mainmenu_edit_real", Group::MainMenu, false, "Main menu > Customise > Edit Players (real)"},
    {"mainmenu_edit_created", Group::MainMenu, false, "Main menu > Customise > Edit Players (created)"},
};

// AttributeName values: the game's own name -> id table (0x142369C28 on build 6AB9813C-211EF000)
const KeptAttribute kKept[kKeptCount] = {{"BODY_TYPE", 11}, {"GENDER", 29}, {"PREFERRED_POSITION", 77}, {"TEAM", 100}};

constexpr const char kPrefix[] = "avatarcustomizationcfg_";
constexpr const char kSuffix[] = ".json";

char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; }

// Does s[0..n) start with the lower-case literal `lit` (case-insensitive)? Never allocates.
bool istarts(const char* s, size_t n, const char* lit) {
    size_t i = 0;
    for (; lit[i]; ++i)
        if (i >= n || lower(s[i]) != lit[i]) return false;
    return true;
}

// Every kept attribute stays, on every screen, experimental or not (1.1.4: the manager gender under "Unlock everything"
// was dropped with the other experiments that could close the game)
bool kept_by_index(int, const Context&, bool) { return true; }

struct Walker {
    const Context& c;
    bool experimental;
    const KeepIds& ids;
    ReadableFn readable;
    const Layout& L;
    bool apply;
    WalkResult& r;

    bool fail(const char* why) {
        r.ok = false;
        r.error = why;
        return false;
    }

    // [begin, end) of the eastl::vector whose header is at `at`
    bool vec(uint8_t* at, size_t stride, size_t max, uint8_t*& begin, size_t& count) {
        begin = nullptr;
        count = 0;
        if (!readable(at, 2 * sizeof(void*))) return fail("a vector header is not readable");
        uint8_t* b = nullptr;
        uint8_t* e = nullptr;
        std::memcpy(&b, at, sizeof(b));
        std::memcpy(&e, at + sizeof(b), sizeof(e));
        if (!b && !e) return true;
        const uintptr_t ub = reinterpret_cast<uintptr_t>(b), ue = reinterpret_cast<uintptr_t>(e);
        if (!b || !e || ue < ub) return fail("a vector is malformed");
        const size_t bytes = static_cast<size_t>(ue - ub);
        if (bytes % stride) return fail("a vector size is not a multiple of the element size (layout changed)");
        if (bytes / stride > max) return fail("a vector holds more elements than any editor config");
        if (!readable(b, bytes)) return fail("a vector's elements are not readable");
        begin = b;
        count = bytes / stride;
        return true;
    }

    bool items(uint8_t* at, int depth) {
        if (depth > kMaxDepth) return fail("items are nested deeper than any editor config");
        uint8_t* b = nullptr;
        size_t n = 0;
        if (!vec(at, L.item_stride, kMaxItemsPerVector, b, n)) return false;
        for (size_t i = 0; i < n; ++i) {
            uint8_t* item = b + i * L.item_stride;
            if (++r.items > kMaxItemsTotal) return fail("more items than any editor config");
            int32_t gid = -1, aid = -1;
            std::memcpy(&gid, item + L.item_group_id, sizeof(gid));
            std::memcpy(&aid, item + L.item_attr_id, sizeof(aid));
            uint8_t* ed = item + L.item_editable;
            uint8_t* vis = item + L.item_visible;
            if (*ed > 1 || *vis > 1) return fail("a flag byte is not 0 or 1 (layout changed)");
            if (keep_id(gid, ids, c, experimental) || keep_id(aid, ids, c, experimental)) {
                ++r.kept;
            } else {
                if (*ed == 0) {
                    if (apply) *ed = 1;
                    ++r.unlocked;
                }
                if (*vis == 0) {
                    if (apply) *vis = 1;
                    ++r.unlocked;
                }
            }
            if (!items(item + L.item_children, depth + 1)) return false;
        }
        return true;
    }

    bool config(uint8_t* cfg) {
        uint8_t* b = nullptr;
        size_t n = 0;
        if (!vec(cfg, L.cat_stride, kMaxCategories, b, n)) return false;
        for (size_t i = 0; i < n; ++i) {
            ++r.categories;
            if (!items(b + i * L.cat_stride + L.cat_items, 0)) return false;
        }
        r.ok = true;
        return true;
    }
};

}  // namespace

const Context& context_at(int i) { return kContexts[(i >= 0 && i < kContextCount) ? i : 0]; }

const char* group_label(Group g) {
    switch (g) {
        case Group::CareerPlayers: return "Career Edit Player";
        case Group::CreateClubPlayers: return "Create-a-Club players";
        case Group::Manager: return "Your manager";
        case Group::MainMenu: return "Main menu editors";
    }
    return "?";
}

int context_index(const char* path, size_t len) {
    if (!path) return -1;
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '\0') {
            len = i;
            break;
        }
        if (path[i] == '/' || path[i] == '\\') start = i + 1;
    }
    const char* base = path + start;
    const size_t n = len - start;
    const size_t pre = sizeof(kPrefix) - 1, suf = sizeof(kSuffix) - 1;
    if (n <= pre + suf || !istarts(base, n, kPrefix) || !istarts(base + n - suf, suf, kSuffix)) return -1;
    const char* ctx = base + pre;
    const size_t ctx_len = n - pre - suf;
    for (int i = 0; i < kContextCount; ++i) {
        const char* name = kContexts[i].name;
        if (std::strlen(name) == ctx_len && istarts(ctx, ctx_len, name)) return i;
    }
    return -1;
}

int context_index(const std::string& path) { return context_index(path.c_str(), path.size()); }

const KeptAttribute& kept_at(int i) { return kKept[(i >= 0 && i < kKeptCount) ? i : 0]; }

bool keep_name(const std::string& name, const Context& c, bool experimental) {
    for (int i = 0; i < kKeptCount; ++i)
        if (name == kKept[i].name) return kept_by_index(i, c, experimental);
    return false;
}

bool KeepIds::complete() const {
    for (int i = 0; i < kKeptCount; ++i)
        if (ids[i] < 0) return false;
    return true;
}

KeepIds expected_keep_ids() {
    KeepIds k;
    for (int i = 0; i < kKeptCount; ++i) k.ids[i] = kKept[i].id;
    return k;
}

bool keep_id(int id, const KeepIds& ids, const Context& c, bool experimental) {
    if (id < 0) return false;
    for (int i = 0; i < kKeptCount; ++i)
        if (ids.ids[i] == id) return kept_by_index(i, c, experimental);
    return false;
}

WalkResult unlock_parsed_config(uint8_t* config, const Context& c, bool experimental, const KeepIds& ids,
                                ReadableFn readable, bool apply, const Layout& layout) {
    WalkResult check;
    if (!config || !readable) {
        check.error = "no config";
        return check;
    }
    if (!ids.complete()) {
        check.error = "the keep-list ids are not resolved";
        return check;
    }
    Walker v{c, experimental, ids, readable, layout, false, check};
    if (!v.config(config) || !apply) return check;  // validated only (or refused: nothing written)
    WalkResult done;
    Walker w{c, experimental, ids, readable, layout, true, done};
    w.config(config);
    return done;
}

}  // namespace edit_unlock
}  // namespace turbo
