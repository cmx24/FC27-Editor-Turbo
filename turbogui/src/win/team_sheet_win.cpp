// FC 27 LE Turbo GUI - tactics channel E inside FC27.exe (see team_sheet_win.h, core/team_sheet.h,
// turbo_output\TACTICS_CHANNEL_E_REPORT.txt section 4)
#include "team_sheet_win.h"

#include <chrono>
#include <cstdio>
#include <thread>

#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

bool g_installed = false;
std::string g_off;  // why channel E has no service at all ("" = the service exists; its anchors say the rest)
ProcessMemory g_mem;
std::shared_ptr<tsheet::CoreService> g_service;

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

double now_us() {
    return double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Find(registry, id) of the service registry (registry vtable +0x38; the match tactic copy calls it with rcx = registry, edx = id and takes
// the result from rax). Called on the game thread only, after registry_find_fn checked both pointers lie inside FC27.exe.
uint64_t call_registry_find(uint64_t fn, uint64_t registry, uint32_t id) {
    using FindFn = void* (*)(void*, uint32_t);
    return reinterpret_cast<uint64_t>(reinterpret_cast<FindFn>(static_cast<uintptr_t>(fn))(reinterpret_cast<void*>(static_cast<uintptr_t>(registry)), id));
}

}  // namespace

std::shared_ptr<tsheet::Service> team_sheet_service() { return g_service; }

void install_team_sheet() {
    if (g_installed) return;
    g_installed = true;
    tsheet::Host h;
    h.mem = &g_mem;
    h.anchors.build = game_build_key();
    h.anchors.image_base = game_image_base();
    h.anchors.image_size = game_image_size();
    h.anchors.vt1 = game_signature("teamsheet_vt1");
    h.anchors.vt2 = game_signature("teamsheet_vt2");
    h.anchors.set = game_signature("teamsheet_set");
    h.anchors.set_body = game_signature("teamsheet_set_body");
    h.anchors.update = game_signature("teamsheet_update");
    h.anchors.update_body = game_signature("teamsheet_update_body");
    h.anchors.active = game_signature("teamsheet_active");
    h.anchors.active_body = game_signature("teamsheet_active_body");
    h.anchors.registry = game_signature("service_registry");
    h.anchors.registry_this = game_signature("service_registry_this");
    h.psm_vtable = game_signature("psm_vtable");
    h.out_dir = le_root() / "turbo_output";
    // the prompt dispatcher only (the game_tick hook): a job that waited for the next career event could run after the hub was left
    h.run_on_game_thread = [](std::function<void()> fn) -> std::string {
        if (!game_hooks_allowed() || !game_thread_hooked()) return "";
        const char* via = run_on_game_thread(std::move(fn));
        return via ? via : "";
    };
    h.run_background = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
    h.regions = []() { return private_regions(0); };
    h.call_find = &call_registry_find;
    h.log = [](const std::string& s) { log("%s", s.c_str()); };
    h.clock_us = &now_us;
    g_service = std::make_shared<tsheet::CoreService>(std::move(h));
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: team_sheet (channel E) off (%s)", g_off.c_str());
        return;
    }
    const std::string why = g_service->anchors_status();  // logs the outcome once
    if (why.empty())
        log("game calls: team_sheet (channel E) resolved: VT1 %s VT2 %s SetSheet %s UpdateSheet %s GetActiveSheetId %s registry %s; writes need "
            "turbo_output\\%s, kill switches turbo_output\\%s and \\%s, the registry Find off with \\%s, the cm_mentalities mirror off with \\%s",
            hex(game_signature("teamsheet_vt1")).c_str(), hex(game_signature("teamsheet_vt2")).c_str(), hex(game_signature("teamsheet_set")).c_str(),
            hex(game_signature("teamsheet_update")).c_str(), hex(game_signature("teamsheet_active")).c_str(), hex(game_signature("service_registry")).c_str(),
            tsheet::opt_in_file(), tsheet::kill_switch_file(), tsheet::master_switch_file(), tsheet::find_off_file(), tsheet::mirror_off_file());
    else
        log("game calls: team_sheet (channel E) not available on this build: %s", why.c_str());
}

std::vector<std::string> team_sheet_status() {
    std::vector<std::string> out;
    if (!g_service) {
        out.push_back("team_sheet (channel E): not installed");
        return out;
    }
    if (!g_off.empty()) {
        out.push_back("team_sheet (channel E): off (" + g_off + ")");
        return out;
    }
    const std::string why = g_service->anchors_status();
    if (!why.empty()) {
        out.push_back("team_sheet (channel E): not available on this build (" + why + ")");
        return out;
    }
    const tsheet::Switches sw = tsheet::read_switches(le_root() / "turbo_output");
    const std::string block = sw.blocking();
    std::string state = !block.empty() ? "off (kill switch turbo_output\\" + block + ")"
                        : !sw.opt_in   ? std::string("off until turbo_output\\") + tsheet::opt_in_file() + " exists"
                                       : "on";
    const std::string sess = g_service->session_off();
    if (!sess.empty()) state = "off for this session (" + sess + ")";
    out.push_back("team_sheet (channel E): " + state + " | " + g_service->locate_status() + " | registry Find " +
                  std::to_string(g_service->finds()) + "x, scans " + std::to_string(g_service->scans()));
    out.push_back("  " + g_service->allow_status());
    const std::string ko = g_service->kickoff_status();
    if (!ko.empty()) out.push_back("  " + ko);
    return out;
}

}  // namespace host
