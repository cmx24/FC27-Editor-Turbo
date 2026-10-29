// FC 27 LE Turbo GUI - Kits & club pages of the GUI revamp (see ui_kits_page.h). New file: every action below is an existing function
// (ui_kits.h apply / restore / status words, the public kitswap::Runtime, the club_*_section editors); nothing here writes the game.
#include "ui_kits_page.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>

#include "app.h"
#include "core/kit_swap.h"
#include "file_picker.h"
#include "imgui.h"
#include "nlohmann/json.hpp"
#include "ui_club_page.h"
#include "ui_club_search.h"
#include "ui_crest_shirt.h"
#include "ui_identity.h"
#include "ui_career_pages.h"
#include "ui_club_tools.h"
#include "ui_kits.h"
#include "ui_page.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace fs = std::filesystem;
using kits::Slot;

namespace {

KitsPageState g_state;
FilePicker g_fp_zip, g_fp_folder;

// What the page re-reads about the selected club and slot at most once a second (files and table snapshots must not be read every frame)
struct Look {
    int64_t team = -1;
    Slot slot = Slot::Unknown;
    double at = -100.0;
    std::vector<KitSlotRow> rows;
    bool colours = false;
    uint8_t c1[3] = {120, 120, 120}, c2[3] = {200, 200, 200}, c3[3] = {120, 120, 120};
    std::string record;  // "Custom kit from <file>, <when>" ("" = no record)
    bool staged = false;
};
Look g_look;

void invalidate_look() { g_look.at = -100.0; }

std::string record_words(App& app, int64_t team, Slot slot) {
    std::ifstream f(kits::slot_dir(app.bridge.root(), team, slot) / "import.json", std::ios::binary);
    if (!f) return std::string();
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::string();
    const std::string src = j.value("source", std::string());
    const std::string when = j.value("when", std::string());
    std::string s = "Custom kit";
    if (!src.empty()) s += " from " + text_path(src).filename().string();
    if (!when.empty()) s += ", " + when;
    return s;
}

void refresh_look(App& app, int64_t team, Slot slot) {
    const double now = ImGui::GetTime();
    if (g_look.team == team && g_look.slot == slot && now - g_look.at < 1.0 && now >= g_look.at) return;
    g_look.team = team;
    g_look.slot = slot;
    g_look.at = now;
    g_look.rows = kit_slot_rows(app, team);
    g_look.colours = false;
    g_look.record.clear();
    g_look.staged = false;
    if (team <= 0) return;
    const Table* kt = app.db.table("teamkits");
    const uint64_t row = kt ? kit_row(app, team, slot) : 0;
    if (kt && row) {
        uint8_t a[3], b[3], c[3];
        if (read_colour(app, *kt, row, "teamcolorprim", a) && read_colour(app, *kt, row, "teamcolorsec", b)) {
            std::copy(a, a + 3, g_look.c1);
            std::copy(b, b + 3, g_look.c2);
            if (!read_colour(app, *kt, row, "teamcolortert", c)) std::copy(a, a + 3, c);
            std::copy(c, c + 3, g_look.c3);
            g_look.colours = true;
        }
    }
    for (const KitSlotRow& r : g_look.rows)
        if (r.slot == slot && r.custom) {
            g_look.record = record_words(app, team, slot);
            std::error_code ec;
            g_look.staged = fs::exists(kits::slot_dir(app.bridge.root(), team, slot) / "manifest.json", ec);
        }
}

bool slot_is_custom(Slot slot) {
    for (const KitSlotRow& r : g_look.rows)
        if (r.slot == slot) return r.custom;
    return false;
}

ImU32 col32(const uint8_t c[3]) { return IM_COL32(c[0], c[1], c[2], 255); }

// A drawn shirt in the kit's colours (the picture of a kit that has no zip picture to show)
void draw_shirt(const ImVec2& p, float w, float h, ImU32 body, ImU32 trim) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto P = [&](float x, float y) { return ImVec2(p.x + x * w, p.y + y * h); };
    dl->AddQuadFilled(P(0.24f, 0.05f), P(0.02f, 0.22f), P(0.12f, 0.44f), P(0.26f, 0.34f), body);
    dl->AddQuadFilled(P(0.76f, 0.05f), P(0.98f, 0.22f), P(0.88f, 0.44f), P(0.74f, 0.34f), body);
    dl->AddRectFilled(P(0.24f, 0.05f), P(0.76f, 0.97f), body, S(3.0f));
    dl->AddLine(P(0.02f, 0.22f), P(0.12f, 0.44f), trim, S(3.0f));
    dl->AddLine(P(0.98f, 0.22f), P(0.88f, 0.44f), trim, S(3.0f));
    dl->AddTriangleFilled(P(0.38f, 0.05f), P(0.62f, 0.05f), P(0.50f, 0.18f), trim);
}

// A hash of the first pixels of a picture: the cache key's version, so another zip with the same file name shows its own picture
uint64_t picture_version(const Rgba& img) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t(img.w) << 20) ^ uint64_t(img.h);
    const size_t n = std::min<size_t>(img.px.size(), 8192);
    for (size_t i = 0; i < n; ++i) h = (h ^ img.px[i]) * 1099511628211ull;
    return h;
}

// ---------------------------------------------------------------- the club row (both pages)
// The club's name with "My club" and "Change club...": the chooser (name or number) lives in a popup, so no club number is on the page.
void club_row(App& app) {
    const TeamRow* tr = app.model.team(app.sel_team);
    ImGui::AlignTextToFramePadding();
    if (tr) ImGui::TextUnformatted(tr->name.c_str());
    else theme::muted_text("No club picked");
    if (ImGui::SmallButton("Change club...##kpchange")) ImGui::OpenPopup("##kpclubpop");
    if (app.bridge.state().user_team > 0 && app.bridge.state().user_team != app.sel_team) {
        ImGui::SameLine();
        if (ImGui::SmallButton("My club##kpmine")) app.sel_team = app.bridge.state().user_team;
    }
    if (ImGui::BeginPopup("##kpclubpop")) {
        theme::muted_text("Type a club name, then pick it in the list.");
        if (club_search_box(app, "##kpclub", app.sel_team, S(280.0f), "Club name")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// A bordered card with a title; AutoResizeY so its height follows what it holds
bool card_begin(const char* id, const char* title) {
    const bool open = ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    return open;
}
void card_end() { ImGui::EndChild(); }

void tag(const char* text, const char* hover) {
    ImGui::SameLine();
    theme::warn_text("%s", text);
    if (hover && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hover);
}

// ---------------------------------------------------------------- Kits
struct KitsCtx {
    int64_t team = 0;
    std::vector<size_t> which;  // ready kits of the selected slot
    bool waiting = false;       // the game's own minikit is still loading
};

// 4.0 S9 fix round 3: the ONE path of every Apply on this page (the bar Apply and "Apply all N kits"). It applies, then arms the status line:
// draw_kits_page reads the outcome when the staging worker has ended. A button that called apply_items itself left the line at an earlier "Done".
static bool g_apply_pending = false;  // an Apply was pressed and its result is not on the status line yet
static std::string g_apply_why;       // why the database part of that Apply failed, read the moment apply_items returned
void apply_and_track(App& app, int64_t team, const std::vector<size_t>& which) {
    apply_items(app, team, which);
    invalidate_look();
    g_apply_pending = true;
    // The outcome of the database part is read HERE: poll_jobs replaces the panel line with "textures staged..." when the worker ends.
    // Only the items just applied (which) count, not a stale "not applied" item of another slot.
    g_apply_why.clear();
    for (size_t i : which)
        if (g_apply_why.empty() && kit_panel_status(i).rfind("not applied", 0) == 0) g_apply_why = kit_panel_status(i);
    // apply_kit_import returns ok=false with a line that does not start "not applied" when the import record could not be saved
    if (g_apply_why.empty() && kit_panel_line().find("was not saved") != std::string::npos) g_apply_why = kit_panel_line();
}

void select_panel(App& app, void* user) {
    KitsCtx& c = *static_cast<KitsCtx*>(user);
    theme::muted_text("Club");
    club_row(app);
    ImGui::Separator();
    theme::muted_text("Kit");
    for (const KitSlotRow& r : g_look.rows) {
        const std::string id = std::string(r.label) + "##kpslot";
        if (ImGui::Selectable(id.c_str(), kit_page_slot() == r.slot)) kit_page_select(r.slot);
        ImGui::SameLine(S(140.0f));
        if (r.custom) theme::ok_text("%s", kit_slot_state_words(true));
        else theme::muted_text("%s", kit_slot_state_words(false));
    }
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("Restoring a kit only changes this club and this slot. Other clubs are never changed.");
    ImGui::PopTextWrapPos();
    (void)c;
}

// The Preview card: the kit's picture, one status line, the way to choose a kit file
void preview_card(App& app, KitsCtx& c, Slot slot, bool custom) {
    if (!card_begin("##kpprev", "Preview")) {
        card_end();
        return;
    }
    // the picture: the first ready zip's jersey picture, else the kit drawn in the club's colours
    const Rgba* thumb = nullptr;
    std::string thumb_name;
    for (size_t i : c.which) {
        const kits::KitPackage* pk = kit_panel_kit(i);
        if (pk && !pk->thumb.empty()) {
            thumb = &pk->thumb;
            thumb_name = pk->source.string();
            break;
        }
    }
    const float side = S(118.0f);
    ImGui::BeginGroup();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side * 1.3f));
    if (thumb) {
        TextureCache::Pic pic = app.textures.pixels("kp_thumb_" + thumb_name, picture_version(*thumb), *thumb);
        if (pic.tex && pic.w > 0) {
            const float s = std::min(side / float(pic.w), side * 1.3f / float(pic.h));
            ImGui::GetWindowDrawList()->AddImage(pic.tex->GetTexRef(), p0, ImVec2(p0.x + pic.w * s, p0.y + pic.h * s));
        }
    } else {
        draw_shirt(p0, side, side * 1.3f, g_look.colours ? col32(g_look.c1) : IM_COL32(110, 114, 122, 255),
                   g_look.colours ? col32(g_look.c2) : IM_COL32(180, 184, 192, 255));
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    // one status line
    std::string line;
    bool good = true;
    if (kit_panel_busy()) {
        line = "Working on the kit...";
        good = false;
    } else if (!c.which.empty()) {
        line = "Ready to apply: " + kit_panel_kit(c.which.front())->source.filename().string();
    } else {
        std::string other;
        for (size_t i = 0; i < kit_panel_count() && other.empty(); ++i)
            if (kit_panel_slot(i) == slot) other = kit_panel_status(i);
        if (!other.empty() && other != "ready") {
            line = other;
            good = other.rfind("applied", 0) == 0;
        } else if (custom && !g_look.record.empty()) {
            line = g_look.record;
        } else if (custom) {
            line = "Custom kit";
        } else {
            line = "The game's own kit is in use.";
            good = false;
        }
    }
    if (c.waiting && !c.which.empty()) line += ". Waiting for the game's own kit picture, then Apply kit works.";
    g_state.status = line;
    ImGui::PushTextWrapPos(0.0f);
    if (good) theme::ok_text("%s", line.c_str());
    else theme::muted_text("%s", line.c_str());
    ImGui::Spacing();
    theme::muted_text("Kits come from FIFA Kit Creator files (fifakitcreator.com).");
    if (c.which.empty() && !kit_panel_busy()) theme::muted_text("Choose a kit file to enable Apply kit.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool busy = kit_panel_busy();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Choose kit file...##kpfile")) {
        g_fp_zip.mode = PickMode::Open;
        g_fp_zip.title = "Choose a kit file (FIFA Kit Creator download)";
        g_fp_zip.exts = {".zip"};
        g_fp_zip.key = "kit_import";
        g_fp_zip.start = kit_import_start_folder(app);
        g_fp_zip.places = {{"My Mods", fs::path("C:/FC_Tools/My Mods")}, {"Live Editor", app.bridge.root()}};
        ImGui::OpenPopup("##kpzip");
    }
    if (busy) ImGui::EndDisabled();
    fs::path chosen;
    if (file_picker_modal("##kpzip", g_fp_zip, chosen)) kit_panel_open(app, chosen);
    ImGui::EndGroup();
    card_end();
}

// The share choices for the slot, and the options that follow the kit maker's files; all through the public Runtime
void share_choices(App& app, int64_t team, Slot slot, bool custom) {
    kitswap::Runtime* rt = app.kit_swap;
    bool decals = true, keep_crest = true, share_club = false, share_other = false;
    const bool have = rt && rt->opted_in() && custom && rt->slot_options(int(team), kits::slot_type(slot), &decals, &keep_crest, &share_club, &share_other);
    theme::muted_text("If other clubs use the same kit pictures");
    bool changed = false;
    if (!have) ImGui::BeginDisabled();
    int sel = share_other ? 1 : 0;
    changed |= ImGui::RadioButton("Leave them alone##kpshare", &sel, 0);
    ImGui::SameLine();
    theme::muted_text("(a kit with shared pictures stays the game's kit)");
    changed |= ImGui::RadioButton("Change their kits too##kpshare", &sel, 1);
    tag("Not tested yet", "Changing the shared pictures also changes the other clubs' kits. Not tested in a match yet.");
    if (!have) ImGui::EndDisabled();
    // the per-match option (brief 2.16-kit-match-gate): shown as planned, never selectable here
    ImGui::BeginDisabled();
    int never = -1;
    ImGui::RadioButton("Only in my matches, when no club sharing the kit plays##kpplanned", &never, 0);
    ImGui::EndDisabled();
    tag("planned", "Planned. Not available yet.");
    g_state.planned_shown = true;
    if (!have) ImGui::BeginDisabled();
    changed |= ImGui::Checkbox("Also change my club's other kits that use the same pictures##kpshareclub", &share_club);
    tag("Not tested yet", nullptr);
    if (!have) ImGui::EndDisabled();
    if (changed && have) rt->set_slot_options(int(team), kits::slot_type(slot), decals, keep_crest, share_club, sel == 1);
}

// The one 3D tick and its honest line
void pitch_tick(App& app, int64_t team, Slot slot, bool custom) {
    kitswap::Runtime* rt = app.kit_swap;
    const fs::path switch_file = app.bridge.root() / "turbo_output" / "kits3d_on.txt";
    bool opted = false, pending = false, staged = false, active_swap = false, watch_only = false;
    // The switch counts as "on" only when it was there when this session started (the game hook is put in after a start with it, as the old
    // button said: restart the game). A switch made in this session is "pending": the tick shows it as set and can still take it back.
    static const kitswap::Runtime* seen = nullptr;
    static bool opted_at_start = false;
    if (!rt) seen = nullptr;
    if (rt) {
        const bool file = rt->opted_in();
        if (rt != seen) {
            seen = rt;
            opted_at_start = file;
        }
        opted = file && opted_at_start;
        pending = file && !opted_at_start;
        if (opted) {
            staged = custom && g_look.staged && !kit_panel_busy();
            kitswap::Mode m = kitswap::Mode::Off;
            const bool active = rt->is_active(int(team), kits::slot_type(slot), &m);
            active_swap = active && m == kitswap::Mode::Swap;
            watch_only = active && m == kitswap::Mode::Observe;
        }
    }
    bool tick = opted ? active_swap : pending;
    const bool enabled = rt && (!opted || staged);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::Checkbox("Show this kit on the pitch (3D)##kp3d", &tick);
    if (!enabled) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Your kit shows in menus either way. This also puts it on the players in matches.");
    if (clicked && rt) {
        if (!opted) {
            // the same switch the old button made: the empty file that turns the 3D option on from the next game start
            std::error_code ec;
            if (tick) {
                fs::create_directories(switch_file.parent_path(), ec);
                std::ofstream(switch_file).put('\n');
                const bool ok = fs::exists(switch_file, ec);
                app.notify(ok ? "3D kits turn on after you restart the game." : "Could not switch 3D kits on.", !ok);
            } else {
                fs::remove(switch_file, ec);
                app.notify("3D kits stay off.");
            }
        } else {
            std::string err;
            const kitswap::Mode want = tick ? kitswap::Mode::Swap : kitswap::Mode::Off;
            const bool ok = rt->activate(int(team), kits::slot_type(slot), want, &err);
            app.log(std::string("kit swap: team ") + std::to_string(team) + " " + kits::slot_label(slot) + ": " +
                    (ok ? kitswap::mode_name(want) : "refused: " + err));
            if (!ok) app.notify("3D kit: " + err, true);
        }
    }
    const std::string line = kit_pitch_line(rt != nullptr, opted, pending, staged, active_swap, watch_only);
    g_state.pitch_tick = tick;
    g_state.pitch_enabled = enabled;
    g_state.pitch_line = line;
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("%s", line.c_str());
    // 2.20: the kit's swap plan (made once when the kit is staged; the game start only loads it)
    const kitswap::Status st = rt && opted ? rt->status() : kitswap::Status();
    const int sl = kits::slot_type(slot);
    const kitswap::Status::PlanRow* plan = nullptr;
    for (const auto& p : st.plans)
        if (p.team == int(team) && p.slot == sl) plan = &p;
    bool plan_ready = !plan || plan->state == "ready";
    if (rt && opted && plan && (staged || active_swap)) {
        if (plan->state == "planning") {
            theme::warn_text("Planning your kit... (about %.0f s)", std::max(1.0, st.plan_estimate_s));
        } else if (plan->state == "queued") {
            theme::muted_text("Waiting to plan your kit (%d %s to plan first).", std::max(0, st.plans_queued - 1), st.plans_queued - 1 == 1 ? "kit" : "kits");
        } else if (plan->state != "ready" && active_swap) {
            theme::warn_text("Off on the pitch: %s.", plan->reason.empty() ? "this kit has no swap plan" : plan->reason.c_str());
            if (ImGui::SmallButton("Plan it now##kpplan")) {
                rt->request_plan(int(team), sl, true);
                app.log("kit swap: team " + std::to_string(team) + " " + kits::slot_label(slot) + ": plan asked for");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reads the game's kit data for this kit once (about 20 s). The game start then only loads the result.");
        }
    }
    // why a kit is only partly served: the club names of the shared kits, in plain words
    if (rt && opted && active_swap && plan_ready) {
        const std::string said = kit_swap_status_line(st, team, sl, [&](int64_t id) { return app.model.team_name(id); });
        if (said.compare(0, 7, "swapped") == 0) {
            theme::ok_text("Shows on the pitch from the next match.");
        } else if (!said.empty()) {
            theme::warn_text("Not on the pitch yet: the game keeps its own kit when a part of it is shared with another club.");
            const size_t cut = said.find("; tick");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (cut == std::string::npos ? said : said.substr(0, cut)).c_str());
        }
        // 2.20 (donor help): the clubs whose kits use the same game data, said plainly instead of offering to change them
        const std::string also = kit_swap_shared_clubs(st, team, sl, [&](int64_t id) { return app.model.team_name(id); });
        if (!also.empty()) theme::muted_text("This design would also appear on %s.", also.c_str());
    }
    // 2.20: the old full build, only as an explicit action (Advanced): every kit planned again, one after the other
    if (rt && opted && app.prefs.advanced_mode && !st.slots.empty()) {
        const bool busy = st.plans_queued > 0 || st.state == "planning";
        if (busy) ImGui::BeginDisabled();
        char label[96];
        std::snprintf(label, sizeof(label), "Rebuild all kit plans (%zu %s, about %.0f s each)##kpreplan", st.slots.size(), st.slots.size() == 1 ? "kit" : "kits", 20.0);
        if (ImGui::SmallButton(label)) {
            rt->replan_all();
            app.log("kit swap: every kit plan asked for again (" + std::to_string(st.slots.size()) + " kits)");
        }
        if (busy) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Prepares every kit that is switched on again from the game's own files. Needed only after a game update or when a kit asks for it.");
    }
    ImGui::PopTextWrapPos();
}

void matches_card(App& app, int64_t team, Slot slot, bool custom) {
    if (!card_begin("##kpmatch", "In matches")) {
        card_end();
        return;
    }
    pitch_tick(app, team, slot, custom);
    ImGui::Separator();
    share_choices(app, team, slot, custom);
    card_end();
}

// Numbers & colours (folded): the kit maker's files, then the colours and number fonts of every kit of the club
void numbers_section(App& app, int64_t team, Slot slot, bool custom) {
    if (!ImGui::CollapsingHeader("Numbers & colours##kpcolours")) return;
    kitswap::Runtime* rt = app.kit_swap;
    bool decals = true, keep_crest = true, share_club = false, share_other = false;
    const bool have = rt && rt->opted_in() && custom && rt->slot_options(int(team), kits::slot_type(slot), &decals, &keep_crest, &share_club, &share_other);
    if (!have) ImGui::BeginDisabled();
    bool changed = ImGui::Checkbox("Keep the kit maker's badge and sponsors##kpdecals", &decals);
    if (!decals) ImGui::BeginDisabled();
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Ignore the crest in the kit file##kpcrest", &keep_crest);
    if (!decals) ImGui::EndDisabled();
    if (changed && have) rt->set_slot_options(int(team), kits::slot_type(slot), decals, keep_crest, share_club, share_other);
    bool hot = rt ? rt->get_options().apply_hotspots : false;
    if (ImGui::Checkbox("Names and numbers use the kit maker's position##kphot", &hot) && rt) {
        kitswap::Options o = rt->get_options();
        o.apply_hotspots = hot;
        rt->set_options(o);
        if (!rt->status().slots.empty()) rt->rebuild();
    }
    if (!have) ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("These take effect when the game next loads the kit.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    club_kit_colours_section(app, team);
}

// More (folded): a whole team folder at once, and the kit files read
void more_section(App& app, int64_t team, KitsCtx& c) {
    if (!ImGui::CollapsingHeader("More##kpmore")) return;
    draw_crest_shirt_toggle(app, team);  // 2.20 K2: the club's crest picture on the shirt (ui_crest_shirt.cpp)
    const bool busy = kit_panel_busy();
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("Read the home, away, third and goalkeeper files of one club at once.");
    ImGui::PopTextWrapPos();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Choose a team folder...##kpfolder")) {
        g_fp_folder.mode = PickMode::Folder;
        g_fp_folder.title = "Choose a team folder (its home, away, third and goalkeeper files are read at once)";
        g_fp_folder.exts = {".zip"};
        g_fp_folder.key = "kit_import";
        g_fp_folder.start = kit_import_start_folder(app);
        g_fp_folder.places = {{"My Mods", fs::path("C:/FC_Tools/My Mods")}, {"Live Editor", app.bridge.root()}};
        ImGui::OpenPopup("##kpfolderpick");
    }
    if (busy) ImGui::EndDisabled();
    fs::path chosen;
    if (file_picker_modal("##kpfolderpick", g_fp_folder, chosen)) kit_panel_open(app, chosen);
    // every file read: its slot (pick another when the name does not say) and what is wrong with it
    std::vector<size_t> ready;
    for (size_t i = 0; i < kit_panel_count(); ++i) {
        const kits::KitPackage* pk = kit_panel_kit(i);
        if (!pk) continue;
        ImGui::PushID(int(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(pk->source.filename().string().c_str());
        ImGui::SameLine(S(260.0f));
        ImGui::SetNextItemWidth(S(130.0f));
        if (ImGui::BeginCombo("##kpfileslot", kit_panel_slot(i) == Slot::Unknown ? "Pick the kit" : kits::slot_label(kit_panel_slot(i)))) {
            for (Slot s : {Slot::Home, Slot::Away, Slot::Third, Slot::Goalkeeper, Slot::Fourth})
                if (ImGui::Selectable(kits::slot_label(s), kit_panel_slot(i) == s)) kit_panel_set_slot(i, s);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const std::string st = kit_panel_status(i);
        if (st == "ready") {
            theme::ok_text("ready");
            ready.push_back(i);
        } else {
            theme::muted_text("%s", st.c_str());
        }
        ImGui::PopID();
    }
    if (ready.size() > 1) {
        bool waiting = false;
        for (size_t i : ready) waiting = waiting || minikit_state(app, team, kit_panel_slot(i)) == LegacyImages::State::Waiting;
        const bool can = !busy && !waiting && app.model.team(team);
        if (!can) ImGui::BeginDisabled();
        const std::string all = "Apply all " + std::to_string(ready.size()) + " kits##kpall";
        if (ImGui::Button(all.c_str())) apply_and_track(app, team, ready);
        if (!can) ImGui::EndDisabled();
    }
    (void)c;
}

void editor_panel(App& app, void* user) {
    KitsCtx& c = *static_cast<KitsCtx*>(user);
    const Slot slot = kit_page_slot();
    if (c.team <= 0) {
        theme::muted_text("Pick a club on the left.");
        return;
    }
    const bool custom = slot_is_custom(slot);
    // title and the restore button (right-aligned)
    const char* restore = "Restore the game's kit##kprestore";
    const float bw = ImGui::CalcTextSize("Restore the game's kit").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s kit", kits::slot_label(slot));
    ImGui::SameLine();
    theme::muted_text("%s", app.model.team_name(c.team).c_str());
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right));
    const bool can = custom && !kit_panel_busy();
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button(restore)) {
        std::string msg;
        const bool ok = undo_kit_import(app, c.team, slot, &msg);
        app.notify("Kit: " + msg, !ok);
        if (ok) page::done("kits", msg);
        else page::couldnt(app, "kits", "restore the game's kit", msg);
        invalidate_look();
    }
    if (!can) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(custom ? "Puts the game's own kit back for this club and this slot." : "Nothing to restore: the game's own kit is in use.");
    ImGui::Spacing();
    preview_card(app, c, slot, custom);
    matches_card(app, c.team, slot, custom);
    numbers_section(app, c.team, slot, custom);
    more_section(app, c.team, c);
    g_state.custom = custom;
}

}  // namespace

const char* kit_slot_state_words(bool custom) { return custom ? "Custom" : "Game's own"; }

std::string kit_pitch_line(bool runtime, bool opted, bool pending, bool staged, bool active_swap, bool watch_only) {
    if (!runtime) return "Not available in this session.";
    if (!opted)
        return pending ? "Needs a game restart. Tested on a few kits so far, so some kits may not show on the pitch."
                       : "Off: your kit shows in menus and on the minikit, not on the pitch.";
    if (!staged) return "Apply the kit first, then you can show it on the pitch.";
    if (watch_only) return "It is set to watch only: nothing is replaced on the pitch. Tick the box to show your kit.";
    if (active_swap) return "Tested on a few kits so far, so some kits may not show on the pitch.";
    return "Off: your kit shows in menus and on the minikit, not on the pitch.";
}

const KitsPageState& kits_page_state() { return g_state; }

void draw_kits_page(App& app) {
    g_state = KitsPageState();
    const bool on = app.connected();
    KitsCtx ctx;
    bool can_apply = false;
    if (on) {
        poll_jobs(app);
        ctx.team = club_page_team(app);
        // a zip or folder was just read: show the slot of its first kit
        static size_t seen_items = 0;
        if (kit_panel_count() != seen_items) {
            seen_items = kit_panel_count();
            if (seen_items > 0 && kit_panel_slot(0) != Slot::Unknown) kit_page_select(kit_panel_slot(0));
            invalidate_look();
        }
        const Slot slot = kit_page_slot();
        refresh_look(app, ctx.team, slot);
        for (size_t i = 0; i < kit_panel_count(); ++i) {
            if (kit_panel_slot(i) != slot || kit_panel_status(i) != "ready") continue;
            ctx.which.push_back(i);
            ctx.waiting = ctx.waiting || minikit_state(app, ctx.team, slot) == LegacyImages::State::Waiting;
        }
        g_state.teamid = ctx.team;
        g_state.slot = slot;
        can_apply = !ctx.which.empty() && !ctx.waiting && !kit_panel_busy() && app.model.team(ctx.team) != nullptr;
        g_state.apply_enabled = can_apply;
    }
    // 4.0 S9: the page template (title, help, body, one status line, bar). The Apply of the bar is the page's one main button.
    page::Frame f(app, {"kits", "Kits", "Pick a slot, read a kit file, then Apply kit. Restore puts the game's own kit back.", on ? "Apply kit" : nullptr, can_apply});
    if (!on) not_connected_hint();
    else shell::page::frame(app, select_panel, editor_panel, &ctx, nullptr);
    const bool clicked = f.end();
    if (!on) return;
    if (clicked && can_apply) apply_and_track(app, ctx.team, ctx.which);
    if (g_apply_pending && !kit_panel_busy()) {
        g_apply_pending = false;
        std::string why = g_apply_why;
        // The worker may have ended in this very frame (a missing zip ends it at once), after apply_items and before the next frame's poll_jobs:
        // consume its result now, so the line read below is the staging line and a texture that could not be staged ends it with "; not staged: <why>"
        poll_jobs(app);
        const std::string line = kit_panel_line();
        const size_t ns = line.find("; not staged: ");
        if (why.empty() && ns != std::string::npos) why = "the textures were not staged: " + line.substr(ns + 14);
        if (why.empty()) page::done("kits", line);
        else page::couldnt(app, "kits", "apply the kit", why);
    }
}

// ---------------------------------------------------------------- Club & crest: Identity | Stadium & crowd | Staff
namespace {

// Name, colours and crest: three cards, each with its own Save (unchanged from the Identity page)
void identity_tab(App& app) {
    ImGui::BeginChild("##identity", ImVec2(0, 0), ImGuiChildFlags_None);  // the id of the old Identity page, kept so the label manifest has no removed id
    const bool wide = ImGui::GetContentRegionAvail().x >= S(760.0f);
    if (wide) ImGui::Columns(2, "##kpidcols", false);
    if (card_begin("##kpname", "Name")) {
        theme::muted_text("What your club is called in the game.");
        club_name_section(app);
    }
    card_end();
    if (card_begin("##kpcolours", "Colours")) {
        theme::muted_text("The club's three colours.");
        club_colours_section(app, false);
    }
    card_end();
    if (wide) ImGui::NextColumn();
    if (card_begin("##kpcrest", "Crest")) {
        theme::muted_text("The badge shown for your club.");
        club_crest_section(app);
    }
    card_end();
    if (card_begin("##kpvoice", "Commentary name")) {
        theme::muted_text("What the commentator calls your club in matches.");
        club_voice_section(app);
    }
    card_end();
    if (wide) ImGui::Columns(1);
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("Each part has its own Save. Name and colour changes have no Undo yet: set them back by hand. The commentary name goes back with \"Use its own name\".");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// The old Stadium & extras page, as it was: stadium and kit colours, crowd and tifo, home ground, then the customisation hub
void stadium_tab(App& app) {
    club_stadium_section(app);
    ImGui::Spacing();
    ImGui::SeparatorText("More for your club");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);  // the extras' own header
    draw_club_tools(app);
}

}  // namespace

void draw_club_identity_page(App& app) {
    page::Frame f(app, {"club_crest", "Club & crest", "Your club's name, colours, crest, stadium and staff. Each part has its own Save.", nullptr, true, false});  // 4.0 integration (S9 merge): no bar Undo here, it would take back the last PLAYER change (gui-s9.md:41)
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    club_page_team(app);
    club_row(app);
    ImGui::Separator();
    if (ImGui::BeginTabBar("##kc_tabs")) {
        if (ImGui::BeginTabItem("Identity###kctabid")) {
            identity_tab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stadium & crowd###kctabstadium")) {
            stadium_tab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Staff###kctabstaff")) {
            draw_career_staff(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ---------------------------------------------------------------- Kit diagnostics (for Advanced)
void draw_kit_diagnostics(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    const int64_t team = club_page_team(app);
    if (team <= 0) {
        theme::muted_text("Pick a club first.");
        return;
    }
    kits_editor(app, team);
}

}  // namespace turbo
