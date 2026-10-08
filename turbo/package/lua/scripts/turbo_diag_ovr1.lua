-- FC 27 LE Turbo: READ-ONLY diagnostic for players whose overall rating drops to 1 after matches.
-- Run it from Live Editor's Lua Engine with a career loaded. It writes turbo_output\diag_ovr1.txt (and .json) and
-- changes nothing. Attach the two files to the bug report together with turbo_output\turbo_gui.log and turbo.log.
--
-- What it records:
--   * every player whose overallrating or potential is 5 or below: id, name, club, position, overall, potential, every
--     attribute (so one can see whether the attributes are damaged too, or only the overall), and whether the game has a
--     development plan for him (PlayerHasDevelopementPlan, the user's club only)
--   * the user's club: every player with his overall and the lowest / highest attribute of his group, to spot a
--     mismatch between the overall and the attributes
--   * the tables whose name contains growth / develop / plan / youth, with their row counts
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'
local util = require 'imports/turbo/core/util'

local ATTRS = { "acceleration", "sprintspeed", "agility", "balance", "jumping", "stamina", "strength", "reactions",
    "aggression", "composure", "interceptions", "positioning", "vision", "ballcontrol", "crossing", "dribbling",
    "finishing", "freekickaccuracy", "headingaccuracy", "longpassing", "shortpassing", "defensiveawareness", "shotpower",
    "longshots", "standingtackle", "slidingtackle", "volleys", "curve", "penalties", "gkdiving", "gkhandling",
    "gkkicking", "gkreflexes", "gkpositioning" }
local LOW = 5

local lines, out = {}, { low_players = {}, user_club = {}, tables = {}, generated = os.date("%Y-%m-%d %H:%M:%S") }
local function say(fmt, ...) lines[#lines + 1] = string.format(fmt, ...) end

local function has_plan(pid)
    if type(PlayerHasDevelopementPlan) ~= "function" then return "native missing" end
    local ok, yes = pcall(PlayerHasDevelopementPlan, pid)
    if not ok then return "error: " .. tostring(yes) end
    return tostring(yes)
end

local function main()
    if not game.in_cm() then return false, "load a Manager Career first" end
    local players, err = db.get_table("players")
    if not players then return false, err end
    local links = db.get_table("teamplayerlinks")
    local team_of = {}
    if links then
        for rec in db.records(links) do
            local pid = links:GetRecordFieldValue(rec, "playerid")
            local tid = links:GetRecordFieldValue(rec, "teamid")
            if pid and tid and not team_of[pid] then team_of[pid] = tid end
        end
    end
    local user = game.user_team_id() or 0
    say("diag_ovr1 %s  user club %s (%d)  players table written_records=%s", out.generated, game.team_name(user), user,
        tostring(players.written_records))

    local fields = {}
    for _, a in ipairs(ATTRS) do if db.has_field(players, a) then fields[#fields + 1] = a end end
    local n_low, n_user, n_all = 0, 0, 0
    for rec in db.records(players) do
        n_all = n_all + 1
        local pid = players:GetRecordFieldValue(rec, "playerid") or -1
        local ovr = players:GetRecordFieldValue(rec, "overallrating") or -1
        local pot = players:GetRecordFieldValue(rec, "potential") or -1
        local tid = team_of[pid] or -1
        if ovr <= LOW or pot <= LOW then
            n_low = n_low + 1
            local attrs, low_attrs = {}, 0
            for _, a in ipairs(fields) do
                local v = players:GetRecordFieldValue(rec, a)
                attrs[a] = v
                if v ~= nil and v <= LOW then low_attrs = low_attrs + 1 end
            end
            local row = { playerid = pid, name = game.player_name(pid), teamid = tid, team = game.team_name(tid),
                overall = ovr, potential = pot, position = players:GetRecordFieldValue(rec, "preferredposition1"),
                attributes_at_or_below_5 = low_attrs, attributes = attrs,
                plan = (tid == user) and has_plan(pid) or "n/a (not user club)" }
            out.low_players[#out.low_players + 1] = row
            local parts = {}
            for _, a in ipairs(fields) do parts[#parts + 1] = a .. "=" .. tostring(attrs[a]) end
            say("LOW  pid %d %s | %s (%d) | pos %s | ovr %d pot %d | attrs<=%d: %d of %d | plan %s", pid, row.name, row.team,
                tid, tostring(row.position), ovr, pot, LOW, low_attrs, #fields, row.plan)
            say("     %s", table.concat(parts, " "))
        end
        if tid == user then
            n_user = n_user + 1
            local lo, hi = 999, -1
            for _, a in ipairs(fields) do
                local v = players:GetRecordFieldValue(rec, a)
                if v ~= nil then
                    if v < lo then lo = v end
                    if v > hi then hi = v end
                end
            end
            out.user_club[#out.user_club + 1] = { playerid = pid, name = game.player_name(pid), overall = ovr, potential = pot,
                min_attr = lo, max_attr = hi, plan = has_plan(pid) }
        end
    end
    table.sort(out.user_club, function(a, b) return a.overall < b.overall end)
    say("")
    say("USER CLUB (%d players, sorted by overall): pid name ovr pot min_attr max_attr plan", n_user)
    for _, r in ipairs(out.user_club) do
        say("  %d %s ovr %d pot %d attrs %d..%d plan %s", r.playerid, r.name, r.overall, r.potential, r.min_attr, r.max_attr, r.plan)
    end

    say("")
    if type(GetDBTablesNames) == "function" then
        local ok, names = pcall(GetDBTablesNames)
        if ok and type(names) == "table" then
            for _, name in ipairs(names) do
                local n = string.lower(tostring(name))
                if n:find("growth") or n:find("develop") or n:find("plan") or n:find("youth") or n:find("xp") then
                    local t = db.get_table(name)
                    local rows = t and tostring(t.written_records) or "?"
                    out.tables[#out.tables + 1] = { name = name, rows = rows }
                    say("TABLE %s rows=%s", name, rows)
                end
            end
        end
    end
    say("")
    say("SUMMARY: %d players scanned, %d with overall or potential <= %d, %d in the user club", n_all, n_low, LOW, n_user)
    out.summary = { players = n_all, low = n_low, user_club = n_user }

    local dir = env.output_dir()
    local txt = util.join(dir, "diag_ovr1.txt")
    local f = io.open(txt, "wb")
    if f then f:write(table.concat(lines, "\n"), "\n"); f:close() end
    local jf = io.open(util.join(dir, "diag_ovr1.json"), "wb")
    if jf then
        local ok_json, json = pcall(require, 'imports/external/json')
        if ok_json and json and json.encode then jf:write(json.encode(out)) else jf:write("{}") end
        jf:close()
    end
    for _, l in ipairs(lines) do log.info("%s", l) end
    return true, string.format("diag_ovr1: %d low players of %d, written to %s", n_low, n_all, txt)
end

local ok, msg = pcall(main)
if ok then
    log.info("%s", tostring(msg))
else
    log.warn("diag_ovr1 failed: %s", tostring(msg))
end
