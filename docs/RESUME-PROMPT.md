# Resume prompt for Cowork session `cse_018KNHBkXKaKC6RHBU17Afhu`

Paste everything under the line into that session (after its 5-hour limit resets at **19:00 UTC** on 2026-10-03), or let
another Claude session send it with `send_message` to `session_018KNHBkXKaKC6RHBU17Afhu`. The session is bound to the
user's PC, so it cannot be started by a scheduled routine from anywhere else. It is the only place that has the 0.3.0
source, the PC connection and the context.

---

The user asked to resume the build. Your 5-hour limit has reset. A handover for this session was written from your transcript and is on GitHub: cmx24/FC27-Editor-Turbo, branch claude/cool-brahmagupta-e599j5, files docs/HANDOVER-0.3.0.md, docs/re/standings-fixtures-notes.md and docs/re/pending-re-briefs.md. The key facts are repeated here in case you cannot read GitHub.

STEP 0 (do this first, before any game work): get your unpushed work out of this container.
- GitHub's claude/trusting-cannon-rkxwrw ends at 195cdb5 (Turbo 0.2.4). Your commits 89fc73e, 23d65be, 6c44d80, e4c58b7, 663d67f and 41d1ed5 exist only here. I found no patch export of 663d67f or 41d1ed5.
- In /home/claude/FC27-Editor-Turbo: run git status, commit anything uncommitted, then git format-patch 379554c..HEAD into /mnt/user-data/outputs/turbo_0.3.0_patches, tar it, and also make a git bundle. Copy the tar and the bundle to C:\FC 27 Live Editor\turbo_dev\ with device_commit_files, the way you did for turbo_0.2.5_patches.tar.
- Copy /home/claude/re/notes_standings.md, rx.py, rx2.py, fields.py and /home/claude/bin/mkdeploy.sh to /mnt/user-data/outputs/ as well (not fc27.bin, it is 555 MB and can be dumped again).
- Then try a plain git push origin HEAD:claude/trusting-cannon-rkxwrw. Never force. If it is rejected because the history differs, do not push anywhere else; just tell the user the push was rejected and that the patches and bundle are saved.

STEP 1: resume the build. The user's priorities and standing rules are unchanged: keep working autonomously, send a plain-language status about every 30 minutes, focus on the user's priorities, report only what you actually ran in the game versus what is database-only or untested. Rules: offline Career Mode only; do not touch FCLiveEditor.DLL; never save or modify the user's career turbotest (Torino); never commit Live Editor's files or personal data.
1. Make 0.3.0 safe. Your own plan at 16:40: block Transfer / Loan / Release / Delete into or out of the user's own club (in Lua and in the GUI), mark the others as unverified, keep delete_generated_players behind the same gate. The crash in turbolab (CrashDump_2026.10.03_12.34.23.152.mdmp) was never root-caused. Use a fresh test career, not turbolab, to find which structure still lists a moved player. Then rebuild, run the full offline tests, run scripts/package.sh to produce dist/FC27_LE_Turbo_0.3.0.zip (version in core/version.lua and kGuiVersion must both be 0.3.0), and give the user a short test list.
2. Then the user's wishlist in their order: (a) miniface generated from a snapshot of the in-game 3D player model, like FC 26 Live Editor, plus verify manager minifaces in the game; (b) edit league tables and match results (use your standings notes: patch the fixture and the two standings rows, locate DataManager by the heap vtable scan, test after a few league matches have been played); (c) create a job offer.
3. One open question for the user, to put in your first status message: static disassembly of the game image was done on the assumption that the user's "ALL features" message lifts the rule against disassembling or patching the game. The user has not confirmed that. Ask them to confirm or reject it, and do not extend the disassembly or patch game code until they answer. The three RE sub-agents you started at 16:45 all died on the rate limit and only the standings notes were saved, so the portrait-capture and job-offer RE has to be redone, and only once the user has answered.

Start your first status message with what you saved in STEP 0 and whether the push worked.
