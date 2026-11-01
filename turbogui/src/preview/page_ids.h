// FC 27 LE Turbo GUI preview host (tools/gui-preview): page ids. An id is "<area>/<page>": the labels drawn on the rail and the page strip
// (shell::area_label / page_label), lower case, spaces as '-' ("players/squad", "lab/row-editor"). They are the names the dev-service
// open_page op takes (shell::find_place), so the preview's "before" picture and the game's "after" shot name the same page. A bare page
// label ("squad") is accepted when only one area has it. No second id table: the shell's own tables are the only source.
#pragma once
#include <string>
#include <vector>

#include "ui/ui_shell.h"

namespace preview {

std::string id_of(const turbo::shell::Place& p);                      // "players/squad"
std::vector<turbo::shell::Place> all_places();                        // every page of every area, in rail order
// false with an error that names the valid ids (unknown id, unknown area/page, ambiguous bare page label)
bool resolve_page_id(const std::string& id, turbo::shell::Place& out, std::string& error);
std::string file_stem(const turbo::shell::Place& p);                  // "players__squad" (file names)

}  // namespace preview
