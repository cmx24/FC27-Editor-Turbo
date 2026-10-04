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
-- SAFETY (1.1.1): the same path for every club, your own included (docs/turbo-reference.md "Player moves"). Before
-- anything is written each move checks what keeps the career consistent:
--   * squads: the club he joins has fewer than 52 players (the team sheet's 52 slots); a club with a match squad (18 or
--     more) never drops below 18; a club's only goalkeeper does not leave it;
--   * shirt numbers: a number free at the new club, else the move is refused;
--   * loans: a transfer or release of a loaned player ends the loan first (his parent club is the seller); a player on
--     loan cannot be loaned again; a free agent cannot be loaned (no club to loan him from);
--   * your club: your team sheet loses or gains him, and a player on your transfer / loan list comes off it through the
--     game's own remove (Turbo.dll) before he leaves, so the Transfer Hub never lists a player you no longer have.
-- These moves are database writes: the squad screens show them after the career is saved and loaded again.
-- (0.3.0 to 1.1.0 refused every move into or out of your club after a test career crashed while simulating
-- following several such moves, 03-10-2026; that career had no squad or list checks.)

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

-- Needs a loaded career (your club known): outside one Turbo makes no move
function M.guard(from_team, to_team)
    local user = M.user_team()
    if user <= 0 then return false, "your club is not known (load a career first)" end
    return true
end

M.MAX_SQUAD = 52   -- the most players a club holds (the team sheet's 52 slots, cm_teamsheets playerid0..51)
M.MIN_SQUAD = 18   -- a match squad: 11 starters + 7 substitutes

-- Players linked to a club (teamplayerlinks rows of that team; loaned-in players count)
function M.squad_size(teamid)
    local links = db.get_table("teamplayerlinks")
    if not links then return 0 end
    local n = 0
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid then n = n + 1 end
    end
    return n
end

-- True when pid is a goalkeeper (preferredposition1 0) and no other player of `teamid` is one
function M.is_last_keeper(pid, teamid)
    local players = db.get_table("players")
    if not players or not db.has_field(players, "preferredposition1") then return false end
    local prec = db.find(players, "playerid", pid)
    if not prec or players:GetRecordFieldValue(prec, "preferredposition1") ~= 0 then return false end
    local links = db.get_table("teamplayerlinks")
    local mates = {}
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid then
            local other = links:GetRecordFieldValue(rec, "playerid")
            if other ~= pid then mates[other] = true end
        end
    end
    for rec in db.records(players) do
        if mates[players:GetRecordFieldValue(rec, "playerid")] and players:GetRecordFieldValue(rec, "preferredposition1") == 0 then
            return false
        end
    end
    return true
end

-- The squad rules of a move of pid from `from` to `to` (Free Agents has no limits). opts.skip_min: the minimum squad
-- and the goalkeeper rule are not checked (the end of a loan: the game ends loans whatever the squad)
function M.check_squads(pid, from, to, opts)
    opts = opts or {}
    if to and to ~= M.FREE_AGENTS then
        local n = M.squad_size(to)
        if n >= M.MAX_SQUAD then
            return false, string.format("%s (%d) has %d players, the most a squad holds", game.team_name(to), to, n)
        end
    end
    if from and from ~= M.FREE_AGENTS and not opts.skip_min then
        local n = M.squad_size(from)
        if n >= M.MIN_SQUAD and n - 1 < M.MIN_SQUAD then
            return false, string.format("%s (%d) would have %d players, fewer than the %d a match squad needs",
                game.team_name(from), from, n - 1, M.MIN_SQUAD)
        end
        if M.is_last_keeper(pid, from) then
            return false, string.format("player %d is the only goalkeeper of %s (%d)", pid, game.team_name(from), from)
        end
    end
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

-- A shirt number not used at the club (prefers `wanted`), 1..99; nil when every number is taken
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
    if not used[1] then return 1 end
    return nil
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

M.STARTERS = 11   -- team sheet slots playerid0..playerid10 are the starting XI (playerid0 the goalkeeper)

local function is_keeper(pid)
    local players = db.get_table("players")
    if not players or not db.has_field(players, "preferredposition1") then return false end
    local prec = db.find(players, "playerid", pid)
    return prec ~= nil and players:GetRecordFieldValue(prec, "preferredposition1") == 0
end

-- Remove pid from the user's team sheet (cm_teamsheets of `teamid`). A starter's slot is taken by the first
-- substitute (for the goalkeeper's slot, the first goalkeeper on the bench or in reserve when there is one), so the
-- other starters keep their positions; the bench and reserves after him move up. Set-piece takers that were him
-- become the first player of the sheet.
local function plan_sheet_remove(plan, teamid, pid)
    local sheets = db.get_table("cm_teamsheets")
    if not sheets or not db.has_field(sheets, "teamid") then return true end
    for rec in db.records(sheets) do
        if sheets:GetRecordFieldValue(rec, "teamid") == teamid then
            local ids, at = {}, nil
            for i = 0, 51 do
                local f = "playerid" .. i
                if db.has_field(sheets, f) then
                    local v = sheets:GetRecordFieldValue(rec, f)
                    if v == nil or v == -1 then break end
                    ids[#ids + 1] = v
                    if v == pid and not at then at = #ids end
                end
            end
            if at then
                if at <= M.STARTERS and #ids > M.STARTERS then
                    local pick = M.STARTERS + 1
                    if at == 1 then
                        for k = M.STARTERS + 1, #ids do
                            if is_keeper(ids[k]) then pick = k; break end
                        end
                    end
                    ids[at] = ids[pick]
                    table.remove(ids, pick)
                else
                    table.remove(ids, at)
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

-- Move the club link of pid to `to_team`. opts: contract = { months, wage, release_clause } (nil = keep contract),
-- skip_min (see M.check_squads)
local function plan_move(plan, pid, to_team, opts)
    local rec, from_or_err, links = M.club_link(pid)
    if not rec then return nil, from_or_err end
    local from = from_or_err
    local okg, gerr = M.guard(from, to_team)
    if not okg then return nil, gerr end
    if from == to_team then return nil, string.format("player %d is already at team %d", pid, to_team) end
    local oks, serr = M.check_squads(pid, from, to_team, { skip_min = opts.skip_min })
    if not oks then return nil, serr end
    local jersey = M.free_jersey(links, to_team, links:GetRecordFieldValue(rec, "jerseynumber"), pid)
    if not jersey then return nil, string.format("%s (%d) has no free shirt number", game.team_name(to_team), to_team) end
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

-- ---------------------------------------------------------------- transfer / loan lists (the game's own helper)
-- Unlike the moves above these are not database writes: Turbo.dll calls the game's own UserActionsHandlingHelperImpl
-- (what the Transfer Hub's "Add to transfer list" / "Add to loan list" / "Remove from list" run) on the game thread
-- (turbogui/src/core/transfer_list.h, docs/re/transfer_lists.md), so the Transfer Hub lists, the news and the
-- USER_TRANSFERLISTED / USER_LOANLISTED events all come from the game. bridge.install_natives defines
-- TurboTransferList(action, playerid, club) once Turbo.dll's game-call export is found.
-- The game lists on YOUR club whoever the player is, so only your own players under contract are accepted (checked
-- here and again in the DLL against the game's UserManager). FC 27 sets no asking price when listing (AI clubs make
-- offers, you negotiate) and the game's remove always takes the player off both lists.
M.LIST_ACTION = { transfer_list = 1, loan_list = 2, unlist = 3, unlist_transfer = 4, unlist_loan = 5, list_status = 6 }
M.LIST_LABEL = { transfer_list = "add to the transfer list", loan_list = "add to the loan list", unlist = "remove from the lists",
    unlist_transfer = "remove from the transfer list", unlist_loan = "remove from the loan list", list_status = "list status" }
M.LIST_STATUS_NAME = { [0] = "not listed", [7] = "transfer listed", [8] = "loan listed", [9] = "transfer and loan listed" }

function M.list_native()
    local f = _G["TurboTransferList"]
    if type(f) == "function" then return f end
    return nil
end

M.LIST_NATIVE_MISSING = "the transfer / loan lists are a Turbo.dll game call: start the Turbo GUI in a career (its " ..
    "game-call export defines TurboTransferList)"

-- One list action for one player. opts: { teamid = his club (0 / nil = from teamplayerlinks), dry = true }.
-- Returns ok, message, status ("ok" | "queued" | "failed" | "dry"), status before, status after (contract status:
-- 0 not listed, 7 transfer listed, 8 loan listed, 9 both; only with "ok").
function M.list(pid, action, opts)
    opts = opts or {}
    local code = M.LIST_ACTION[action]
    if not code then return false, "unknown list action " .. tostring(action), "failed" end
    local native = M.list_native()
    if not native then return false, M.LIST_NATIVE_MISSING, "failed" end
    pid = util.to_int(pid)
    if not pid or pid <= 0 then return false, "player id must be a positive whole number", "failed" end
    local club = util.to_int(opts.teamid) or 0
    if club <= 0 then
        local rec, tid = M.club_link(pid)
        club = (rec and tid) or 0
    end
    if action ~= "list_status" then
        local user = M.user_team()
        if user <= 0 then return false, "your club is not known (load a career first)", "failed" end
        if club <= 0 then return false, string.format("player %d has no club link in teamplayerlinks", pid), "failed" end
        if club ~= user then
            return false, string.format("player %d plays for team %d, not your club (%d): the game lists only your own players",
                pid, club, user), "failed"
        end
        if M.loan_row(pid) then
            return false, string.format("player %d is on loan (playerloans): the game lists only players under contract " ..
                "at your club", pid), "failed"
        end
    end
    if opts.dry then return true, string.format("player %d: %s (dry run)", pid, M.LIST_LABEL[action]), "dry" end
    local okc, ok, text, status, before, after = pcall(native, code, pid, club)
    if not okc then return false, "game call error: " .. tostring(ok), "failed" end
    if ok and status == "queued" then
        return true, string.format("player %d: %s queued for the game thread (%s)", pid, M.LIST_LABEL[action], tostring(text)), status
    end
    return ok == true, tostring(text or ""), status or (ok and "ok" or "failed"), before, after
end

-- Live Editor's own Lua API on top of the game call: FC 27 LE v27.1.2 keeps the FC 26 wrappers (AddPlayerToTransferList
-- -> cAddPlayerToTransferList, ...) but not the natives; the missing natives are defined here, so the wrappers (and
-- scripts written for FC 26 Live Editor) work again. A native Live Editor ships itself is never replaced.
-- Returns the names defined.
function M.install_le_natives()
    local function act(action)
        return function(pid, teamid)
            local ok, msg = M.list(pid, action, { teamid = teamid })
            return ok, msg
        end
    end
    local function is(listed)
        return function(pid, teamid)
            local ok, _, status, before = M.list(pid, "list_status", { teamid = teamid })
            if not ok or status ~= "ok" then return false end
            return listed(math.tointeger(before) or -1)
        end
    end
    local defs = {
        cAddPlayerToTransferList = act("transfer_list"),
        cAddPlayerToLoanList = act("loan_list"),
        cRemovePlayerFromLists = act("unlist"),
        cRemovePlayerFromTransferList = act("unlist_transfer"),
        cRemovePlayerFromLoanList = act("unlist_loan"),
        cIsPlayerTransferListed = is(function(s) return s == 7 or s == 9 end),
        cIsPlayerLoanListed = is(function(s) return s == 8 or s == 9 end),
    }
    local names = {}
    for name, fn in pairs(defs) do
        if type(_G[name]) ~= "function" then
            _G[name] = fn
            names[#names + 1] = name
        end
    end
    table.sort(names)
    return names
end

-- ---------------------------------------------------------------- the moves (any club, your own included)

-- Before a player leaves your club (transfer, loan, release, delete) he comes off your transfer / loan list through the
-- game's own remove, so the Transfer Hub lists and the AI clubs' offers never point at a player your club no longer
-- has. Returns ok, note (note = what was done, for the summary). One of your players is moved only when the game
-- answered his list status ("ok") and, when he was listed, its remove answered "ok" with him off both lists: without
-- the Turbo GUI's game call, with another game call still pending, with an answer "queued" for later or any other
-- failure the move is refused (false, reason) and nothing is written. Other clubs' players are never on your lists.
local function lists_unchecked(pid, why)
    return false, string.format("player %d not moved: could not check your transfer / loan list right now (%s); " ..
        "nothing was changed, try again in a moment", pid, why)
end

function M.leave_lists(pid, from)
    local user = M.user_team()
    if user <= 0 or from ~= user or M.loan_row(pid) then return true end   -- loaned-in players are never on your lists
    if not M.list_native() then
        return false, string.format("player %d not moved: could not check your transfer / loan list (%s); nothing was " ..
            "changed", pid, M.LIST_NATIVE_MISSING)
    end
    local ok, msg, status, before = M.list(pid, "list_status", { teamid = from })
    before = math.tointeger(before)
    if status == "queued" then return lists_unchecked(pid, "the game queued the status read instead of answering") end
    if not ok or status ~= "ok" then
        -- bridge.game_call: "game call #N (...) is still queued" while an earlier call waits for the game thread
        if tostring(msg):find("still queued", 1, true) then return lists_unchecked(pid, "another game call is running") end
        return lists_unchecked(pid, tostring(msg))
    end
    if not before then return lists_unchecked(pid, "the game gave no list status") end
    if before ~= 7 and before ~= 8 and before ~= 9 then return true end
    local name = M.LIST_STATUS_NAME[before] or "listed"
    local ok2, msg2, status2, _, after = M.list(pid, "unlist", { teamid = from })
    after = math.tointeger(after)
    if status2 == "queued" then
        return false, string.format("player %d not moved: he is %s and the game's remove is queued for the next game " ..
            "tick, not done; nothing was changed, try again in a moment", pid, name)
    end
    if not ok2 or status2 ~= "ok" then
        return false, string.format("player %d is %s and the game's remove failed: %s", pid, name, tostring(msg2))
    end
    if after == nil or after == 7 or after == 8 or after == 9 then
        return false, string.format("player %d not moved: he is %s and after the game's remove he is still %s; " ..
            "nothing was changed", pid, name, after and (M.LIST_STATUS_NAME[after] or "listed") or "of unknown list status")
    end
    return true, "taken off your transfer / loan list first"
end

-- Live Editor's row delete ends a loan (his playerloans row goes): checked before the first write of a move that ends
-- one, so a build without it refuses instead of moving him and leaving the old loan row behind
local function can_delete_rows()
    if type(_G["DeleteDBTableRowByAddr"]) ~= "function" then
        return false, "DeleteDBTableRowByAddr is not available in this Live Editor build"
    end
    return true
end

local function with_note(text, note) return note and (text .. " (" .. note .. ")") or text end

-- Runs a planned move: your lists first, then the database writes, then (ending a loan) his playerloans row
local function run_move(plan, pid, from, end_loan)
    if end_loan then
        local okd, derr = can_delete_rows()
        if not okd then return false, derr end
    end
    local okl, note = M.leave_lists(pid, from)
    if not okl then return false, note end
    local ok, err = apply(plan, false)
    if not ok then return false, err end
    if end_loan then
        local n, derr = delete_rows("playerloans", pid)
        if not n then return false, derr end
    end
    return true, note
end

-- Returns true, summary or false, error. A loaned player: the loan ends (his parent club sells him).
function M.transfer(pid, to_team, o, dry)
    o = o or {}
    local ok, err = check_team(to_team)
    if not ok then return false, err end
    local lrec, loans = M.loan_row(pid)
    local parent = lrec and loans:GetRecordFieldValue(lrec, "teamidloanedfrom") or nil
    local plan = {}
    local from, merr = plan_move(plan, pid, to_team, { contract = {
        months = o.months or 36, wage = o.wage, release_clause = o.release_clause } })
    if not from then return false, merr end
    local text = string.format("player %d: team %d -> %d", pid, from, to_team)
    if parent then text = text .. string.format(", loan from %d ended", parent) end
    if dry then return true, text end
    local okm, note = run_move(plan, pid, from, parent ~= nil)
    if not okm then return false, note end
    return true, with_note(text, note)
end

-- Release to Free Agents (wage and release clause 0). A loaned player: the loan ends and his parent club releases him.
function M.release(pid, dry)
    local lrec, loans = M.loan_row(pid)
    local parent = lrec and loans:GetRecordFieldValue(lrec, "teamidloanedfrom") or nil
    local plan = {}
    local from, merr = plan_move(plan, pid, M.FREE_AGENTS, { contract = { wage = 0, release_clause = 0 } })
    if not from then return false, merr end
    local text = string.format("player %d released from team %d", pid, from)
    if parent then text = text .. string.format(", loan from %d ended", parent) end
    if dry then return true, text end
    local okm, note = run_move(plan, pid, from, parent ~= nil)
    if not okm then return false, note end
    return true, with_note(text, note)
end

function M.loan(pid, to_team, months, dry)
    local ok, err = check_team(to_team)
    if not ok then return false, err end
    if M.loan_row(pid) then
        return false, string.format("player %d is already on loan: end that loan first (Terminate loan)", pid)
    end
    if to_team == M.FREE_AGENTS then return false, "Free Agents is not a club: pick the club he goes to" end
    if M.team_of_player(pid) == M.FREE_AGENTS then
        return false, string.format("player %d is a free agent: no club can loan him out (transfer him instead)", pid)
    end
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
    local text = string.format("player %d: loan %d -> %d for %d months", pid, from, to_team, months)
    if dry then return true, text end
    local insert = _G["InsertDBTableRow"]
    if type(insert) ~= "function" then return false, "InsertDBTableRow is not available in this Live Editor build" end
    -- the row delete takes the new playerloans row back out when the club move fails after the insert
    local okd, derr = can_delete_rows()
    if not okd then return false, derr end
    local okl, note = M.leave_lists(pid, from)
    if not okl then return false, note end
    -- InsertDBTableRow returns the new row (DOC.MD: DBRow, each field { value, addr }); the row is then looked up in
    -- the table itself, so a call that returned something but added nothing is caught (and a row it did add goes)
    local okc, res = pcall(insert, "playerloans", row)
    if not okc or type(res) ~= "table" or not M.loan_row(pid) then
        if M.loan_row(pid) then delete_rows("playerloans", pid) end
        return false, "could not add the playerloans row: " .. tostring(res)
    end
    ok, err = apply(plan, false)
    if not ok then
        -- he was not on loan before (checked above): the new playerloans row goes, so no loan row without the move
        local n, rerr = delete_rows("playerloans", pid)
        if not n then err = tostring(err) .. "; removing the new playerloans row also failed: " .. tostring(rerr) end
        return false, err
    end
    return true, with_note(text, note)
end

-- The loan ends: he goes back to his parent club (playerloans.teamidloanedfrom), the playerloans row is deleted. The
-- loan club's minimum squad is not checked (the game ends loans whatever the squad); the parent club must have room.
function M.terminate_loan(pid, dry)
    local lrec, loans = M.loan_row(pid)
    if not lrec then return false, string.format("player %d is not on loan", pid) end
    local back = loans:GetRecordFieldValue(lrec, "teamidloanedfrom")
    local plan = {}
    local from, merr = plan_move(plan, pid, back, { skip_min = true })
    if not from then return false, merr end
    local text = string.format("player %d: loan at %d ended, back to %d", pid, from, back)
    if dry then return true, text end
    local okd, derr = can_delete_rows()
    if not okd then return false, derr end
    local ok, err = apply(plan, false)
    if not ok then return false, err end
    local n, derr = delete_rows("playerloans", pid)
    if not n then return false, derr end
    return true, text
end

-- Release (with every check above), then his players / teamplayerlinks / editedplayernames / playerloans rows go
function M.delete(pid, dry)
    local players = db.get_table("players")
    if not players or not db.find(players, "playerid", pid) then return false, string.format("player %d not found", pid) end
    local okg, gerr = M.guard()
    if not okg then return false, gerr end
    if not dry then
        local okd, derr = can_delete_rows()
        if not okd then return false, derr end
    end
    local _, tid = M.club_link(pid)
    if tid and tid ~= M.FREE_AGENTS then
        local ok, err = M.release(pid, dry)
        if not ok then return false, err end
    end
    if dry then return true, string.format("player %d would be deleted", pid) end
    for _, t in ipairs({ "teamplayerlinks", "editedplayernames", "playerloans", "players" }) do
        local n, err = delete_rows(t, pid)
        if not n then return false, err end
    end
    return true, string.format("player %d deleted", pid)
end

-- Adds pid at the end of the team sheet of `teamid` when that club has one (your club): a player Turbo creates there
function M.add_to_sheet(teamid, pid)
    local plan = {}
    local ok, err = plan_sheet_add(plan, teamid, pid)
    if not ok then return false, err end
    return apply(plan, false)
end

return M
