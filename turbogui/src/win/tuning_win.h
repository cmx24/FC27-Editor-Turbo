// FC 27 LE Turbo GUI - gameplay tuning inside FC27.exe (core/tuning.h): the knobs file of the running build (<Live Editor>\turbo\tuning),
// the game's memory, the signature table and the code scan for the locators' anchors, Turbo's game-thread dispatcher (the game_tick hook
// only), a background thread for the scans. The native locators (code walks a pointer chain cannot express) are registered here.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/tuning.h"

namespace host {

// After install_game_hooks(): reads the knobs file of the running build and logs what it found. Nothing is written by the install; the
// tuning profile (turbo_output\tuning_profile.json, opt-in) is applied by the view's tick at the boot / career_load / hub moments
void install_tuning();
std::shared_ptr<turbo::tuning::Service> tuning_service();
// Status tab lines ("gameplay tuning: on | profile on | 23 knobs, 13 not confirmed in the running game ... | 2 of 2 locators resolved (2
// through the attribute manager)", then the attribute manager's summary once the chain locate ran)
std::vector<std::string> tuning_status();

}  // namespace host
