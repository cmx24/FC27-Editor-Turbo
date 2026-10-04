// FC 27 LE Turbo GUI - the Callname tab's play buttons on Windows (core/callname_audio.h): PlaySoundW from winmm.dll,
// loaded at run time (LoadLibraryW + GetProcAddress, so Turbo.dll imports nothing new), called on a worker thread so a
// click never waits for the disk. Nothing here touches the game.
#pragma once
#include <memory>

#include "core/callname_audio.h"

namespace host {

std::shared_ptr<turbo::WavPlayer> callname_wav_player();

}  // namespace host
