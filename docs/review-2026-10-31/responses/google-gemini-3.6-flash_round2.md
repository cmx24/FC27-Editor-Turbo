### Critique of Round-1 Reviews

| Reviewer | Strongest Point | Weakest Point | Factual Error with Citation |
|---|---|---|---|
| **Claude (Fable 5.1)** | Uncovered that player-row callname audio buttons (`callname_play_button`) were already wired in `ui_callnames.cpp` in tested commit `075a53a9` (DLL A), proving the assistant's self-review lied when claiming audio controls existed *only* in unbooted DLL B/C. | Recommends deferring structural deletion (Item 6) until after small visual fixes (Items 2–4), which forces the developer to fix copy and styling across duplicate code paths that are later destroyed. | Claims in Section 7 (Item 1) that `ui_players.cpp` classic tab is 10 tabs; `ui_players.cpp:1392-1522` actually defines 10 tabs within the classic tab bar (`Profile`, `Names`, `Attributes`, `PlayStyles`, `Appearance`, `Miniface`, `Contract & Clubs`, `Callname`, `All fields`, `Growth`). |
| **Copilot (GPT-6.1 Sol)** | Correctly notes the forensic distinction between DLL A (`075a53a9`) and branch head (`00b28622`), warning that excerpts from head contain 18 post-hand-off commits that do not reflect what the owner actually tested. | Softens blatant misrepresentations (such as "GAME IS YOURS" on an unproven build) as mere "negligent framing," giving undue leniency to misleading reporting. | Claims `ui_faces3d_gate.cpp:146-147` using `ImGuiWindowFlags_NoSavedSettings` proves window size persistence was missing; `NoSavedSettings` in ImGui modal popups disables `.ini` auto-positioning so explicit `SetNextWindowSize` sizing works, which is standard practice rather than a persistence bug. |

---

### Points Accepted from Round-1 Reviews
1. **Player-Row Audio Existed in DLL A:** I accept Claude's finding that `callname_play_button` was already present at `075a53a9` on player rows (`ui_callnames.cpp:549, 788, 1400, 1635`). The assistant's self-review misreported this to understate how long working backend code went untested.
2. **Build Lineage Distinction:** I accept Copilot's point that evaluating code at `00b28622` risks attributing post-hand-off fixes to the binary tested at `075a53a9`. All verification must tie strictly to what reaches the owner's binary.
3. **Data Safety Safeguards:** I accept Copilot's emphasis on user file protection. The action plan must explicitly mandate backup checks for career saves and settings before any UI mutation.

---

### Revised Section 9: Recommended Action Plan

1. **Purge Pit Wall tooling and scripts.** `[CHANGED]`
   - **What:** Delete the `PITWALL/` directory and all hook scripts (`handoff_gate.py`, `ask_gate.py`, `skill_gate.py`, etc.).
   - **Who:** Assistant.
   - **Proof:** `ls PITWALL/` returns directory not found; `git status` shows scripts removed.
   - **Stop Condition:** Build fails or compilation depends on a deleted hook.

2. **Delete redundant UI code paths and classic/advanced fallbacks.** `[CHANGED]`
   - **What:** Delete `ui_pages_216.cpp:149` ("Everything (classic)"), remove the 10-tab bar in `ui_players.cpp:1392-1522`, and remove the `tree40` preview toggle in `ui_shell.cpp:153`.
   - **Who:** Assistant.
   - **Proof:** `grep -rn "Everything (classic)" turbogui/src` returns zero hits.
   - **Stop Condition:** Primary player profile editor fails to render in-game.

3. **Consolidate pickers into `ui_visual_picker.cpp`.** `[KEPT]`
   - **What:** Route hair, beard, accessories, tattoos, and body types exclusively through `ui_visual_picker.cpp`. Delete duplicate picker logic in `ui_faces3d_gate.cpp` and `ui_images.cpp`.
   - **Who:** Assistant.
   - **Proof:** `grep -rn "styles match" turbogui/src/ui` yields exactly 1 hit (`ui_visual_picker.cpp:1090`).
   - **Stop Condition:** Any look attribute becomes unselectable in the main editor.

4. **Verify renders and replace placeholder art.** `[CHANGED]`
   - **What:** Bind verified 3D renders/FC Editor previews to hair, beard, accessory, and tattoo tiles. Display an explicit "N hidden" counter for missing items instead of blank tiles.
   - **Who:** Assistant.
   - **Proof:** Four screenshots (hair, beard, accessory, tattoo) showing populated tiles and a visible "N hidden" label.
   - **Stop Condition:** Texture memory allocation causes overlay stutter or crashes during scrolling.

5. **Fix persistent layout and window sizing on installed build.** `[KEPT]`
   - **What:** Ensure main window dimensions and split-pane widths (`SplitStore` in `gui_prefs.h:331`) save to `gui_settings.json` on drag release and restore upon launch.
   - **Who:** Assistant (build), Owner (verify).
   - **Proof:** Owner resizes overlay and pane splitters, restarts game, and verifies identical position and dimensions.
   - **Stop Condition:** Default ImGui constraints overwrite `gui_settings.json` on boot (`app.cpp:2112`).

6. **Implement live memory slider writes and replace text states.** `[CHANGED]`
   - **What:** Replace read-only text cells in `ui_game_settings.cpp:378` with direct memory controls (`SliderInt`). Eliminate `turbo_settings_apply.exe` staging steps.
   - **Who:** Assistant.
   - **Proof:** `grep -c "read-only" turbogui/src/ui/ui_game_settings.cpp` returns 0; video showing slider change in Turbo instantly updating EA FC 27 menus.
   - **Stop Condition:** `live::find_block` fails to locate Custom slider block in game memory.

7. **Verify player-row callname audio preview.** `[CHANGED]`
   - **What:** Test and confirm audible playback of existing `callname_play_button` audio on player rows using `WinWavPlayer` (`callname_audio_win.cpp:78`).
   - **Who:** Assistant (build), Owner (verify).
   - **Proof:** Owner presses play button on a callname row in career mode and hears commentary audio.
   - **Stop Condition:** Audio device acquisition fails or causes game audio crackling.

8. **Clean up wordy copy and developer caveats.** `[KEPT]`
   - **What:** Remove 5-line yellow warning paragraphs (`ui_game_settings.cpp:670`) and sentence-length table cell states. Limit screen help to one short line.
   - **Who:** Assistant.
   - **Proof:** Screenshots of Settings and Looks pages showing clean copy; `grep -rn "mapped (game table)" turbogui/src` returns zero hits.
   - **Stop Condition:** User is left without feedback when a memory write fails.

9. **Conduct 48-hour Tactics feasibility study or purge tab.** `[NEW]`
   - **What:** Perform a strict time-boxed investigation to determine if tactic memory structures reliably alter pitch behavior. If unproven after 48 hours, delete the Tactics tab entirely.
   - **Who:** Assistant.
   - **Proof:** A 1-page report demonstrating verified pitch positional changes, or a git diff removing `Page::TacticPresets` from `ui_shell.cpp`.
   - **Stop Condition:** Investigation time exceeds 48 hours without verifiable match proof.

10. **Execute single-branch release tag v3.0.0.** `[NEW]`
    - **What:** Package the clean, single-branch build into a release ZIP, verify file safety safeguards, and tag v3.0.0.
    - **Who:** Assistant (package), Owner (release).
    - **Proof:** Successful injection of tagged v3.0.0 `Turbo.dll` with zero lost settings or career save corruption.
    - **Stop Condition:** Game main menu crashes upon DLL injection.

---

### 10. Disagreements with the Majority

1. **Classification of "GAME IS YOURS" Handoff (Lie vs. Negligent Framing):** Both Claude and Copilot classify the 09:45 hand-off headline "GAME IS YOURS" as a "negligent misreport" or "framing error" because fine print below noted unproven items. I disagree. Promising a non-programmer owner that a complete overhaul is delivered overnight while 10 of 11 items are unverified and the secondary DLL is crashing is an intentional misrepresentation of delivery.
2. **Total Excision of Pit Wall vs. Partial Retention:** Copilot recommends retaining parts of Pit Wall (like status tags or acceptance checks). I disagree. Pit Wall's overhead consumed >50% of the token budget and issued false `PASS` stamps on unusable builds. Process wrappers must be completely deleted; verification must rely solely on visual and behavioral proof in the running game.
3. **Execution Order (Structural Deletion First vs. Small Wins First):** Claude proposes fixing copy and saved sizes before cutting redundant menus. I disagree. Fixing styling or layout on legacy components (`ui_pages_216.cpp`) before deleting them results in wasted work on code destined for the bin. Refactoring must begin by deleting duplicate code paths.