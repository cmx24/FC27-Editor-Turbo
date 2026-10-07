-- Turbo feature: mass actions for the players of one team (Teams > team edit > Mass actions in the Turbo GUI).
--   "team_mass": { "teamid": 48, "actions": ["long_contract", "morale", "squad_roles", "block_offers"] }
--   "actions": a list of the names below, or "all".
-- Actions:
--   long_contract  every player of the team gets a contract of 60 months from today (a valid join date too); players
--                  loaned in from another club keep the contract of their parent club.
--   morale         "very happy": your club's players get the highest morale the game's own level function still calls
--                  "very happy" for their emotion type, written with the game's SetTotalMorale (Turbo.dll TurboPlayerMorale,
--                  op 13: the overall's morale modifier refreshes too; 100 is the top level "complacent", +0 OVR). Players
--                  with no morale record (moved in by Turbo's old database moves: "Unknown") get one first, created the way
--                  the game's signing handler does (counted only when turbo_output\call_player_morale_create_off.txt is
--                  present or creation is not resolved / fails); stale records of players who left are counted, not
--                  removed. Without the native call (or for another club):
--                  Live Editor's SetPlayerMorale with 85, nothing read back.
--   squad_roles    your club only (the game keeps no squad roles for other clubs): players aged 19 or more get the
--                  Rotation role, younger players Prospect (squad_role.lua writes it through the PlayerStatusManager).
--                  Role rule engine (Turbo 2.0): "role_rules" = ordered list of { name, role 1..5, age_min, age_max, ovr_rank_min,
--                  ovr_rank_max, pos = GK | DEF | MID | ATT | list } (first match wins; default = the two rules above),
--                  "role_pins" = { "<playerid>": 1..5 | 0 = leave alone } (a pin beats the rules), "role_preview": true = write
--                  nothing, put the rows (player, age, ovr, old role, new role, skip reason) in turbo_output\role_preview.json,
--                  "role_save": false = do not save the rule for the re-apply after the game's season reset (default: saved to
--                  turbo_output\role_rule.json when something was written), "role_rule_clear": true = delete the saved rule.
--   block_offers   the "Block Offers" player status of the Squad hub (the game's own toggle, called through Turbo.dll:
--                  moves.list(pid, "block_offers")); your own club only. Players already blocked are left alone (the game's
--                  call is a toggle: Turbo reads the state first).
--   form           your club: SetPlayerForm 100 (Live Editor, user's team only) and teamplayerlinks.form 5 (schema max);
--                  other clubs: teamplayerlinks.form 5 of the team's links.
--   match_xp       with Live Editor's development manager (PlayerDevelopmentManagerAddPlayer + Save; any club): XP x2.0
--                  from matches and training. Without it (v27.1.2; FC 27 has no match XP field): a one-off +1 growth
--                  through development.lua "add", never above the player's potential.
--   dev_bonus      cfg.bonus_xp (1..10, default 2): with the manager, that bonus in the manager entry (with match_xp in the
--                  same run both go in ONE call per player); without it, +N on each attribute of the player's group
--                  through development.lua "add" (plans updated), N limited to potential - overall, players at potential skipped.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local moves = require 'imports/turbo/core/moves'
local log = require 'imports/turbo/core/log'

local M = {}

M.ACTIONS = { "block_offers", "squad_roles", "morale", "long_contract", "form", "match_xp", "dev_bonus" }
M.CONTRACT_MONTHS = 60
M.FORM = 100            -- SetPlayerForm's top value
M.XP_MULTIPLIER = 2.0   -- match XP: twice the XP from matches and training
M.BONUS_XP = 2          -- dev bonus default (cfg.bonus_xp overrides it): points, or bonus XP for the LE manager
M.LINK_FORM = 5         -- teamplayerlinks.form: the schema range is 0..5
M.BONUS_XP_MAX = 10
-- "Very happy": FC 27 shows 95 and above as "Complacent" (+0 OVR); its bands are very unhappy < 15 < unhappy < 40 < content < 65 <
-- happy < 75 < very happy < 95 <= complacent (seen in a live career 2026-10-05: 100 gave "Complacent")
M.MORALE = 85   -- the Live Editor fallback only: the native call (op 13) asks the game's own level function
M.ROLE_ROTATION, M.ROLE_PROSPECT = 3, 5
M.ADULT_AGE = 19   -- this age and older: Rotation; younger: Prospect

local LABEL = {
    block_offers = "block offers", squad_roles = "squad roles", morale = "morale (very happy)",
    long_contract = "long contract", form = "form", match_xp = "match XP", dev_bonus = "dev bonus",
}

-- Hooks for the two actions that need a game call (installed by core/moves.lua or the bridge when Turbo.dll has them):
-- M.hooks.block_offers(pids, ctx) -> ok, summary (tests)
M.hooks = {}

-- Players of the team: club links only, the team's own contracts (no national-team links)
local function team_players(teamid)
    local out, n = {}, 0
    local links = db.get_table("teamplayerlinks")
    if not links or not db.has_fields(links, { "teamid", "playerid" }) then return out, 0 end
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid then
            local pid = links:GetRecordFieldValue(rec, "playerid")
            if pid and pid > 0 and not out[pid] then
                out[pid] = true
                n = n + 1
            end
        end
    end
    return out, n
end

-- Players on the team who are loaned in (their contract belongs to the parent club)
local function loaned_in(players, teamid)
    local out = {}
    local loans = db.get_table("playerloans")
    if not loans or not db.has_fields(loans, { "playerid", "teamidloanedfrom" }) then return out end
    for rec in db.records(loans) do
        local pid = loans:GetRecordFieldValue(rec, "playerid")
        if players[pid] and loans:GetRecordFieldValue(rec, "teamidloanedfrom") ~= teamid then out[pid] = true end
    end
    return out
end

-- Players of `players` with any playerloans row (loaned in or out): the game's list / block helpers take only players under
-- contract at your club (moves.list refuses them), so Block offers skips them instead of counting them as failures
local function with_loan(players)
    local out = {}
    local loans = db.get_table("playerloans")
    if not loans or not db.has_field(loans, "playerid") then return out end
    for rec in db.records(loans) do
        local pid = loans:GetRecordFieldValue(rec, "playerid")
        if players[pid] then out[pid] = true end
    end
    return out
end

-- record of every wanted player in ONE pass over the players table (a find per player would read the whole table each time)
local function player_records(ptbl, pids)
    local out = {}
    for rec in db.records(ptbl) do
        local pid = ptbl:GetRecordFieldValue(rec, "playerid")
        if pids[pid] then out[pid] = rec end
    end
    return out
end

local function age_of(prec, ptbl, today_days)
    local birth = util.to_int(ptbl:GetRecordFieldValue(prec, "birthdate"))
    if not birth or birth <= 0 then return nil end
    local by, bm, bd = util.date_from_gregorian_days(birth)
    local ty, tm, tdd = util.date_from_gregorian_days(today_days)
    local age = ty - by
    if tm < bm or (tm == bm and tdd < bd) then age = age - 1 end
    return age
end

local function action_long_contract(ctx, pids, teamid)
    local ptbl, err = db.get_table("players")
    if not ptbl then return false, err end
    local skip = loaned_in(pids, teamid)
    local n, kept = 0, 0
    local year = moves.contract_end_year(M.CONTRACT_MONTHS)
    local recs = player_records(ptbl, pids)
    for pid in pairs(pids) do
        if skip[pid] then
            kept = kept + 1
        else
            local prec = recs[pid]
            if prec then
                local cur_join = moves.read_field(ptbl, prec, "playerjointeamdate")
                local cur_cvu = moves.read_field(ptbl, prec, "contractvaliduntil")
                local join, cvu = moves.contract_values(cur_join, cur_cvu, { months = M.CONTRACT_MONTHS })
                if join ~= nil and join ~= cur_join then
                    local ok, werr = db.set(ptbl, prec, "playerjointeamdate", join, ctx.dry)
                    if not ok then return false, werr end
                end
                if cvu ~= nil and cvu ~= cur_cvu then
                    local ok, werr = db.set(ptbl, prec, "contractvaliduntil", cvu, ctx.dry)
                    if not ok then return false, werr end
                end
                n = n + 1
            end
        end
    end
    local text = string.format("%d players to %d months (contracts end in June %d)", n, M.CONTRACT_MONTHS, year)
    if kept > 0 then text = text .. string.format("; %d loaned-in players keep their parent club's contract", kept) end
    return true, text
end

local function morale_native(ctx, pids, native)
    if ctx.dry then return true, string.format("%d players to very happy (game call)", util.count(pids)) end
    local set_n, none_n, created_n, queued_n, fails, lo, hi = 0, 0, 0, 0, {}, nil, nil
    local levels, none_why = {}, nil
    for pid in pairs(pids) do
        local ok, text, status, total, level = native(1, pid, 0)
        if status == "off" then return nil, text end
        if ok and status == "ok" then
            set_n = set_n + 1
            if type(text) == "string" and text:find("record created", 1, true) then created_n = created_n + 1 end
            if total then lo = math.min(lo or total, total); hi = math.max(hi or total, total) end
            if level then levels[level] = (levels[level] or 0) + 1 end
        elseif ok and status == "queued" then
            queued_n = queued_n + 1
            break   -- one game call at a time: the rest would be refused while it waits for the game thread
        elseif total == -2 then
            none_n = none_n + 1
            none_why = none_why or (type(text) == "string" and text:match("not created %((.-)%), counted only")) or nil
        else
            fails[#fails + 1] = string.format("%d: %s", pid, tostring(text))
        end
    end
    local text = string.format("%d players set to very happy", set_n)
    if lo then text = text .. string.format(" (morale %s, level %s)", lo == hi and tostring(lo) or (lo .. ".." .. hi),
        (levels[4] == set_n) and "very happy" or "see the log") end
    if created_n > 0 then text = text .. string.format(", including %d whose missing morale record was created", created_n) end
    if queued_n > 0 then text = text .. "; queued for the game thread: run it again after the next career-mode event" end
    if none_n > 0 then
        text = text .. string.format("; %d have no morale record (the game shows \"Unknown\"; not created: %s)", none_n,
            none_why or "see the log")
    end
    local okc, _, cstatus, stale, recs = native(3, 0, 0)
    if okc and cstatus == "ok" and stale then
        text = text .. string.format("; %d of %d morale records are stale (players who left: counted, not removed)", stale, recs or 0)
    end
    if #fails > 0 then
        text = text .. string.format("; %d failed (%s)", #fails, table.concat(fails, "; ", 1, math.min(#fails, 3)))
    end
    return #fails == 0, text
end

local function action_morale(ctx, pids, teamid)
    local native = _G["TurboPlayerMorale"]
    local note = ""
    if type(native) == "function" and teamid == game.user_team_id() then
        local ok, text = morale_native(ctx, pids, native)
        if ok ~= nil then return ok, text end
        note = "; " .. tostring(text)   -- the call is off: Live Editor's way below
    end
    local fn = _G["SetPlayerMorale"]
    if type(fn) ~= "function" then return false, "SetPlayerMorale is not available in this Live Editor build" .. note end
    local ok_n, fail_n = 0, 0
    if not ctx.dry then
        for pid in pairs(pids) do
            if pcall(fn, pid, M.MORALE) then ok_n = ok_n + 1 else fail_n = fail_n + 1 end
        end
    else
        ok_n = util.count(pids)
    end
    local text = string.format("%d players to morale %d (Live Editor's SetPlayerMorale, nothing read back)", ok_n, M.MORALE)
    if fail_n > 0 then text = text .. string.format("; %d calls failed", fail_n) end
    return fail_n == 0, text .. note
end

-- Squad role rule engine ----------------------------------------------------------------------------------------------
-- A rule set is an ordered list; the FIRST rule whose conditions all hold gives the role. A rule is
--   { name = "veterans", role = 1..5, age_min = n, age_max = n, ovr_rank_min = n, ovr_rank_max = n, pos = "GK" | {"DEF", "MID"} }
-- every condition optional (a rule with none matches everyone), age in whole years today, ovr_rank = 1 for the best overall
-- of the squad (ties: lower player id first), pos = position group of preferredposition1: GK, DEF, MID, ATT.
-- Per-player pins override the rules: { [playerid] = 1..5 }, or 0 = leave that player alone.
-- A rule with an age condition cannot be judged for a player with no birthdate: when such a rule comes before the matching
-- one the player is skipped ("no birthdate") instead of getting a role the age might have changed.
M.ROLE_RULES_MAX = 32
M.POS_GROUPS = { "GK", "DEF", "MID", "ATT" }
M.ROLE_RULE_FILE = "role_rule.json"
M.ROLE_PREVIEW_FILE = "role_preview.json"

-- FC position ids: 0 GK; 1..8 SW, RWB, RB, RCB, CB, LCB, LB, LWB; 9..19 DM, M and AM slots; 20..27 forwards and wingers
function M.pos_group(pos)
    pos = util.to_int(pos)
    if pos == nil then return nil end
    if pos == 0 then return "GK" end
    if pos >= 1 and pos <= 8 then return "DEF" end
    if pos >= 9 and pos <= 19 then return "MID" end
    if pos >= 20 and pos <= 27 then return "ATT" end
    return nil
end

-- The defaults reproduce the fixed button of 1.2.4: ADULT_AGE and older Rotation, younger Prospect
function M.default_role_rules()
    return {
        { name = string.format("age %d and older", M.ADULT_AGE), age_min = M.ADULT_AGE, role = M.ROLE_ROTATION },
        { name = "younger", role = M.ROLE_PROSPECT },
    }
end

local function opt_int(r, key, lo, hi, idx)
    local v = r[key]
    if v == nil then return nil end
    local iv = util.to_int(v)
    if iv == nil or iv < lo or iv > hi then
        return nil, string.format("rule %d: %s must be a whole number %d..%d", idx, key, lo, hi)
    end
    return iv
end

-- raw rules (a Lua list from JSON) -> clean list, or nil, error. nil / missing raw = the defaults.
function M.normalize_role_rules(raw)
    if raw == nil then return M.default_role_rules() end
    if type(raw) ~= "table" or not util.is_array(raw) then return nil, "role_rules must be a list of rules" end
    if #raw == 0 then return nil, "role_rules is empty: give at least one rule" end
    if #raw > M.ROLE_RULES_MAX then return nil, string.format("at most %d rules", M.ROLE_RULES_MAX) end
    local groups = util.set_of(M.POS_GROUPS)
    local out = {}
    for i, r in ipairs(raw) do
        if type(r) ~= "table" then return nil, string.format("rule %d is not an object", i) end
        local role = util.to_int(r.role)
        if not role or role < 1 or role > 5 then return nil, string.format("rule %d: role must be 1 (Crucial) .. 5 (Prospect)", i) end
        local c = { role = role, name = type(r.name) == "string" and r.name or nil }
        local err
        for _, spec in ipairs({ { "age_min", 0, 99 }, { "age_max", 0, 99 }, { "ovr_rank_min", 1, 999 }, { "ovr_rank_max", 1, 999 } }) do
            c[spec[1]], err = opt_int(r, spec[1], spec[2], spec[3], i)
            if err then return nil, err end
        end
        if c.age_min and c.age_max and c.age_min > c.age_max then return nil, string.format("rule %d: age_min is above age_max", i) end
        if c.ovr_rank_min and c.ovr_rank_max and c.ovr_rank_min > c.ovr_rank_max then
            return nil, string.format("rule %d: ovr_rank_min is above ovr_rank_max", i)
        end
        if r.pos ~= nil then
            local list = type(r.pos) == "table" and r.pos or { r.pos }
            if #list == 0 then return nil, string.format("rule %d: pos is empty", i) end
            c.pos = {}
            for _, g in ipairs(list) do
                g = type(g) == "string" and g:upper() or nil
                if not g or not groups[g] then
                    return nil, string.format("rule %d: pos must be GK, DEF, MID or ATT", i)
                end
                c.pos[#c.pos + 1] = g
            end
        end
        out[#out + 1] = c
    end
    return out
end

-- raw pins ({"158023": 1}) -> {[pid]=0..5}, or nil, error
function M.normalize_role_pins(raw)
    if raw == nil then return {} end
    if type(raw) ~= "table" then return nil, "role_pins must be an object {playerid: role}" end
    local out = {}
    for k, v in pairs(raw) do
        local pid, role = util.to_int(k), util.to_int(v)
        if not pid or pid <= 0 then return nil, "role_pins: bad player id " .. tostring(k) end
        if role == nil or role < 0 or role > 5 then
            return nil, string.format("role_pins: player %d needs a role 1..5 (or 0 to leave the player alone)", pid)
        end
        out[pid] = role
    end
    return out
end

-- Role for one player. f = { pid, age (nil unknown), ovr_rank (nil unknown), group (nil unknown) }
-- Returns role, nil, rule index | "pin"   or   nil, skip reason
function M.role_for(rules, pins, f)
    local pin = pins and pins[f.pid]
    if pin ~= nil then
        if pin == 0 then return nil, "pinned: left alone" end
        return pin, nil, "pin"
    end
    local age_unknown = false
    for i, r in ipairs(rules) do
        local ok = true
        if r.ovr_rank_min or r.ovr_rank_max then
            if f.ovr_rank == nil then
                ok = false
            else
                if r.ovr_rank_min and f.ovr_rank < r.ovr_rank_min then ok = false end
                if r.ovr_rank_max and f.ovr_rank > r.ovr_rank_max then ok = false end
            end
        end
        if ok and r.pos then
            local hit = false
            for _, g in ipairs(r.pos) do if g == f.group then hit = true end end
            ok = hit
        end
        if ok and (r.age_min or r.age_max) then
            if f.age == nil then
                ok = false
                age_unknown = true
            else
                if r.age_min and f.age < r.age_min then ok = false end
                if r.age_max and f.age > r.age_max then ok = false end
            end
        end
        if ok then
            if age_unknown then return nil, "no birthdate" end
            return r.role, nil, i
        end
    end
    if age_unknown then return nil, "no birthdate" end
    return nil, "no rule matched"
end

local function json_lib()
    local ok, j = pcall(require, 'imports/external/json')
    if ok and type(j) == "table" then return j end
    return nil
end

-- The saved rule (turbo_output\role_rule.json): what squad_role.lua applies again after the game's season reset
function M.save_role_rule(out_dir, rules, pins)
    local j = json_lib()
    if not out_dir or not j then return false end
    local pin_obj = {}
    for pid, role in pairs(pins or {}) do pin_obj[tostring(pid)] = role end
    local ok, text = pcall(j.encode, { version = 1, rules = rules, pins = pin_obj })
    if not ok then return false end
    return util.write_file(util.join(out_dir, M.ROLE_RULE_FILE), text) == true
end

-- -> { rules, pins } or nil (no file, unreadable, or invalid rules)
function M.load_role_rule(out_dir)
    local j = json_lib()
    if not out_dir or not j then return nil end
    local path = util.join(out_dir, M.ROLE_RULE_FILE)
    if not util.file_exists(path) then return nil end
    local text = util.read_file(path)
    local ok, data = pcall(j.decode, text or "")
    if not ok or type(data) ~= "table" then
        log.warn("ignoring unreadable %s", path)
        return nil
    end
    local rules, err = M.normalize_role_rules(data.rules)
    if not rules then
        log.warn("ignoring %s: %s", path, tostring(err))
        return nil
    end
    local pins = M.normalize_role_pins(data.pins) or {}
    return { rules = rules, pins = pins }
end

function M.clear_role_rule(out_dir)
    if not out_dir then return false end
    local path = util.join(out_dir, M.ROLE_RULE_FILE)
    if not util.file_exists(path) then return false end
    local ok = pcall(os.remove, path)
    return ok and not util.file_exists(path)
end

local function rules_label(rules, pins)
    local function plain(r) return not (r.age_max or r.ovr_rank_min or r.ovr_rank_max or r.pos) end
    local is_default = #rules == 2 and rules[1].age_min == M.ADULT_AGE and rules[1].role == M.ROLE_ROTATION
        and rules[2].role == M.ROLE_PROSPECT and next(pins) == nil and plain(rules[1])
        and plain(rules[2]) and not rules[2].age_min
    if is_default then return string.format("Rotation for age %d+, Prospect below", M.ADULT_AGE) end
    return string.format("%d role rules, %d pins", #rules, util.count(pins))
end

local function action_squad_roles(ctx, pids, teamid)
    local out_dir = ctx.out_dir
    if ctx.cfg.role_rule_clear == true then
        local cleared = M.clear_role_rule(out_dir)
        return true, cleared and "saved role rule removed: roles are no longer re-applied after a season reset"
            or "no saved role rule to remove"
    end
    if teamid ~= game.user_team_id() then
        return false, "squad roles exist for your own club only (the game keeps none for other clubs)"
    end
    local rules, rerr = M.normalize_role_rules(ctx.cfg.role_rules)
    if not rules then return false, rerr end
    local pins, perr = M.normalize_role_pins(ctx.cfg.role_pins)
    if not pins then return false, perr end
    local role = require 'imports/turbo/features/squad_role'
    local ptbl = db.get_table("players")
    if not ptbl then return false, "players table not found" end
    local today = moves.today_days and moves.today_days() or nil
    if not today then
        local d = game.current_date()
        today = d and d.year and util.gregorian_days_from_date(d.year, d.month, d.day) or nil
    end
    if not today then return false, "current in-game date not available" end
    local recs = player_records(ptbl, pids)

    -- overall rank inside the squad (1 = best; ties: lower player id first)
    local ranked = {}
    for pid, rec in pairs(recs) do
        ranked[#ranked + 1] = { pid = pid, ovr = util.to_int(ptbl:GetRecordFieldValue(rec, "overallrating")) or 0 }
    end
    table.sort(ranked, function(a, b) if a.ovr ~= b.ovr then return a.ovr > b.ovr end return a.pid < b.pid end)
    local rank_of, ovr_of = {}, {}
    for i, e in ipairs(ranked) do
        rank_of[e.pid] = i
        ovr_of[e.pid] = e.ovr
    end

    local info = {}   -- per player facts, for the preview rows
    local function role_of(pid)
        local prec = recs[pid]
        local pos = prec and util.to_int(ptbl:GetRecordFieldValue(prec, "preferredposition1")) or nil
        local f = { pid = pid, age = prec and age_of(prec, ptbl, today) or nil, ovr_rank = rank_of[pid], group = M.pos_group(pos) }
        local r, why, rule = M.role_for(rules, pins, f)
        info[pid] = { age = f.age, ovr = ovr_of[pid], rank = f.ovr_rank, pos = pos, group = f.group, rule = rule }
        return r, why
    end

    local preview = ctx.cfg.role_preview == true
    local sub = { cfg = { include_loaned_in = false, use_memory = true }, dry = ctx.dry, preview = preview,
        out_dir = out_dir, all = ctx.all }
    local ok, summary, report = role.apply(sub, role_of, rules_label(rules, pins))
    if report and preview then
        local no_entry = util.set_of(report.no_entry)
        local rows, seen = {}, {}
        local function row(pid)
            seen[pid] = true
            local f = info[pid] or {}
            local skip = report.skipped[pid]
            if not skip and report.layout and no_entry[pid] then
                skip = "no entry in the role list (contract table only)"
            end
            rows[#rows + 1] = { pid = pid, age = f.age, ovr = f.ovr, rank = f.rank, pos = f.pos, group = f.group,
                old_role = report.old[pid], new_role = report.plan[pid], rule = f.rule, skip = skip }
        end
        for pid in pairs(report.plan) do row(pid) end
        for pid in pairs(report.skipped) do if not seen[pid] then row(pid) end end
        for _, pid in ipairs(report.outside) do
            if not seen[pid] then
                seen[pid] = true
                rows[#rows + 1] = { pid = pid, old_role = report.old[pid], skip = "not in your squad (left alone)" }
            end
        end
        table.sort(rows, function(a, b)
            if (a.ovr or -1) ~= (b.ovr or -1) then return (a.ovr or -1) > (b.ovr or -1) end
            return a.pid < b.pid
        end)
        local j = json_lib()
        local path = out_dir and util.join(out_dir, M.ROLE_PREVIEW_FILE)
        if j and path then
            local enc_ok, text = pcall(j.encode, { team = teamid, counts = report.reasons, loaned = report.loaned,
                no_entry = report.no_entry, outside = report.outside, rows = rows })
            if enc_ok then util.write_file(path, text) end
        end
        return ok, summary .. (path and ("; rows in " .. M.ROLE_PREVIEW_FILE) or "")
    end
    if ok and not ctx.dry and not preview and ctx.cfg.role_save ~= false then
        if M.save_role_rule(out_dir, rules, pins) then
            -- arm the re-apply for this session (a later boot arms it from the saved file)
            role.install_reapply()
            pcall(function() require('imports/turbo/core/events').ensure_registered() end)
        else
            log.warn("squad roles: the rule could not be saved for the season-reset re-apply")
        end
    end
    return ok, summary
end

-- Block offers: the game's own toggle for each player of your club (Turbo.dll's transfer_list call, actions 7 / 9)
local function action_block_offers(ctx, pids, teamid)
    if M.hooks.block_offers then return M.hooks.block_offers(pids, ctx) end
    if teamid ~= game.user_team_id() then
        return false, "Block Offers belongs to your own club (the game keeps no block list for other clubs)"
    end
    if not moves.list_native() then return false, moves.LIST_NATIVE_MISSING end
    local skip = with_loan(pids)
    local blocked, already, queued, skipped, failed, first = 0, 0, 0, 0, 0, nil
    local ids = {}
    for pid in pairs(pids) do ids[#ids + 1] = pid end
    table.sort(ids)
    for _, pid in ipairs(ids) do
        if skip[pid] then
            skipped = skipped + 1
        else
            local ok, text, status, before, after = moves.list(pid, "block_offers", { teamid = teamid, dry = ctx.dry })
            if not ok then
                failed = failed + 1
                first = first or text
            elseif status == "queued" then
                queued = queued + 1
            elseif status == "dry" then
                blocked = blocked + 1
            elseif before == 1 then
                already = already + 1
            else
                blocked = blocked + 1
            end
        end
    end
    local text = string.format("%d players blocked", blocked)
    if already > 0 then text = text .. string.format(", %d were already blocked", already) end
    if queued > 0 then text = text .. string.format(", %d queued for the game thread", queued) end
    if skipped > 0 then
        text = text .. string.format(", %d players on loan skipped (the game blocks only players under contract at your club)", skipped)
    end
    if failed > 0 then text = text .. string.format("; %d failed (%s)", failed, tostring(first)) end
    return failed == 0, text
end

-- Form: your club through Live Editor's SetPlayerForm (form_morale.lua's call, user's team only) plus the database field;
-- other clubs: teamplayerlinks.form (0..M.LINK_FORM, the schema's range) of the team's links
local function action_form(ctx, pids, teamid)
    local own = teamid == game.user_team_id()
    local ok_n, fail_n, links_n = 0, 0, 0
    local fn = _G["SetPlayerForm"]
    if own and type(fn) == "function" then
        if ctx.dry then
            ok_n = util.count(pids)
        else
            for pid in pairs(pids) do
                if pcall(fn, pid, M.FORM) then ok_n = ok_n + 1 else fail_n = fail_n + 1 end
            end
        end
    end
    local links = db.get_table("teamplayerlinks")
    if links and db.has_fields(links, { "teamid", "playerid", "form" }) then
        for rec in db.records(links) do
            if links:GetRecordFieldValue(rec, "teamid") == teamid then
                local ok = db.set(links, rec, "form", M.LINK_FORM, ctx.dry)
                if ok then links_n = links_n + 1 else fail_n = fail_n + 1 end
            end
        end
    elseif not own then
        return false, "teamplayerlinks.form not found in this database"
    end
    local text
    if own then
        text = type(fn) == "function" and string.format("%d players to form %d (Live Editor's SetPlayerForm)", ok_n, M.FORM)
            or "SetPlayerForm is not available in this Live Editor build"
        text = text .. string.format("; teamplayerlinks.form %d on %d links", M.LINK_FORM, links_n)
    else
        text = string.format("%d players to teamplayerlinks.form %d (the database's best form)", links_n, M.LINK_FORM)
    end
    if fail_n > 0 then text = text .. string.format("; %d writes failed", fail_n) end
    return fail_n == 0 and (links_n > 0 or ok_n > 0), text
end

-- The number of this run (cfg.bonus_xp, else M.BONUS_XP), or nil, reason
local function bonus_of(ctx)
    local v = ctx.cfg.bonus_xp
    if v == nil then return M.BONUS_XP end
    local b = util.to_int(v)
    if not b or b < 1 or b > M.BONUS_XP_MAX then return nil, string.format("bonus_xp must be 1..%d", M.BONUS_XP_MAX) end
    return b
end

local function le_dev_manager()
    return type(PlayerDevelopmentManagerAddPlayer) == "function" and type(PlayerDevelopmentManagerSave) == "function"
end

-- Live Editor's development manager entry for every player (bulk_edit.lua's development call)
local function dev_entry(ctx, pids, mult, bonus)
    local ok_n, fail_n = 0, 0
    if ctx.dry then
        ok_n = util.count(pids)
    else
        for pid in pairs(pids) do
            if pcall(PlayerDevelopmentManagerAddPlayer, pid, mult, bonus, false) then ok_n = ok_n + 1 else fail_n = fail_n + 1 end
        end
        if ok_n > 0 then pcall(PlayerDevelopmentManagerSave) end
    end
    local text = string.format("%d players: XP x%.1f, bonus %d XP (Live Editor's development manager)", ok_n, mult, bonus)
    if fail_n > 0 then text = text .. string.format("; %d calls failed", fail_n) end
    return fail_n == 0, text
end

-- Without Live Editor's development manager: Turbo's development.lua "add" (+points on each attribute of the player's group,
-- development plans updated too), never above the player's potential (delta limited to potential - overall)
local function growth(ctx, pids, points)
    local dev = require 'imports/turbo/features/development'
    local ptbl = db.get_table("players")
    if not ptbl then return false, "players table not found" end
    local recs = player_records(ptbl, pids)
    local by_delta, grown, at_pot = {}, 0, 0
    for pid in pairs(pids) do
        local rec = recs[pid]
        if rec then
            local pot = util.to_int(db.get(ptbl, rec, "potential")) or 0
            local ovr = util.to_int(db.get(ptbl, rec, "overallrating")) or 0
            local d = math.min(points, pot - ovr)
            if d <= 0 then
                at_pot = at_pot + 1
            else
                by_delta[d] = by_delta[d] or {}
                table.insert(by_delta[d], pid)
            end
        end
    end
    local fails = {}
    for d, list in pairs(by_delta) do
        table.sort(list)
        local ok, msg = dev.run({ cfg = { scope = { playerids = list }, mode = "add", delta = d, confirm = true }, dry = ctx.dry })
        if ok then grown = grown + #list else fails[#fails + 1] = tostring(msg) end
    end
    local text = string.format("+%d on %d players (%d already at potential; Turbo's development, plans updated)", points, grown,
        at_pot)
    if #fails > 0 then text = text .. "; failed: " .. table.concat(fails, "; ") end
    return #fails == 0, text
end

-- Match XP: the multiplier; with dev_bonus in the same run, the bonus goes in the same call (one entry per player).
-- Without the manager (FC 27 has no match XP field): a one-off +1 growth.
local function action_match_xp(ctx, pids)
    if not le_dev_manager() then
        local ok, text = growth(ctx, pids, 1)
        return ok, "one-off growth " .. text .. " (no Live Editor development manager, FC 27 has no match XP field)"
    end
    local bonus = 0
    if ctx.team_mass_both_dev then
        local b, err = bonus_of(ctx)
        if not b then return false, err end
        bonus = b
    end
    return dev_entry(ctx, pids, M.XP_MULTIPLIER, bonus)
end

local function action_dev_bonus(ctx, pids)
    local b, err = bonus_of(ctx)
    if not b then return false, err end
    if not le_dev_manager() then return growth(ctx, pids, b) end
    if ctx.team_mass_both_dev then return true, string.format("bonus %d XP given with match XP (same call)", b) end
    return dev_entry(ctx, pids, 1.0, b)
end

local RUN = {
    long_contract = action_long_contract,
    morale = action_morale,
    squad_roles = action_squad_roles,
    block_offers = action_block_offers,
    form = action_form,
    match_xp = action_match_xp,
    dev_bonus = action_dev_bonus,
}

local function wanted(actions)
    if actions == "all" then return M.ACTIONS end
    if type(actions) ~= "table" then return nil, "actions must be \"all\" or a list of action names" end
    local seen, list = {}, {}
    for _, a in ipairs(actions) do
        if a == "all" then return M.ACTIONS end
        if not RUN[a] then return nil, "unknown action: " .. tostring(a) end
        if not seen[a] then
            seen[a] = true
            list[#list + 1] = a
        end
    end
    if #list == 0 then return nil, "no action selected" end
    -- fixed order: contracts first, the game-side states last
    local ordered = {}
    for _, a in ipairs({ "long_contract", "squad_roles", "morale", "form", "match_xp", "dev_bonus", "block_offers" }) do
        if seen[a] then ordered[#ordered + 1] = a end
    end
    return ordered
end

function M.run(ctx)
    local teamid = util.to_int(ctx.cfg.teamid)
    if not teamid or teamid <= 0 then return false, "teamid is required" end
    if moves.national_teams()[teamid] then return false, "national teams have no club squad to change" end
    -- Free Agents is not a club: a contract there would give every free agent a club contract without a club
    if teamid == moves.FREE_AGENTS then return false, "Free Agents is not a club: its players have no club contract to change" end
    local list, err = wanted(ctx.cfg.actions)
    if not list then return false, err end
    local has = util.set_of(list)
    ctx.team_mass_both_dev = has.match_xp == true and has.dev_bonus == true

    local pids, n = team_players(teamid)
    if n == 0 then return false, string.format("team %d has no players", teamid) end

    local parts, any_ok, all_ok = {}, false, true
    for _, a in ipairs(list) do
        local ok, res, text = pcall(RUN[a], ctx, pids, teamid)
        local good, msg
        if not ok then
            good, msg = false, "crashed: " .. tostring(res)
            log.error("team_mass %s: %s", a, msg)
        else
            good, msg = res, text
        end
        parts[#parts + 1] = string.format("%s: %s%s", LABEL[a], good and "" or "not done - ", tostring(msg))
        any_ok = any_ok or good
        all_ok = all_ok and good
    end
    local summary = string.format("team %d (%d players) - %s", teamid, n, table.concat(parts, " | "))
    return any_ok, summary
end

return M
