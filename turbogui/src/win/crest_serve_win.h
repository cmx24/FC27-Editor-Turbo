// FC 27 LE Turbo GUI - crest serve inside FC27.exe (2.18; the Windows host of core/crest_serve.h, docs/re/legacy_async_crest.md).
//
//   * install_crest_serve(): after install_game_hooks(). Resolves the nine cs_* signatures (the 6AC07E31 table) or, on a build whose
//     table lacks them (X 2.0.1), finds the three call sites on the running game in a background thread (crest_serve::locate: exact
//     pattern, else relaxed) and refuses (logs, does nothing) unless all three async callers call the BeginLoadAsync pattern, both TryFindFile calls agree and the bytes at
//     BeginLoadAsync are the expected function; reads turbo_config.json "crest_serve"; hooks BeginLoadAsync ("crest_serve").
//   * the detour: a path under a configured prefix (default data/ui/imgAssets/crest/) whose file is in <LE folder>\mods\legacy with
//     exactly the requested size is read into the caller's buffer, then the original BeginLoadAsync runs for a small native decoy file
//     into Turbo's scratch buffer (the game's own request completes). Everything else: the original, untouched.
//   * a failed serve returns NULL to the callers that test it (ChunkFileLoaderData, ChunkFileAsyncLoad), the original call to any other.
//   * every serve and refusal goes to turbo_gui.log with the prefix "crest serve:".
//   * kill switches: turbo_output\crest_serve_off.txt (install and every crest call), hook_crest_serve_off.txt, game_hooks_off.txt;
//     turbo_config.json "crest_serve": {"enabled": false}.
#pragma once
#include <string>

namespace host {

void install_crest_serve();
// One line for the log: "crest serve: on (...), N serve(s), M failure(s)" or why it is off
std::string crest_serve_status();

}  // namespace host
