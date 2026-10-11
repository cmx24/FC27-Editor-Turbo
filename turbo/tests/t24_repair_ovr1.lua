package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t24 OVR-1 repair safety: opt-in gate, kill switch, dry run by default, undo snapshot, honest plan state, logged failures")

local sim = H.setup({ in_cm = true, no_gui = true })   -- bridge.start is driven here the way t07 does it
W.build(sim, { development = true })

-- Two user players stand in for the six Torino targets: A has NO development plan entry (the OVR-1 situation), B has one
local A, B = 1003, 1004
local NF = 2 + #W.ATTRS + 1 + #W.ATTRS   -- players: overall, potential, 34 attributes; growth row: overall + 34 attributes

-- the growth table the repair also writes (FC 27 career_playergrowthuserseason), one row per target, all at 1
local gfields = { { name = "playerid", short = "pid_", depth = 21 }, { name = "overall", short = "ovrl", depth = 7 } }
for i, a in ipairs(W.ATTRS) do gfields[#gfields + 1] = { name = a, short = string.format("g%03d", i), depth = 7 } end
local function grow_row(pid)
    local r = { playerid = pid, overall = 1 }
    for _, a in ipairs(W.ATTRS) do r[a] = 1 end
    return r
end
sim:add_table({ name = "career_playergrowthuserseason", short = "DyYs", fields = gfields, rows = { grow_row(A), grow_row(B) } })

-- Turbo.dll loading stub for bridge.start, as in t07
package.loadlib = function() return true end
os.execute(string.format("mkdir -p '%s/turbo' '%s/turbo_output/players' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE, H.LE))

local json = require 'imports/external/json'
local log = require 'imports/turbo/core/log'
local moves = require 'imports/turbo/core/moves'
local repair = require 'imports/turbo/features/repair_ovr1'
local bridge = require 'imports/turbo/bridge'

repair.TARGETS = { [A] = "Test_A_1003.json", [B] = "Test_B_1004.json" }

-- the sim gives every user player a plan entry; A must have none
local real_has = PlayerHasDevelopementPlan
PlayerHasDevelopementPlan = function(pid)
    if pid == A then
        sim:record("PlayerHasDevelopementPlan", pid)
        return false
    end
    return real_has(pid)
end

-- the presets the repair restores from
local GOOD = { overallrating = 76, potential = 82 }
for i, a in ipairs(W.ATTRS) do GOOD[a] = 50 + i end
local function write_preset(pid, file, name)
    local doc = { format = "turbo-player-preset", playerid = pid, name = name, players = {} }
    for k, v in pairs(GOOD) do doc.players[k] = v end
    local f = assert(io.open(H.out("players/" .. file), "wb"))
    f:write(json.encode(doc))
    f:close()
end
write_preset(A, "Test_A_1003.json", "Test A")
write_preset(B, "Test_B_1004.json", "Test B")

local function prow(pid) return sim:find_row("players", "playerid", pid) end
local function pval(pid, f) return sim:value("players", prow(pid), f) end
local function gval(pid, f)
    return sim:value("career_playergrowthuserseason", sim:find_row("career_playergrowthuserseason", "playerid", pid), f)
end
local function corrupt(pid)
    local rec = prow(pid)
    sim:set_field(rec, sim.tables.players.overallrating, 1)
    for _, a in ipairs(W.ATTRS) do sim:set_field(rec, sim.tables.players[a], 1) end
    local grec = sim:find_row("career_playergrowthuserseason", "playerid", pid)
    sim:set_field(grec, sim.tables.career_playergrowthuserseason.overall, 1)
    for _, a in ipairs(W.ATTRS) do sim:set_field(grec, sim.tables.career_playergrowthuserseason[a], 1) end
end
local function touch(name)
    local f = assert(io.open(H.out(name), "wb"))
    f:write("x")
    f:close()
end
local function plan_sets() return sim:count_calls("PlayerSetValueInDevelopementPlan") end
local function report() return H.read(H.out("REPAIR_REPORT.txt")) end
local function tagged_lines(level)
    local n = 0
    for _, l in ipairs(log.lines) do
        if (level == nil or l:find("^" .. level .. " ")) and l:find("[OVR1-REPAIR]", 1, true) then n = n + 1 end
    end
    return n
end

corrupt(A)
corrupt(B)

H.case("moves.sync_plan with force: the read-back decides, no_entry for a player without a plan entry, never plan", function()
    sim.dev_plans[A] = nil
    H.eq(moves.sync_plan(A, { finishing = 70 }, true), "no_entry", "force on a player without a plan entry")
    H.eq(moves.sync_plan(A, { finishing = 70 }), "none", "without force nothing is written")
    H.eq(moves.sync_plan(B, { finishing = 70 }, true), "plan", "force on a player with a plan entry")
    H.eq(sim.dev_plans[B].finishing, 70, "value in the plan")
    local real_set = PlayerSetValueInDevelopementPlan
    PlayerSetValueInDevelopementPlan = function() error("native boom") end
    H.has(moves.sync_plan(B, { finishing = 70 }, true), "failed: finishing: ", "every write failed is reported, not none")
    PlayerSetValueInDevelopementPlan = real_set
    sim.dev_plans[B] = nil
end)

H.case("opt-in off: career events check nothing, write nothing and log exactly one line", function()
    log.reset()
    local sets = plan_sets()
    local ran, why = repair.on_career_event(15)
    H.eq(ran, false, "did not run"); H.eq(why, "off")
    repair.on_career_event(29)
    H.eq(pval(A, "overallrating"), 1, "nothing written"); H.eq(gval(A, "overall"), 1, "growth row untouched")
    H.eq(plan_sets(), sets, "no plan write")
    H.eq(#log.lines, 1, "exactly one log line for two events")
    H.has(log.lines[1], "repair_ovr1_on.txt"); H.has(log.lines[1], "nothing written")
    H.eq(report(), nil, "no report when nothing ran")
    -- asking the automatic path to write without the opt-in is a dry run
    local ok, msg = repair.check_and_repair({ write = true, manual = false })
    H.eq(ok, true, msg); H.has(msg, "DRY RUN (no opt-in): 2 of 2 players previewed")
    H.eq(pval(A, "overallrating"), 1, "still nothing written"); H.eq(plan_sets(), sets)
end)

H.case("kill switch wins over the opt-in: automatic path and a confirmed manual write both write nothing", function()
    touch("repair_ovr1_on.txt")
    touch("repair_ovr1_off.txt")
    H.eq(repair.gate(H.LE .. "/turbo_output"), "kill")
    H.eq(repair.enabled(H.LE .. "/turbo_output"), false)
    log.reset()
    local sets = plan_sets()
    local ran, why = repair.on_career_event(15)
    H.eq(ran, false); H.eq(why, "kill")
    H.eq(pval(A, "overallrating"), 1, "nothing written"); H.eq(plan_sets(), sets)
    H.eq(#log.lines, 1, "one log line"); H.has(log.lines[1], "kill switch")
    local ok, msg = repair.check_and_repair({ write = true, manual = true })
    H.eq(ok, true, msg); H.has(msg, "DRY RUN (kill switch)")
    H.eq(pval(A, "overallrating"), 1, "a manual confirm does not beat the kill switch"); H.eq(plan_sets(), sets)
    H.has(report(), "kill switch turbo_output\\repair_ovr1_off.txt present: nothing is written, dry run instead")
    H.eq(H.read(H.out("repair_ovr1_undo_1003.json")), nil, "no undo file without a write")
    os.remove(H.out("repair_ovr1_off.txt"))
    H.eq(repair.gate(H.LE .. "/turbo_output"), "on")
end)

H.case("bridge.start runs no repair; with the opt-in a career event repairs, saves undo values, reports the plan honestly", function()
    local sets = plan_sets()
    H.turbo().boot()
    local ok, msg = bridge.start()
    H.eq(ok, true, msg)
    H.eq(pval(A, "overallrating"), 1, "bridge.start writes nothing"); H.eq(plan_sets(), sets, "bridge.start syncs no plan")
    local src = H.read(H.LE .. "/lua/libs/v2/imports/turbo/bridge.lua")
    local start_body = src:match("function M%.start%(cfg%)(.-)\nend")
    H.ok(start_body ~= nil, "bridge.start found")
    H.eq(start_body:find("check_and_repair", 1, true), nil, "no repair call left in bridge.start")
    H.eq(start_body:find("features/repair_ovr1')", 1, true), nil, "no require of the module left in bridge.start")
    log.reset()
    sim:fire("post__CareerModeEvent", 0, 15, 0)
    H.eq(pval(A, "overallrating"), 76, "A repaired on the career event"); H.eq(pval(B, "overallrating"), 76, "B repaired")
    H.eq(pval(A, "finishing"), GOOD.finishing); H.eq(pval(A, "potential"), 82)
    H.eq(gval(A, "overall"), 76, "growth row written"); H.eq(gval(B, "finishing"), GOOD.finishing)
    H.ok(plan_sets() > sets, "plan values pushed"); H.eq(sim.dev_plans[B].finishing, GOOD.finishing)
    H.ok(tagged_lines("INFO") >= 3, "gate line + check line + player lines logged")
    local rep = report()
    H.has(rep, "=== TURBO OVR-1 REPAIR: WRITE at")
    H.has(rep, "checked 2 target players: 2 need repair, 0 normal, 0 not in this career")
    H.has(rep, "WRITE player 1003 (Test A): " .. NF .. " fields from Test_A_1003.json, " .. NF .. " of them differ; growth row: yes; plan entry before: false")
    H.has(rep, "WRITE player 1004 (Test B): " .. NF .. " fields from Test_B_1004.json, " .. NF .. " of them differ; growth row: yes; plan entry before: true")
    H.has(rep, "    players.overallrating: 1 -> 76"); H.has(rep, "    career_playergrowthuserseason.overall: 1 -> 76")
    H.has(rep, "    player 1003: " .. NF .. " of " .. NF .. " fields written; plan entry still missing (the game must create it)")
    H.has(rep, "    player 1004: " .. NF .. " of " .. NF .. " fields written; development plan updated")
    H.eq(rep:find("plan sync: plan", 1, true), nil, "the old unconditional 'plan' wording is gone")
    H.has(rep, "finished: 2 of 2 players written, 0 failures"); H.has(rep, "FAILURES: 0")
    local u = json.decode(H.read(H.out("repair_ovr1_undo_1003.json")))
    H.eq(u.format, "turbo-repair-ovr1-undo"); H.eq(u.playerid, 1003); H.eq(#u.runs, 1)
    H.eq(u.runs[1].before.players.overallrating, 1); H.eq(u.runs[1].before.players.finishing, 1)
    H.eq(u.runs[1].before.players.potential, 73, "the old potential is saved too")
    H.eq(u.runs[1].before.career_playergrowthuserseason.overall, 1); H.eq(u.runs[1].plan_before, false)
    H.has(u.runs[1].preset, "Test_A_1003.json")
    H.eq(json.decode(H.read(H.out("repair_ovr1_undo_1004.json"))).runs[1].plan_before, true)
end)

H.case("dry run (the script's default) writes nothing and lists every field per player, old -> new", function()
    corrupt(A)
    local sets = plan_sets()
    local ok, msg = repair.check_and_repair({ write = false, manual = true, only = A })
    H.eq(ok, true, msg); H.has(msg, "DRY RUN: 1 of 1 players previewed, 0 failures")
    H.eq(pval(A, "overallrating"), 1, "dry run writes nothing"); H.eq(gval(A, "overall"), 1); H.eq(plan_sets(), sets, "no plan write")
    local rep = report()
    H.has(rep, "=== TURBO OVR-1 REPAIR: DRY RUN at")
    H.has(rep, "checked 1 target players: 1 need repair")
    H.has(rep, "DRY RUN player 1003 (Test A): " .. NF .. " fields from Test_A_1003.json")
    H.has(rep, "    players.overallrating: 1 -> 76"); H.has(rep, "    players.potential: 82 -> 82 (same)", "unchanged fields are listed too")
    H.has(rep, "    players.finishing: 1 -> " .. GOOD.finishing); H.has(rep, "    players.gkpositioning: 1 -> " .. GOOD.gkpositioning)
    H.has(rep, "    career_playergrowthuserseason.overall: 1 -> 76"); H.has(rep, "    career_playergrowthuserseason.finishing: 1 -> " .. GOOD.finishing)
    H.has(rep, "    dry run: nothing written; plan sync would push " .. #W.ATTRS .. " attributes")
    H.has(rep, "finished: 1 of 1 players previewed (nothing written), 0 failures")
    H.eq(rep:find("1004", 1, true), nil, "only the requested player")
    H.eq(#json.decode(H.read(H.out("repair_ovr1_undo_1003.json"))).runs, 1, "a dry run adds no undo entry")
    ok, msg = repair.check_and_repair({ write = false, manual = true, only = 4242 })
    H.eq(ok, false); H.has(msg, "not an OVR-1 repair target")
end)

H.case("a failed write is logged (log.warn + LOGGER:LogWarning) and listed in REPAIR_REPORT.txt; the rest is still written", function()
    local TABLE = require 'imports/t3db/table'
    local real_set = TABLE.SetRecordFieldValue
    TABLE.SetRecordFieldValue = function(self, rec, field, v)
        if self.name == "career_playergrowthuserseason" and field == "overall" then error("simulated write fault") end
        return real_set(self, rec, field, v)
    end
    local warnings = {}
    LOGGER.LogWarning = function(_, text) warnings[#warnings + 1] = text end
    log.reset()
    local ok, msg = repair.check_and_repair({ write = true, manual = true, only = A })
    TABLE.SetRecordFieldValue = real_set
    LOGGER.LogWarning = nil
    H.eq(ok, false, "a failure makes the run not ok"); H.has(msg, "WRITE: 1 of 1 players written, 1 failures")
    H.eq(pval(A, "overallrating"), 76, "the other fields were written"); H.eq(gval(A, "overall"), 1, "the faulty one was not")
    H.eq(gval(A, "finishing"), GOOD.finishing, "the growth attributes were written")
    H.eq(tagged_lines("WARN"), 1, "one WARN line in Turbo's log"); H.eq(#warnings, 1, "LOGGER:LogWarning called once")
    H.has(warnings[1], "[Turbo] [OVR1-REPAIR] player 1003: career_playergrowthuserseason.overall = 76 not written: ")
    H.has(warnings[1], "simulated write fault")
    local rep = report()
    H.has(rep, "FAILURES: 1"); H.has(rep, "  1. player 1003: career_playergrowthuserseason.overall = 76 not written: ")
    H.has(rep, "FAIL player 1003: career_playergrowthuserseason.overall")
    H.has(rep, "    player 1003: " .. (NF - 1) .. " of " .. NF .. " fields written; plan entry still missing (the game must create it)")
    local u = json.decode(H.read(H.out("repair_ovr1_undo_1003.json")))
    H.eq(#u.runs, 2, "second run appended, the first kept"); H.eq(u.runs[2].before.players.overallrating, 1)
    H.eq(u.runs[1].before.players.potential, 73); H.eq(u.runs[2].before.players.potential, 82, "run 2 saw the repaired potential")
end)

H.case("a raised native is a logged failure, not a silent one", function()
    corrupt(B)
    local real_set = PlayerSetValueInDevelopementPlan
    PlayerSetValueInDevelopementPlan = function() error("plan native boom") end
    log.reset()
    local ok, msg = repair.check_and_repair({ write = true, manual = true, only = B })
    PlayerSetValueInDevelopementPlan = real_set
    H.eq(ok, false, msg); H.has(msg, "1 failures")
    H.eq(pval(B, "overallrating"), 76, "table written")
    H.eq(tagged_lines("WARN"), 1)
    H.has(report(), "FAIL player 1004: development plan NOT updated (failed: acceleration: ")
    H.has(report(), "plan native boom")
end)

H.case("undo: preview first, then the restore puts the saved values back; the kill switch blocks the restore", function()
    touch("repair_ovr1_off.txt")
    local ok, msg = repair.undo({ write = true, only = B })
    H.eq(ok, true, msg); H.has(msg, "UNDO DRY RUN (kill switch)")
    H.eq(pval(B, "overallrating"), 76, "kill switch: nothing restored")
    os.remove(H.out("repair_ovr1_off.txt"))
    ok, msg = repair.undo({ write = false, only = B })
    H.eq(ok, true, msg); H.has(msg, "UNDO DRY RUN: 1 of 1 players previewed, 0 failures")
    H.eq(pval(B, "overallrating"), 76, "preview restores nothing")
    local rep = report()
    H.has(rep, "=== TURBO OVR-1 REPAIR: UNDO DRY RUN at")
    H.has(rep, "UNDO DRY RUN player 1004: " .. NF .. " fields back to the values saved ")
    H.has(rep, "run 2 of 2 in "); H.has(rep, "plan entry before the repair: true")
    H.has(rep, "    players.overallrating: 76 -> 1"); H.has(rep, "    players.potential: 82 -> 82 (same)", "run 2 saw the repaired potential")
    H.has(rep, "    career_playergrowthuserseason.overall: 76 -> 1"); H.has(rep, "    dry run: nothing restored")
    local sets = plan_sets()
    ok, msg = repair.undo({ write = true, only = B })
    H.eq(ok, true, msg); H.has(msg, "UNDO: 1 of 1 players restored, 0 failures")
    H.eq(pval(B, "overallrating"), 1, "overall back"); H.eq(pval(B, "finishing"), 1, "attribute back")
    H.eq(gval(B, "overall"), 1, "growth overall back"); H.eq(gval(B, "finishing"), 1)
    H.ok(plan_sets() > sets, "plan given the restored attributes"); H.eq(sim.dev_plans[B].finishing, 1)
    H.has(report(), "    player 1004: " .. NF .. " of " .. NF .. " fields restored; development plan updated")
    -- A has no plan entry: the undo says so too
    ok, msg = repair.undo({ write = true, only = A })
    H.eq(ok, true, msg); H.eq(pval(A, "overallrating"), 1, "A restored"); H.eq(pval(A, "potential"), 82, "run 2 saved the repaired potential")
    H.has(report(), "fields restored; plan entry still missing (the game must create it)")
    -- no undo file: reported, nothing raised
    os.rename(H.out("repair_ovr1_undo_1003.json"), H.out("repair_ovr1_undo_1003.json.bak"))
    ok, msg = repair.undo({ write = true, only = A })
    H.eq(ok, true, msg); H.has(msg, "UNDO: 0 of 0 players restored"); H.has(report(), "undo files present for 0 of 1 target players")
    os.rename(H.out("repair_ovr1_undo_1003.json.bak"), H.out("repair_ovr1_undo_1003.json"))
end)

H.case("nothing to repair: one log line, the report says so, nothing written", function()
    sim:fire("post__CareerModeEvent", 0, 15, 0)   -- A and B are back at 1: repaired again by the opt-in path
    H.eq(pval(A, "overallrating"), 76); H.eq(pval(B, "overallrating"), 76)
    log.reset()
    local sets = plan_sets()
    sim:fire("post__CareerModeEvent", 0, 15, 0)
    H.eq(plan_sets(), sets, "nothing written")
    H.eq(tagged_lines(), 1, "one repair log line for a clean check")
    H.has(log.lines[#log.lines] or "", "checked 2 target players: 0 need repair, 2 normal, 0 not in this career")
    H.has(report(), "checked 2 target players: 0 need repair, 2 normal, 0 not in this career"); H.has(report(), "FAILURES: 0")
end)

H.case("runner script: dry run by default; TURBO_REPAIR_CONFIRM writes once and is cleared; TURBO_REPAIR_UNDO restores", function()
    corrupt(A)
    corrupt(B)
    local boxes = #sim.boxes
    H.script("turbo_repair_ovr1")
    H.eq(#sim.boxes, boxes + 1, "message box shown")
    H.has(sim.boxes[#sim.boxes].title, "(DRY RUN)"); H.has(sim.boxes[#sim.boxes].text, "DRY RUN - nothing written.")
    H.has(sim.boxes[#sim.boxes].text, "2 of 2 players previewed"); H.has(sim.boxes[#sim.boxes].text, "TURBO_REPAIR_CONFIRM = true")
    H.eq(pval(A, "overallrating"), 1, "dry run by default")
    TURBO_REPAIR_CONFIRM = true
    H.script("turbo_repair_ovr1")
    H.eq(TURBO_REPAIR_CONFIRM, nil, "confirm flag consumed by the run")
    H.eq(pval(A, "overallrating"), 76, "written with the confirm flag"); H.eq(pval(B, "overallrating"), 76)
    H.has(sim.boxes[#sim.boxes].text, "WRITE: 2 of 2 players written, 0 failures")
    H.script("turbo_repair_ovr1")
    H.has(sim.boxes[#sim.boxes].text, "none needs repair")
    TURBO_REPAIR_UNDO = true
    TURBO_REPAIR_CONFIRM = true
    TURBO_REPAIR_PLAYER = B
    H.script("turbo_repair_ovr1")
    H.eq(TURBO_REPAIR_UNDO, nil); H.eq(TURBO_REPAIR_CONFIRM, nil); H.eq(TURBO_REPAIR_PLAYER, nil)
    H.eq(pval(B, "overallrating"), 1, "B undone"); H.eq(pval(A, "overallrating"), 76, "A untouched")
    H.has(sim.boxes[#sim.boxes].title, "UNDO"); H.has(sim.boxes[#sim.boxes].text, "UNDO: 1 of 1 players restored")
    -- console functions
    H.eq(type(repair_ovr1), "function"); H.eq(type(repair_ovr1_undo), "function")
    local ok, msg = repair_ovr1(B)
    H.eq(ok, true, msg); H.has(msg, "DRY RUN: 1 of 1 players previewed"); H.eq(pval(B, "overallrating"), 1, "console dry run writes nothing")
    ok, msg = repair_ovr1(B, true)
    H.eq(ok, true, msg); H.eq(pval(B, "overallrating"), 76, "console write")
    ok, msg = repair_ovr1_undo(B, true)
    H.eq(ok, true, msg); H.eq(pval(B, "overallrating"), 1, "console undo")
    ok, msg = repair_ovr1(4242, true)
    H.eq(ok, false); H.has(msg, "not an OVR-1 repair target")
    -- the kill switch beats the script's confirm flag
    touch("repair_ovr1_off.txt")
    corrupt(A)
    TURBO_REPAIR_CONFIRM = true
    H.script("turbo_repair_ovr1")
    H.eq(pval(A, "overallrating"), 1, "kill switch: the script writes nothing"); H.has(sim.boxes[#sim.boxes].text, "DRY RUN (kill switch)")
    os.remove(H.out("repair_ovr1_off.txt"))
    -- outside a career: refused, nothing raised
    sim.in_cm = false
    TURBO_REPAIR_CONFIRM = true
    H.script("turbo_repair_ovr1")
    H.has(sim.boxes[#sim.boxes].text, "not in Manager Career")
    sim.in_cm = true
end)

H.finish()
