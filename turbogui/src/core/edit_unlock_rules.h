// FC 27 LE Turbo GUI - unlocking the game's own editors: the shared rule table (research/edit_unlock_plan.md).
//
// The greyed-out / hidden fields of the game's player and manager editors come from one JSON config per screen,
// data/avatar/avatarcustomizationcfg_<context>.json, read by the config loader 0x1470F0D50 through the legacy-file
// service. Turbo unlocks them in two ways that share THIS table (self-contained: no other Turbo header):
//   * the data override (Live Editor's mods\legacy folder): name-based recipe, keep_name();
//   * the in-memory fallback (win/edit_unlock_hook_win.cpp): a post-hook on the loader walks the parsed config and sets
//     isEditable / isVisible, keep_id() with the attribute ids the host verified against the game's own enum table.
//
// Parsed config layout (build 6AB9813C-211EF000, every offset pinned by a layout-guard signature in core/sigscan.cpp):
//   config:   eastl::vector<Category> at +0x00 (begin, end, capacity)
//   Category: 0x58 bytes, int id +0x00, eastl::vector<Item> items +0x08
//   Item:     0xA8 bytes, int group id +0x00 (the "name" when the parser reads a group), int attribute id +0x08 (the
//             "name" as AttributeName: TEAM, GENDER, ...), bool isEditable +0x0C, isMandatory +0x0D, isVisible +0x0E,
//             eastl::vector<Item> children +0x88
// The walk never allocates (it runs inside the game's loader): bounded counts, bounded depth, every vector checked
// readable before it is touched, and a first pass that only validates (a flag byte that is not 0/1 means the layout
// moved: nothing is written).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "nlohmann/json.hpp"

namespace turbo {
namespace edit_unlock {

// ---------------------------------------------------------------- the editors Turbo unlocks
enum class Group { CareerPlayers, CreateClubPlayers, Manager, MainMenu };

struct Context {
    const char* name;   // <context> of avatarcustomizationcfg_<context>.json
    Group group;
    bool manager;       // a manager editor (GENDER may be unlocked by the experimental switch)
    const char* label;  // GUI label ("Details")
};

// The unlocked contexts (never *_online, managerlive_*, playercareer_*, clubs*, tournament_*)
constexpr int kContextCount = 7;
const Context& context_at(int i);  // 0 <= i < kContextCount
// The context of a config path ("data/avatar/avatarcustomizationcfg_managercareer_editplayers.json", '/' or '\\',
// any case); -1 when the file is not one Turbo unlocks.
int context_index(const char* path, size_t len);
int context_index(const std::string& path);
const char* group_label(Group g);

// ---------------------------------------------------------------- the keep-list
// Fields that stay as the game ships them: TEAM (a transfer outside the transfer engine), GENDER (model, kit and
// commentary; a manager's only under the experimental switch), PREFERRED_POSITION (set by ROLE), BODY_TYPE.
struct KeptAttribute {
    const char* name;
    int id;  // AttributeName enum value on build 6AB9813C-211EF000 (the host re-checks it against the game's table)
};
constexpr int kKeptCount = 4;
const KeptAttribute& kept_at(int i);  // BODY_TYPE, GENDER, PREFERRED_POSITION, TEAM
// Data override: keep this field (by its JSON "name")?
bool keep_name(const std::string& name, const Context& c, bool experimental);

// The attribute ids the host resolved (-2 = unknown: such an id never matches, so an unresolved table must not be used;
// the hook is not installed then)
struct KeepIds {
    int ids[kKeptCount] = {-2, -2, -2, -2};  // same order as kept_at()
    bool complete() const;
};
// The ids of this build as the table above states them (tests; the host builds its own from the game)
KeepIds expected_keep_ids();
// In-memory: keep the item whose group id / attribute id is this?
bool keep_id(int id, const KeepIds& ids, const Context& c, bool experimental);

// ---------------------------------------------------------------- settings (gui_settings.json "edit_unlock")
// {"enabled": true, "experimental": false, "hook": true, "off": ["mainmenu_edit_real", ...]}
struct Settings {
    bool enabled = true;        // "Game editors": stage 1 is on by default
    bool experimental = false;  // "Unlock everything (experimental)"
    bool hook = true;           // the in-memory fallback may patch what the game loaded
    uint32_t context_off = 0;   // bit i = context_at(i) switched off in "Details"
    bool context_on(int i) const { return i >= 0 && i < kContextCount && !(context_off & (1u << i)); }
};
constexpr const char* kSettingsKey = "edit_unlock";
Settings settings_from_json(const nlohmann::json& gui_settings);
void settings_to_json(const Settings& s, nlohmann::json& gui_settings);

// ---------------------------------------------------------------- the parsed-config walk
struct Layout {
    size_t cat_stride = 0x58, cat_items = 0x08;
    size_t item_stride = 0xA8, item_group_id = 0x00, item_attr_id = 0x08, item_editable = 0x0C, item_visible = 0x0E,
           item_children = 0x88;
};
constexpr size_t kMaxCategories = 64;
constexpr size_t kMaxItemsPerVector = 1024;
constexpr size_t kMaxItemsTotal = 8192;
constexpr int kMaxDepth = 6;

// May the walk read and write [p, p + n)? (the host: committed read-write memory; tests: their fake buffers)
using ReadableFn = bool (*)(const void* p, size_t n);

struct WalkResult {
    bool ok = false;
    const char* error = "";   // static text only (no allocation inside the hook)
    uint32_t categories = 0;
    uint32_t items = 0;       // items seen (children included)
    uint32_t kept = 0;        // items left alone by the keep-list
    uint32_t unlocked = 0;    // flag bytes changed 0 -> 1 (isEditable and isVisible count separately)
};

// Validates the whole tree first; only when it is sound (and apply) sets isEditable / isVisible to 1 on every item the
// keep-list does not keep. Never allocates; never reads outside what `readable` allowed.
WalkResult unlock_parsed_config(uint8_t* config, const Context& c, bool experimental, const KeepIds& ids,
                                ReadableFn readable, bool apply, const Layout& layout = Layout());

}  // namespace edit_unlock
}  // namespace turbo
