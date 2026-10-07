// FC 27 LE Turbo GUI - the words the panels use for things that are not a database field (user_text.h).
#include "core/user_text.h"

#include <cstring>

#include "core/match_setup.h"

namespace turbo::usertext {

std::string pending_text(const std::string& label) {
    struct Verb {
        const char* prefix;
        const char* text;
    };
    static const Verb verbs[] = {
        {"Transfer", "Moving the player"},
        {"Loan", "Loaning the player"},
        {"Delete player", "Deleting the player"},
        {"Job offer", "Asking for the job offer"},
        {"Mass actions", "Applying the team actions"},
        {"Manager move", "Moving the manager"},
        {"Manager rules", "Setting the job security"},
        {"Reveal", "Revealing player data"},
        {"Develop", "Developing the player"},
        {"Growth", "Setting the growth"},
        {"Potential", "Setting the potential"},
        {"Create player", "Creating the player"},
        {"Clone player", "Cloning the player"},
        {"Import", "Importing"},
        {"Export", "Exporting"},
        {"Repair names", "Repairing names"},
        {"Keep shown name", "Keeping the shown name"},
        {"Names ", "Changing the name"},
        {"Player callname", "Setting the spoken name"},
        {"Ban", "Blocking offers"},
        {"Remove ban", "Lifting the offer block"},
        {"List bans", "Listing the offer blocks"},
    };
    for (const Verb& v : verbs)
        if (label.compare(0, std::strlen(v.prefix), v.prefix) == 0) return v.text;
    return label;
}

std::string difficulty_text(int level) {
    if (level < 0) return "your setting";
    if (level == 0) return "Beginner";
    if (level == 5) return "Legendary";
    return "level " + std::to_string(level) + " of 5";
}

static std::string when_text(const char* when) {
    const std::string w = when ? when : "";
    if (w == "at kick-off") return "applies at kick-off";
    if (w == "during the match") return "applies during the match";
    if (w == "when a match is set up") return "applies when the next match is set up";
    return w.empty() ? std::string() : "applies " + w;
}

std::string match_var_set(const std::string& name, int value) {
    const gv::KnownVar* v = gv::known(name);
    if (!v) return name + " set to " + std::to_string(value);
    std::string val;
    if (v->min == 0 && v->max == 1) val = value ? "on" : "off";
    else if (name == "OVERRIDE_MATCH_DIFFICULTY") val = difficulty_text(value);
    else val = std::to_string(value);
    std::string s = std::string(v->label) + " set to " + val;
    const std::string w = when_text(v->when);
    return w.empty() ? s : s + "; " + w;
}

std::string match_var_clear(const std::string& name) {
    const gv::KnownVar* v = gv::known(name);
    return std::string(v ? v->label : name.c_str()) + ": the game decides again";
}

std::string match_var_outcome(bool ok, bool cleared, const std::string& name, int value, const std::string& message) {
    if (ok) return cleared ? match_var_clear(name) : match_var_set(name, value);
    std::string m = message;
    const gv::KnownVar* v = gv::known(name);
    if (v && !name.empty()) {
        const size_t at = m.find(name);
        if (at != std::string::npos) m.replace(at, name.size(), v->label);
    }
    return m;
}

std::string health_line(int signatures_found, int signatures_total, int hooks_active, int hooks_killed, bool dispatcher_ok,
                        bool lua_answering) {
    std::string problems;
    auto add = [&](const std::string& s) { problems += (problems.empty() ? "" : ", ") + s; };
    if (signatures_found < signatures_total) add(std::to_string(signatures_total - signatures_found) + " of " + std::to_string(signatures_total) + " game code spots not found");
    if (hooks_killed > 0) add(std::to_string(hooks_killed) + (hooks_killed == 1 ? " hook switched off" : " hooks switched off"));
    if (!dispatcher_ok) add("the game-thread dispatcher is not running");
    if (!lua_answering) add("Live Editor's Lua side is not answering yet");
    if (problems.empty()) return "Everything Turbo needs from the game is working (" + std::to_string(hooks_active) + " hooks active)";
    return "Needs attention: " + problems;
}

}  // namespace turbo::usertext
