// FC 27 LE Turbo GUI - the Turbo 3.0 look: a deep graphite/navy palette and rounder metrics, readable over a bright game picture.
// apply_turbo_theme replaces the old app.cpp turbo_theme(): it starts from Dear ImGui's dark style (so it resets everything it does not
// set) and then sets the Turbo colours and the UNSCALED sizes; App::update_style scales them afterwards.
#pragma once

#include "imgui.h"

namespace turbo::theme {

void apply_turbo_theme(ImGuiStyle& st);

}  // namespace turbo::theme
