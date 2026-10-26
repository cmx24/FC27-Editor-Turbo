// FC 27 LE Turbo GUI - Team > Squad page of the 4.0 GUI overhaul (S4). Defined in ui_teams.cpp.
// The page is the club picker of the Teams page plus squad_table (ui_team_pages.h), inside page::Frame (id "team_squad", title "Squad").
// Nothing new: the same picker, the same table, the same edits; S3 registers it as Team > Squad.
#pragma once

namespace turbo {

class App;

void draw_team_squad_page(App& app);

}  // namespace turbo
