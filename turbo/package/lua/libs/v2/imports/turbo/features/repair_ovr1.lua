-- FC 27 LE Turbo: OVR-1 repair for corrupted players (the six Torino players brought in by Turbo 1.1.x database moves
-- and later found with overall 1 and every attribute 1).
--
-- SAFETY RULES (2026-10-11; the user's rule: no write to a career without his confirmation, every game write has a
-- kill switch and is reversible):
--   * Automatic path (bridge career events, M.on_career_event): runs ONLY while the opt-in file
--     turbo_output\repair_ovr1_on.txt exists. Without it nothing is checked and nothing is written (one log line).
--   * Kill switch turbo_output\repair_ovr1_off.txt ALWAYS wins: no write by any path (automatic, script, undo); a
--     requested write becomes a dry run that says so.
--   * The runner script lua\scripts\turbo_repair_ovr1.lua is a DRY RUN by default: it lists what it would write per
--     player and field; it writes only with the confirm flag TURBO_REPAIR_CONFIRM = true (see the script header).
--   * Reversible: before the first write to a player, the current value of every field that will change is saved to
--     turbo_output\repair_ovr1_undo_<pid>.json (one entry per run; never overwritten). M.undo / the script put them
--     back. When that file cannot be written and read back, the player is NOT changed.
--   * No silent failures: every failure is logged with log.warn (Live Editor's log) and LOGGER:LogWarning when such a
--     method exists, and listed under FAILURES in turbo_output\REPAIR_REPORT.txt. One line is logged even when nothing
--     needs repair.
--   * Honest plan state: after moves.sync_plan(pid, attrs, true) the module reads PlayerHasDevelopementPlan(pid) back
--     and reports "plan entry still missing (the game must create it)" when it is false. It never reports "plan"
--     for a player without a plan entry (PlayerSetValueInDevelopementPlan does not create one).
--
-- What a repair writes, per corrupted target player (overall <= 5, or crossing and finishing both <= 5), from the
-- preset turbo_output\players\<name>_<pid>.json:
--   1. players: the preset's profile, attributes, positions and playstyles groups; modifier = 0
--   2. career_playergrowthuserseason (when the table and the player's row exist): overall + the 34 attributes
--   3. the development plan through moves.sync_plan(pid, attrs, true), state reported as above
-- The report of every run (dry or not) goes to turbo_output\REPAIR_REPORT.txt; the summary lines also go to Live
-- Editor's log as "[Turbo] [OVR1-REPAIR] ...".

local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'
local util = require 'imports/turbo/core/util'
local moves = require 'imports/turbo/core/moves'
local preset = require 'imports/turbo/core/preset'

local M = {}

M.KILL_FILE = "repair_ovr1_off.txt"      -- present: no write by any path
M.OPTIN_FILE = "repair_ovr1_on.txt"      -- present (and no kill file): the automatic path may write
M.REPORT_FILE = "REPAIR_REPORT.txt"
M.UNDO_FORMAT = "turbo-repair-ovr1-undo"
M.GROWTH_TABLE = "career_playergrowthuserseason"
M.TAG = "[OVR1-REPAIR]"

M.TARGETS = {
    [220493] = "Antonio_Barreca_220493.json",
    [199254] = "Jo_o_Pedro_199254.json",
    [216145] = "Armando_Izzo_216145.json",
    [220491] = "Emmanuel_Gyasi_220491.json",
    [243497] = "Sebastian_Walukiewicz_243497.json",
    [244836] = "Jacopo_Segre_244836.json",
}

-- The preset groups a repair writes into the players table
local GROUPS = { profile = true, attributes = true, positions = true, playstyles = true }

function M.undo_file(pid)
    return string.format("repair_ovr1_undo_%d.json", pid)
end

-- ---------------------------------------------------------------- gate
-- "kill": the kill switch is present (nothing may be written); "on": opt-in present, no kill switch; "off": neither
function M.gate(out_dir)
    if util.file_exists(util.join(out_dir, M.KILL_FILE)) then return "kill" end
    if util.file_exists(util.join(out_dir, M.OPTIN_FILE)) then return "on" end
    return "off"
end

-- The automatic path may run (opt-in present, kill switch absent)
function M.enabled(out_dir)
    return M.gate(out_dir) == "on"
end

function M.kill_switch_on(out_dir)
    return M.gate(out_dir) == "kill"
end

-- ---------------------------------------------------------------- run log / report
-- A run collects its report lines and failures; summaries go to the log, details to the report only
local function new_run(mode, out_dir)
    return { mode = mode, out_dir = out_dir, lines = {}, failures = {}, started = os.date("%Y-%m-%d %H:%M:%S") }
end

local function logger_call(method, text)
    if type(LOGGER) == "table" and type(LOGGER[method]) == "function" then
        pcall(function() LOGGER[method](LOGGER, text) end)
    end
end

-- report + log
local function say(run, fmt, ...)
    local s = string.format(fmt, ...)
    run.lines[#run.lines + 1] = s
    log.info("%s %s", M.TAG, s)
end

-- report only (per-field detail)
local function note(run, fmt, ...)
    run.lines[#run.lines + 1] = string.format(fmt, ...)
end

-- a failure: report (FAILURES section + inline), log.warn, and LOGGER:LogWarning when that method exists
local function warn(run, fmt, ...)
    local s = string.format(fmt, ...)
    run.lines[#run.lines + 1] = "FAIL " .. s
    run.failures[#run.failures + 1] = s
    log.warn("%s %s", M.TAG, s)
    logger_call("LogWarning", "[Turbo] " .. M.TAG .. " " .. s)
end

-- a warning outside a run (gate messages, handler errors)
local function warn_plain(fmt, ...)
    local s = string.format(fmt, ...)
    log.warn("%s %s", M.TAG, s)
    logger_call("LogWarning", "[Turbo] " .. M.TAG .. " " .. s)
end

local function write_report(run)
    local out = { string.format("=== TURBO OVR-1 REPAIR: %s at %s ===", run.mode, run.started) }
    for _, l in ipairs(run.lines) do out[#out + 1] = l end
    out[#out + 1] = string.format("FAILURES: %d", #run.failures)
    for i, f in ipairs(run.failures) do out[#out + 1] = string.format("  %d. %s", i, f) end
    local path = util.join(run.out_dir, M.REPORT_FILE)
    local ok, err = util.write_file(path, table.concat(out, "\n") .. "\n")
    if not ok then warn_plain("report not written to %s: %s", path, tostring(err)) end
    return path
end

-- ---------------------------------------------------------------- plan state
local function plan_state_text(state)
    if state == "plan" then return "development plan updated"
    elseif state == "no_entry" then return "plan entry still missing (the game must create it)"
    elseif state == "none" then return "no development plan values to sync"
    elseif state == "unavailable" then return "development plan natives not in this Live Editor"
    else return "development plan NOT updated (" .. tostring(state) .. ")" end
end

-- PlayerHasDevelopementPlan(pid): true / false, nil when the native is missing or raised (logged)
local function has_plan(run, pid)
    local has = _G["PlayerHasDevelopementPlan"]
    if type(has) ~= "function" then return nil end
    local ok, yes = pcall(has, pid)
    if not ok then
        warn(run, "player %d: PlayerHasDevelopementPlan raised: %s", pid, tostring(yes))
        return nil
    end
    return yes == true
end

-- moves.sync_plan with force, then the module's own read-back: never "plan" when the game says there is no entry
local function sync_plan(run, pid, attrs)
    if next(attrs) == nil then return "none" end
    local state = moves.sync_plan(pid, attrs, true)
    if type(state) ~= "string" then state = "failed: " .. tostring(state) end
    if state:sub(1, 7) == "failed:" then
        warn(run, "player %d: %s", pid, plan_state_text(state))
        return state
    end
    if state ~= "unavailable" then
        local yes = has_plan(run, pid)
        if yes == false then state = "no_entry" end
    end
    return state
end

-- ---------------------------------------------------------------- detection
function M.needs_repair(players, prec)
    local cro = db.get_int(players, prec, "crossing", -1)
    local fin = db.get_int(players, prec, "finishing", -1)
    local ovr = db.get_int(players, prec, "overallrating", -1)
    return (ovr > 0 and ovr <= 5) or (cro > 0 and cro <= 5 and fin > 0 and fin <= 5)
end

-- The target players' rows in one pass over the players table: { [pid] = rec }
local function target_rows(players, targets)
    local rows = {}
    for prec in db.records(players) do
        local pid = db.get_int(players, prec, "playerid", 0)
        if targets[pid] then rows[pid] = prec end
    end
    return rows
end

local function target_pids(only)
    local pids = {}
    for pid in pairs(M.TARGETS) do
        if only == nil or pid == only then pids[#pids + 1] = pid end
    end
    table.sort(pids)
    return pids
end

-- ---------------------------------------------------------------- the writes of one player
-- Builds the list of writes for a player from his preset, without writing: { tbl = table name, t = table, rec,
-- field, new, old }, plus the attribute values for the plan. Returns the plan or nil, err
local function plan_player(out_dir, pid, filename, players, prec)
    local path = util.join(util.join(out_dir, "players"), filename)
    local parsed, err = preset.load(path)
    if not parsed then return nil, "could not load preset " .. path .. ": " .. tostring(err) end
    local row, rerr = preset.pick_row(parsed, {})
    if not row then return nil, "could not pick row from preset: " .. tostring(rerr) end
    local fields, _, skipped = preset.map_row(players, row, {})
    if db.has_field(players, "modifier") then fields.modifier = 0 end   -- unearned modifiers must not distort ratings
    local writes, plan_attrs = {}, {}
    for _, f in ipairs(util.sorted_keys(fields)) do
        local g = preset.group_of(f)
        if GROUPS[g] and db.has_field(players, f) then
            writes[#writes + 1] = { tbl = "players", t = players, rec = prec, field = f, new = fields[f], old = db.get(players, prec, f) }
            if g == "attributes" then plan_attrs[f] = fields[f] end
        end
    end
    local growth = db.get_table(M.GROWTH_TABLE)
    local grec = growth and db.find(growth, "playerid", pid) or nil
    if grec then
        if fields.overallrating ~= nil and db.has_field(growth, "overall") then
            writes[#writes + 1] = { tbl = M.GROWTH_TABLE, t = growth, rec = grec, field = "overall", new = fields.overallrating,
                                    old = db.get(growth, grec, "overall") }
        end
        for _, a in ipairs(util.sorted_keys(plan_attrs)) do
            if db.has_field(growth, a) then
                writes[#writes + 1] = { tbl = M.GROWTH_TABLE, t = growth, rec = grec, field = a, new = plan_attrs[a],
                                        old = db.get(growth, grec, a) }
            end
        end
    end
    local name = (type(parsed.doc) == "table" and parsed.doc.name) or preset.describe(row)
    return { writes = writes, plan_attrs = plan_attrs, name = tostring(name), skipped = skipped or {}, preset_path = path,
             growth_row = grec ~= nil }
end

-- Saves the old values of the planned writes to turbo_output\repair_ovr1_undo_<pid>.json (appends a run; an existing
-- file is never overwritten) and reads the file back. Returns true, path or false, err
local function snapshot(run, pid, plan)
    local j = preset.json()
    if not j then return false, "json library not available" end
    local path = util.join(run.out_dir, M.undo_file(pid))
    local doc
    if util.file_exists(path) then
        local text = util.read_file(path)
        local ok, d = pcall(j.decode, text or "")
        if ok and type(d) == "table" and d.format == M.UNDO_FORMAT and type(d.runs) == "table" then
            doc = d
        else
            local aside = path .. ".unreadable_" .. os.date("%Y%m%d_%H%M%S")
            os.rename(path, aside)
            warn(run, "player %d: existing undo file %s not readable, moved to %s", pid, path, aside)
        end
    end
    doc = doc or { format = M.UNDO_FORMAT, version = 1, playerid = pid, runs = {} }
    local entry = { taken = run.started, preset = plan.preset_path, plan_before = plan.plan_before, before = {} }
    for _, w in ipairs(plan.writes) do
        entry.before[w.tbl] = entry.before[w.tbl] or {}
        entry.before[w.tbl][w.field] = w.old
    end
    doc.runs[#doc.runs + 1] = entry
    local oke, text = pcall(j.encode, doc)
    if not oke then return false, "undo values could not be encoded: " .. tostring(text) end
    local ok, err = util.write_file(path, text)
    if not ok then return false, "undo file " .. path .. " not written: " .. tostring(err) end
    local back = util.read_file(path)
    local okb, d2 = pcall(j.decode, back or "")
    if not okb or type(d2) ~= "table" or type(d2.runs) ~= "table" or #d2.runs ~= #doc.runs then
        return false, "undo file " .. path .. " could not be read back"
    end
    return true, path
end

-- Applies the writes; every refused write is a logged failure. Returns the number written
local function apply_writes(run, pid, writes)
    local done = 0
    for _, w in ipairs(writes) do
        local ok, err = db.set(w.t, w.rec, w.field, w.new)
        if ok then
            done = done + 1
        else
            warn(run, "player %d: %s.%s = %s not written: %s", pid, w.tbl, w.field, tostring(w.new), tostring(err))
        end
    end
    return done
end

-- Repairs (write=true) or previews one target player. Returns ok (the player was processed; a refused field is already
-- in run.failures and is not a second failure), detail
function M.repair_player(run, pid, filename, players, prec, write)
    local plan, err = plan_player(run.out_dir, pid, filename, players, prec)
    if not plan then return false, err end
    plan.plan_before = has_plan(run, pid)
    local changes = 0
    for _, w in ipairs(plan.writes) do if w.old ~= w.new then changes = changes + 1 end end
    say(run, "%s player %d (%s): %d fields from %s, %d of them differ; growth row: %s; plan entry before: %s",
        write and "WRITE" or "DRY RUN", pid, plan.name, #plan.writes, filename, changes,
        plan.growth_row and "yes" or "no", tostring(plan.plan_before))
    for _, w in ipairs(plan.writes) do
        note(run, "    %s.%s: %s -> %s%s", w.tbl, w.field, tostring(w.old), tostring(w.new), (w.old == w.new) and " (same)" or "")
    end
    for _, s in ipairs(plan.skipped) do note(run, "    skipped out-of-range preset value: %s", s) end
    if not write then
        note(run, "    dry run: nothing written; plan sync would push %d attributes", util.count(plan.plan_attrs))
        return true, string.format("%d fields previewed", #plan.writes)
    end
    local oks, spath = snapshot(run, pid, plan)
    if not oks then return false, "undo values not saved, player NOT changed: " .. tostring(spath) end
    note(run, "    undo values saved to %s", spath)
    local done = apply_writes(run, pid, plan.writes)
    local state = sync_plan(run, pid, plan.plan_attrs)
    say(run, "    player %d: %d of %d fields written; %s", pid, done, #plan.writes, plan_state_text(state))
    return true, string.format("%d of %d fields written, %s", done, #plan.writes, plan_state_text(state))
end

-- ---------------------------------------------------------------- check + repair
-- opts: write (true = write, else dry run), manual (true = the script with the user's confirm flag; the opt-in file
-- is not required then), only (one target pid). The kill switch turns any write into a dry run. Returns ok, message
function M.check_and_repair(opts)
    opts = opts or {}
    if not game.in_cm() then return false, "not in Manager Career (load a career first)" end
    local out_dir = env.output_dir()
    if not out_dir then return false, "no writable output folder" end
    local only = util.to_int(opts.only)
    if only and not M.TARGETS[only] then return false, string.format("player %d is not an OVR-1 repair target", only) end
    local write = opts.write == true
    local gate = M.gate(out_dir)
    local run = new_run(write and "WRITE" or "DRY RUN", out_dir)
    if write and gate == "kill" then
        write = false
        run.mode = "DRY RUN (kill switch)"
        say(run, "kill switch turbo_output\\%s present: nothing is written, dry run instead", M.KILL_FILE)
    elseif write and not opts.manual and gate ~= "on" then
        write = false
        run.mode = "DRY RUN (no opt-in)"
        say(run, "opt-in file turbo_output\\%s not present: nothing is written, dry run instead", M.OPTIN_FILE)
    end

    local players, perr = db.get_table("players")
    if not players then
        warn(run, "players table not found: %s", tostring(perr))
        write_report(run)
        return false, "players table not found"
    end
    local rows = target_rows(players, M.TARGETS)
    -- career guard: a target is repaired only while he plays for the user's club of the LOADED career (the Torino career);
    -- in any other career (Leicester, a copy, another save) the same player id is left alone and listed
    local user = game.user_team_id() or 0
    local to_repair, normal, missing, elsewhere = {}, {}, {}, {}
    for _, pid in ipairs(target_pids(only)) do
        local prec = rows[pid]
        if not prec then missing[#missing + 1] = pid
        else
            local _, tid = moves.club_link(pid)
            if user <= 0 or tid ~= user then
                elsewhere[#elsewhere + 1] = string.format("%d (club %s)", pid, tostring(tid))
            elseif M.needs_repair(players, prec) then
                to_repair[#to_repair + 1] = { pid = pid, file = M.TARGETS[pid], prec = prec }
            else
                normal[#normal + 1] = pid
            end
        end
    end
    local checked = #to_repair + #normal + #missing + #elsewhere
    say(run, "checked %d target players: %d need repair, %d normal, %d not in this career%s%s", checked, #to_repair, #normal,
        #missing, (#missing > 0) and (" (" .. table.concat(missing, ", ") .. ")") or "",
        (#elsewhere > 0) and string.format("; %d not in your club %d (left alone: %s)", #elsewhere, user, table.concat(elsewhere, ", ")) or "")
    if #to_repair == 0 then
        local rp = write_report(run)
        return true, string.format("checked %d target players: none needs repair (%s)", checked, rp)
    end

    local done = 0
    for _, item in ipairs(to_repair) do
        local ok, msg = M.repair_player(run, item.pid, item.file, players, item.prec, write)
        if ok then done = done + 1 else warn(run, "player %d: %s", item.pid, tostring(msg)) end
    end
    say(run, "finished: %d of %d players %s, %d failures", done, #to_repair, write and "written" or "previewed (nothing written)",
        #run.failures)
    local rp = write_report(run)
    local summary = string.format("%s: %d of %d players %s, %d failures; details in %s", run.mode, done, #to_repair,
        write and "written" or "previewed", #run.failures, rp)
    return #run.failures == 0, summary
end

-- ---------------------------------------------------------------- undo
-- Puts back the values saved by the last repair of a player. Returns ok, detail
function M.undo_player(run, pid, write)
    local j = preset.json()
    if not j then return false, "json library not available" end
    local path = util.join(run.out_dir, M.undo_file(pid))
    local text = util.read_file(path)
    if not text then return false, "no undo file " .. path end
    local ok, doc = pcall(j.decode, text)
    if not ok or type(doc) ~= "table" or doc.format ~= M.UNDO_FORMAT or type(doc.runs) ~= "table" or #doc.runs == 0 then
        return false, "undo file not readable: " .. path
    end
    if util.to_int(doc.playerid) ~= pid then return false, "undo file is for player " .. tostring(doc.playerid) end
    local entry = doc.runs[#doc.runs]
    local writes = {}
    for _, tname in ipairs({ "players", M.GROWTH_TABLE }) do
        local vals = type(entry.before) == "table" and entry.before[tname] or nil
        if type(vals) == "table" and next(vals) ~= nil then
            local tbl = db.get_table(tname)
            local rec = tbl and db.find(tbl, "playerid", pid) or nil
            if not tbl then
                warn(run, "player %d: table %s not found, %d saved values not restored", pid, tname, util.count(vals))
            elseif not rec then
                warn(run, "player %d: no row in %s, %d saved values not restored", pid, tname, util.count(vals))
            else
                for _, f in ipairs(util.sorted_keys(vals)) do
                    if db.has_field(tbl, f) then
                        writes[#writes + 1] = { tbl = tname, t = tbl, rec = rec, field = f, new = vals[f], old = db.get(tbl, rec, f) }
                    else
                        warn(run, "player %d: field %s.%s not in this game, not restored", pid, tname, f)
                    end
                end
            end
        end
    end
    say(run, "%s player %d: %d fields back to the values saved %s (run %d of %d in %s); plan entry before the repair: %s",
        write and "UNDO" or "UNDO DRY RUN", pid, #writes, tostring(entry.taken), #doc.runs, #doc.runs, path, tostring(entry.plan_before))
    for _, w in ipairs(writes) do
        note(run, "    %s.%s: %s -> %s%s", w.tbl, w.field, tostring(w.old), tostring(w.new), (w.old == w.new) and " (same)" or "")
    end
    if not write then
        note(run, "    dry run: nothing restored")
        return true, string.format("%d fields previewed", #writes)
    end
    local done = apply_writes(run, pid, writes)
    -- the plan outranks the players table: give it the restored attributes (its values before the repair cannot be
    -- read back by any native, so this is the closest an undo can get)
    local is_attr, attrs = util.set_of(moves.PLAN_ATTRS), {}
    for _, w in ipairs(writes) do if w.tbl == "players" and is_attr[w.field] then attrs[w.field] = w.new end end
    local state = sync_plan(run, pid, attrs)
    say(run, "    player %d: %d of %d fields restored; %s", pid, done, #writes, plan_state_text(state))
    return true, string.format("%d of %d fields restored, %s", done, #writes, plan_state_text(state))
end

-- opts: write (true = restore, else preview), only (one target pid). The kill switch turns the restore into a preview.
-- Returns ok, message
function M.undo(opts)
    opts = opts or {}
    if not game.in_cm() then return false, "not in Manager Career (load a career first)" end
    local out_dir = env.output_dir()
    if not out_dir then return false, "no writable output folder" end
    local only = util.to_int(opts.only)
    if only and not M.TARGETS[only] then return false, string.format("player %d is not an OVR-1 repair target", only) end
    local write = opts.write == true
    local run = new_run(write and "UNDO" or "UNDO DRY RUN", out_dir)
    if write and M.gate(out_dir) == "kill" then
        write = false
        run.mode = "UNDO DRY RUN (kill switch)"
        say(run, "kill switch turbo_output\\%s present: nothing is restored, dry run instead", M.KILL_FILE)
    end
    local with_file, done = {}, 0
    for _, pid in ipairs(target_pids(only)) do
        if util.file_exists(util.join(out_dir, M.undo_file(pid))) then with_file[#with_file + 1] = pid end
    end
    say(run, "undo files present for %d of %d target players", #with_file, #target_pids(only))
    for _, pid in ipairs(with_file) do
        local ok, msg = M.undo_player(run, pid, write)
        if ok then done = done + 1 else warn(run, "player %d: %s", pid, tostring(msg)) end
    end
    say(run, "undo finished: %d of %d players %s, %d failures", done, #with_file, write and "restored" or "previewed (nothing written)",
        #run.failures)
    local rp = write_report(run)
    return #run.failures == 0, string.format("%s: %d of %d players %s, %d failures; details in %s", run.mode, done, #with_file,
        write and "restored" or "previewed", #run.failures, rp)
end

-- ---------------------------------------------------------------- automatic path (bridge career events)
local last_gate_logged = nil

-- Called by bridge.on_career_event. Writes only while the opt-in file exists and the kill switch does not; otherwise
-- it logs why it did nothing (once per gate state). Returns ran (true/false), message
function M.on_career_event(event_id)
    if not game.in_cm() then return false, "not in career" end
    local out_dir = env.output_dir()
    if not out_dir then return false, "no output folder" end
    local gate = M.gate(out_dir)
    if gate ~= "on" then
        if last_gate_logged ~= gate then
            last_gate_logged = gate
            if gate == "kill" then
                log.info("%s off: kill switch turbo_output\\%s present (nothing checked, nothing written)", M.TAG, M.KILL_FILE)
            else
                log.info("%s off: opt-in file turbo_output\\%s not present (nothing checked, nothing written)", M.TAG, M.OPTIN_FILE)
            end
        end
        return false, gate
    end
    if last_gate_logged ~= "on" then
        last_gate_logged = "on"
        log.info("%s opt-in turbo_output\\%s present: the target players are checked on career events", M.TAG, M.OPTIN_FILE)
    end
    local ok, res, msg = pcall(M.check_and_repair, { write = true, manual = false })
    if not ok then
        warn_plain("check on career event %s failed: %s", tostring(event_id), tostring(res))
        return false, tostring(res)
    end
    return res, msg
end

return M
