// FC 27 LE Turbo GUI - the per-side gameplay slider game variables ("channel A" of docs/plans/tactics-2.1/PLAN.md section 5)
// and the two one-click A/B presets that decide in one played match whether that channel reaches the pitch.
//
// The game owns a family of game variables named GAMEPLAY_CUSTOMIZATION/<SLIDER>_USER and _CPU (34 sliders x 2 sides, plus
// POWER_BAR_SPEED_USER and the four INJURY_*_USER / _CPUAI names Turbo already offers). A settings reset reads each of them
// with GetInt(name, current slider byte) and stores the result in the slider block (research.json "gameplay-sliders",
// evidence: strings 0x14AB23B48..0x14AB24F68 and disassembly 0x1442DCA5F..0x1442DD073 of build 1.0.140.64835). Whether that
// value SURVIVES to kick-off is contested between two research reports; nobody has seen one of these names change a played
// match. HONESTY: every name here is "accepted, untested" at most, OFF by default, and "off" clears the variable (never
// sends 50 as off). The deciding A/B match is the user's; this file only prepares it.
//
// Pure data and logic: no game memory, no ImGui. The host sends the writes through msetup::Service (core/match_setup.h) with
// its kill switch turbo_output\call_gamevar_off.txt; the panel also honours turbo_output\tactics_off.txt.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "match_setup.h"

namespace turbo {
namespace gs {

enum class Side { User, Cpu };
// the three {index, name} groups the reset function fills (block offsets +0x330 / +0x1440 / +0x1488 per research)
enum class Group { Basic, Positioning, CpuBehaviour };

struct Slider {
    const char* id;     // the game's name stem, e.g. "SPRINT_SPEED" -> GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_USER / _CPU
    const char* label;  // plain language
    const char* what;   // one line for the user (which way is "more")
    Group group;
    int block_index;    // the slider's index inside its group's block (research "gameplay-sliders", exact names)
};

// The 34 sliders in block order: 16 basic (error / speed), 7 team positioning, 11 CPU behaviour
const std::vector<Slider>& family();
const Slider* slider(const std::string& id);
const char* side_suffix(Side s);  // "USER" / "CPU"
const char* side_label(Side s);   // "your team" / "CPU team"
const char* group_name(Group g);  // "Basic", "Team positioning", "CPU behaviour"
// "GAMEPLAY_CUSTOMIZATION/<id>_<USER|CPU>"
std::string var_name(const Slider& s, Side side);
std::string var_name(const std::string& id, Side side);
// The slider and side of a game variable name of this family; false for any other name
bool parse_var(const std::string& name, const Slider*& out, Side& side);
// Every game variable name of the family (68 names), block order, USER before CPU per slider
std::vector<std::string> all_var_names();
// The registry key of a per-side slider: "match.<lower id>_<user|cpu>"
std::string registry_key(const Slider& s, Side side);

constexpr int32_t kMin = 0, kMax = 100, kNeutral = 50;
constexpr int32_t kOff = -1;  // "off" means cleared: this value is never sent

// ---------------------------------------------------------------- A/B presets
struct Write {
    std::string name;  // game variable
    int32_t value = 0;
};

struct Preset {
    const char* id;            // "baseline" / "cpu_extremes"
    const char* title;         // button text
    const char* summary;       // one paragraph: what the match should show
    bool clear_all_first;      // every active override is cleared before the writes (both presets)
    std::vector<Write> writes; // what is sent after the clear (empty for the baseline)
};

// The two presets, in order: baseline (clears everything, sends nothing) and CPU handicap extremes (CPU side only:
// sprint 0, acceleration 0, pass error 100, shot error 100, first touch (TRAP_ERROR) 100, goalkeeper 0, line height 100,
// run frequency 0, plus INJURY_FREQUENCY_CPU / INJURY_SEVERITY_CPU 100 as the positive control of the _CPU names)
const std::vector<Preset>& presets();
const Preset* preset(const std::string& id);

// What pressing Apply will do, as the user sees it before confirming: one line per write ("NAME = value"), then the
// clear. Empty writes give the single line "clear every override, send nothing".
std::vector<std::string> describe(const Preset& p);

// Gate and plan: "" and the writes when the preset may be sent, else the reason (tactics_off = turbo_output\tactics_off.txt
// exists, gamevar_off = the host's kill switch / vars not ready, with its text). Nothing is sent when a switch is on.
std::string plan(const Preset& p, bool tactics_off, bool gamevar_off, const std::string& gamevar_why, std::vector<Write>& out);

struct ApplyReport {
    int cleared_all = 0;  // clear_all calls that were accepted (0 or 1)
    int sent = 0;         // set_var calls accepted
    int refused = 0;      // set_var calls refused by the service
    std::string first_error;
};
// Send a planned preset through the service: clear_all first (when the preset asks for it), then every write. Every
// name is checked against gv::known and clamped to its range; an unknown name counts as refused and is not sent.
ApplyReport apply(msetup::Service& svc, const Preset& p, const std::vector<Write>& planned);

// Self-check of the tables (empty = fine): 34 sliders with unique ids, 68 distinct valid names, every preset write names
// a known variable of this family or the injury family on the CPU side within 0..100, no preset write is "off"
std::string validate();

}  // namespace gs
}  // namespace turbo
