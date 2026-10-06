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

-- role_of(pid) for the players `pids` ({[pid] = true}): Rotation from M.ADULT_AGE, Prospect below, nil when his age is not
-- known. Also used by squad_role.repair_entries (the bridge's crash guard). Returns nil, reason without a date / players table.
function M.role_by_age(pids)
    local ptbl = db.get_table("players")
    if not ptbl then return nil, "players table not found" end
    local today = moves.today_days and moves.today_days() or nil
    if not today then
        local d = game.current_date()
        today = d and d.year and util.gregorian_days_from_date(d.year, d.month, d.day) or nil
    end
    if not today then return nil, "current in-game date not available" end
    local recs = player_records(ptbl, pids)
    return function(pid)
        local prec = recs[pid]
        local age = prec and age_of(prec, ptbl, today) or nil
        if not age then return nil end
        return age >= M.ADULT_AGE and M.ROLE_ROTATION or M.ROLE_PROSPECT
    end
end

local function action_squad_roles(ctx, pids, teamid)
    if teamid ~= game.user_team_id() then
        return false, "squad roles exist for your own club only (the game keeps none for other clubs)"
    end
    local role = require 'imports/turbo/features/squad_role'
    local role_of, rerr = M.role_by_age(pids)
    if not role_of then return false, rerr end
    local sub = { cfg = { include_loaned_in = false, use_memory = true }, dry = ctx.dry, out_dir = ctx.out_dir, all = ctx.all }
    local ok, summary = role.apply(sub, role_of, string.format("Rotation for age %d+, Prospect below", M.ADULT_AGE))
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
