// FC 27 LE Turbo GUI - native tests: the game editors' rule table and the in-memory fallback's walk on SYNTHETIC
// parsed-config memory (core/edit_unlock_rules.h, core/edit_unlock_hook.h). No EA file is used.
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/devops.h"
#include "core/edit_unlock.h"
#include "core/edit_unlock_hook.h"
#include "core/edit_unlock_rules.h"
#include "core/sigscan.h"

using namespace turbo;
using namespace turbo::edit_unlock;

namespace {

int* g_p = nullptr;
int* g_f = nullptr;

void check(bool ok, const char* what, int line) {
    if (ok) {
        ++*g_p;
    } else {
        ++*g_f;
        std::printf("  FAIL %s (test_edit_unlock_hook.cpp:%d)\n", what, line);
    }
}
#define EU_CHECK(cond, msg) check((cond), (msg), __LINE__)

// ---------------------------------------------------------------- fake parsed-config memory
// The memory the walk may touch: every buffer the fake config owns (the host checks committed read-write pages)
std::vector<std::pair<const uint8_t*, size_t>> g_regions;
bool fake_readable(const void* p, size_t n) {
    const uint8_t* a = static_cast<const uint8_t*>(p);
    for (const auto& r : g_regions)
        if (a >= r.first && a + n <= r.first + r.second) return true;
    return false;
}

const Layout kL;

struct FakeItem {
    int gid = -1, aid = -1;
    uint8_t editable = 1, visible = 1;
    std::vector<FakeItem> children;
};

FakeItem item(int aid, uint8_t ed, uint8_t vis, std::vector<FakeItem> kids = {}) {
    FakeItem i;
    i.aid = aid;
    i.editable = ed;
    i.visible = vis;
    i.children = std::move(kids);
    return i;
}

FakeItem group(int gid, std::vector<FakeItem> kids) {
    FakeItem i;
    i.gid = gid;
    i.children = std::move(kids);
    return i;
}

struct FakeConfig {
    std::deque<std::vector<uint8_t>> bufs;  // stable addresses
    std::vector<uint8_t>* config = nullptr;

    uint8_t* alloc(size_t n) {
        bufs.emplace_back(n ? n : 1, 0);
        g_regions.push_back({bufs.back().data(), n});
        return bufs.back().data();
    }
    static void set_vec(uint8_t* at, uint8_t* b, size_t bytes) {
        uint8_t* e = b ? b + bytes : nullptr;
        std::memcpy(at, &b, sizeof(b));
        std::memcpy(at + 8, &e, sizeof(e));
        std::memcpy(at + 16, &e, sizeof(e));
    }
    void fill_items(uint8_t* vec_at, const std::vector<FakeItem>& items) {
        if (items.empty()) {
            set_vec(vec_at, nullptr, 0);
            return;
        }
        uint8_t* b = alloc(items.size() * kL.item_stride);
        for (size_t i = 0; i < items.size(); ++i) {
            uint8_t* it = b + i * kL.item_stride;
            std::memcpy(it + kL.item_group_id, &items[i].gid, 4);
            std::memcpy(it + kL.item_attr_id, &items[i].aid, 4);
            it[kL.item_editable] = items[i].editable;
            it[kL.item_editable + 1] = 0;  // isMandatory: never touched
            it[kL.item_visible] = items[i].visible;
            fill_items(it + kL.item_children, items[i].children);
        }
        set_vec(vec_at, b, items.size() * kL.item_stride);
    }
    explicit FakeConfig(const std::vector<std::vector<FakeItem>>& cats) {
        uint8_t* cfg = alloc(0x88);
        config = &bufs.back();
        uint8_t* cb = alloc(cats.size() * kL.cat_stride);
        for (size_t c = 0; c < cats.size(); ++c) {
            int id = static_cast<int>(c);
            std::memcpy(cb + c * kL.cat_stride, &id, 4);
            fill_items(cb + c * kL.cat_stride + kL.cat_items, cats[c]);
        }
        set_vec(cfg, cb, cats.size() * kL.cat_stride);
    }
    uint8_t* root() { return config->data(); }
    // the i-th item of category c (top level)
    uint8_t* at(size_t c, size_t i) {
        uint8_t* cb = nullptr;
        std::memcpy(&cb, root(), 8);
        uint8_t* ib = nullptr;
        std::memcpy(&ib, cb + c * kL.cat_stride + kL.cat_items, 8);
        return ib + i * kL.item_stride;
    }
    static uint8_t* child(uint8_t* it, size_t i) {
        uint8_t* b = nullptr;
        std::memcpy(&b, it + kL.item_children, 8);
        return b + i * kL.item_stride;
    }
    // a copy of every byte (nothing-written checks)
    std::vector<std::vector<uint8_t>> snapshot() const { return std::vector<std::vector<uint8_t>>(bufs.begin(), bufs.end()); }
    bool same(const std::vector<std::vector<uint8_t>>& s) const {
        if (s.size() != bufs.size()) return false;
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i] != bufs[i]) return false;
        return true;
    }
};

bool ed(const uint8_t* it) { return it[kL.item_editable] == 1; }
bool vis(const uint8_t* it) { return it[kL.item_visible] == 1; }

const char* kEditPlayers = "data/avatar/avatarcustomizationcfg_managercareer_editplayers.json";
const char* kEditManager = "data/avatar/avatarcustomizationcfg_managercareer_edit.json";

Gate gate_on() {
    Gate g;
    g.live = true;
    return g;
}

int load(FakeConfig& f, const char* path, const Gate& g, HookCounters& c, const KeepIds& ids = expected_keep_ids()) {
    return on_config_loaded(f.root(), path, std::strlen(path), g, ids, &fake_readable, c);
}

// AttributeName ids used below (build 6AB9813C-211EF000): FIRST_NAME 24, HEIGHT 41, KIT_NAME 49, the kept 11/29/77/100
std::vector<std::vector<FakeItem>> sample() {
    return {{item(24, 0, 1), item(100, 0, 0), item(29, 0, 1), item(41, 1, 0)},
            {group(3, {item(49, 0, 1), item(11, 0, 0), group(4, {item(77, 0, 0), item(13, 0, 0)})})}};
}

}  // namespace

void test_edit_unlock_hook(int& pass, int& fail) {
    g_p = &pass;
    g_f = &fail;

    // ---------------------------------------------------------------- which files
    {
        EU_CHECK(context_index(kEditPlayers) == 0, "career Edit Player file is context 0");
        EU_CHECK(context_index(kEditManager) == 2, "Edit Manager file is context 2");
        EU_CHECK(context_index("DATA\\AVATAR\\AvatarCustomizationCfg_ManagerCareer_Edit_RetiredReal.JSON") == 3,
                 "backslashes and any case match");
        EU_CHECK(context_index("avatarcustomizationcfg_mainmenu_edit_created.json") == 6, "a bare file name matches");
        EU_CHECK(context_index("data/avatar/avatarcustomizationcfg_managercareer_edit_online.json") < 0, "the online file is never unlocked");
        EU_CHECK(context_index("data/avatar/avatarcustomizationcfg_managerlive_edit.json") < 0, "Manager Live is never unlocked");
        EU_CHECK(context_index("data/avatar/avatarcustomizationcfg_playercareer_edit_position.json") < 0, "Player Career is not a target");
        EU_CHECK(context_index("data/avatar/avatarcustomizationcfg_automatch.json") < 0, "other avatar configs are left alone");
        EU_CHECK(context_index("data/avatar/managercareer_editplayers.json") < 0, "the prefix is required");
        EU_CHECK(context_index("data/avatar/avatarcustomizationcfg_managercareer_editplayers.jso") < 0, "the suffix is required");
        const char embedded[] = "data/avatar/avatarcustomizationcfg_managercareer_edit.json\0garbage";
        EU_CHECK(context_index(embedded, sizeof(embedded)) == 2, "the path ends at its terminator");
        EU_CHECK(context_index(nullptr, 10) < 0 && context_index("", 0) < 0, "empty paths are not editor configs");
        int managers = 0;
        for (int i = 0; i < kContextCount; ++i) managers += context_at(i).manager ? 1 : 0;
        EU_CHECK(managers == 3, "three manager editors");
    }
    // ---------------------------------------------------------------- the keep-list
    {
        const Context& player = context_at(0);
        const Context& manager = context_at(2);
        EU_CHECK(keep_name("TEAM", player, false) && keep_name("TEAM", manager, true), "TEAM is always kept");
        EU_CHECK(keep_name("GENDER", player, false) && keep_name("GENDER", player, true), "player GENDER is always kept");
        EU_CHECK(keep_name("GENDER", manager, false) && !keep_name("GENDER", manager, true), "manager GENDER only under the experimental switch");
        EU_CHECK(keep_name("PREFERRED_POSITION", player, true) && keep_name("BODY_TYPE", player, true), "PREFERRED_POSITION and BODY_TYPE kept");
        EU_CHECK(!keep_name("FIRST_NAME", player, false) && !keep_name("COMMENTARY_NAME", player, false), "names are unlocked");
        const KeepIds ids = expected_keep_ids();
        EU_CHECK(ids.complete() && ids.ids[0] == 11 && ids.ids[1] == 29 && ids.ids[2] == 77 && ids.ids[3] == 100,
                 "expected ids BODY_TYPE 11, GENDER 29, PREFERRED_POSITION 77, TEAM 100");
        EU_CHECK(keep_id(100, ids, player, true) && keep_id(29, ids, player, true) && !keep_id(29, ids, manager, true) &&
                     keep_id(29, ids, manager, false),
                 "keep_id follows keep_name");
        EU_CHECK(!keep_id(-1, ids, player, false) && !keep_id(24, ids, player, false), "-1 and other ids are not kept");
        KeepIds none;
        EU_CHECK(!none.complete() && !keep_id(-2, none, player, false), "an unresolved table keeps nothing and is not complete");
        EU_CHECK(keep_name("GENDER", context_at(4), true), "career start with a real manager: GENDER stays even when experimental");
        // the file override (eu::keep_list) reads this same table, context by context, with and without experiments
        bool same = true;
        for (int i = 0; i < kContextCount; ++i)
            for (int x = 0; x < 2; ++x) {
                turbo::eu::Options o;
                o.experimental = x == 1;
                const std::set<std::string> k = turbo::eu::keep_list(turbo::eu::avatar_path(context_at(i).name), o);
                for (int j = 0; j < kKeptCount; ++j)
                    same = same && (k.count(kept_at(j).name) == 1) == keep_name(kept_at(j).name, context_at(i), x == 1);
                same = same && k.size() <= static_cast<size_t>(kKeptCount);
            }
        EU_CHECK(same, "file override and in-memory fallback keep exactly the same fields");
    }
    // ---------------------------------------------------------------- settings: one object (eu::Options), the hook's view of it
    {
        namespace eu = turbo::eu;
        nlohmann::json g = nlohmann::json::object();
        const Settings d = eu::Options::load(g).hook_settings();
        EU_CHECK(d.enabled && !d.experimental && d.hook && d.context_off == 0, "defaults: stage 1 on, experimental off, hook on");
        eu::Options o;
        o.experimental = true;
        o.main_menu = false;                                        // contexts 5 and 6
        o.files_off.insert(eu::avatar_path("managercareer_edit"));  // context 2
        o.save(g);
        g[eu::kSettingsKey]["files_off"].push_back(7);  // malformed entries are ignored
        const Settings r = eu::Options::load(g).hook_settings();
        EU_CHECK(r.enabled && r.experimental && r.hook && r.context_off == ((1u << 2) | (1u << 5) | (1u << 6)) && !r.context_on(5) &&
                     r.context_on(0),
                 "the hook follows the file override's screen and file switches");
        g[eu::kSettingsKey]["enabled"] = "yes";
        EU_CHECK(eu::Options::load(g).hook_settings().enabled, "a malformed value keeps the default");
        g[eu::kSettingsKey]["note"] = "kept";
        eu::Options h = eu::Options::load(g);
        h.hook = false;
        h.save(g);
        const eu::Options back = eu::Options::load(g);
        EU_CHECK(!back.hook && back.experimental && !back.main_menu && back.files_off.count(eu::avatar_path("managercareer_edit")) &&
                     !back.career_settings && g[eu::kSettingsKey]["note"] == "kept",
                 "saving the hook switch keeps every file switch (one load / save path)");
        EU_CHECK(!Settings().context_on(-1) && !Settings().context_on(kContextCount), "context_on is bounded");
    }
    // ---------------------------------------------------------------- the walk: nested children and the keep-list
    {
        g_regions.clear();
        FakeConfig f(sample());
        HookCounters c;
        EU_CHECK(load(f, kEditPlayers, gate_on(), c) == 0, "career Edit Player config patched");
        EU_CHECK(ed(f.at(0, 0)) && vis(f.at(0, 0)), "FIRST_NAME becomes editable");
        EU_CHECK(!ed(f.at(0, 1)) && !vis(f.at(0, 1)), "TEAM stays locked and hidden");
        EU_CHECK(!ed(f.at(0, 2)), "player GENDER stays locked");
        EU_CHECK(vis(f.at(0, 3)), "HEIGHT becomes visible");
        uint8_t* grp = f.at(1, 0);
        EU_CHECK(ed(FakeConfig::child(grp, 0)), "a child (KIT_NAME) is unlocked");
        EU_CHECK(!ed(FakeConfig::child(grp, 1)) && !vis(FakeConfig::child(grp, 1)), "a kept child (BODY_TYPE) is left alone");
        uint8_t* sub = FakeConfig::child(grp, 2);
        EU_CHECK(!ed(FakeConfig::child(sub, 0)) && !vis(FakeConfig::child(sub, 0)), "a kept grandchild (PREFERRED_POSITION) is left alone");
        EU_CHECK(ed(FakeConfig::child(sub, 1)) && vis(FakeConfig::child(sub, 1)), "a grandchild (COMMENTARY_NAME) is unlocked");
        EU_CHECK(f.at(0, 0)[kL.item_editable + 1] == 0, "isMandatory is never written");
        // FIRST_NAME ed, HEIGHT vis, KIT_NAME ed, COMMENTARY_NAME ed+vis = 5 (groups were already 1/1)
        EU_CHECK(c.configs.load() == 1 && c.fields.load() == 5 && c.kept.load() == 4 && c.loads.load() == 1 && c.last_context.load() == 0,
                 "counters: 1 config, 5 fields, 4 kept");
        const auto snap = f.snapshot();
        EU_CHECK(load(f, kEditPlayers, gate_on(), c) == 0 && f.same(snap) && c.fields.load() == 5, "a second pass changes nothing");
        const std::vector<std::string> lines = status_lines(StatusInput(), c);
        EU_CHECK(!lines.empty() && lines[0] == "Game editors hook: on | configs patched 2 | fields unlocked 5", "Status tab line");
    }
    // ---------------------------------------------------------------- manager gender (experimental)
    {
        g_regions.clear();
        FakeConfig f({{item(29, 0, 0), item(24, 0, 1)}});
        HookCounters c;
        load(f, kEditManager, gate_on(), c);
        EU_CHECK(!ed(f.at(0, 0)) && ed(f.at(0, 1)), "manager GENDER kept without the experimental switch");
        Gate g = gate_on();
        g.settings.experimental = true;
        load(f, kEditManager, g, c);
        EU_CHECK(ed(f.at(0, 0)) && vis(f.at(0, 0)), "manager GENDER unlocked by the experimental switch");
        FakeConfig p({{item(29, 0, 0)}});
        load(p, kEditPlayers, g, c);
        EU_CHECK(!ed(p.at(0, 0)), "player GENDER stays locked even under the experimental switch");
    }
    // ---------------------------------------------------------------- unknown path, switches, kill switch
    {
        g_regions.clear();
        FakeConfig f(sample());
        const auto snap = f.snapshot();
        HookCounters c;
        const char* automatch = "data/avatar/avatarcustomizationcfg_automatch.json";
        EU_CHECK(on_config_loaded(nullptr, automatch, std::strlen(automatch), gate_on(), expected_keep_ids(), &fake_readable, c) < 0 &&
                     c.loads.load() == 0,
                 "an unknown path is left alone without looking at the config");
        EU_CHECK(load(f, "data/avatar/avatarcustomizationcfg_managercareer_editplayers_online.json", gate_on(), c) < 0 && f.same(snap),
                 "the online variant is left alone");
        Gate k = gate_on();
        k.kill_switch = true;
        EU_CHECK(load(f, kEditPlayers, k, c) < 0 && f.same(snap), "kill switch: nothing written");
        Gate dead;  // not live (hook killed / global switch)
        EU_CHECK(load(f, kEditPlayers, dead, c) < 0 && f.same(snap), "hook not live: nothing written");
        Gate off = gate_on();
        off.settings.enabled = false;
        EU_CHECK(load(f, kEditPlayers, off, c) < 0 && f.same(snap), "Game editors off: nothing written");
        Gate nohook = gate_on();
        nohook.settings.hook = false;
        EU_CHECK(load(f, kEditPlayers, nohook, c) < 0 && f.same(snap), "in-memory fallback off: nothing written");
        Gate file_off = gate_on();
        file_off.settings.context_off = 1u << 0;
        EU_CHECK(load(f, kEditPlayers, file_off, c) < 0 && f.same(snap), "per-file toggle off: nothing written");
        EU_CHECK(c.skipped_off.load() == 5 && c.configs.load() == 0 && c.loads.load() == 5, "skips counted");
        StatusInput in;
        in.kill_switch = true;
        EU_CHECK(status_lines(in, c)[0].find("off (turbo_output\\edit_unlock_hook_off.txt present)") != std::string::npos,
                 "Status line names the kill switch");
        in.kill_switch = false;
        in.settings.enabled = false;
        EU_CHECK(status_lines(in, c)[0].find("switched off") != std::string::npos, "Status line names the switch");
        StatusInput gone;
        gone.off_reason = "game build";
        EU_CHECK(status_lines(gone, c).size() == 1 && status_lines(gone, c)[0] == "Game editors hook: off (game build)",
                 "Status line when not installed");
        EU_CHECK(load(f, kEditPlayers, gate_on(), c, KeepIds()) < 0 && f.same(snap) && c.refused.load() == 1,
                 "unresolved keep-list ids: refused, nothing written");
    }
    // ---------------------------------------------------------------- walk bounds (validated before any write)
    {
        HookCounters c;
        g_regions.clear();
        {   // a bad vector in the second category: the first category's items are not written either
            FakeConfig f(sample());
            uint8_t* cb = nullptr;
            std::memcpy(&cb, f.root(), 8);
            uint8_t* b = nullptr;
            std::memcpy(&b, cb + kL.cat_stride + kL.cat_items, 8);
            uint8_t* e = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(b) - kL.item_stride);  // end before begin
            std::memcpy(cb + kL.cat_stride + kL.cat_items + 8, &e, 8);
            const auto snap = f.snapshot();
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0 && f.same(snap), "end < begin: refused, nothing written");
            EU_CHECK(std::strstr(c.last_error.load(), "malformed") != nullptr, "the reason is kept");
            e = b + kL.item_stride + 8;  // not a multiple of the item size
            std::memcpy(cb + kL.cat_stride + kL.cat_items + 8, &e, 8);
            const auto snap2 = f.snapshot();
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0 && f.same(snap2), "size not a multiple of 0xA8: refused");
        }
        {   // a flag byte that is not a bool: the layout moved
            g_regions.clear();
            FakeConfig f(sample());
            f.at(1, 0)[kL.item_visible] = 7;
            const auto snap = f.snapshot();
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0 && f.same(snap), "flag byte 7: refused, nothing written");
        }
        {   // elements outside readable memory
            g_regions.clear();
            FakeConfig f(sample());
            g_regions.erase(g_regions.begin() + 2);  // config, categories, then the first category's item buffer
            const auto snap = f.snapshot();
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0 && f.same(snap), "unreadable items: refused");
        }
        {   // nested deeper than any editor config
            FakeItem deep = item(24, 0, 0);
            for (int i = 0; i < kMaxDepth + 2; ++i) deep = group(5, {deep});
            FakeConfig f({{deep}});
            const auto snap = f.snapshot();
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0 && f.same(snap), "too deep: refused");
        }
        {   // too many categories
            std::vector<std::vector<FakeItem>> many(kMaxCategories + 1);
            FakeConfig f(many);
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0, "too many categories: refused");
        }
        {   // too many items in one vector
            std::vector<FakeItem> lots(kMaxItemsPerVector + 1, item(24, 0, 0));
            FakeConfig f({lots});
            const auto snap = f.snapshot();
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) < 0 && f.same(snap), "too many items: refused");
        }
        {   // empty config / empty categories are fine
            FakeConfig f(std::vector<std::vector<FakeItem>>{});
            EU_CHECK(load(f, kEditPlayers, gate_on(), c) == 0, "an empty config is accepted (nothing to do)");
            FakeConfig g2({{}, {}});
            EU_CHECK(load(g2, kEditPlayers, gate_on(), c) == 0, "empty categories are accepted");
        }
        EU_CHECK(c.refused.load() == 7, "every refusal counted");
        WalkResult r = unlock_parsed_config(nullptr, context_at(0), false, expected_keep_ids(), &fake_readable, true);
        EU_CHECK(!r.ok, "no config: refused");
        g_regions.clear();
    }
    // ---------------------------------------------------------------- the built-in signatures
    {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        const char* names[] = {"edit_cfg_loader",      "edit_cfg_category_stride", "edit_cfg_category_items", "edit_cfg_item_vector",
                               "edit_cfg_item_children", "edit_cfg_item_flags",     "edit_cfg_item_ids",       "edit_cfg_item_stride",
                               "edit_attr_body_type",  "edit_attr_gender",         "edit_attr_preferred_position", "edit_attr_team"};
        bool all = t != nullptr;
        for (const char* n : names) {
            const Signature* s = t ? t->find(n) : nullptr;
            std::vector<uint8_t> bytes;
            std::vector<bool> mask;
            all = all && s && parse_pattern(s->pattern, bytes, mask) && bytes.size() >= 20;
        }
        EU_CHECK(all, "the game editors' signatures are in the built-in table and parse");
        // each attribute guard pins its id in the pattern (mov dword [rax], id) and resolves the name string
        const char* attrs[] = {"edit_attr_body_type", "edit_attr_gender", "edit_attr_preferred_position", "edit_attr_team"};
        bool ids_ok = t != nullptr;
        for (int i = 0; i < kKeptCount && t; ++i) {
            const Signature* s = t->find(attrs[i]);
            char tail[24];
            std::snprintf(tail, sizeof(tail), "C7 00 %02X 00 00 00", kept_at(i).id);
            ids_ok = ids_ok && s && s->resolve == "rip" && s->pattern.size() > 17 &&
                     s->pattern.compare(s->pattern.size() - 17, 17, tail) == 0;
        }
        EU_CHECK(ids_ok, "the attribute guards pin BODY_TYPE / GENDER / PREFERRED_POSITION / TEAM to the rule table's ids");
    }
}
