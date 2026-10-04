#include "bridge.h"

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

uint64_t parse_hex_addr(const std::string& s) {
    if (s.empty()) return 0;
    const char* p = s.c_str();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) p += 2;
    char* end = nullptr;
    unsigned long long v = std::strtoull(p, &end, 16);
    if (end == p || *end != '\0') return 0;
    return static_cast<uint64_t>(v);
}

std::string hex_addr(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

static bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

static bool write_text_atomic(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
    }
    return !ec;
}

static uint64_t addr_field(const json& j, const char* key) {
    if (!j.contains(key)) return 0;
    const json& v = j[key];
    if (v.is_string()) return parse_hex_addr(v.get<std::string>());
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer()) return static_cast<uint64_t>(v.get<int64_t>());
    return 0;
}

static bool parse_meta_impl(const std::string& text, DbMeta& out, std::string* err) {
    out = DbMeta();
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = "bridge_meta.json is not valid JSON";
        return false;
    }
    if (!j.contains("shortname_name_tables_map") || !j.contains("field_desc_map")) {
        if (err) *err = "bridge_meta.json lacks shortname_name_tables_map / field_desc_map";
        return false;
    }
    const json& names = j["shortname_name_tables_map"];
    const json& fields = j["field_desc_map"];
    if (!names.is_object() || !fields.is_object()) {
        if (err) *err = "bridge_meta.json maps have the wrong shape";
        return false;
    }
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (!it.value().is_string() || it.key().size() != 4) continue;
        TableMeta tm;
        tm.name = it.value().get<std::string>();
        if (fields.contains(it.key()) && fields[it.key()].is_object()) {
            for (auto f = fields[it.key()].begin(); f != fields[it.key()].end(); ++f) {
                const json& fd = f.value();
                if (!fd.is_object() || !fd.contains("name") || !fd["name"].is_string()) continue;
                FieldMeta fm;
                fm.name = fd["name"].get<std::string>();
                fm.depth = fd.contains("depth") && fd["depth"].is_number() ? fd["depth"].get<int>() : 0;
                fm.min = fd.contains("min") && fd["min"].is_number() ? static_cast<int64_t>(fd["min"].get<double>()) : 0;
                tm.fields[f.key()] = fm;
            }
        }
        out.tables[it.key()] = tm;
    }
    if (out.tables.empty()) {
        if (err) *err = "bridge_meta.json has no tables";
        return false;
    }
    return true;
}

static bool parse_state_impl(const std::string& text, BridgeState& out) {
    out = BridgeState();
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        out.error = "bridge_state.json is not valid JSON";
        return false;
    }
    out.session = j.value("session", std::string());
    out.seq = j.value("seq", 0LL);
    out.db_gen = j.value("db_gen", 0LL);
    out.le_version = j.value("le_version", std::string());
    out.db_service = addr_field(j, "db_service");
    out.comm_service = addr_field(j, "comm_service");
    out.ifce = addr_field(j, "ifce");
    out.svm = addr_field(j, "svm");
    out.managers = addr_field(j, "managers");
    out.in_cm = j.value("in_cm", false);
    out.user_team = j.value("user_team", 0LL);
    out.names_count = j.value("names_count", 0LL);
    out.transfer_budget = j.contains("transfer_budget") && j["transfer_budget"].is_number() ? j["transfer_budget"].get<long long>() : -1;
    out.turbo_made.clear();
    if (j.contains("turbo_made") && j["turbo_made"].is_array())  // an empty Lua table may arrive as {}
        for (const auto& v : j["turbo_made"])
            if (v.is_string()) out.turbo_made.push_back(v.get<std::string>());
    out.unavailable.clear();
    if (j.contains("unavailable") && j["unavailable"].is_object())  // an empty Lua table may arrive as []
        for (auto it = j["unavailable"].begin(); it != j["unavailable"].end(); ++it)
            if (it.value().is_string()) out.unavailable[it.key()] = it.value().get<std::string>();
    if (j.contains("meta_error") && j["meta_error"].is_string()) out.meta_error = j["meta_error"].get<std::string>();
    if (j.contains("game_call") && j["game_call"].is_object()) {
        const json& g = j["game_call"];
        out.game_call_seq = g.contains("seq") && g["seq"].is_number() ? g["seq"].get<long long>() : 0;
        out.game_call_ok = g.value("ok", false);
        if (g.contains("text") && g["text"].is_string()) out.game_call_text = g["text"].get<std::string>();
    }
    if (j.contains("date") && j["date"].is_object()) {
        out.date.year = j["date"].value("year", 0);
        out.date.month = j["date"].value("month", 0);
        out.date.day = j["date"].value("day", 0);
    }
    if (j.contains("settings") && j["settings"].is_object()) {
        const json& s = j["settings"];
        out.has_settings = true;
        out.dry_run = s.value("dry_run", false);
        if (s.contains("auto") && s["auto"].is_object()) {
            const json& a = s["auto"];
            if (a.contains("form_morale") && a["form_morale"].is_object()) {
                const json& fm = a["form_morale"];
                out.auto_form_enabled = fm.value("enabled", false);
                out.auto_form = static_cast<int>(fm.value("form", 100.0));
                out.auto_morale = static_cast<int>(fm.value("morale", 100.0));
                out.auto_fitness = static_cast<int>(fm.value("fitness", 0.0));
            }
            if (a.contains("pap_playstyles") && a["pap_playstyles"].is_object())
                out.auto_playstyles_enabled = a["pap_playstyles"].value("enabled", false);
        }
    }
    out.loaded = true;
    return true;
}

bool Bridge::parse_names(const std::string& text, NameMap& out, std::string* session) {
    out.clear();
    size_t pos = text.find('\n');
    std::string header = text.substr(0, pos == std::string::npos ? text.size() : pos);
    if (!header.empty() && header.back() == '\r') header.pop_back();
    if (header.rfind("#turbo-names ", 0) != 0) return false;
    size_t a = 13, b = header.find(' ', a);
    if (session) *session = header.substr(a, b == std::string::npos ? std::string::npos : b - a);
    long long expected = b == std::string::npos ? -1 : std::strtoll(header.c_str() + b + 1, nullptr, 10);
    long long lines = 0;
    while (pos != std::string::npos && pos + 1 < text.size()) {
        size_t start = pos + 1;
        pos = text.find('\n', start);
        size_t end = pos == std::string::npos ? text.size() : pos;
        if (end > start && text[end - 1] == '\r') --end;
        size_t tab = text.find('\t', start);
        if (tab == std::string::npos || tab >= end || tab == start) continue;
        char* stop = nullptr;
        std::string id_text = text.substr(start, tab - start);
        long long id = std::strtoll(id_text.c_str(), &stop, 10);
        if (!stop || *stop != '\0') continue;
        std::string name = text.substr(tab + 1, end - tab - 1);
        if (!name.empty()) {
            out[static_cast<int64_t>(id)] = std::move(name);
            ++lines;
        }
    }
    // Lua rewrites the file in place: a read while it is being written sees fewer lines than the header announces
    return expected < 0 || lines == expected;
}

// Wrong value types in the files must never throw into the game's render thread
bool Bridge::parse_meta(const std::string& text, DbMeta& out, std::string* err) {
    try {
        return parse_meta_impl(text, out, err);
    } catch (const std::exception& e) {
        out = DbMeta();
        if (err) *err = std::string("bridge_meta.json: ") + e.what();
        return false;
    }
}

bool Bridge::parse_state(const std::string& text, BridgeState& out) {
    try {
        return parse_state_impl(text, out);
    } catch (const std::exception& e) {
        out = BridgeState();
        out.error = std::string("bridge_state.json: ") + e.what();
        return false;
    }
}

fs::path Bridge::locate(const char* name) const {
    std::error_code ec;
    fs::path a = dir() / name;
    if (fs::exists(a, ec)) return a;
    fs::path b = root_ / name;
    if (fs::exists(b, ec)) return b;
    return a;
}

// Lua rewrites these files in place, so a read can catch one half written. A file that does not
// parse is read again on the next poll (the last good copy stays in use); after a few failures at
// the same timestamp the error is reported.
bool Bridge::poll_files() {
    bool changed = false;
    std::error_code ec;
    fs::path meta_p = locate("bridge_meta.json");
    auto mt = fs::last_write_time(meta_p, ec);
    if (!ec && !too_old(mt) && (!have_meta_time_ || mt != meta_time_)) {
        std::string text;
        if (read_text(meta_p, text)) {
            DbMeta m;
            std::string err;
            if (parse_meta(text, m, &err)) {
                meta_ = std::move(m);
                meta_error_.clear();
                meta_time_ = mt;
                have_meta_time_ = true;
                meta_failures_ = 0;
                changed = true;
            } else if (++meta_failures_ >= 4) {
                if (meta_.empty()) meta_error_ = err;
                meta_time_ = mt;
                have_meta_time_ = true;
                meta_failures_ = 0;
                changed = true;
            }
        }
    }
    ec.clear();
    fs::path state_p = locate("bridge_state.json");
    auto st = fs::last_write_time(state_p, ec);
    if (!ec && !too_old(st) && (!have_state_time_ || st != state_time_)) {
        std::string text;
        if (read_text(state_p, text)) {
            BridgeState s;
            if (parse_state(text, s)) {
                state_ = s;
                state_time_ = st;
                have_state_time_ = true;
                state_failures_ = 0;
                changed = true;
            } else if (++state_failures_ >= 4) {
                if (!state_.loaded) state_ = s;
                state_time_ = st;
                have_state_time_ = true;
                state_failures_ = 0;
                changed = true;
            }
        }
    }
    ec.clear();
    fs::path names_p = locate("bridge_names.txt");
    auto nt = fs::last_write_time(names_p, ec);
    if (!ec && !too_old(nt) && (!have_names_time_ || nt != names_time_)) {
        std::string text;
        if (read_text(names_p, text)) {
            auto m = std::make_shared<NameMap>();
            try {
                if (parse_names(text, *m)) {
                    names_ = std::move(m);
                    changed = true;
                    names_time_ = nt;
                    have_names_time_ = true;
                    names_failures_ = 0;
                } else if (++names_failures_ >= 4) {  // not a names file: stop re-reading it until it changes
                    names_time_ = nt;
                    have_names_time_ = true;
                    names_failures_ = 0;
                }  // else: half written; read again on the next poll
            } catch (const std::exception&) {
                names_time_ = nt;  // out of memory or similar: keep the previous names
                have_names_time_ = true;
            }
        }
    }
    return changed;
}

bool Bridge::publish_mailbox(uint64_t mailbox_addr, const std::string& session, const std::string& gui_version) {
    json j;
    j["mailbox"] = hex_addr(mailbox_addr);
    j["session"] = session;
    j["gui_version"] = gui_version;
    // Live stamp (unix seconds): Lua only trusts this file while it is fresh, so an address left over from an
    // earlier game session is never read. App::tick refreshes it every couple of seconds.
    j["updated"] = static_cast<int64_t>(std::time(nullptr));
    return write_text_atomic(dir() / "bridge_dll.json", j.dump(2));
}

bool Bridge::write_gui_settings(const std::string& json_text) {
    return write_text_atomic(root_ / "turbo_output" / "gui_settings.json", json_text);
}

// ---------------------------------------------------------------- mailbox
bool Mailbox::init() {
    std::vector<uint8_t> zero(kMailboxSize, 0);
    if (!mem_.write(addr_, zero.data(), zero.size())) return false;
    uint32_t magic = kMailboxMagic, ver = kMailboxVersion;
    return mem_.wr(addr_, magic) && mem_.wr(addr_ + 4, ver);
}

bool Mailbox::pending() {
    int32_t ack = 0;
    if (!mem_.rd(addr_ + kMbAckSeq, ack)) return false;
    return ack != seq_;
}

bool Mailbox::submit(const std::string& text, std::string* err) {
    if (pending()) {
        if (err) *err = "a command is still running";
        return false;
    }
    if (text.size() >= kMbTextSize) {
        if (err) *err = "command too long";
        return false;
    }
    std::vector<uint8_t> clear(kMbTextSize, 0);
    if (!mem_.write(addr_ + kMbResult, clear.data(), clear.size())) return false;
    std::vector<uint8_t> cmd(kMbTextSize, 0);
    std::memcpy(cmd.data(), text.data(), text.size());
    if (!mem_.write(addr_ + kMbCmd, cmd.data(), cmd.size())) return false;
    int32_t next = seq_ + 1;
    if (!mem_.wr(addr_ + kMbCmdSeq, next)) return false;
    seq_ = next;
    return true;
}

bool Mailbox::take_result(bool& ok, std::string& result) {
    if (seq_ == taken_ || pending()) return false;
    int32_t status = 0;
    mem_.rd(addr_ + kMbStatus, status);
    ok = status == 1;
    result = mem_.read_cstr(addr_ + kMbResult, kMbTextSize);
    taken_ = seq_;
    return true;
}

void Mailbox::cancel() {
    if (!pending()) return;
    mem_.wr(addr_ + kMbAckSeq, seq_);
    taken_ = seq_;
}

int32_t Mailbox::heartbeat() {
    int32_t hb = 0;
    mem_.rd(addr_ + kMbHeartbeat, hb);
    return hb;
}

}  // namespace turbo
