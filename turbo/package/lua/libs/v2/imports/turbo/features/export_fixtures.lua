-- Turbo feature: fixtures and results to CSV.
-- Port of FC 26 export_fixtures.lua. FC 26 read two fixed lists inside FCEDataManager
-- (fixtures at +0x60, standings at +0x88). Turbo checks those first and, when FC 27 moved them,
-- searches the manager for lists whose entries look like fixtures/standings (real team IDs,
-- valid YYYYMMDD dates). Nothing is exported from a list that fails these checks.
-- FC 27 (seen in game, 02-10-2026): the FC 26 lists are gone from FCEDataManager. What Turbo could find is the list of
-- your club's remaining fixtures in the MainHubManager: a count followed by a pointer to 0x130-byte entries
--   +0x2C competition object id   +0x38 date (YYYYMMDD)   +0x3C kick-off (HHMM)   +0x128 home club   +0x12C away club
-- When the FC 26 lists are not there, Turbo exports that list instead (your club's upcoming fixtures, no results), after
-- checking every entry: real dates in order, real clubs, your club in each match.
--   "export_fixtures": {}

local util = require 'imports/turbo/core/util'
local game = require 'imports/turbo/core/game'
local mem = require 'imports/turbo/core/mem'
local calib = require 'imports/turbo/core/calib'
local csv = require 'imports/turbo/core/csv'

local M = {}

M.LAYOUT = {
    chain = { 0x18, 0x10, 0x08, 0x00 },  -- IFCEInterface plugin -> FCEDataManager
    fixtures_off = 0x60,
    standings_off = 0x88,
    list_begin = 0x28,
    list_count = 0x1C,
    item_size = 0x18,
}
local MAX_ITEMS = 60000

function M.data_manager()
    pcall(require, 'imports/services/enums')
    if type(GetPlugin) ~= "function" or type(ENUM_djb2IFCEInterface_CLSS) ~= "number" then return nil end
    local ok, iface = pcall(GetPlugin, ENUM_djb2IFCEInterface_CLSS)
    if not ok or not mem.is_ptr(iface, 8) then return nil end
    return mem.chain(iface, M.LAYOUT.chain)
end

-- list object -> begin, count
local function list_info(list_obj)
    if not list_obj then return nil end
    local b = mem.ptr(list_obj + M.LAYOUT.list_begin, 4)
    local count = mem.int(list_obj + M.LAYOUT.list_count)
    if not b or not count or count < 1 or count > MAX_ITEMS then return nil end
    return b, count
end

local function read_standing(addr)
    return {
        id = mem.short(addr + 0x00),
        compobjid = mem.short(addr + 0x02),
        teamid = mem.int(addr + 0x04),
        points = mem.short(addr + 0x14),
        used = mem.bool(addr + 0x16),
    }
end

local function read_fixture(addr)
    return {
        date = mem.int(addr + 0x00),
        time = mem.short(addr + 0x04),
        id = mem.short(addr + 0x06),
        compobjid = mem.short(addr + 0x08),
        home = mem.short(addr + 0x0A),
        away = mem.short(addr + 0x0C),
        home_score = mem.byte(addr + 0x0F),
        home_pens = mem.byte(addr + 0x10),
        away_score = mem.byte(addr + 0x11),
        away_pens = mem.byte(addr + 0x12),
        completed = mem.bool(addr + 0x13),
        used = mem.bool(addr + 0x14),
    }
end

-- Standings list: >= 8 used entries and >= 90% of them with a real team id
function M.read_standings(list_obj, team_set)
    local b, count = list_info(list_obj)
    if not b then return nil end
    local by_index, used, good = {}, 0, 0
    for i = 0, count - 1 do
        local s = read_standing(b + i * M.LAYOUT.item_size)
        if s.used and s.teamid and s.teamid > 0 then
            used = used + 1
            if team_set[s.teamid] then good = good + 1 end
            by_index[i] = s
        end
        -- Early reject: random memory fails fast
        if i == 255 and used > 0 and good < used * 0.5 then return nil end
    end
    if used < 8 or good < used * 0.9 then return nil end
    return by_index, count, used
end

-- Fixtures list: >= 1 used entry and >= 95% with a valid date and standings indexes in range
function M.read_fixtures(list_obj, n_standings)
    local b, count = list_info(list_obj)
    if not b then return nil end
    local out, used, good = {}, 0, 0
    for i = 0, count - 1 do
        local f = read_fixture(b + i * M.LAYOUT.item_size)
        if f.used then
            used = used + 1
            if util.is_yyyymmdd(f.date) and f.home and f.away and f.home >= 0 and f.away >= 0
                and f.home < n_standings and f.away < n_standings and (f.compobjid or 0) > 0 then
                good = good + 1
                out[#out + 1] = f
            end
        end
        if i == 255 and used > 0 and good < used * 0.5 then return nil end
    end
    if used < 1 or good < used * 0.95 then return nil end
    return out
end

-- Returns { dm, fixtures_off, standings_off, fixtures, standings } or nil, reason
function M.locate(team_set, hint)
    local dm = M.data_manager()
    if not dm then return nil, "FCEDataManager not reachable (IFCEInterface chain changed)" end

    local pairs_to_try = {}
    if type(hint) == "table" and util.to_int(hint.fixtures_off) and util.to_int(hint.standings_off) then
        pairs_to_try[#pairs_to_try + 1] = { util.to_int(hint.fixtures_off), util.to_int(hint.standings_off) }
    end
    pairs_to_try[#pairs_to_try + 1] = { M.LAYOUT.fixtures_off, M.LAYOUT.standings_off }

    local function try_pair(fo, so)
        local st_list = mem.ptr(dm + so)
        local fx_list = mem.ptr(dm + fo)
        if not st_list or not fx_list then return nil end
        local standings, n_st = M.read_standings(st_list, team_set)
        if not standings then return nil end
        local fixtures = M.read_fixtures(fx_list, n_st)
        if not fixtures then return nil end
        return { dm = dm, fixtures_off = fo, standings_off = so, fixtures = fixtures, standings = standings }
    end

    for _, p in ipairs(pairs_to_try) do
        local r = try_pair(p[1], p[2])
        if r then return r end
    end

    -- Search: find a standings list first, then a fixtures list that refers to it
    for so = 0x08, 0x400, 8 do
        local st_list = mem.ptr(dm + so)
        local standings, n_st = nil, nil
        if st_list then standings, n_st = M.read_standings(st_list, team_set) end
        if standings then
            for fo = 0x08, 0x400, 8 do
                if fo ~= so then
                    local fx_list = mem.ptr(dm + fo)
                    local fixtures = fx_list and M.read_fixtures(fx_list, n_st)
                    if fixtures then
                        return { dm = dm, fixtures_off = fo, standings_off = so, fixtures = fixtures, standings = standings }
                    end
                end
            end
        end
    end
    return nil, "no fixtures/standings lists matching real teams and dates were found"
end

------------------------------------------------------------------ FC 27: your club's remaining fixtures
M.FC27_USER = { manager = 58, size = 0x130, comp = 0x2C, date = 0x38, time = 0x3C, home = 0x128, away = 0x12C,
    max = 200, scan_to = 0x400 }

local function read_user_list(mgr, off, team_set, user_team)
    local L = M.FC27_USER
    local b = mem.ptr(mgr + off)
    local count = mem.int(mgr + off - 8)
    if not b or not count or count < 1 or count > L.max then return nil end
    if not mem.readable(b, count * L.size) then return nil end
    local out, mine, last = {}, 0, 0
    for i = 0, count - 1 do
        local e = b + i * L.size
        local date, home, away = mem.int(e + L.date), mem.int(e + L.home), mem.int(e + L.away)
        if not util.is_yyyymmdd(date) or date < last then return nil end
        if not (home and away and team_set[home] and team_set[away]) or home == away then return nil end
        if home == user_team or away == user_team then mine = mine + 1 end
        last = date
        out[#out + 1] = { date = date, time = mem.int(e + L.time), compobjid = mem.int(e + L.comp), home = home, away = away }
    end
    if user_team > 0 and mine < count * 0.9 then return nil end
    return out
end

-- Returns { off, fixtures } or nil, reason
function M.locate_fc27(team_set, user_team, hint)
    local id = _G["ENUM_FCEGameModesFCECareerModeMainHubManager"]
    local mgr = mem.manager(type(id) == "number" and id or M.FC27_USER.manager)
    if not mgr then return nil, "MainHubManager not found" end
    local tries = {}
    local h = type(hint) == "table" and util.to_int(hint.user_list_off)
    if h then tries[#tries + 1] = h end
    for off = 0x08, M.FC27_USER.scan_to, 8 do tries[#tries + 1] = off end
    for _, off in ipairs(tries) do
        local list = read_user_list(mgr, off, team_set, user_team)
        if list then return { off = off, fixtures = list } end
    end
    return nil, "your club's fixture list was not found in the MainHubManager"
end

local COLUMNS = { "competition", "compobjid", "date", "time", "hometeamid", "hometeam", "homescore", "awayscore",
    "awayteam", "awayteamid", "completed" }

function M.write_user_fixtures(ctx, mine)
    local names, comps, rows = {}, {}, {}
    for _, f in ipairs(mine.fixtures) do
        names[f.home] = names[f.home] or game.team_name(f.home)
        names[f.away] = names[f.away] or game.team_name(f.away)
        comps[f.compobjid] = comps[f.compobjid] or game.competition_name(f.compobjid)
        rows[#rows + 1] = {
            competition = comps[f.compobjid], compobjid = f.compobjid, date = f.date, time = f.time,
            hometeamid = f.home, hometeam = names[f.home], awayteamid = f.away, awayteam = names[f.away],
            homescore = "", awayscore = "", completed = 0,
        }
    end
    local path = util.join(ctx.out_dir, "turbo_fixtures_" .. util.timestamp_suffix(game.current_date()) .. ".csv")
    if not ctx.dry then
        local wok, werr = csv.write(path, COLUMNS, rows)
        if not wok then return false, "cannot write " .. path .. ": " .. tostring(werr) end
    end
    return true, string.format("%d upcoming fixtures of your club saved to %s (FC 27 MainHubManager+0x%X; the full "
        .. "fixture and results lists of FC 26 were not found in this game)", #rows, path, mine.off)
end

function M.run(ctx)
    if not ctx.out_dir then return false, "no writable output folder" end
    local team_set = game.team_ids()
    if util.count(team_set) == 0 then return false, "teams table not readable" end

    if not mem.map_available() then return false, mem.NO_MAP end
    local found, err = M.locate(team_set, calib.get(ctx.out_dir, "fixtures"))
    if not found then
        local user_team = game.in_cm() and game.user_team_id() or 0
        local mine, err27 = M.locate_fc27(team_set, user_team, calib.get(ctx.out_dir, "fixtures_fc27"))
        if not mine then return false, string.format("%s; %s", err, err27) end
        calib.put(ctx.out_dir, "fixtures_fc27", { user_list_off = mine.off })
        return M.write_user_fixtures(ctx, mine)
    end
    calib.put(ctx.out_dir, "fixtures", { fixtures_off = found.fixtures_off, standings_off = found.standings_off })
    local names, comps, rows = {}, {}, {}
    for _, f in ipairs(found.fixtures) do
        local hs, as = found.standings[f.home], found.standings[f.away]
        local htid = hs and hs.teamid or 0
        local atid = as and as.teamid or 0
        names[htid] = names[htid] or game.team_name(htid)
        names[atid] = names[atid] or game.team_name(atid)
        comps[f.compobjid] = comps[f.compobjid] or game.competition_name(f.compobjid)
        rows[#rows + 1] = {
            competition = comps[f.compobjid], compobjid = f.compobjid, date = f.date, time = f.time,
            hometeamid = htid, hometeam = names[htid], awayteamid = atid, awayteam = names[atid],
            homescore = f.completed and f.home_score or "", awayscore = f.completed and f.away_score or "",
            completed = f.completed and 1 or 0,
        }
    end
    table.sort(rows, function(a, b)
        if a.date ~= b.date then return a.date < b.date end
        if a.time ~= b.time then return (a.time or 0) < (b.time or 0) end
        return a.hometeamid < b.hometeamid
    end)

    local path = util.join(ctx.out_dir, "turbo_fixtures_" .. util.timestamp_suffix(game.current_date()) .. ".csv")
    if not ctx.dry then
        local wok, werr = csv.write(path, COLUMNS, rows)
        if not wok then return false, "cannot write " .. path .. ": " .. tostring(werr) end
    end
    return true, string.format("%d fixtures saved to %s (lists at +0x%X / +0x%X)", #rows, path,
        found.fixtures_off, found.standings_off)
end

return M
