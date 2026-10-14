"""knobs_v5.py - Turbo 2.5 track A: builds and checks the knobs catalogue v5 (turbo/package/turbo/tuning/knobs_6AC07E31.json) from the
offline inventory (docs/re/attrib_inventory_6AC07E31.json). OFFLINE: never talks to the game.

  python -I tools/knobs_v5.py build [--knobs <knobs v4 or v5 file>] [--inv <inventory>] [--out <knobs file>]
  python -I tools/knobs_v5.py check [--knobs <file>] [--inv <inventory>]      validates a catalogue against the inventory
  python -I tools/knobs_v5.py --self-test

What v5 is: the 23 knobs of v4 stay exactly as they are (ids, ranges, verified flags: saved presets keep working) and gain a rank and a
category; new CANDIDATE knobs are added below them, each with the group, field, offset, type, FC 27's own value (and the value of the
Authentic copy where it differs), a proposed range, a plain-language label and the rank the offline assessment gives it (1 = the largest
expected on-pitch effect). Honest by construction: every new knob is "verified": false, "candidate": true, with no proof and no slider;
Turbo shows it as not yet working and never writes it. The ranking and the ranges are offline judgements [A]; a knob becomes a slider only
after the in-game proof of the 2.4 rules (docs/TACTICS_OVERHAUL_DECISION.md 3.1).
"""
import copy, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import attrib_inventory as ai  # noqa: E402

KNOBS = os.path.join(ROOT, "turbo", "package", "turbo", "tuning", "knobs_6AC07E31.json")
INV = ai.DEFAULT_INV
WHEN = ["boot", "career_load", "hub"]
NOT_YET = "Not yet confirmed in the running game"
IMPACT = ("high", "medium", "low")

# ---------------------------------------------------------------------------------------------------------------------- the spec
# Existing v4 knobs: (id, rank, impact). Rank 1 = the largest expected on-pitch effect of the whole catalogue [A].
EXISTING = [("pressing.trigger_depth", 1, "high"), ("shape.length_out", 2, "high"), ("shape.width_in", 3, "high"), ("shape.width_out", 4, "high"),
            ("runs.frequency", 5, "high"), ("marking.tightness", 6, "high"), ("shape.length_in", 7, "high"), ("pressing.partial_cooldownmultiplier", 9, "high"),
            ("pressing.partial_highworkratetime", 10, "high"), ("pressing.zone_marking_depth", 11, "high"), ("shape.gap_dmf", 14, "medium"),
            ("shape.gap_mf_deep", 15, "medium"), ("shape.gap_dmf_deep", 16, "medium"), ("lineband.deep_max", 17, "high"), ("lineband.balanced_min", 18, "high"),
            ("lineband.balanced_max", 19, "high"), ("lineband.high_min", 20, "high"), ("lineband.high_max", 21, "high"), ("lineband.aggressive_min", 22, "high"),
            ("shape.holding_line_depth", 24, "medium"), ("pressing.no_double_marking_own_half", 30, "medium"), ("shape.width_in_cb", 33, "medium"),
            ("shape.length_in_possession", 12, "high")]

# New candidates. (rank, id, group, inventory group, field, label, lo, hi, impact, unit, what it does in plain words)
# lo / hi: a number, or ("x", factor) = FC 27's value times the factor (rounded). Bools and whole-number fields are set, not scaled.
X = "x"
NEW = [
    (8, "pressing.stepup_distance_bonus", "Pressing", "gp_positioning_defender_stepup", "distanceBonusByStepUp", "Defender step-up reach", (X, 0.3), (X, 2.5), "high", "",
     "How much farther a defender is willing to leave the line to engage the ball carrier."),
    (13, "pressing.stepup_mid_distance_bonus", "Pressing", "gp_positioning_defender_stepupbymidfielders", "distanceBonusByStepUp", "Midfielder step-up reach", (X, 0.3), (X, 2.5), "high", "",
     "How much farther a midfielder steps out of his line to engage the ball carrier."),
    (23, "pressing.stepup_trigger_speed", "Pressing", "gp_positioning_defender_stepup", "forwardSpeedThreshold", "Defender step-up trigger speed", 0.05, 0.8, "medium", "",
     "The speed of the attacker's forward move above which the defender steps up (lower = steps up earlier)."),
    (27, "pressing.stepup_opponent_range", "Pressing", "gp_positioning_defender_stepup", "maxDistOppToDefender", "Defender step-up opponent range", (X, 0.3), (X, 2.5), "medium", "",
     "The largest distance between an attacker and a defender for the defender to consider stepping up."),
    (25, "pressing.partial_max_penalty", "Pressing", "gp_positioning_partialteampress", "partialTeamPressMaxPenaltyTime", "Partial press: longest rest", (X, 0.3), (X, 3.0), "medium", "",
     "The longest rest a player takes after a partial press before he may press again."),
    (26, "pressing.partial_min_penalty", "Pressing", "gp_positioning_partialteampress", "partialTeamPressMinPenaltyTime", "Partial press: shortest rest", (X, 0.0), (X, 3.0), "medium", "",
     "The shortest rest a player takes after a partial press before he may press again."),
    (28, "pressing.partial_lowworkratetime", "Pressing", "gp_positioning_partialteampress", "partialTeamPressLowWorkRateTime", "Partial press: duration (low work rate)", (X, 0.25), (X, 5.0), "medium", "",
     "How long a low-work-rate player keeps pressing in a partial press."),
    (29, "pressing.contain_high_time", "Pressing", "gp_positioning_contain", "containHighWorkRateTime", "Containing: duration (high work rate)", (X, 0.3), (X, 4.0), "high", "",
     "How long the defender nearest the ball keeps containing the carrier (high work rate)."),
    (31, "pressing.contain_low_time", "Pressing", "gp_positioning_contain", "containLowWorkRateTime", "Containing: duration (low work rate)", (X, 0.3), (X, 4.0), "medium", "",
     "How long the defender nearest the ball keeps containing the carrier (low work rate)."),
    (32, "pressing.contain_cooldown", "Pressing", "gp_positioning_contain", "containCooldownMultiplier", "Containing: cooldown", (X, 0.1), (X, 3.0), "medium", "",
     "How long a defender must wait before he contains again."),
    (34, "pressing.team_press_marking_distance", "Pressing", "gp_positioning_defense_general", "DfMarkingDistWhenTeamPress", "Marking distance during a team press", (X, 0.3), (X, 3.0), "medium", "",
     "How close defenders mark while the whole team presses."),
    (35, "pressing.team_press_own_goal_relief", "Pressing", "gp_positioning_defense_general", "teamPressDefensiveTightnessReductionFactorCloseToDefenderGoalLine", "Press tightness near the own goal", 0.3, 1.5, "medium", "x",
     "How much the press loosens when the ball is close to the own goal line (lower = looser)."),
    (36, "pressing.transition_defense_depth", "Pressing", "gp_positioning_defense_general", "depthThresholdForTransitionDefense", "Depth for transition defending", 0.0, 1.0, "medium", "",
     "The team defensive depth above which the team defends the moment it loses the ball."),
    (37, "pressing.aggressive_interception_depth", "Pressing", "gp_positioning_defense_general", "depthThresholdForAggressiveInterception", "Depth for aggressive interceptions", 0.0, 1.0, "medium", "",
     "The team defensive depth above which defenders try to intercept aggressively."),
    (38, "pressing.align_with_deepest_cb_depth", "Pressing", "gp_positioning_defense_general", "depthThresholdForAlignWithDeepestCB", "Depth for lining up with the deepest centre-back", 0.0, 1.0, "medium", "",
     "The team defensive depth above which the line lines up with the deepest centre-back."),
    (39, "pressing.keep_press_after_gain", "Pressing", "gp_positioning_defense_general", "teamPressDisactivateAfterPossessionGain", "Stop the press when the ball is won", 0, 1, "low", "",
     "Switch: whether the team press ends when possession is won."),
    (40, "pressing.opponent_third_press_depth", "Pressing", "gp_positioning_zonal_defense", "DefDepthThresholdForOppThirdPress", "Depth for pressing in the opposition third", 0.0, 1.5, "high", "",
     "The team defensive depth above which the team presses in the opposition's third."),
    (41, "pressing.cut_lane_reach", "Pressing", "gp_positioning_zonal_defense_cutpassinglanes", "MaxDistToIntercept_WithoutInstruction", "Passing-lane cutting reach", (X, 0.3), (X, 3.0), "high", "",
     "How far a defender goes out of position to cut the passing lane (no player instruction)."),
    (42, "pressing.cut_lane_reach_instructed", "Pressing", "gp_positioning_zonal_defense_cutpassinglanes", "MaxDistToIntercept_WithInstruction", "Passing-lane cutting reach (instructed)", (X, 0.3), (X, 3.0), "medium", "",
     "The same with an interception instruction."),
    (43, "pressing.cut_lane_enabled", "Pressing", "gp_positioning_zonal_defense_cutpassinglanes", "UseFeature", "Defenders cut passing lanes", 0, 1, "medium", "",
     "Switch: the whole passing-lane cutting behaviour."),
    (44, "marking.tight_in_box", "Marking", "gp_positioning_zonal_defense", "TightMarkingInBoxEnabled", "Tight marking in the box", 0, 1, "high", "",
     "Switch: defenders mark attackers tightly inside the penalty box."),
    (45, "marking.mark_possessor", "Marking", "gp_positioning_zonal_defense", "MarkingPossessorEnabled", "Mark the ball carrier", 0, 1, "medium", "",
     "Switch: a defender marks the player on the ball."),
    (46, "marking.shot_block", "Marking", "gp_positioning_zonal_defense", "EnableShotBlockMarking", "Mark to block shots", 0, 1, "medium", "",
     "Switch: defenders position to block shots."),
    (47, "marking.man_mark_near_goal", "Marking", "gp_positioning_zonal_defense", "ManMarkNearGoalEnabled", "Man-marking near the goal", 0, 1, "medium", "",
     "Switch: defenders switch to man-marking close to their own goal."),
    (48, "marking.reaction_defenders", "Marking", "gp_positioning_zonal_defense_attribute", "ReactionTimeMasterModifierDf", "Defenders' reaction time", 0.3, 2.5, "high", "x",
     "A master multiplier of how quickly zonal defenders react (higher = slower)."),
    (49, "marking.reaction_midfielders", "Marking", "gp_positioning_zonal_defense_attribute", "ReactionTimeMasterModifierMf", "Midfielders' reaction time", 0.3, 2.5, "high", "x",
     "A master multiplier of how quickly zonal midfielders react (higher = slower)."),
    (50, "marking.reaction_attackers", "Marking", "gp_positioning_zonal_defense_attribute", "ReactionTimeMasterModifierAttackers", "Attackers' defensive reaction time", 0.3, 2.5, "medium", "x",
     "A master multiplier of how quickly attackers react when defending (higher = slower)."),
    (51, "marking.top_speed_defenders", "Marking", "gp_positioning_zonal_defense_attribute", "TopSpeedMasterModifierDf", "Defenders' top speed when marking", 0.6, 1.4, "medium", "x",
     "A master multiplier of the top speed zonal defenders use."),
    (52, "marking.global_reaction", "Marking", "gp_positioning_defending_attributes", "globalReactionTimeMultiflier", "Global defending reaction time", 0.3, 2.5, "high", "x",
     "One multiplier on the defenders' reaction time for every defending decision (higher = slower)."),
    (53, "marking.after_kickoff_period", "Marking", "gp_positioning_zonal_defense", "AfterKickoffZonalMarkingPeriod", "Zonal marking time after kick-off", 0, 120, "low", "",
     "How long the zonal marking rules apply after a kick-off."),
    (54, "offside.play_offside_depth", "Shape without the ball", "gp_positioning_zonal_defense_playoffsides", "MinDepthToPlayOffside", "Depth for playing offside", 0.0, 1.0, "high", "",
     "The team defensive depth above which the line steps up to catch attackers offside."),
    (55, "offside.prevent_retreat_depth", "Shape without the ball", "gp_positioning_zonal_defense_playoffsides", "MinDepthToPreventFromRetreating", "Depth for not retreating", 0.0, 1.0, "medium", "",
     "The team defensive depth above which the line does not drop back."),
    (56, "offside.hold_line_speed_depth", "Shape without the ball", "gp_positioning_zonal_defense_playoffsides", "MinDepthToUseHoldLineSpeed", "Depth for holding the line", 0.0, 1.0, "medium", "",
     "The team defensive depth above which the line holds instead of following the runner."),
    (57, "offside.pushup_enabled", "Shape without the ball", "gp_positioning_zonal_defense_playoffsides", "ShouldPushupEnabled", "Push the line up", 0, 1, "medium", "",
     "Switch: the line pushes up towards the next line."),
    (58, "offside.pushup_ratio", "Shape without the ball", "gp_positioning_zonal_defense_playoffsides", "MaxPushUpTowardsNextLineRatio", "Push-up distance", 0.0, 1.0, "medium", "x",
     "How far towards the next line the back line may push up."),
    (59, "shape.gap_mf_par", "Shape without the ball", "gp_positioning_zonal_defense", "mfLineOffsetWithDmfLineWhenTouchIsOnPar", "Gap to the midfield line (ball level)", 4.0, 30.0, "medium", "m",
     "The distance to the midfield line when the ball is at a normal height and a holding line exists."),
    (60, "shape.gap_mf_nodmf_deep", "Shape without the ball", "gp_positioning_zonal_defense", "mfLineOffsetWithoutDmfLineWhenTouchIsDeep", "Gap to the midfield line, no holding line (ball deep)", 4.0, 30.0, "low", "m",
     "The distance to the midfield line when there is no holding line and the ball is deep."),
    (61, "shape.gap_mf_nodmf_par", "Shape without the ball", "gp_positioning_zonal_defense", "mfLineOffsetWithoutDmfLineWhenTouchIsOnPar", "Gap to the midfield line, no holding line (ball level)", 4.0, 30.0, "low", "m",
     "The distance to the midfield line when there is no holding line and the ball is at a normal height."),
    (62, "shape.width_in_back3", "Shape with the ball", "gp_positioningformation_attackinglinelengthwidth", "lineWidthModifierForBack3", "Width of a back three with the ball", 0.4, 1.7, "medium", "x",
     "How wide a three-man back line spreads while the team has the ball."),
    (63, "shape.width_in_back5", "Shape with the ball", "gp_positioningformation_attackinglinelengthwidth", "lineWidthModifierForBack5", "Width of a back five with the ball", 0.4, 1.7, "medium", "x",
     "How wide a five-man back line spreads while the team has the ball."),
    (64, "shape.length_in_fastbuildup", "Shape with the ball", "gp_positioningformation_attackinglinelengthwidth", "LineLengthModifierForFastBuildUp", "Team length, fast build-up", 0.5, 1.5, "medium", "x",
     "Distance from the back line to the front line when attacking with the fast build-up."),
    (65, "shape.length_in_longball", "Shape with the ball", "gp_positioningformation_attackinglinelengthwidth", "LineLengthModifierForLongBall", "Team length, long-ball build-up", 0.5, 1.5, "medium", "x",
     "Distance from the back line to the front line when attacking with the long-ball build-up."),
    (66, "shape.width_out_cb", "Shape without the ball", "gp_positioningformation_defensivelinelengthwidth", "lineWidthModifierForCB", "Centre-back split without the ball", 0.6, 1.7, "medium", "x",
     "How far apart the centre-backs stand while defending."),
    (67, "shape.length_out_longball", "Shape without the ball", "gp_positioningformation_defensivelinelengthwidth", "LineLengthModifierForLongBall", "Team length without the ball, long-ball team", 0.5, 1.5, "low", "x",
     "Distance from the back line to the front line when defending against the long-ball build-up."),
    (68, "runs.decisive_run_enabled", "Attacking runs", "gp_positioning_attacking_general", "EnableDecisiveRun", "Decisive runs", 0, 1, "high", "",
     "Switch: attackers start decisive runs in behind."),
    (69, "runs.decisive_run_min_speed", "Attacking runs", "gp_positioning_attacking_general", "MinSpeedToStartDecisiveRun", "Speed to start a decisive run", 0.1, 1.0, "medium", "",
     "The speed an attacker needs before he starts a decisive run (lower = runs start earlier)."),
    (70, "runs.stick_to_position_frequency", "Attacking runs", "gp_positioning_attacker_common", "RunFrequencyFactorForStickToPositionPlayer", "Run frequency of stay-in-position players", 0.0, 2.0, "medium", "x",
     "How often players with a stay-in-position instruction make runs."),
    (71, "runs.come_short_frequency", "Attacking runs", "gp_positioning_attacker_common", "RunFrequencyFactorForComeShortPlayer", "Run frequency of come-short players", 0.0, 2.0, "medium", "x",
     "How often players with a come-short instruction make runs."),
    (72, "runs.form_return_defenders", "Attacking runs", "gp_positioning_attacker_toformpos", "formReturnWeightAtDF", "Defenders' return to position", 0.0, 1.5, "medium", "x",
     "How strongly defenders return to their formation spot while the team attacks."),
    (73, "runs.form_return_forwards", "Attacking runs", "gp_positioning_attacker_toformpos", "formReturnWeightAtFW", "Forwards' return to position", 0.0, 1.5, "medium", "x",
     "How strongly forwards return to their formation spot while the team attacks."),
    (74, "runs.build_up_cb_cover", "Attacking runs", "gp_positioning_attacker_safesupport", "CoverAreaExtensionXForBallPlayingCB", "Build-up space for a ball-playing centre-back", (X, 0.3), (X, 2.5), "medium", "",
     "How far support players are allowed to come to show for a ball-playing centre-back."),
    (75, "roles.fullback_width", "Roles", "gp_positioning_tacticalrole", "FormationReferencePosXRatioForFullback", "Full-back width reference", 0.0, 1.5, "high", "x",
     "How far up and out a full-back's reference spot is pushed (0 = FC 27)."),
    (76, "roles.wingback_width", "Roles", "gp_positioning_tacticalrole", "FormationReferencePosXRatioForWingback", "Wing-back width reference", 0.0, 1.5, "high", "x",
     "How far up and out a wing-back's reference spot is pushed."),
    (77, "roles.offensive_wingback_width", "Roles", "gp_positioning_tacticalrole", "FormationReferencePosXRatioForOffensiveWingback", "Attacking wing-back width reference", 0.0, 1.5, "medium", "x",
     "How far up and out an attacking wing-back's reference spot is pushed."),
    (78, "roles.fullback_far_side", "Roles", "gp_positioning_tacticalrole", "FormationOffsetPosZForOppositeBallSideFullback", "Full-back tuck-in on the far side", -0.3, 0.2, "medium", "",
     "How far a full-back moves across when the ball is on the other side."),
    (79, "roles.mark_speed_advanced", "Roles", "gp_positioning_tacticalrole", "markSpeedModifierForAdvanced", "Marking speed of well-suited players", (X, 0.6), (X, 1.5), "low", "x",
     "The marking-speed multiplier for a player who is fully familiar with his role."),
    (80, "recovery.defenders_sprint_back", "Pace and fatigue", "gp_positioning_backwardspeed", "defenderSprintBackSpeedRate", "Defenders' recovery sprint speed", 0.6, 1.3, "high", "x",
     "How fast defenders sprint back towards their own goal."),
    (81, "recovery.midfielders_sprint_back", "Pace and fatigue", "gp_positioning_backwardspeed", "midfielderSprintBackSpeedRate", "Midfielders' recovery sprint speed", 0.5, 1.3, "high", "x",
     "How fast midfielders sprint back towards their own goal."),
    (82, "recovery.forwards_sprint_back", "Pace and fatigue", "gp_positioning_backwardspeed", "forwardSprintBackSpeedRate", "Forwards' recovery sprint speed", 0.5, 1.3, "medium", "x",
     "How fast forwards sprint back towards their own goal."),
    (83, "cpuai.commit_early_default", "CPU AI", "gp_cpuai_callforaction", "DefaultCommitEarlyScoreThreshold", "CPU releases to runners earlier", 0.2, 1.0, "high", "",
     "The score above which the computer releases the ball to a team mate at once (lower = earlier)."),
    (84, "cpuai.commit_early_regular_pass", "CPU AI", "gp_cpuai_callforaction", "RegularPassCommitEarlyScoreThreshold", "CPU releases ground passes earlier", 0.2, 1.0, "medium", "",
     "The same for ordinary ground passes."),
    (85, "cpuai.commit_early_through_pass", "CPU AI", "gp_cpuai_callforaction", "ThroughPassCommitEarlyScoreThreshold", "CPU releases through balls earlier", 0.2, 1.0, "medium", "",
     "The same for through balls."),
    (86, "cpuai.commit_early_cross", "CPU AI", "gp_cpuai_callforaction", "CrossCommitEarlyScoreThreshold", "CPU releases crosses earlier", 0.2, 1.0, "medium", "",
     "The same for crosses."),
    (87, "cpuai.through_pass_mod", "CPU AI", "gp_cpuai_cpuaiballhandler", "ThroughPassMod", "CPU liking for through balls", 0.3, 2.0, "high", "x",
     "How much the computer favours through balls when it chooses a pass."),
    (88, "cpuai.ground_pass_min_safety", "CPU AI", "gp_cpuai_cpuaiballhandler", "GroundPassDecisionMinCourseSafety", "CPU safety needed for a ground pass", 0.8, 1.0, "high", "",
     "How safe a pass lane must be before the computer plays it (higher = fewer risky passes)."),
    (89, "cpuai.slide_tackle_probability", "CPU AI", "gp_cpuai_cpuaiballhandler", "SlideTackleProbability", "CPU slide-tackle frequency", 0.0, 1.0, "medium", "",
     "How often the computer chooses a slide tackle."),
    (90, "cpuai.one_time_pass", "CPU AI", "gp_cpuai_cpuaiballhandler", "OneTimePassProbabilityMod", "CPU first-time passing", 0.0, 1.0, "medium", "",
     "How often the computer passes first time."),
    (91, "cpuai.finesse_shot_probability", "CPU AI", "gp_cpuai_cpuaiballhandler", "FinesseShotProbability", "CPU finesse-shot frequency", 0.0, 1.0, "low", "",
     "How often the computer chooses a finesse shot."),
    (92, "passing.speed_reduce_by_attribute", "Passing", "gp_kickpass_passing", "PASS_SpeedReduceRateByAttr", "Pass speed lost by weak passers", 0.0, 0.6, "medium", "",
     "How much slower the passes of low-rated passers are."),
    (93, "passing.transition_error", "Passing", "gp_kickpass_passing", "PASS_TransitionErrorScalar", "Pass error after a change of direction", 0.0, 3.0, "medium", "x",
     "A multiplier on passing error when the passer changes direction."),
    (94, "passing.openness_scalar", "Passing", "gp_kickpass_targetcalc_scoring_through", "OpennessScalar", "Receiver openness for through balls", (X, 0.3), (X, 3.0), "medium", "x",
     "How much a free receiver counts when a through ball is aimed."),
    (95, "passing.block_rate_scalar", "Passing", "gp_kickpass_targetcalc_scoring_through", "BlockRateScalar", "Blocked-lane penalty for through balls", (X, 0.0), (X, 3.0), "medium", "x",
     "How strongly a blocked lane counts against a through ball."),
    (96, "shooting.base_speed_max", "Shooting", "gp_kickshot_shooting", "SHOT_BaseSpeedMax", "Strongest shot speed", (X, 0.6), (X, 1.4), "high", "",
     "The ball speed of a full-power shot."),
    (97, "shooting.base_speed_min", "Shooting", "gp_kickshot_shooting", "SHOT_BaseSpeedMin", "Weakest shot speed", (X, 0.5), (X, 1.5), "medium", "",
     "The ball speed of a minimum-power shot."),
    (98, "shooting.transition_error", "Shooting", "gp_kickshot_shooting", "SHOT_TransitionErrorScalar", "Shot error after a change of direction", 0.0, 3.0, "medium", "x",
     "A multiplier on shooting error when the shooter changes direction."),
    (99, "goalkeeper.rush_radius", "Goalkeeper", "gp_goalkeeper_goalkeeper", "GK_RushToContain_Max_Radius", "Goalkeeper rush-out radius", (X, 0.3), (X, 2.0), "high", "",
     "How far from the goal the keeper rushes out to close down an attacker."),
    (100, "goalkeeper.rush_min_distance", "Goalkeeper", "gp_goalkeeper_goalkeeper", "GK_RushToContain_Min_Dist_From_Goal", "Goalkeeper rush-out minimum distance", (X, 0.3), (X, 3.0), "medium", "",
     "The smallest distance from the goal at which the keeper starts to rush out."),
    (101, "goalkeeper.rush_enabled", "Goalkeeper", "gp_goalkeeper_goalkeeper", "EnableGKRushChargeToContain", "Goalkeeper rushes out", 0, 1, "high", "",
     "Switch: the keeper charges out to contain an attacker."),
    (102, "goalkeeper.cover_far_post", "Goalkeeper", "gp_goalkeeper_goalkeeper", "EnableGKCoverFarPost", "Goalkeeper covers the far post", 0, 1, "medium", "",
     "Switch: the keeper positions to cover the far post."),
    (103, "goalkeeper.catch_score", "Goalkeeper", "gp_goalkeeper_newgk", "GK_Save_Difficulty_Catch_Score", "Goalkeeper catch limit", 0.2, 1.0, "high", "",
     "The save difficulty up to which the keeper catches the ball instead of parrying."),
    (104, "goalkeeper.parry_close_score", "Goalkeeper", "gp_goalkeeper_newgk", "GK_Save_Difficulty_Parry_Close_Score", "Goalkeeper close-parry limit", 0.2, 1.0, "medium", "",
     "The save difficulty up to which the keeper parries the ball close to himself."),
    (105, "goalkeeper.playback_scale", "Goalkeeper", "gp_goalkeeper_newgk", "GK_Playback_Scale", "Goalkeeper movement speed", 0.6, 1.5, "high", "x",
     "The playback speed of the keeper's save animations."),
    (106, "defending.jockey_speed", "Defending", "gp_defense_jockey", "Jockey_SpeedJog", "Jockeying speed", 0.05, 0.6, "high", "",
     "How fast a defender backs off while jockeying (the Authentic copy is slower)."),
    (107, "defending.instant_effort_distance", "Defending", "gp_defense_tackling", "Tackle_InstantEffortMaxDistance", "Distance for an instant tackle effort", (X, 0.3), (X, 2.0), "medium", "",
     "The largest distance at which a tackle attempt starts at once."),
    (108, "defending.block_reflex_ticks", "Defending", "gp_defense_blockintercept", "BlockReflexTicks", "Block reaction time", (X, 0.3), (X, 2.5), "medium", "",
     "How long a defender needs to react to a shot or pass he can block."),
    (109, "defending.block_effort_required", "Defending", "gp_defense_blockintercept", "Block_EffortRequired", "Effort needed to block", (X, 0.2), (X, 3.0), "low", "",
     "How much effort a defender needs to commit to a block."),
    (110, "referee.yellow_threshold", "Referee and fouls", "gp_rules_refereestrictness", "referee_strictness_yellowcard_threshold", "Yellow-card threshold", 20.0, 100.0, "medium", "",
     "The foul score at which the referee shows a yellow card."),
    (111, "referee.red_threshold", "Referee and fouls", "gp_rules_refereestrictness", "referee_strictness_redcard_threshold", "Red-card threshold", 40.0, 150.0, "medium", "",
     "The foul score at which the referee shows a red card."),
    (112, "referee.advantage_window", "Referee and fouls", "gp_rules_foul", "AdvantageWindowTimeLength", "Advantage window", 0, 600, "low", "",
     "How long the referee waits to play the advantage after a foul."),
    (113, "ball.linear_drag", "Ball and physics", "gp_physics_physics", "BallLinearDrag", "Air drag of the ball", (X, 0.0), (X, 5.0), "medium", "",
     "How strongly the air slows the ball in flight."),
    (114, "ball.magnus", "Ball and physics", "gp_physics_physics", "MagnusCoefficient", "Curl of the ball", (X, 0.0), (X, 3.0), "medium", "",
     "How strongly spin bends the ball's flight."),
    (115, "ball.roll_damping", "Ball and physics", "gp_physics_physics", "BallRollLinearDamping", "Rolling slow-down of the ball", 0.95, 1.0, "medium", "",
     "How quickly the ball loses speed when it rolls (lower = slows faster)."),
    (116, "ball.bounce_restitution", "Ball and physics", "gp_physics_physics", "BallRestitution", "Bounciness of the ball", 0.3, 1.1, "low", "",
     "How lively the ball bounces."),
    (117, "ball.field_friction", "Ball and physics", "gp_physics_physics", "FieldDynamicFriction", "Pitch friction", 0.2, 1.2, "low", "",
     "How much the pitch grips the ball."),
    (118, "fatigue.short_term_rate", "Pace and fatigue", "gp_playerattribute_attributes", "ATTR_ShortTermFatigueSlowdownRate", "Slow-down from short-term tiredness", 0.0, 1.0, "medium", "",
     "How strongly short-term fatigue slows a player."),
    (119, "fatigue.long_term_speed", "Pace and fatigue", "gp_playerattribute_attributes", "ATTR_LongTermFatigueSpeedFactor", "Speed kept when long-term tired", 0.5, 1.0, "medium", "",
     "The share of top speed a long-term tired player keeps (the Authentic copy is lower)."),
    (120, "fatigue.pressing_cost", "Pace and fatigue", "gp_playerattribute_attributes", "fatigueModifierForPressureTactics", "Tiredness cost of pressing", 0.0, 20.0, "medium", "",
     "How much a pressing tactic tires the players."),
    (121, "fatigue.partial_press_cost", "Pace and fatigue", "gp_playerattribute_attributes", "fatigueModifierForPartialTeamPress", "Tiredness cost of the partial press", 0.0, 6.0, "low", "",
     "How much the partial team press tires the players."),
    (122, "fatigue.injury_scalar", "Pace and fatigue", "gp_customization_customization", "InjuryMaxProbabilityScalar", "Injury frequency", 0.0, 30.0, "low", "",
     "A multiplier on how often players get injured."),
    (123, "shape.def_line_range_max", "Shape", "gp_customization_customization", "PosDefLineHeightWhenDefendingModMax", "Highest defensive line the slider can reach", 0.0, 20.0, "medium", "",
     "The upper end of the defensive line height the positioning slider maps onto."),
    (124, "shape.def_line_range_min", "Shape", "gp_customization_customization", "PosDefLineHeightWhenDefendingModMin", "Deepest defensive line the slider can reach", -20.0, 0.0, "medium", "",
     "The lower end of the defensive line height the positioning slider maps onto."),
]


def sig3(v):
    return float("%.3g" % v)


def step_for(kind, lo, hi):
    if kind != "f32":
        return None
    span = hi - lo
    if span <= 0:
        return 0.01
    for s in (0.001, 0.01, 0.05, 0.1, 0.5, 1.0):
        if span / s <= 400:
            return s
    return 1.0


def plain_type(kind):
    return {"f32": "f32", "i32": "i32", "bool": "bool"}.get(kind)


def hexs(v):
    return "0x%x" % v


def mixed_name(asset_path):
    """the instance name as the game stores it ('Fifa/Attribulator/Gameplay/...'): the first three path parts are capitalised"""
    p = asset_path.split("/")
    cap = {"fifa": "Fifa", "attribulator": "Attribulator", "gameplay": "Gameplay", "prematch": "PreMatch"}
    return "/".join(cap.get(x, x) if i < 3 else x for i, x in enumerate(p))


def db_name(asset_path):
    p = asset_path.split("/")
    gi = p.index("groups")
    base = "/".join([{"fifa": "Fifa", "attribulator": "Attribulator", "gameplay": "Gameplay", "prematch": "PreMatch"}.get(x, x) for x in p[:3]])
    sub = [x for x in p[3:gi] if x != "databases"]
    if sub:
        return base + "/databases/" + "/".join(sub) + "/" + sub[-1]
    return base + "/" + ("gameplayattribdb" if p[2] == "gameplay" else "prematchattribdb")


def locator_for(gname, g):
    """a new, unverified locator for an inventory group (the same shape as the v4 unverified locators)"""
    variants = [{"name": "base", "instance": mixed_name(g["asset"]), "db": db_name(g["asset"])}]
    if g.get("guid"):
        variants[0]["guid"] = g["guid"]
    sim = g["variants"].get("simulation")
    if sim:
        v = {"name": "simulation", "instance": mixed_name(sim["asset"]), "db": db_name(sim["asset"])}
        if sim.get("guid"):
            v["guid"] = sim["guid"]
        variants.append(v)
    dbname = "gameplayattribdb" if g["db"] == "gameplay" else "prematchattribdb"
    return {"name": "attrib." + re.sub(r"^(gp_|pm_)", "", gname), "kind": "attrib", "type": g["type"], "type_signature": hexs(g["sig"]), "type_size": hexs(g["size"]),
            "type_fields": g["nfields"], "variants": variants, "verified": False,
            "note": "inventory candidate (turbo 2.5 track A); not located live yet. The instance name's letter case is assessed, not read from the game.",
            "db_type": "AttribDBRuntime_" + dbname, "db_type_signature": hexs(g["db_sig"]), "group_type": g["group_type"],
            "group_type_signature": hexs(g["group_sig"]), "family_slot": hexs(g["family_slot"]), "group_slot": hexs(g["group_slot"])}


def new_knob(row, inv, loc_name=None):
    rank, kid, group, gname, field, label, lo, hi, impact, unit, what = row
    g = inv["groups"][gname]
    f = next((x for x in g["fields"] if x[0] == field), None)
    if f is None:
        raise KeyError("%s: no field %s in %s" % (kid, field, gname))
    _, off, kind, dv = f
    t = plain_type(kind)
    if t is None or dv is None:
        raise ValueError("%s: %s is %s (not a single value Turbo can write)" % (kid, field, kind))
    if t == "bool":
        dv = 1 if dv else 0
        lo, hi = 0, 1
    else:
        def val(x):
            return sig3(dv * x[1]) if isinstance(x, tuple) else x
        lo, hi = val(lo), val(hi)
        if t == "i32":
            lo, hi = int(math.floor(lo)), int(math.ceil(hi))
            dv = int(dv)
    sim = g["variants"].get("simulation")
    cd = {}
    if sim and field in sim["differs"]:
        sv = sim["differs"][field]
        cd["simulation"] = (1 if sv else 0) if t == "bool" else sv
        if t != "bool":
            lo, hi = min(lo, sv), max(hi, sv)
    if t != "bool":
        lo, hi = min(lo, dv), max(hi, dv)
    scale = ai.scale_class(kind, g_default(dv, kind), field)
    k = {"id": kid, "label": label, "group": group, "collection": gname, "field": field, "type": t, "unit": unit, "min": lo, "max": hi, "default": dv}
    if cd:
        k["copy_defaults"] = cd
    st = step_for(t, lo, hi)
    if st:
        k["step"] = st
    k.update({"locator": loc_name or "attrib." + re.sub(r"^(gp_|pm_)", "", gname), "offset": hexs(off), "applies": "unknown", "when": list(WHEN), "verified": False,
              "candidate": True, "rank": rank, "impact": impact, "category": ai.area_of(gname)[0], "scale": scale,
              "notes": "%s FC 27 = %s%s. %s: a candidate from the offline inventory (expected on-pitch impact %s), never written." %
              (what, ("%g" % dv) if t != "bool" else ("on" if dv else "off"), (" (Authentic copy: %s)" % (("on" if cd["simulation"] else "off") if t == "bool" else "%g" % cd["simulation"])) if cd else "", NOT_YET, impact)})
    return k


def g_default(dv, kind):
    return float(dv) if kind == "f32" else dv


def dumps(v, ind=0):
    """json with 2-space indent, lists of plain values on one line (a small diff against the v4 file's layout)"""
    pad = "  " * (ind + 1)
    if isinstance(v, dict):
        if not v:
            return "{}"
        return "{\n" + (",\n").join("%s%s: %s" % (pad, json.dumps(k, ensure_ascii=False), dumps(x, ind + 1)) for k, x in v.items()) + "\n" + "  " * ind + "}"
    if isinstance(v, list):
        if all(not isinstance(x, (dict, list)) for x in v) and len(json.dumps(v, ensure_ascii=False)) <= 100 + 100 * (ind == 1):
            return json.dumps(v, ensure_ascii=False)
        if not v:
            return "[]"
        return "[\n" + (",\n").join(pad + dumps(x, ind + 1) for x in v) + "\n" + "  " * ind + "]"
    return json.dumps(v, ensure_ascii=False)


def build(knobs_doc, inv):
    doc = copy.deepcopy(knobs_doc)
    ranks = {i: (r, im) for i, r, im in EXISTING}
    have = {k["id"] for k in doc["knobs"]}
    for k in doc["knobs"]:
        if k["id"] in ranks:
            k["rank"], k["impact"] = ranks[k["id"]]
        gname = k["collection"]
        k["category"] = ai.area_of(gname)[0] if gname in inv["groups"] else k.get("category", "Other")
        k.setdefault("candidate", False)
        fld = next((x for x in inv["groups"].get(gname, {}).get("fields", []) if x[0] == k.get("field")), None)
        if fld is not None:
            k["scale"] = ai.scale_class(fld[2], fld[3], fld[0])
    locs = {l["name"] for l in doc["locators"]}
    by_instance = {l["variants"][0]["instance"].lower(): l["name"] for l in doc["locators"] if l.get("kind") == "attrib" and l.get("variants")}
    new_ids = set()
    for row in NEW:
        if row[1] in have or row[1] in new_ids:
            continue
        g = inv["groups"][row[3]]
        inst = mixed_name(g["asset"]).lower()
        known = by_instance.get(inst)  # a group that a v4 locator already names keeps that locator
        k = new_knob(row, inv, known)
        new_ids.add(k["id"])
        if k["locator"] not in locs:
            doc["locators"].append(locator_for(k["collection"], g))
            locs.add(k["locator"])
            by_instance[inst] = k["locator"]
        doc["knobs"].append(k)
    # the final rank: expected impact tier first, then the order of the assessment (the spec's numbers are priorities inside the tier)
    prio = {i: (IMPACT.index(im), r) for i, r, im in EXISTING}
    prio.update({row[1]: (IMPACT.index(row[8]), row[0]) for row in NEW})
    order = sorted(prio, key=lambda i: prio[i])
    final = {i: n + 1 for n, i in enumerate(order)}
    for k in doc["knobs"]:
        if k["id"] in final:
            k["rank"] = final[k["id"]]
            k["impact"] = IMPACT[prio[k["id"]][0]]
    if isinstance(doc.get("_doc"), list) and doc["_doc"]:
        doc["_doc"][0] = doc["_doc"][0].replace("version 4 (2.4,", "version 5 (2.5 track A; v4 = 2.4,")
        if not any("candidate" in x for x in doc["_doc"]):
            doc["_doc"].append("v5 candidates (\"candidate\": true): an offline inventory of the engine tables (tools/attrib_inventory.py, docs/re/attrib_inventory_6AC07E31.json); "
                               "\"rank\" 1 = the largest expected on-pitch effect, \"impact\", \"category\" and \"scale\" are offline judgements. Built and checked by tools/knobs_v5.py.")
    doc["version"] = 5
    n_cand = sum(1 for k in doc["knobs"] if k.get("candidate"))
    doc["note"] = ("v5 (2.5 track A): the v4 catalogue unchanged plus %d inventory candidates (rank, impact and category from the offline assessment; "
                   "ranges are proposals). Every new knob is unverified and has no proof and no slider: Turbo lists it as not yet working and never writes it. "
                   "FC 27 defaults only. A knob becomes a slider only with its in-game proof (docs/TACTICS_OVERHAUL_DECISION.md 3.1)." % n_cand)
    return doc


def check(doc, inv):
    """-> list of problems of a v5 catalogue against the inventory (every knob: group, field, offset, type, FC 27's value, ranges, honesty)"""
    out = []
    locs = {l["name"]: l for l in doc.get("locators", [])}
    ids = set()
    ranks = set()
    for k in doc.get("knobs", []):
        kid = k.get("id", "?")
        if kid in ids:
            out.append("%s: twice" % kid)
        ids.add(kid)
        g = inv["groups"].get(k.get("collection"))
        if g is None:
            out.append("%s: group %s is not in the inventory" % (kid, k.get("collection")))
            continue
        f = next((x for x in g["fields"] if x[0] == k.get("field")), None)
        if f is None:
            out.append("%s: field %s is not in %s" % (kid, k.get("field"), k.get("collection")))
            continue
        _, off, kind, dv = f
        try:
            got_off = int(str(k.get("offset")), 16)
        except ValueError:
            got_off = -1
        if got_off != off:
            out.append("%s: offset %s but the type data says %s" % (kid, k.get("offset"), hexs(off)))
        if plain_type(kind) != k.get("type"):
            out.append("%s: type %s but the inventory says %s" % (kid, k.get("type"), kind))
        want = (1 if dv else 0) if kind == "bool" else dv
        if k.get("default") is None or (isinstance(want, (int, float)) and abs(float(k["default"]) - float(want)) > 1e-4 * max(1.0, abs(float(want)))):
            out.append("%s: default %s but FC 27's value is %s" % (kid, k.get("default"), want))
        lo, hi = k.get("min"), k.get("max")
        if k.get("type") == "bool":
            lo, hi = 0, 1
        if lo is None or hi is None or lo > hi or not (lo <= float(k["default"]) <= hi):
            out.append("%s: range %s..%s does not hold the default %s" % (kid, lo, hi, k.get("default")))
        sim = g["variants"].get("simulation")
        want_cd = {}
        if sim and k.get("field") in sim["differs"]:
            sv = sim["differs"][k["field"]]
            want_cd["simulation"] = (1 if sv else 0) if kind == "bool" else sv
        got_cd = k.get("copy_defaults") or {}
        if got_cd != want_cd:
            out.append("%s: copy_defaults %s but the Authentic copy says %s" % (kid, got_cd, want_cd))
        for v in got_cd.values():
            if not (lo <= v <= hi):
                out.append("%s: copy default %s outside %s..%s" % (kid, v, lo, hi))
        if g["size"] < off + (1 if kind == "bool" else 4):
            out.append("%s: offset outside the type" % kid)
        loc = locs.get(k.get("locator"))
        if loc is None:
            out.append("%s: locator %s is not in the file" % (kid, k.get("locator")))
        elif hexs(g["group_slot"]) != loc["group_slot"] or hexs(g["family_slot"]) != loc["family_slot"] or hexs(g["sig"]) != loc["type_signature"] or \
                hexs(g["size"]) != loc["type_size"] or g["nfields"] != loc["type_fields"]:
            out.append("%s: locator %s does not match the inventory's slots / type" % (kid, k.get("locator")))
        elif sim and not any(v["name"] == "simulation" for v in loc["variants"]) and got_cd:
            out.append("%s: needs the simulation variant" % kid)
        if k.get("candidate"):
            if k.get("verified") or k.get("proof") or k.get("slider") or k.get("live"):
                out.append("%s: a candidate must be unverified, with no proof, no slider and not live" % kid)
            if NOT_YET not in k.get("notes", ""):
                out.append("%s: a candidate's notes must say it is not yet confirmed" % kid)
            if "_" in k.get("label", "") or k.get("field", "") in k.get("label", ""):
                out.append("%s: the label must be plain words" % kid)
            if k.get("impact") not in IMPACT:
                out.append("%s: impact" % kid)
        if k.get("rank") is not None:
            if k["rank"] in ranks:
                out.append("%s: rank %s twice" % (kid, k["rank"]))
            ranks.add(k["rank"])
        elif k.get("candidate"):
            out.append("%s: a candidate needs a rank" % kid)
        if k.get("when") != WHEN:
            out.append("%s: when %s" % (kid, k.get("when")))
    for k in doc.get("knobs", []):
        if k.get("rank") is None:
            out.append("%s: no rank" % k.get("id"))
    seen = {}
    for loc in doc.get("locators", []):
        if loc.get("kind") != "attrib":
            continue
        inst = loc["variants"][0]["instance"].lower()
        if inst in seen:
            out.append("locator %s and %s name the same instance" % (seen[inst], loc["name"]))
        seen[inst] = loc["name"]
        if not loc["name"].startswith("attrib.") or len(loc["name"]) > 64 or not re.fullmatch(r"[a-z0-9._-]+", loc["name"]):
            out.append("locator %s: bad name" % loc["name"])
        if not loc.get("verified") and any(v.get("guid") and not re.fullmatch(r"[0-9a-f-]{36}", v["guid"]) for v in loc["variants"]):
            out.append("locator %s: bad guid" % loc["name"])
    if doc.get("version") != 5 or doc.get("schema") != "turbo-tuning-knobs/1":
        out.append("version / schema")
    return out


def table(doc):
    """the ranked catalogue as a markdown table (rank order)"""
    rows = ["| rank | id | impact | group / field | type | FC 27 | range | Authentic copy | how | status |", "|---|---|---|---|---|---|---|---|---|---|"]
    for k in sorted(doc["knobs"], key=lambda k: k.get("rank", 9999)):
        cd = k.get("copy_defaults") or {}
        rng = "on / off" if k["type"] == "bool" else "%g .. %g%s" % (k["min"], k["max"], (" " + k["unit"]) if k.get("unit") else "")
        dv = ("on" if k["default"] else "off") if k["type"] == "bool" else "%g" % k["default"]
        sv = "" if not cd else (("on" if cd["simulation"] else "off") if k["type"] == "bool" else "%g" % cd["simulation"])
        status = "candidate" if k.get("candidate") else ("located, effect not proven" if k.get("verified") else "v4, not located yet")
        rows.append("| %d | %s | %s | %s / %s | %s | %s | %s | %s | %s | %s |" % (k["rank"], k["id"], k["impact"], k["collection"], k["field"], k["type"], dv, rng, sv, k["scale"], status))
    return "\n".join(rows) + "\n"


def self_test():
    ok = True

    def chk(cond, what):
        nonlocal ok
        ok = ok and bool(cond)
        print("  %s %s" % ("ok  " if cond else "FAIL", what))

    # synthetic inventory + v4-like file: the builder keeps v4, adds a candidate with a simulation copy, refuses a field that is not there
    inv = {"build": ai.BUILD, "groups": {"gp_demo_press": {
        "db": "gameplay", "family": "gp_demo", "asset": "fifa/attribulator/gameplay/groups/gp_demo/gp_demo_press_runtime", "guid": "11111111-2222-3333-4444-555555555555",
        "type": "AttribSchema_demo", "sig": 21, "size": 0x60, "nfields": 3, "family_slot": 0x38, "group_slot": 0x20, "db_sig": 11, "group_type": "AttribGroupRuntime_gp_demo", "group_sig": 13,
        "variants": {"simulation": {"asset": "fifa/attribulator/gameplay/databases/simulation/groups/gp_demo/gp_demo_press_runtime", "guid": "66666666-2222-3333-4444-555555555555",
                                    "differs": {"pressDistance": 9.0}}},
        "fields": [["pressDistance", 0x28, "f32", 8.0], ["pressOn", 0x2c, "bool", True], ["pressCount", 0x30, "i32", 20]]}}}
    old = {"schema": "turbo-tuning-knobs/1", "version": 4, "locators": [], "knobs": [{"id": "a.old", "collection": "gp_demo_press", "field": "pressOn", "group": "Pressing", "type": "bool", "offset": "0x2c", "default": 1,
                                                                             "min": 0, "max": 1, "locator": "attrib.demo_press", "when": list(WHEN), "verified": False, "label": "Old", "notes": ""}]}
    rows = [(5, "x.dist", "Pressing", "gp_demo_press", "pressDistance", "Press distance", (X, 0.5), (X, 2.0), "high", "", "Demo."),
            (6, "x.on", "Pressing", "gp_demo_press", "pressOn", "Press on", 0, 1, "low", "", "Demo."),
            (7, "x.n", "Pressing", "gp_demo_press", "pressCount", "Press count", 0, 120, "low", "", "Demo.")]
    global NEW, EXISTING
    saved = (NEW, EXISTING)
    NEW, EXISTING = rows, [("a.old", 1, "low")]
    try:
        d = build(old, inv)
    finally:
        NEW, EXISTING = saved
    ks = {k["id"]: k for k in d["knobs"]}
    chk([ks_["rank"] for ks_ in d["knobs"]] and sorted(k_["rank"] for k_ in d["knobs"]) == [1, 2, 3, 4] and {k_["id"]: k_["rank"] for k_ in d["knobs"]}["x.dist"] == 1,
        "build: ranks 1..n, the high-impact candidate first")
    chk(d["version"] == 5 and len(d["knobs"]) == 4 and len(d["locators"]) == 1 and "3" in d["note"], "build: v5, the old knob kept, three candidates, one locator")
    chk(ks["x.dist"]["default"] == 8.0 and ks["x.dist"]["min"] == 4.0 and ks["x.dist"]["max"] == 16.0 and ks["x.dist"]["copy_defaults"] == {"simulation": 9.0} and
        ks["x.dist"]["offset"] == "0x28" and ks["x.dist"]["type"] == "f32", "build: the float scaled from FC 27's value, the Authentic copy recorded")
    chk(ks["x.on"]["type"] == "bool" and ks["x.on"]["default"] == 1 and "min" in ks["x.on"] and ks["x.n"]["type"] == "i32" and ks["x.n"]["default"] == 20, "build: a switch and a whole number")
    chk(all(ks[i]["verified"] is False and ks[i]["candidate"] is True and "slider" not in ks[i] and "proof" not in ks[i] for i in ("x.dist", "x.on", "x.n")), "build: candidates are unverified, no proof, no slider")
    loc = d["locators"][0]
    chk(loc["name"] == "attrib.demo_press" and loc["family_slot"] == "0x38" and loc["group_slot"] == "0x20" and loc["type_signature"] == "0x15" and len(loc["variants"]) == 2 and
        loc["variants"][0]["instance"] == "Fifa/Attribulator/Gameplay/groups/gp_demo/gp_demo_press_runtime" and
        loc["variants"][1]["db"] == "Fifa/Attribulator/Gameplay/databases/simulation/simulation" and not loc["verified"], "locator: slots, signature, mixed-case names, the simulation database")
    chk(check(d, inv) == [], "check: a good catalogue has no problem: %s" % check(d, inv)[:2])
    bad = copy.deepcopy(d)
    bk = next(k for k in bad["knobs"] if k["id"] == "x.dist")
    bk["default"] = 7.0
    bk["offset"] = "0x2c"
    bk["slider"] = True
    bk["rank"] = ks["x.on"]["rank"]
    p = check(bad, inv)
    chk(any("default 7.0" in x for x in p) and any("offset" in x for x in p) and any("no proof, no slider" in x for x in p) and any("twice" in x and "rank" in x for x in p), "check: a wrong default, offset, slider and a repeated rank are all found")
    try:
        NEW = [(9, "x.no", "Pressing", "gp_demo_press", "nope", "Nope", 0, 1, "low", "", "")]
        new_knob(NEW[0], inv)
        chk(False, "new_knob: a missing field must raise")
    except KeyError:
        chk(True, "new_knob: a missing field raises")
    finally:
        NEW = saved[0]
    try:
        new_knob((9, "x.c", "Pressing", "gp_demo_press", "pressDistance", "Press distance", 0, 1, "low", "", ""), {"groups": {"gp_demo_press": dict(inv["groups"]["gp_demo_press"], fields=[["pressDistance", 0x28, "curve", [2, 0, 1]]])}})
        chk(False, "new_knob: a curve must raise")
    except ValueError:
        chk(True, "new_knob: a curve is refused (not a single value)")
    chk(mixed_name("fifa/attribulator/prematch/groups/pm_x/x_runtime") == "Fifa/Attribulator/PreMatch/groups/pm_x/x_runtime" and
        db_name("fifa/attribulator/prematch/groups/pm_x/x_runtime") == "Fifa/Attribulator/PreMatch/prematchattribdb", "names: prematch spelling")
    chk(len({r[1] for r in NEW}) == len(NEW) and len({r[0] for r in NEW} | {r[1] for r in EXISTING}) == len(NEW) + len(EXISTING), "spec: unique ids and ranks (%d new, %d old)" % (len(NEW), len(EXISTING)))
    chk(all(r[8] in IMPACT for r in NEW) and all(re.fullmatch(r"[a-z0-9._-]{1,64}", r[1]) for r in NEW), "spec: impact words and id spelling")
    # the committed files, when present: the shipped catalogue against the committed inventory
    if os.path.exists(INV) and os.path.exists(KNOBS):
        real_inv = json.load(open(INV, encoding="utf-8"))
        real = json.load(open(KNOBS, encoding="utf-8"))
        if real.get("version") == 5:
            probs = check(real, real_inv)
            chk(not probs, "the shipped catalogue v5 against the inventory (%d knobs, %d problems) %s" % (len(real["knobs"]), len(probs), probs[:3]))
            n_new = sum(1 for k in real["knobs"] if k.get("candidate"))
            chk(40 <= n_new <= 120, "the shipped catalogue holds 40..120 candidates (%d)" % n_new)
            chk(not any(k.get("proof") or k.get("slider") for k in real["knobs"]), "the shipped catalogue: 0 proof entries, 0 sliders")
            rebuilt = build(real, real_inv)
            chk(json.dumps(rebuilt, sort_keys=True) == json.dumps(real, sort_keys=True), "the builder is idempotent on the shipped catalogue")
    t = table(d)
    chk(t.count("\n") == len(d["knobs"]) + 2 and "| 1 | x.dist | high |" in t and "candidate" in t, "table: one row per knob, ranked")
    print("SELF-TEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def main(argv):
    if argv[1:2] == ["--self-test"]:
        return self_test()
    if len(argv) < 2 or argv[1] not in ("build", "check", "table"):
        print(__doc__)
        return 2
    args = {}
    i = 2
    while i < len(argv) - 1:
        args[argv[i]] = argv[i + 1]
        i += 2
    inv = json.load(open(args.get("--inv", INV), encoding="utf-8"))
    src = args.get("--knobs", KNOBS)
    doc = json.load(open(src, encoding="utf-8"))
    if argv[1] == "table":
        sys.stdout.write(table(doc))
        return 0
    if argv[1] == "check":
        probs = check(doc, inv)
        for p in probs:
            print("  " + p)
        print("knobs v5 check: %d knobs, %d problem(s)" % (len(doc["knobs"]), len(probs)))
        return 1 if probs else 0
    out = build(doc, inv)
    probs = check(out, inv)
    if probs:
        for p in probs:
            print("  " + p)
        return 1
    with open(args.get("--out", src), "w", encoding="utf-8", newline="\n") as f:
        f.write(dumps(out))
        f.write("\n")
    print("knobs v5: %d knobs (%d candidates), %d locators -> %s" % (len(out["knobs"]), sum(1 for k in out["knobs"] if k.get("candidate")), len(out["locators"]), args.get("--out", src)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
