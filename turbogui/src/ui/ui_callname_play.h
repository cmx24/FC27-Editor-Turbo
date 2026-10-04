// FC 27 LE Turbo GUI - the small play button of Players > Callname (core/callname_audio.h)
#pragma once
#include <cstdint>

#include "core/callname_audio.h"

namespace turbo {

class App;

// A play button (a triangle; a square while it plays) for a generic callname (commentary id) or a player's own
// recording (player id), one text line high. Disabled, with the reason in its tooltip, when the master list has no
// wav folder, no segment for the id or the wav is missing. Its label is "##play_g<id>" / "##play_o<id>".
void callname_play_button(App& app, CallnameAudioKind kind, int64_t id);

}  // namespace turbo
