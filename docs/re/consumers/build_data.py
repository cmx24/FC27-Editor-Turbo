"""build_data.py - writes docs/re/consumers_6AC07E31.json (per knob: my ranking, consumer evidence, freshness, DB copy, observable, metric, extremes).
Inputs: c1_sig.json (signatures), the inventory, the knobs file. All addresses are OLD image 6AB9813C; live is 6AC07E31-2145C000 [A]."""
import json, os
ROOT = r"C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers"
inv = json.load(open(ROOT + r"\docs\re\attrib_inventory_6AC07E31.json"))["groups"]
kn = json.load(open(ROOT + r"\turbo\package\turbo\tuning\knobs_6AC07E31.json"))["knobs"]
sig = json.load(open("c1_sig.json"))

# (id, tier, status, freshness, role, observable, metric, ext[lo,hi], effect-note)   -- in MY rank order (c_rank = position)
TOP = [
 ("marking.tightness", "S", "V", "per-call",
  "value x [[ctx+0x1A0]+0xFC4] -> max/min clamp (0..1, constants held in xmm8/xmm9 by the caller [A]) -> compared with xmm15, feeds the marking-distance blend (calls 0x141335F68, 0x141336140)",
  None, "nn_opp:without", [0.0, 1.2], "above the saturation point (v x Tm >= 1, Tm unknown [U]) nothing changes: the catalogue maximum 2.5 is wasted, 1.2 is the safe high extreme"),
 ("pressing.partial_highworkratetime", "S", "V", "per-call",
  "per-player partial-press burst length (OLD 0x14164F5A8): D = 60 x [grp+0x84] ticks for work-rate level 2, 60 x [grp+0x9C] for level 1, their mean otherwise; state in s = [Player*+0x28]: +0x294 active, +0x288 start tick, +0x284 rest-end tick (the chain is [A], offsets [V OLD])",
  "press_cycle", "press_n8:without", [2.0, 25.0], "how long a pressing player keeps closing down before he rests (default 6 s): the most direct 'press harder' lever found"),
 ("pressing.partial_min_penalty", "S", "V", "per-call",
  "rest after a burst (OLD 0x14164F338): s+0x284 = start + D + lerp(60 x [grp+0x88] .. 60 x [grp+0xA0]) by a stamina-like player term; the Relentless classes read 0x5C/0x64 (regular) and 0x68/0x60 (icon)",
  "press_rest", "press_n8:without", [0.0, 21.0], "write min and max together: 0 / 0 = the press never rests (default 7..15 s); the rest range is exactly readable"),
 ("pressing.partial_max_penalty", "S", "V", "per-call",
  "see partial_min_penalty: the longest rest, read at 0x14164F425", "press_rest", "press_n8:without", [4.5, 45.0], "pair with partial_min_penalty: 0 / 4.5 = almost no rest, 21 / 45 = long rests"),
 ("pressing.stepup_distance_bonus", "S", "V", "per-call",
  "defender step-up loop (function 0x146F204A8): bonus distance added to the step-up reach of the nearest defender for each opponent in the team's opponent list",
  None, "press_n8:without", [2.25, 18.0], "defenders leave the line to press an advancing carrier"),
 ("shape.def_line_range_max", "S", "A", "latched-ctor",
  "copied by the match-settings constructor (0x146A5EBB4 -> 0x146A521EC) from the customization group into [obj+0x5BA4] (min into +0x5BA0); widens how far the Defensive Line Height slider moves the line (+/-7.5 default)",
  "latched_copy:def_line", "last_def_depth:without", [7.5, 20.0], "LATCHED at match construction: write in the hub before kick-off; a mid-match write is not expected to act"),
 ("runs.frequency", "S", "U", "unknown",
  "group gp_positioning_attackingaggressiveness (0x38:0x30): no reader found by the DB-chain sweep (0 functions) - cached or read through a path not swept [U]",
  None, "beyond_share:with", [0.1, 3.0], "attackers' off-ball run frequency"),
 ("pressing.trigger_depth", "S", "U", "unknown",
  "zonal-defense context builder cluster around 0x140D2D494..0x140D2D512 copies zonal fields 0x684/0x68C/0x6AC [A]; the field-0x644 reader is not identified [U]",
  None, "press_n8:without", [0.05, 0.9], "when the team starts to press, by field depth"),
 ("pressing.partial_cooldownmultiplier", "A", "V", "per-call",
  "partial-team-press controller (0x14164F338): elapsed-tick counter [ctrl+0x290] += dt / max(1, [grp+0x74]); controller state +0x284 (end tick), +0x288, +0x28C, +0x294 (active flag), +0x298; the controller object is not located [U]",
  None, "press_n8:without", [0.1, 5.0], "how often the partial press re-arms"),
 ("goalkeeper.rush_radius", "A", "V", "per-call",
  "GK rush-to-contain parameter block builder (0x146FD6B34): radius and minimum distance stored into a 16-byte block at [rdi] (a caller out-param, transient [A])",
  None, "gk_out:without", [18.0, 120.0], "sweeper-keeper behaviour: distance from the goal line when an opposing runner is through"),
 ("pressing.stepup_mid_distance_bonus", "A", "V", "per-call",
  "midfielder step-up (function 0x1416965F0): bonus distance for the midfielders' step-up reach", None, "press_n8:without", [1.8, 15.0], "midfielders step out to the carrier"),
 ("marking.global_reaction", "A", "V", "per-call",
  "global defending reaction-time multiplier (function 0x1416F2750): value x base -> per-player slot [array+idx*8+0x48]; defenders react later / earlier", None, "nn_opp:without", [0.3, 2.5], "defenders' reaction delay to attackers' movement"),
 ("marking.reaction_defenders", "A", "V", "per-call",
  "ReactionTimeMasterModifierDf read at 0x141A12A83 (function 0x141A125D0), a delay factor fed through the 0x140B2F1F8 curve calls", None, "nn_opp:without", [0.3, 2.5], "defenders' marking reaction time"),
 ("offside.play_offside_depth", "A", "U", "unknown",
  "group playoffsides (0x38:0x138): sweeps show readers of other fields only (0x141909B10, 0x1428B90A8, 0x146AC16D4); this field has no located reader [U]",
  None, "last_def_depth:without", [0.0, 1.0], "defensive line steps up / holds for the offside trap"),
 ("recovery.defenders_sprint_back", "A", "V", "per-call",
  "backward-speed rate table (function 0x146ED8E8C): defenders 0x2C, midfielders 0x38, forwards 0x48 chosen by role, returned as a speed multiplier", None, "backline_m:without", [0.6, 1.3], "how fast defenders recover when the ball is lost"),
 ("defending.jockey_speed", "A", "V", "per-call",
  "Jockey_SpeedJog (min(.., [grp+0x78]) in function 0x140B55018 and x [grp+0x78] in 0x141985C50); the SIMULATION instance is a separate object (0.18 vs 0.28 in the base copy)", None, "nn_opp:without", [0.05, 0.6], "speed at which defenders jockey the carrier"),
 ("shape.length_in", "A", "V", "per-call",
  "0x140F57098 (x1p / battery): style 1 -> formation length = H*(x8 + t*(1 + w*(L-1) - x8)), H = pitch length/2, t = clamp((state+0x38 + 1)/2), w a ball-distance blend; persistent output state+0x2C",
  "paired_len", "len_sd:with", [0.5, 1.5], "team compactness with the ball"),
 ("goalkeeper.rush_enabled", "A", "V", "per-call",
  "EnableGKRushChargeToContain read at 0x146D6E9FC (function 0x146D6E944), gates the GK rush branch (virtual calls +0x570 / +0x578 / +0x580)", None, "gk_out:without", [0, 1], "switch: the keeper never charges / may charge"),
 ("lineband.high_min", "A", "V", "setup",
  "PREMATCH database, group pm_teamtacticssettings (family 0x40, slot 0x18): the six thresholds are read together at 0x141DAAD24 (called from 0x141DAAA14 <- 0x146A89AB8) which maps the line-height slider (1..100, default 51) piecewise-linearly between the bands",
  None, "backline_m:without", [40, 75], "re-shapes how the Line Height slider maps to depth; read at tactic set-up (prematch DB), not per frame"),
 ("shape.length_out", "A", "U", "unknown",
  "defensive group +0x30: NO reader found (0x140F57098 reads only the attacking group; the sweeps find none) - probably unused [U]", None, "len_sd:without", [0.5, 1.5], "no effect expected"),
 ("pressing.opponent_third_press_depth", "A", "U", "unknown",
  "zonal DefDepthThresholdForOppThirdPress (0x66C): word reads exist at 0x140627E8C / 0x1407CF002 but the base object is not tied to the group [U]", None, "press_n8:without", [0.0, 1.5], "pressing in the opponent third"),
 ("pressing.zone_marking_depth", "A", "U", "unknown", "zonal TacticDepthThresholdForZoneMarking (0x684): cluster 0x140980ABE / 0x140D2D494 [A]", None, "nn_opp:without", [0.0, 1.5], "zone to man marking switch"),
 ("pressing.cut_lane_reach", "A", "U", "unknown",
  "gp_positioning_zonal_defense_cutpassinglanes (0x38:0xE8): readers of the group exist (0x145A0D994, 0x145A0E328, 0x145A0E8FC) but not of this field [U]; the SIMULATION instance differs (6.0 vs 3.5)",
  None, "nn_opp:without", [1.05, 10.5], "passing-lane cutting reach"),
 ("pressing.contain_high_time", "A", "U", "unknown", "contain group (0x38:0xC0): reader 0x143A046E8 touches other fields [U]", None, "press_m:without", [1.35, 18.0], "how long the nearest defender contains"),
 ("cpuai.through_pass_mod", "B", "U", "unknown", "cpuai ball handler group (0x68:0x28): readers 0x140821528, 0x140C51D9C, 0x1413484C0 touch other fields [U]; acts only for the CPU-controlled team", None, None, [0.3, 2.0], "CPU through-ball frequency"),
 ("shooting.base_speed_max", "B", "U", "unknown", "kick shot group (0xA0:0x28): readers 0x140BA3264, 0x1413B77C8, 0x142CFE7E0 touch other fields [U]", None, None, [21.4, 50.0], "shot speed; measure the ball speed after a shot"),
 ("pressing.partial_lowworkratetime", "B", "V", "per-call", "burst length of work-rate level 1 players (0x9C, OLD 0x14164F638); same machine as partial_highworkratetime", "press_cycle", "press_n8:without", [1.0, 20.0], "low work-rate players' press burst"),
 ("fatigue.pressing_cost", "B", "V", "per-call", "0x14141F604: fatigueModifierForPressureTactics scales the stamina drain; sink [rbx+0x64]", None, None, [0.0, 20.0], "stamina; slow effect (minutes)"),
]
rank_of = {t[0]: i + 1 for i, t in enumerate(TOP)}
out = {"build_old": "6AB9813C", "build_live": "6AC07E31-2145C000",
       "note": "addresses are the OLD image (base 0x140000000); live claims are [A] until the coordinator's check passes; V = read in the disassembly of the old image, A = assessed, U = unknown",
       "knobs": {}}
n = len(TOP)
for x in sorted(kn, key=lambda x: x["rank"]):
    kid = x["id"]
    g = inv[x["collection"]]
    sims = [v for v in g["variants"] if v.startswith("simulation")]
    e = {"id": kid, "group": x["collection"], "field": x["field"], "offset": x["offset"], "family_slot": hex(g["family_slot"]), "group_slot": hex(g["group_slot"]),
         "copy": "sim-distinct" if sims else "shared", "sim_differs_field": bool(sims and x["field"] in g["variants"][sims[0]].get("differs", {})),
         "db": "prematch" if x["collection"].startswith("pm_") else "gameplay"}
    ss = sig.get(kid) or []
    if ss:
        s = ss[0]
        e["consumer"] = {"func": s.get("func"), "read": s.get("read"), "insn": s.get("txt"), "sig": s.get("sig"), "sig_hits": s.get("hits"),
                         "also": [{"func": t.get("func"), "read": t.get("read")} for t in ss[1:]]}
    if kid in rank_of:
        r = TOP[rank_of[kid] - 1]
        e.update(c_rank=rank_of[kid], tier=r[1], status=r[2], freshness=r[3], role=r[4] + (' (the read site is V OLD; this description of its role is A)' if r[2]=='V' else ''), observable=r[5], metric=r[6], ext=r[7], effect=r[8])
    else:
        e.update(c_rank=n + x["rank"], tier="C" if x["impact"] == "low" else "B", status="V" if ss else "U", freshness="per-call" if ss else "unknown",
                 role="read at the listed site (role not analysed)" if ss else "no reader located", observable=None, metric=None, ext=[], effect="")
    out["knobs"][kid] = e
order = sorted(out["knobs"].values(), key=lambda v: (v["c_rank"], v["id"]))
for i, v in enumerate(order):
    v["c_rank"] = i + 1
with open(os.path.join(ROOT, "docs", "re", "consumers_6AC07E31.json"), "w", newline="\n") as f:
    json.dump(out, f, indent=1)
print(len(out["knobs"]), "knobs;", sum(1 for v in out["knobs"].values() if v["status"] == "V"), "V")
