# Turbo 2.1 tactics plan: inputs (handover to the cloud session, 2026-10-07)

The user asked: "give me a plan to implement what I want the way I want it" for tactical tweaks/settings for the team and individual
positions, and game settings. Then: "run everything in cloud and only switch back to local to install".

Files here:
- research.json: five research reports (db-tactics, gameplay-sliders, runtime-path, fc27-surface, turbo-code-map), each with findings
  (VERIFIED/ASSESSED/UNKNOWN + evidence) and candidate write channels with an in-game A/B test each. Produced locally from the repo,
  the real FC 27 probe and web sources.
- fc27_tactic_tables_schema.json: the tactic-like tables and fields (types, ranges, row counts) read from the running game (career
  turbo04, FC 27 build 6AC07E31-2145C000) by probe_tactics. Schema only, no player data.
- ingame-results-2026-10-07.md: what was verified in played matches (weather, injuries, CPU subs, roles, re-apply) and what was not
  (difficulty, time of day, all team/position tactic writes).

Target, as understood (confirm with the user): FM-style control of the team and each position; gameplay sliders; opposition styles
matched to each team's tactics and quality; Authentic Gameplay and Dynamic Opposition stay selected; nothing is called working until it
is tested in a played match.

Work split: code, tests and builds in the cloud; installing and in-game tests on the user's PC (the game, Live Editor and the test career
turbo04 exist only there). Live Editor's Lua libs (turbo/le27/libs) are Live Editor's own code: never commit them.
