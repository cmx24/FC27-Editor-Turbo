// FC 27 LE Turbo GUI - body type catalogue: the names and the use of every bodytypecode, loaded at run time.
// Platform independent. Three sources, nothing is generated and no name is ever guessed:
//   1. turbo_output/bodytypes_fc27.json, written by the probe script bodytypes.lua (when the user has run it in game):
//      per code the player count, height and weight range, headclasscode, gender and up to 5 example names;
//   2. Live Editor's loc/eng_us/localize.json (bodytype_N strings, which name only the generic types);
//   3. the named codes 1-9 and 11 of field_labels.h, used when Live Editor's file is not there.
// A code with no name from 2 or 3 is a player-specific model and reads "Specific body #N" (the examples come from the probe).
//
// Probe file schema (version 1; "bodytypes" may also be an object keyed by the code):
//   {"version": 1, "bodytypes": [{"code": 20, "players": 3, "height": [185, 192], "weight": [78, 88],
//                                 "headclasscode": 0, "gender": 0, "examples": ["A. Name", "B. Name"]}]}
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace turbo::bodytype {

constexpr size_t kMaxExamples = 5;

enum class Kind { Generic, Specific };

// What the probe saw for one code (every number is 0 or -1 when unknown)
struct Entry {
    int64_t code = 0;
    int64_t players = 0;                 // players that carry this code (0 = not probed)
    int height_min = 0, height_max = 0;  // cm; 0 = unknown
    int weight_min = 0, weight_max = 0;  // kg; 0 = unknown
    int headclass = -1;                  // headclasscode of those players: 0 specific heads, 1 generic heads, -1 unknown or mixed
    int gender = -1;                     // 0 male, 1 female, -1 unknown or mixed
    std::vector<std::string> examples;   // at most kMaxExamples
    bool probed = false;
    bool has_height() const { return height_max > 0; }
    bool has_weight() const { return weight_max > 0; }
};

enum class Group { All, Generic, Specific };

// Gallery filters; a bound of 0 is off. A range matches when it overlaps the entry's range; an entry with no height (weight)
// data never matches an active height (weight) filter.
struct Filter {
    Group group = Group::All;
    int height_min = 0, height_max = 0;
    int weight_min = 0, weight_max = 0;
    std::string text;        // case-insensitive: name, code or an example name
    bool used_only = false;  // only codes some probed player carries
};

class Catalog {
public:
    // Reads the probe file. Returns false (and leaves no probe data) when the file is missing or malformed; *err says why.
    // Entries with a bad code are skipped (skipped() counts them), so one damaged row does not hide the others.
    bool load_probe(const std::filesystem::path& file, std::string* err = nullptr);
    // Reads the bodytype_N strings of Live Editor's localize.json; returns how many names were found (0 when absent)
    size_t load_names(const std::filesystem::path& localize_json);
    // load_probe(<le_root>/turbo_output/bodytypes_fc27.json) and load_names(<le_root>/loc/eng_us/localize.json)
    void load_for_root(const std::filesystem::path& le_root);
    void clear();

    bool probe_loaded() const { return probe_loaded_; }
    size_t probed_count() const { return probed_.size(); }
    size_t localized_count() const { return names_.size(); }
    size_t skipped() const { return skipped_; }
    const std::string& probe_error() const { return probe_error_; }

    const Entry* find(int64_t code) const;          // the probe's entry, nullptr when the code was not probed
    bool named(int64_t code) const;                 // Live Editor or field_labels.h names it (a generic type)
    Kind kind(int64_t code) const { return named(code) ? Kind::Generic : Kind::Specific; }
    std::string name(int64_t code) const;           // "Tall and Lean" or "Specific body #20"
    std::string examples_text(int64_t code) const;  // "A. Name, B. Name" or ""
    std::string describe(int64_t code) const;       // name, plus " (examples: ...)" for a specific body that has some
    int64_t players(int64_t code) const;            // 0 when not probed
    std::vector<int64_t> codes() const;             // named codes and probed codes, ascending
    std::vector<int64_t> filter(const Filter& f) const;  // codes() that pass the filter

private:
    std::map<int64_t, Entry> probed_;
    std::map<int64_t, std::string> names_;  // from Live Editor's localize.json
    bool probe_loaded_ = false;
    size_t skipped_ = 0;
    std::string probe_error_;
};

// A specific body model on a head that is not a specific (real-face) head may not match skeleton and kit (untested in game),
// so the editor does not write it unless asked to. head_class: the record's headclasscode (0 specific, 1 generic, -1 unknown,
// e.g. a manager). A generic body never conflicts.
bool risky_pairing(const Catalog& c, int64_t code, int head_class);

}  // namespace turbo::bodytype
