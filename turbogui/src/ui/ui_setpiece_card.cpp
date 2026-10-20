// FC 27 LE Turbo GUI - the Set-pieces card (2.22 G-pickers): free-kick run-up, penalty run-up, penalty kick style / motion style and run style
// as INLINE animated grids in Edit a player > Positions (not a window). Each grid is vp::inline_grid (ui_visual_picker.cpp): the GIF of
// your RDBM26 folder where it has one, a grey "no animation yet" tile where it has none, every tile under its in-game name, the current
// one marked; a click picks, Apply writes that ONE field through the validated edit (App::edit: undo step, kept edits, scoped-edit rules).
#include "ui_setpiece_card.h"

#include "app.h"
#include "core/rdbm_resources.h"
#include "imgui.h"
#include "ui_visual_picker.h"

namespace turbo {
namespace setpiece_card {

void draw(App& app, int64_t player_id) {
    if (player_id <= 0) return;
    const Table* t = app.db.table("players");
    if (!t) return;
    const uint64_t rec = app.db.find(*t, "playerid", player_id);
    if (!rec) return;
    // field, title. Penalty kick style / motion style are listed too: a database that has the fields shows them; FC 27's schema as read
    // for this version has neither (no names exist for them), so on FC 27 the card shows the other three.
    static const struct {
        const char* field;
        const char* title;
    } kFields[] = {{"animfreekickstartposcode", "Free-kick run-up"},
                   {"animpenaltiesstartposcode", "Penalty run-up"},
                   {"animpenaltieskickstylecode", "Penalty kick style"},
                   {"animpenaltiesmotionstylecode", "Penalty motion style"},
                   {"runstylecode", "Run style"}};
    ImGui::PushID("setpiece_card");
    ImGui::SeparatorText("Set pieces");
    ImGui::TextDisabled("Animations come from your RDBM26 folder; RDBM26 has 9 free-kick and no penalty animations.");
    for (const auto& f : kFields) {
        if (!t->has(f.field)) continue;
        if (!visual::set_for_field(f.field)) {  // a field with no names to show: its value only, never a made-up name
            ImGui::TextDisabled("%s: %lld (no names known for this field)", f.title, static_cast<long long>(app.db.get_int(*t, rec, f.field, 0)));
            continue;
        }
        vp::inline_grid(app, *t, rec, f.field, f.title, S(250.0f));
    }
    ImGui::PopID();
}

}  // namespace setpiece_card
}  // namespace turbo
