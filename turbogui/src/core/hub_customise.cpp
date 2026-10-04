// FC 27 LE Turbo GUI - "Reopen club customisation" (see hub_customise.h)
#include "hub_customise.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <system_error>
#include <vector>

#include "standings_refresh.h"  // svm::manager_at: the walk of the career manager table

namespace fs = std::filesystem;

namespace turbo {
namespace mhm {

static std::string hx(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

std::string locate(Memory& mem, uint64_t managers, uint64_t vt, int64_t user_team, State& out) {
    out = State();
    out.team = user_team;
    out.created_club = user_team == kCreatedClubTeam;
    if (!managers) return "no career is loaded (the career manager table is not published yet: advance a day or open a menu)";
    if (!vt) return "the MainHubManager vtable is not known on this game build (signature \"mhm_vtable\" not found): nothing is written";
    std::string err;
    const uint64_t obj = svm::manager_at(mem, managers, kTypeId, err);
    if (!obj) return "MainHubManager: " + err;
    uint64_t have = 0;
    if (!mem.rd(obj, have)) return "the object in manager slot 58 (" + hx(obj) + ") is not readable";
    if (have != vt)
        return "the object in manager slot 58 (" + hx(obj) + ") is not the MainHubManager (vtable " + hx(have) + ", expected " + hx(vt) +
               "): the layout of this game build differs, nothing is written";
    uint64_t table = 0;
    if (!mem.rd(obj + kTable, table) || table != managers)
        return "the MainHubManager at " + hx(obj) + " does not point back at the manager table (+0x8 = " + hx(table) + ")";
    std::vector<uint8_t> whole;
    if (!mem.read_block(obj, static_cast<size_t>(kSize), whole))
        return "the MainHubManager at " + hx(obj) + " is not readable over its 0x8A8 bytes";
    out.obj = obj;
    out.licensed = whole[kLicensed];
    out.customised = whole[kCustomised];
    out.new_season = whole[kNewSeason];
    if (out.licensed > 1 || out.customised > 1 || out.new_season > 1) {
        char b[160];
        std::snprintf(b, sizeof(b), "the MainHubManager flags read %u / %u / %u (expected 0 or 1 each): the layout of this game build differs",
                      unsigned(out.licensed), unsigned(out.customised), unsigned(out.new_season));
        out.obj = 0;
        return b;
    }
    return "";
}

std::string describe(const State& s) {
    if (!s.obj) return "not located";
    if (s.licensed) return "Customise club is hidden: your club plays in a licensed stadium";
    if (s.created_club)
        return s.customised ? "Customise club is hidden until next season: your created club already used the designer this season"
                            : "Customise club is available: kits, crest and stadium designer (your created club)";
    return "Customise club is available: the stadium hub (pitch, nets, seats, tifos, chants, goal song)";
}

Result reopen(Memory& mem, uint64_t managers, uint64_t vt, int64_t user_team, bool licensed_too) {
    Result r;
    std::string err = locate(mem, managers, vt, user_team, r.before);
    if (!err.empty()) {
        r.message = err;
        return r;
    }
    const State& b = r.before;
    if (b.licensed && !licensed_too) {
        r.after = b;
        r.message = "your club plays in a licensed stadium: the game hides Customise club for it. Reopening it for such a club "
                    "can replace the real stadium in this save: back up your save, then use the licensed-stadium option";
        return r;
    }
    const uint8_t zero = 0;
    if (b.customised) {
        if (!mem.wr(b.obj + kCustomised, zero)) {
            r.message = "writing the MainHubManager's +0x512 at " + hx(b.obj) + " failed";
            return r;
        }
        r.changed = true;
    }
    if (licensed_too && b.licensed) {
        if (!mem.wr(b.obj + kLicensed, zero)) {
            r.message = "writing the MainHubManager's +0x511 at " + hx(b.obj) + " failed";
            return r;
        }
        r.changed = true;
    }
    err = locate(mem, managers, vt, user_team, r.after);
    if (!err.empty()) {
        r.message = "read back: " + err;
        return r;
    }
    if (!r.after.tile_shown()) {
        r.message = "the flags still hide Customise club after the write (" + describe(r.after) + ")";
        return r;
    }
    r.ok = true;
    if (!r.changed)
        r.message = describe(r.after) + ". Nothing to change: if the hub does not show it, leave the hub and come back";
    else
        r.message = std::string("Customise club reopened") + (licensed_too && b.licensed ? " (licensed stadium too)" : "") +
                    ": leave the hub (for example open Squad) and come back. " +
                    (r.after.created_club ? "Kits, crest and stadium designer." : "Stadium hub.") +
                    " A new season locks it again: press the button again then";
    return r;
}

// ---------------------------------------------------------------- vtable source
static std::mutex g_m;
static std::function<uint64_t(const char*)> g_lookup;

void set_signature_lookup(std::function<uint64_t(const char*)> fn) {
    std::lock_guard<std::mutex> lock(g_m);
    g_lookup = std::move(fn);
}

uint64_t vtable(uint64_t image_base) {
    std::function<uint64_t(const char*)> fn;
    {
        std::lock_guard<std::mutex> lock(g_m);
        fn = g_lookup;
    }
    if (fn) {
        const uint64_t v = fn(kSignature);
        if (v) return v;
    }
    return image_base ? image_base + kRvaVtable : 0;
}

// ---------------------------------------------------------------- save backup
fs::path default_save_dir() {
    const char* la = std::getenv("LOCALAPPDATA");
    if (!la || !*la) return {};
    return fs::u8path(la) / "EA SPORTS FC 27" / "settings";
}

SaveBackup backup_saves(const fs::path& save_dir, const fs::path& out_root, const std::string& stamp) {
    SaveBackup r;
    std::error_code ec;
    if (save_dir.empty() || !fs::is_directory(save_dir, ec)) {
        r.message = "the game's save folder was not found (" + save_dir.u8string() + ")";
        return r;
    }
    std::vector<fs::path> saves;
    for (fs::directory_iterator it(save_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().u8string();
        if (name.size() >= 6 && name.compare(0, 6, "CmMgrC") == 0) saves.push_back(it->path());
    }
    if (saves.empty()) {
        r.message = "no Manager Career save (CmMgrC*) in " + save_dir.u8string();
        return r;
    }
    r.dir = out_root / stamp;
    fs::create_directories(r.dir, ec);
    if (ec) {
        r.message = "cannot create " + r.dir.u8string() + ": " + ec.message();
        return r;
    }
    for (const fs::path& s : saves) {
        fs::copy_file(s, r.dir / s.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            r.message = "copying " + s.filename().u8string() + " failed: " + ec.message();
            return r;
        }
        ++r.files;
    }
    r.ok = true;
    r.message = std::to_string(r.files) + " career save(s) copied to " + r.dir.u8string();
    return r;
}

}  // namespace mhm
}  // namespace turbo
