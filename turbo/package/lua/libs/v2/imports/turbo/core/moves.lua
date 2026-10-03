-- FC 27 LE Turbo - player moves done by Turbo itself, in the game database.
-- FC 27 Live Editor v27.1.2 has the FC 26 Lua names (TransferPlayer, LoanPlayer, ReleasePlayerFromTeam, TerminateLoan,
-- DeletePlayer) but not the natives behind them. Turbo writes the same result into the career's tables:
--   transfer  : the player's club link (teamplayerlinks) moves to the new club with a free shirt number; join date,
--               contract end, wage and release clause are set in players; your team sheet (cm_teamsheets) loses or
--               gains him; set-piece takers that pointed at him are replaced.
--   loan      : a playerloans row (club he is loaned from, end date) + the club link moves to the loan club.
--   release   : transfer to Free Agents (team 111592), like FC 26 Live Editor's ReleasePlayerFromTeam.
--   terminate : the club link goes back to playerloans.teamidloanedfrom, the playerloans row is deleted.
--   delete    : release, then his players / teamplayerlinks / editedplayernames / playerloans rows are deleted
--               (what FC 26 Live Editor's DeletePlayer did).
-- Every value is validated against the field's range before anything is written; a failed check writes nothing.
--
-- SAFETY (0.3.0): these database-only moves are not seen by the running career until the save is reloaded, and in the
-- test career "turbolab" the game crashed while simulating after several of them (03-10-2026, root cause not found).
-- So every move into or out of YOUR club (the career user's team), and deleting one of your players, is refused: use
-- the game's own transfer screens for your club. Moves between other clubs stay available and are labelled unverified.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'

local M = {}

M.FREE_AGENTS = 111592
M.RESERVE_POSITION = 29   -- teamplayerlinks.position for a player outside the line-up (as the game stores reserves)

local SET_PIECE_FIELDS = { "captainid", "penaltytakerid", "freekicktakerid", "leftfreekicktakerid", "rightfreekicktakerid",
    "leftcornerkicktakerid", "rightcornerkicktakerid", "longkicktakerid", "throwerleft", "throwerright",
    "cksupport1", "cksupport2", "cksupport3", "cksupport4", "cksupport5", "cksupport6", "cksupport7", "cksupport8",
    "cksupport9" }

-- National teams (teamnationlinks); a player's club is his link to a team that is not one of these
local national_cache
function M.national_teams()
    if national_cache then return national_cache end
    local set = {}
    local t = db.get_table("teamnationlinks")
    if t and db.has_field(t, "teamid") then
        for rec in db.records(t) do
            local tid = t:GetRecordFieldValue(rec, "teamid")
            if tid then set[tid] = true end
        end
    end
    national_cache = set
    return set
end

function M.reset_cache() national_cache = nil end

-- The player's club link: rec, teamid (or nil, error)
function M.club_link(pid)
    local links, err = db.get_table("teamplayerlinks")
    if not links then return nil, err end
    local nat = M.national_teams()
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "playerid") == pid then
            local tid = links:GetRecordFieldValue(rec, "teamid")
            if tid and not nat[tid] then return rec, tid, links end
        end
    end
    return nil, string.format("player %d has no club link in teamplayerlinks", pid)
end

-- Your club in the loaded career (0 = unknown). Overridable for tests.
function M.user_team()
    local ok, tid = pcall(game.user_team_id)
    tid = ok and util.to_int(tid) or 0
    return tid or 0
end

M.OWN_CLUB_REFUSED = "Turbo does not move players into or out of your own club (database-only moves can crash the " ..
    "career, seen in FC 27): use the game's transfer screens for your club"

-- Refuses a move that touches the user's club. Unknown user club (outside a career) = refuse too.
function M.guard(from_team, to_team)
    local user = M.user_team()
    if user <= 0 then return false, "your club is not known (load a career first)" end
    if from_team == user or to_team == user then return false, M.OWN_CLUB_REFUSED end
    return true
end

function M.team_of_player(pid)
    local rec, tid = M.club_link(pid)
    return rec and tid or 0
end

-- playerloans row of a player: rec, table (or nil)
function M.loan_row(pid)
    local loans = db.get_table("playerloans")
    if not loans or not db.has_field(loans, "playerid") then return nil end
    for rec in db.records(loans) do
        if loans:GetRecordFieldValue(rec, "playerid") == pid then return rec, loans end
    end
    return nil
end

-- A shirt number not used at the club (prefers `wanted`), 1..99
function M.free_jersey(links, teamid, wanted, except_pid)
    local used = {}
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid and links:GetRecordFieldValue(rec, "playerid") ~= except_pid then
            used[links:GetRecordFieldValue(rec, "jerseynumber") or -1] = true
        end
    end
    if wanted and wanted >= 1 and wanted <= 99 and not used[wanted] then return wanted end
    for n = 2, 99 do
        if not used[n] then return n end
    end
    return 99
end

local function today()
    local d = game.current_date()
    if d and d.year then return d end
    return { year = 2026, month = 7, day = 1 }
end

local function today_days()
    local d = today()
    return util.gregorian_days_from_date(d.year, d.month, d.day)
end

-- Contract end year after `months` from today (contracts end in June: a contract signed in July 2026 for 36 months
-- ends in June 2029)
function M.contract_end_year(months)
    local d = today()
    local season_start = (d.month >= 7) and d.year or (d.year - 1)
    local years = math.max(1, math.ceil(months / 12))
    return season_start + years
end

-- A plan is a list of { table, rec, field, value } writes, validated first, applied after
local function plan_write(plan, tbl, rec, field, value)
    if not db.has_field(tbl, field) then return true end   -- optional field missing in this game: skip
    local v, err = db.validate(tbl, field, value)
    if v == nil then return false, err end
    plan[#plan + 1] = { tbl = tbl, rec = rec, field = field, value = v }
    return true
end

local function apply(plan, dry)
    if dry then return true end
    for _, w in ipairs(plan) do
        local ok, err = db.set(w.tbl, w.rec, w.field, w.value)
        if not ok then return false, string.format("%s.%s: %s", tostring(w.tbl.name), w.field, tostring(err)) end
    end
    return true
end

-- Remove pid from the user's team sheet (cm_teamsheets of `teamid`): the slots after him move up; set-piece takers
-- that were him become the first player left
local function plan_sheet_remove(plan, teamid, pid)
    local sheets = db.get_table("cm_teamsheets")
    if not sheets or not db.has_field(sheets, "teamid") then return true end
    for rec in db.records(sheets) do
        if sheets:GetRecordFieldValue(rec, "teamid") == teamid then
            local ids = {}
            for i = 0, 51 do
                local f = "playerid" .. i
                if db.has_field(sheets, f) then
                    local v = sheets:GetRecordFieldValue(rec, f)
                    if v == nil or v == -1 then break end
                    if v ~= pid then ids[#ids + 1] = v end
                end
            end
            local changed = false
            for i = 0, 51 do
                local f = "playerid" .. i
                if db.has_field(sheets, f) then
                    local want = ids[i + 1] or -1
                    if sheets:GetRecordFieldValue(rec, f) ~= want then
                        local ok, err = plan_write(plan, sheets, rec, f, want)
                        if not ok then return false, err end
                        changed = true
                    end
                end
            end
            local first = ids[1]
            for _, f in ipairs(SET_PIECE_FIELDS) do
                if db.has_field(sheets, f) and sheets:GetRecordFieldValue(rec, f) == pid and first then
                    local ok, err = plan_write(plan, sheets, rec, f, first)
                    if not ok then return false, err end
                    changed = true
                end
            end
            local _ = changed
        end
    end
    return true
end

-- Add pid at the end of the team sheet of `teamid` (if that club has one, i.e. it is the user's club)
local function plan_sheet_add(plan, teamid, pid)
    local sheets = db.get_table("cm_teamsheets")
    if not sheets or not db.has_field(sheets, "teamid") then return true end
    for rec in db.records(sheets) do
        if sheets:GetRecordFieldValue(rec, "teamid") == teamid then
            for i = 0, 51 do
                local f = "playerid" .. i
                if db.has_field(sheets, f) then
                    local v = sheets:GetRecordFieldValue(rec, f)
                    if v == pid then return true end
                    if v == nil or v == -1 then return plan_write(plan, sheets, rec, f, pid) end
                end
            end
            return false, "the team sheet is full (52 players)"
        end
    end
    return true
end

-- Set-piece takers of a team in the teams table that point at pid are cleared to the team's captain or -1... the
-- game picks new ones; Turbo only makes sure no AI club keeps a player it no longer has as its taker
local function plan_team_takers(plan, teamid, pid)
    local teams = db.get_table("teams")
    if not teams then return true end
    for rec in db.records(teams) do
        if teams:GetRecordFieldValue(rec, "teamid") == teamid then
            for _, f in ipairs(SET_PIECE_FIELDS) do
                if db.has_field(teams, f) and teams:GetRecordFieldValue(rec, f) == pid then
                    local info = db.field_info(teams, f)
                    local blank = info and info.min or 0
                    local ok, err = plan_write(plan, teams, rec, f, blank)
                    if not ok then return false, err end
                end
            end
            return true
        end
    end
    return true
end

-- Move the club link of pid to `to_team`. opts: contract = { months, wage, release_clause } (nil = keep contract)
local function plan_move(plan, pid, to_team, opts)
    local rec, from_or_err, links = M.club_link(pid)
    if not rec then return nil, from_or_err end
    local from = from_or_err
    local okg, gerr = M.guard(from, to_team)
    if not okg then return nil, gerr end
    if from == to_team then return nil, string.format("player %d is already at team %d", pid, to_team) end
    local jersey = M.free_jersey(links, to_team, links:GetRecordFieldValue(rec, "jerseynumber"), pid)
    local ok, err = plan_write(plan, links, rec, "teamid", to_team)
    if ok then ok, err = plan_write(plan, links, rec, "jerseynumber", jersey) end
    if ok then ok, err = plan_write(plan, links, rec, "position", M.RESERVE_POSITION) end
    if not ok then return nil, err end

    local players = db.get_table("players")
    local prec = players and db.find(players, "playerid", pid)
    if not prec then return nil, string.format("player %d not found in players", pid) end
    if opts.contract then
        local c = opts.contract
        ok, err = plan_write(plan, players, prec, "playerjointeamdate", today_days())
        if ok and c.months then ok, err = plan_write(plan, players, prec, "contractvaliduntil", M.contract_end_year(c.months)) end
        if ok and c.wage then ok, err = plan_write(plan, players, prec, "wage", c.wage) end
        if ok and c.release_clause then
            ok, err = plan_write(plan, players, prec, "releaseclause", math.max(0, c.release_clause))
        end
        if not ok then return nil, err end
    end

    ok, err = plan_sheet_remove(plan, from, pid)
    if ok then ok, err = plan_sheet_add(plan, to_team, pid) end
    if ok then ok, err = plan_team_takers(plan, from, pid) end
    if not ok then return nil, err end
    return from
end

local function check_team(to_team)
    local teams = game.team_ids()
    if not teams[to_team] then return false, string.format("team %s not found", tostring(to_team)) end
    if M.national_teams()[to_team] then return false, string.format("team %d is a national team", to_team) end
    return true
end

-- Returns true, summary or false, error
function M.transfer(pid, to_team, o, dry)
    o = o or {}
    local ok, err = check_team(to_team)
    if not ok then return false, err end
    if M.loan_row(pid) then
        return false, string.format("player %d is on loan: terminate the loan first", pid)
    end
    local plan = {}
    local from, merr = plan_move(plan, pid, to_team, { contract = {
        months = o.months or 36, wage = o.wage, release_clause = o.release_clause } })
    if not from then return false, merr end
    ok, err = apply(plan, dry)
    if not ok then return false, err end
    return true, string.format("player %d: team %d -> %d", pid, from, to_team)
end

function M.release(pid, dry)
    if M.loan_row(pid) then return false, string.format("player %d is on loan: terminate the loan first", pid) end
    local plan = {}
    local from, merr = plan_move(plan, pid, M.FREE_AGENTS, { contract = { wage = 0, release_clause = 0 } })
    if not from then return false, merr end
    local ok, err = apply(plan, dry)
    if not ok then return false, err end
    return true, string.format("player %d released from team %d", pid, from)
end

function M.loan(pid, to_team, months, dry)
    local ok, err = check_team(to_team)
    if not ok then return false, err end
    if M.loan_row(pid) then return false, string.format("player %d is already on loan", pid) end
    months = util.to_int(months) or 12
    if months < 1 or months > 60 then return false, "loan length must be 1..60 months" end
    local loans = db.get_table("playerloans")
    if not loans or not db.has_fields(loans, { "playerid", "teamidloanedfrom", "loandateend" }) then
        return false, "playerloans table not available"
    end
    local plan = {}
    local from, merr = plan_move(plan, pid, to_team, {})
    if not from then return false, merr end
    local row = {
        playerid = tostring(pid), teamidloanedfrom = tostring(from),
        loandateend = tostring(today_days() + math.floor(months * 30.44 + 0.5)),
        isloantobuy = "0", futureoptiontobuy = "0",
    }
    for f, v in pairs(row) do
        if db.has_field(loans, f) then
            local okv, verr = db.validate(loans, f, v)
            if okv == nil then return false, verr end
        else
            row[f] = nil
        end
    end
    if dry then return true, string.format("player %d: loan %d -> %d for %d months", pid, from, to_team, months) end
    local insert = _G["InsertDBTableRow"]
    if type(insert) ~= "function" then return false, "InsertDBTableRow is not available in this Live Editor build" end
    -- InsertDBTableRow returns the new row (DOC.MD: DBRow, each field { value, addr }); the row is then looked up in
    -- the table itself, so a call that returned something but added nothing is caught
    local okc, res = pcall(insert, "playerloans", row)
    if not okc or type(res) ~= "table" or not M.loan_row(pid) then
        return false, "could not add the playerloans row: " .. tostring(res)
    end
    ok, err = apply(plan, false)
    if not ok then return false, err end
    return true, string.format("player %d: loan %d -> %d for %d months", pid, from, to_team, months)
end

-- Delete every record of `table_name` whose playerid is pid, through Live Editor's DeleteDBTableRowByAddr.
-- FC 27 LE v27.1.2 gives each field of a GetDBTableRows row an `addr` that is the record address as a decimal string
-- (seen in game, 03-10-2026); the same string is what DeleteDBTableRowByAddr takes.
local function delete_rows(table_name, pid)
    local del = _G["DeleteDBTableRowByAddr"]
    if type(del) ~= "function" then return nil, "DeleteDBTableRowByAddr is not available in this Live Editor build" end
    local tbl = db.get_table(table_name)
    if not tbl or not db.has_field(tbl, "playerid") then return 0 end
    local recs = {}
    for rec in db.records(tbl) do
        if tbl:GetRecordFieldValue(rec, "playerid") == pid then recs[#recs + 1] = rec end
    end
    for _, rec in ipairs(recs) do
        local okd, res = pcall(del, table_name, string.format("%d", rec))
        if not okd or res == false then
            return nil, string.format("deleting a %s row failed: %s", table_name, tostring(res))
        end
    end
    return #recs
end

function M.terminate_loan(pid, dry)
    local lrec, loans = M.loan_row(pid)
    if not lrec then return false, string.format("player %d is not on loan", pid) end
    local back = loans:GetRecordFieldValue(lrec, "teamidloanedfrom")
    local plan = {}
    local from, merr = plan_move(plan, pid, back, {})
    if not from then return false, merr end
    if dry then return true, string.format("player %d: loan at %d ended, back to %d", pid, from, back) end
    local ok, err = apply(plan, false)
    if not ok then return false, err end
    local n, derr = delete_rows("playerloans", pid)
    if not n then return false, derr end
    return true, string.format("player %d: loan at %d ended, back to %d", pid, from, back)
end

function M.delete(pid, dry)
    local players = db.get_table("players")
    if not players or not db.find(players, "playerid", pid) then return false, string.format("player %d not found", pid) end
    do
        local _, cur = M.club_link(pid)
        local lrec, loans = M.loan_row(pid)
        local owner = lrec and loans:GetRecordFieldValue(lrec, "teamidloanedfrom") or nil
        local okg, gerr = M.guard(cur or M.FREE_AGENTS, owner or cur or M.FREE_AGENTS)
        if not okg then return false, gerr end
    end
    if dry then return true, string.format("player %d would be deleted", pid) end
    if M.loan_row(pid) then
        local ok, err = M.terminate_loan(pid, false)
        if not ok then return false, err end
    end
    local _, tid = M.club_link(pid)
    if tid and tid ~= M.FREE_AGENTS then
        local ok, err = M.release(pid, false)
        if not ok then return false, err end
    end
    for _, t in ipairs({ "teamplayerlinks", "editedplayernames", "playerloans", "players" }) do
        local n, err = delete_rows(t, pid)
        if not n then return false, err end
    end
    return true, string.format("player %d deleted", pid)
end

return M
