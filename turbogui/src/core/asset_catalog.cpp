// FC 27 LE Turbo GUI - game asset catalog (asset_catalog.h).
// WP-01 scaffolding: names, paths, switches and the scope filter are real; loading, queries, the write gate, the write
// log and the Lua gate are stubs that report "not loaded" (WP-05 implements them).
#include "asset_catalog.h"

#include <system_error>

namespace turbo::assets {

namespace fs = std::filesystem;

namespace {
constexpr const char* kFamilyKeys[kFamilyCount] = {
    "head3d", "miniface", "youthface", "staffface", "legendface", "hair", "facialhair", "boots", "gkglove",
    "tattoo", "accessory", "eyebrow", "skinsurface", "crest", "kit", "ball", "callnames",
};
bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !ec;
}
}  // namespace

const char* family_key(Family f) {
    const size_t i = static_cast<size_t>(f);
    return i < kFamilyCount ? kFamilyKeys[i] : "";
}

bool family_from_key(const std::string& key, Family& out) {
    for (size_t i = 0; i < kFamilyCount; ++i)
        if (key == kFamilyKeys[i]) {
            out = static_cast<Family>(i);
            return true;
        }
    return false;
}

bool family_has_models(Family f) {
    switch (f) {
        case Family::Hair:
        case Family::Facialhair:
        case Family::Boots:
        case Family::Gkglove:
        case Family::Tattoo:
        case Family::Accessory:
        case Family::Ball:
            return true;
        default:
            return false;
    }
}

const char* call_kind_key(CallKind k) {
    switch (k) {
        case CallKind::Generic: return "generic";
        case CallKind::Simple: return "simple";
        case CallKind::Link: return "link";
        case CallKind::High: return "high";
        default: return "";
    }
}

std::string name_source_label(const std::string& source) {
    if (source.empty()) return "";
    if (source.rfind("master:", 0) == 0) return "name from: " + source.substr(7) + " master";
    if (source == "workbook") return "name from: FC 26 workbook";
    if (source == "extra_names") return "name from: extra player names";
    if (source == "slug") return "name from: game file name";
    if (source == "kit_slug") return "name from: kit folder name";
    if (source == "managers") return "name from: managers list";
    return "name from: " + source;
}

std::string fallback_name(Family f, int64_t id) {
    const char* what = "Asset";
    switch (f) {
        case Family::Head3d: what = "Head"; break;
        case Family::Miniface: what = "Miniface"; break;
        case Family::Youthface: what = "Youth face"; break;
        case Family::Staffface: what = "Staff face"; break;
        case Family::Legendface: what = "Legend picture"; break;
        case Family::Hair: what = "Hair"; break;
        case Family::Facialhair: what = "Facial hair"; break;
        case Family::Boots: what = "Boots"; break;
        case Family::Gkglove: what = "GK gloves"; break;
        case Family::Tattoo: what = "Tattoo"; break;
        case Family::Accessory: what = "Accessory"; break;
        case Family::Eyebrow: what = "Eyebrows"; break;
        case Family::Skinsurface: what = "Skin surface"; break;
        case Family::Crest:
        case Family::Kit: what = "Team"; break;
        case Family::Ball: what = "Ball"; break;
        case Family::Callnames: what = "Callname"; break;
        default: break;
    }
    return std::string(what) + " #" + std::to_string(id);
}

const char* state_name(State s) {
    switch (s) {
        case State::NotLoaded: return "not loaded";
        case State::Off: return "off";
        case State::Current: return "current";
        case State::Stale: return "stale";
        case State::Error: return "error";
    }
    return "";
}

Switches read_switches(const fs::path& turbo_output) {
    Switches s;
    s.catalog_off = file_exists(catalog_off_path(turbo_output));
    s.hidden_off = file_exists(hidden_off_path(turbo_output));
    return s;
}

fs::path catalog_dir(const fs::path& turbo_output) { return turbo_output / "asset_catalog"; }
fs::path catalog_file(const fs::path& turbo_output, const std::string& build_key) {
    return catalog_dir(turbo_output) / ("catalog_" + build_key + ".json");
}
fs::path catalog_off_path(const fs::path& turbo_output) { return turbo_output / "asset_catalog_off.txt"; }
fs::path hidden_off_path(const fs::path& turbo_output) { return turbo_output / "asset_hidden_off.txt"; }
fs::path lua_gate_path(const fs::path& turbo_output) { return catalog_dir(turbo_output) / "lua_gate.txt"; }
fs::path write_log_path(const fs::path& turbo_output) { return turbo_output / "asset_writes.log"; }

std::string rebuild_command() {
    return "re_venv\\Scripts\\python.exe -I turbo\\tools\\asset_catalog\\build_asset_catalog.py";
}

bool append_write_log(const fs::path&, const WriteLogEntry&, std::string* err) {
    if (err) *err = "asset write log: not implemented yet";
    return false;
}

const char* scope_label(Scope s) {
    switch (s) {
        case Scope::All: return "All";
        case Scope::InSave: return "In this save";
        case Scope::Hidden: return "Hidden";
    }
    return "";
}

bool scope_shows(Scope s, bool hidden) {
    switch (s) {
        case Scope::All: return true;
        case Scope::InSave: return !hidden;
        case Scope::Hidden: return hidden;
    }
    return true;
}

// ---------------------------------------------------------------- Catalog (stub: never loaded)
struct Catalog::Impl {
    Status status;
};

Catalog::Catalog() : d_(std::make_unique<Impl>()) { d_->status.error = "asset catalog: not implemented yet"; }
Catalog::~Catalog() = default;

bool Catalog::load_file(const fs::path& file, const std::string& running_key, std::string* err) {
    d_->status = Status{};
    d_->status.file = file.u8string();
    d_->status.running_key = running_key;
    d_->status.error = "asset catalog: loading is not implemented yet";
    if (err) *err = d_->status.error;
    return false;
}

bool Catalog::load_dir(const fs::path& turbo_output, const std::string& running_key, std::string* err) {
    d_->status = Status{};
    d_->status.running_key = running_key;
    const Switches sw = read_switches(turbo_output);
    d_->status.hidden_off = sw.hidden_off;
    if (sw.catalog_off) {
        d_->status.state = State::Off;
        return false;
    }
    d_->status.error = "asset catalog: loading is not implemented yet";
    if (err) *err = d_->status.error;
    return false;
}

const Status& Catalog::status() const { return d_->status; }
State Catalog::state() const { return d_->status.state; }
bool Catalog::loaded() const { return d_->status.state == State::Current || d_->status.state == State::Stale; }
bool Catalog::current() const { return d_->status.state == State::Current; }
bool Catalog::hidden_off() const { return d_->status.hidden_off; }

std::string Catalog::status_line() const {
    if (d_->status.state == State::Off) return "off (asset_catalog_off.txt)";
    return state_name(d_->status.state);
}

uint8_t Catalog::proof(Family, int64_t) const { return kProofNone; }
bool Catalog::has(Family f, int64_t id) const { return proof(f, id) != kProofNone; }
bool Catalog::has_model(Family f, int64_t id) const { return (proof(f, id) & kProofModel) != 0; }
std::vector<int64_t> Catalog::ids(Family) const { return {}; }
std::string Catalog::preview_path(Family, int64_t) const { return ""; }
std::vector<int> Catalog::head_vars(int64_t) const { return {}; }
std::string Catalog::head_toc(int64_t) const { return ""; }
bool Catalog::generic_head(int64_t) const { return false; }
std::string Catalog::kit_slug(int64_t) const { return ""; }
std::vector<std::string> Catalog::kits(int64_t) const { return {}; }
std::vector<std::string> Catalog::callname_languages() const { return {}; }
std::vector<std::string> Catalog::callname_teams(const std::string&) const { return {}; }
std::vector<int64_t> Catalog::callname_ids(const std::string&, const std::string&, CallKind) const { return {}; }
bool Catalog::has_recording(const std::string&, const std::string&, int64_t) const { return false; }
Name Catalog::name(NameKind, int64_t) const { return {}; }

bool Catalog::writable(Family, int64_t, std::string* why) const {
    if (why) *why = "the asset catalog is not loaded";
    return false;
}

bool Catalog::hidden(Family, int64_t, const DbUse&) const { return false; }
bool Catalog::hidden_generic_callname(int64_t, const DbUse&) const { return false; }
bool Catalog::not_in_tattoo_table(int64_t, const DbUse&) const { return false; }

std::string Catalog::lua_gate_text() const { return ""; }

bool Catalog::sync_lua_gate(const fs::path& turbo_output, std::string* err) const {
    // No current catalog: the gate file must not exist (Lua falls back to today's rules)
    std::error_code ec;
    fs::remove(lua_gate_path(turbo_output), ec);
    if (ec && err) *err = ec.message();
    return !ec;
}

}  // namespace turbo::assets
