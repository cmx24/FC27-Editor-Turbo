// FC 27 LE Turbo GUI - Live Editor's custom team names file.
//
// Live Editor shows the names in <Live Editor>\extensions\global\custom_team_names.csv instead of the database's
// teams.teamname (format "key;value"; keys TeamName_<id>, TeamName_Abbr3_<id>, TeamName_Abbr10_<id>,
// TeamName_Abbr15_<id>). Live Editor reads that file when it starts, so a change shows after its next start.
// Turbo keeps every other row as it is, writes the file atomically and copies the previous file to
// turbo_output\team_name_backups first.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace turbo {

struct TeamNameKeys {
    std::string full, abbr3, abbr10, abbr15;
};
// The four keys Live Editor uses for a team
TeamNameKeys team_name_keys(int64_t teamid);

class TeamNamesCsv {
public:
    struct Row {
        std::string key;    // empty for comment / blank lines (kept verbatim in `raw`)
        std::string value;
        std::string raw;    // the line as read, for rows Turbo does not touch
    };

    // Read the file; a missing file is an empty list (header added on save). false only when the file is unreadable.
    bool load(const std::filesystem::path& file, std::string* err = nullptr);
    // Value of a key or empty
    std::string get(const std::string& key) const;
    bool has(const std::string& key) const;
    // Set a key (replaces the row in place, or appends); an empty value removes the row
    void set(const std::string& key, const std::string& value);
    // Write back: previous file copied to backup_dir (when it exists), then .tmp + rename
    bool save(const std::filesystem::path& file, const std::filesystem::path& backup_dir, std::string* err = nullptr,
              std::filesystem::path* backup = nullptr) const;

    // Convenience for the Teams tab: names of a team (empty strings when not set)
    void team_names(int64_t teamid, std::string& full, std::string& a3, std::string& a10, std::string& a15) const;
    void set_team_names(int64_t teamid, const std::string& full, const std::string& a3, const std::string& a10, const std::string& a15);

    const std::vector<Row>& rows() const { return rows_; }
    size_t size() const { return rows_.size(); }
    bool loaded() const { return loaded_; }
    std::string text() const;  // the file as it would be written

private:
    std::vector<Row> rows_;
    bool loaded_ = false;
    bool crlf_ = false;
    bool bom_ = false;
};

// Live Editor's file for a given Live Editor folder
std::filesystem::path team_names_file(const std::filesystem::path& le_root);
// Where Turbo keeps copies of the previous file
std::filesystem::path team_names_backup_dir(const std::filesystem::path& le_root);
// A value Live Editor can read: no ';', no line breaks, at most 60 bytes (the DB field is 59 + NUL)
std::string clean_team_name(const std::string& s, size_t max_bytes = 60);

}  // namespace turbo
