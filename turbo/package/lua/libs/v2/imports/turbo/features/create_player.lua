-- Turbo feature: create a new player in the career database, as a copy of a player, from a preset file (Live Editor
-- preset CSV or Turbo player JSON), or blank. Made by Turbo itself: FC 27 Live Editor has no "create player" native.
--   "create_player": {
--     "source": { "playerid": 158023 } | { "file": "C:\\...\\rossi.csv", "row": 0, "preset_playerid": 0 } | { "blank": true },
--     "teamid": 111592,              -- the club (111592 = Free Agents); national teams are refused
--     "jersey": 0,                   -- shirt number (0 = the first free one)
--     "names": { "firstname": "", "surname": "", "commonname": "", "playerjerseyname": "" },   -- "" = keep the source's
--     "set": { "overallrating": 70 },   -- players fields written on top of the source
--     "playerid": 0,                 -- 0 = the first free id above every existing one (below max_playerid)
--     "min_playerid": 0, "max_playerid": 459999,   -- the id range Turbo may use (460000+ is the game's own range)
--   }
-- Any club, your own included (1.1.1; "allow_user_club" is no longer read): the club must have room (fewer than 52
-- players) and a free shirt number; in your club he is added to your team sheet as a reserve.
-- Rows are added with Live Editor's InsertDBTableRow: players (every field: the source's value, else the field's
-- minimum), teamplayerlinks (free shirt number, position 29 = reserve) and editedplayernames when names are given.
-- Everything is validated before the first row is written; a dry run reports the plan.
-- The game's own way (Turbo.dll player_create, op 12, docs/re/created_players.md): when TurboPlayerCreate is defined and the call is
-- on (turbo_output\call_player_create_on.txt, OFF by default), the same row goes through the game's INSERT (indexes, observers),
-- event 0x3A, InsertTeamPlayer into Free Agents and the game's own move into the club (+ the contract record at your club): the
-- squad screens see him at once. No InsertDBTableRow and no team-sheet write then; the shirt number is the game's. When the call
-- answers "off" the database path below runs as before (and the summary says so).

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local moves = require 'imports/turbo/core/moves'
local preset = require 'imports/turbo/core/preset'

local M = {}

M.GENERATED_MIN = 460000   -- the game numbers its own generated players from here

-- The first free player id: above every id below `max_id`, at least `min_id`
function M.free_playerid(min_id, max_id)
    local top = 0
    for _, t in ipairs({ "players", "teamplayerlinks", "editedplayernames", "playerloans" }) do
        local tbl = db.get_table(t)
        if tbl and db.has_field(tbl, "playerid") then
            for rec in db.records(tbl) do
                local pid = tbl:GetRecordFieldValue(rec, "playerid")
                if pid and pid < max_id and pid > top then top = pid end
            end
        end
    end
    local pid = math.max(top + 1, min_id)
    if pid >= max_id then return nil, string.format("no free player id below %d (highest used: %d)", max_id, top) end
    return pid
end

-- Source values: { field = value } for the players table, names { firstname = ... } (may be empty)
local function load_source(players, src)
    src = type(src) == "table" and src or {}
    if src.blank == true then return {}, {}, "blank" end
    if src.playerid ~= nil then
        local pid = util.to_int(src.playerid)
        local rec = pid and db.find(players, "playerid", pid)
        if not rec then return nil, string.format("source player %s not found", tostring(src.playerid)) end
        local vals = {}
        for _, f in ipairs(db.field_names(players)) do vals[f] = players:GetRecordFieldValue(rec, f) end
        local names = {}
        local edited = db.get_table("editedplayernames")
        if edited and db.has_field(edited, "playerid") then
            local erec = db.find(edited, "playerid", pid)
            if erec then
                for _, f in ipairs(preset.NAME_FIELDS) do
                    if db.has_field(edited, f) then names[f] = tostring(edited:GetRecordFieldValue(erec, f) or "") end
                end
            end
        end
        return vals, names, string.format("copy of player %d", pid), {}
    end
    if src.file ~= nil then
        local parsed, perr = preset.load(src.file)
        if not parsed then return nil, perr end
        local row, ridx = preset.pick_row(parsed, src)
        if not row then return nil, ridx end
        local fields, names, skipped = preset.map_row(players, row, { keep_ids = true })
        fields.playerid = nil
        local desc = string.format("%s from %s (row %d of %d)", preset.describe(row), tostring(parsed.path):match("([^\\/]+)$"),
            ridx, parsed.count)
        return fields, names, desc, skipped, parsed
    end
    return nil, "source must be { playerid }, { file } or { blank: true }"
end

local function insert_row(table_name, row)
    local insert = _G["InsertDBTableRow"]
    if type(insert) ~= "function" then return nil, "InsertDBTableRow is not available in this Live Editor build" end
    local srow = {}
    for f, v in pairs(row) do srow[f] = tostring(v) end
    local okc, res = pcall(insert, table_name, srow)
    if not okc or type(res) ~= "table" then return nil, string.format("adding the %s row failed: %s", table_name, tostring(res)) end
    return res
end

-- The payload of TurboPlayerCreate (bridge.lua M.player_create): the players row as integers (a column whose value is not an
-- integer is left to the game's default and counted), the names, the link's form, and the contract for your club
function M.native_payload(players, links, row, name_row, teamid, user)
    local ints, skipped = {}, 0
    for f, v in pairs(row) do
        local info = db.field_info(players, f)
        local iv = (math.type(v) == "integer" and v) or (math.type(v) == "float" and math.tointeger(v)) or nil
        if info and info.type == "int" and iv ~= nil then ints[f] = iv else skipped = skipped + 1 end
    end
    local names
    if name_row then
        names = {}
        for f, v in pairs(name_row) do
            if f ~= "playerid" and type(v) == "string" then names[f] = v end
        end
    end
    local months, wage = 0, 0
    if user > 0 and teamid == user then
        local d = game.current_date()
        local cvu = util.to_int(row.contractvaliduntil)
        local years = (d and cvu) and (cvu - d.year) or 5
        months = math.max(12, math.min(120, years * 12))
        wage = math.max(0, math.min(10000000, util.to_int(row.wage) or 0))
    end
    local link = (links and db.has_field(links, "form")) and { form = 3 } or nil
    return { playerid = row.playerid, team = teamid, months = months, wage = wage, players = ints, names = names, link = link }, skipped
end

local function delete_row(table_name, res)
    local del = _G["DeleteDBTableRowByAddr"]
    local addr = type(res) == "table" and type(res.playerid) == "table" and res.playerid.addr or nil
    if type(del) ~= "function" or not addr then return false end
    local ok, r = pcall(del, table_name, tostring(addr))
    return ok and r ~= false
end

function M.run(ctx)
    local cfg = ctx.cfg
    local players, err = db.get_table("players")
    if not players then return false, err end
    local links = db.get_table("teamplayerlinks")
    if not links or not db.has_fields(links, { "teamid", "playerid", "jerseynumber" }) then
        return false, "teamplayerlinks table not available"
    end

    -- club
    local teamid = util.to_int(cfg.teamid) or moves.FREE_AGENTS
    if not game.team_ids()[teamid] then return false, string.format("team %d not found", teamid) end
    if moves.national_teams()[teamid] then return false, string.format("team %d is a national team", teamid) end
    local user = moves.user_team()
    local oks, serr = moves.check_squads(nil, nil, teamid)
    if not oks then return false, serr end
    -- The database path is not the game's own move (TurboPlayerMove): rows added with InsertDBTableRow are not seen by the
    -- game's own queries until the career is saved and loaded (checked in game 2026-10-05: created at Free Agents, the game's
    -- IsPlayerInTeam answered "not in team 111592"). TurboPlayerCreate (below, when on) inserts through the game's own
    -- query layer instead (docs/re/created_players.md).

    -- id
    local min_id = math.max(0, util.to_int(cfg.min_playerid) or 0)
    local max_id = util.to_int(cfg.max_playerid) or (M.GENERATED_MIN - 1)
    if max_id > M.GENERATED_MIN or max_id < 1000 then return false, "max_playerid must be 1000.." .. M.GENERATED_MIN end
    local pid = util.to_int(cfg.playerid) or 0
    if pid > 0 then
        if game.player_ids()[pid] then return false, string.format("player id %d is already used", pid) end
        if pid >= max_id then return false, string.format("player id %d is not below max_playerid %d", pid, max_id) end
    else
        local free, ferr = M.free_playerid(min_id, max_id)
        if not free then return false, ferr end
        pid = free
    end
    local okp, perr = db.validate(players, "playerid", pid)
    if okp == nil then return false, perr end

    -- values
    local vals, names, desc, skipped, parsed = load_source(players, cfg.source)
    if not vals then return false, names end
    if type(cfg.set) == "table" then
        for f, v in pairs(cfg.set) do
            if not db.has_field(players, f) then return false, "no field " .. tostring(f) .. " in players" end
            if preset.ID_FIELDS[f] then return false, f .. " cannot be set" end
            local ok, verr = db.validate(players, f, v)
            if ok == nil then return false, verr end
            vals[f] = ok
        end
    end
    if type(cfg.names) == "table" then
        for _, f in ipairs(preset.NAME_FIELDS) do
            if type(cfg.names[f]) == "string" and cfg.names[f] ~= "" then names[f] = cfg.names[f] end
        end
    end
    local row, defaults = {}, 0
    for _, f in ipairs(db.field_names(players)) do
        local v = vals[f]
        if f == "playerid" then
            v = pid
        elseif v ~= nil then
            local ok = db.validate(players, f, v)
            if ok == nil then v = nil else v = ok end
        end
        if v == nil then
            local info = db.field_info(players, f)
            if info.type == "int" then v = info.min elseif info.type == "float" then v = 0.0 else v = "" end
            if f ~= "playerid" then defaults = defaults + 1 end
        end
        row[f] = v
    end
    -- head of a player taken from a CMTracker CSV (Turbo's "From CMTracker..."): the real face when CMTracker says the player
    -- has one AND the game has that head (the id is a player the game knows); otherwise a generic head of the new id
    local head_note
    local cm = parsed and parsed.doc and type(parsed.doc.cmtracker) == "table" and parsed.doc.cmtracker or nil
    if cm and db.has_field(players, "headassetid") and db.has_field(players, "headclasscode") then
        local real = util.to_int(cm.real_face_id)
        if real and real > 0 and game.player_ids()[real] then
            row.headassetid, row.headclasscode = real, 0
            head_note = string.format(", real face of player %d", real)
        else
            row.headassetid, row.headclasscode = pid, 1
            head_note = real and ", generic head (the game has no real face for this player)" or nil
        end
    end

    -- join date + contract (moves.contract_values, shared with every move): he joins the club today; the source's
    -- contract is kept when it still runs, else (blank player, a preset of another game or season) a new one is set
    local join, cvu = moves.contract_values(vals.playerjointeamdate, vals.contractvaliduntil, { new_join = true })
    local contract_note
    for f, v in pairs({ playerjointeamdate = join, contractvaliduntil = cvu }) do
        if db.has_field(players, f) then
            local ok, verr = db.validate(players, f, v)
            if ok == nil then return false, verr end
            if f == "contractvaliduntil" and row[f] ~= ok then contract_note = ok end
            row[f] = ok
        end
    end

    -- club link
    local jersey = moves.free_jersey(links, teamid, util.to_int(cfg.jersey) or 0, pid)
    if not jersey then return false, string.format("%s (%d) has no free shirt number", game.team_name(teamid), teamid) end
    local link = {}
    for _, f in ipairs(db.field_names(links)) do
        local info = db.field_info(links, f)
        link[f] = (info.type == "int") and info.min or ((info.type == "float") and 0.0 or "")
    end
    link.teamid, link.playerid, link.jerseynumber = teamid, pid, jersey
    if db.has_field(links, "position") then link.position = moves.RESERVE_POSITION end
    -- form: the game's own links start at 3 (average); the field's minimum (0) showed a new player in poor form
    if db.has_field(links, "form") then link.form = 3 end
    if db.has_field(links, "artificialkey") then
        local top = 0
        for rec in db.records(links) do
            local k = links:GetRecordFieldValue(rec, "artificialkey")
            if k and k > top then top = k end
        end
        link.artificialkey = top + 1
    end
    for f, v in pairs(link) do
        local ok, verr = db.validate(links, f, v)
        if ok == nil then return false, "teamplayerlinks." .. verr end
        link[f] = ok
    end

    -- names
    local name_row
    local any_name = false
    for _, f in ipairs(preset.NAME_FIELDS) do if (names[f] or "") ~= "" then any_name = true end end
    if any_name then
        local edited = db.get_table("editedplayernames")
        if not edited or not db.has_field(edited, "playerid") then return false, "editedplayernames table not available" end
        name_row = { playerid = pid }
        for _, f in ipairs(preset.NAME_FIELDS) do
            if db.has_field(edited, f) then
                local ok, verr = db.validate(edited, f, names[f] or "")
                if ok == nil then return false, "editedplayernames." .. verr end
                name_row[f] = ok
            end
        end
    end

    local shown = names.commonname ~= nil and names.commonname ~= "" and names.commonname
        or util.trim((names.firstname or "") .. " " .. (names.surname or ""))
    local summary = string.format("new player %d%s at %s (%d): %s, shirt %d, position reserve%s%s", pid,
        shown ~= "" and (" " .. shown) or "", game.team_name(teamid), teamid, desc, jersey,
        defaults > 0 and string.format(", %d fields at their minimum", defaults) or "",
        name_row and ", names in editedplayernames" or "")
    if head_note then summary = summary .. head_note end
    summary = summary .. string.format(", joined today%s", contract_note and (", contract until " .. contract_note) or "")
    if skipped and #skipped > 0 then summary = summary .. "; out of range, minimum used: " .. table.concat(skipped, ", ") end
    if user > 0 and teamid == user then summary = summary .. " (your club: added to your team sheet as a reserve)" end
    if ctx.dry then return true, summary end

    -- miniface from a Turbo JSON next to the file (both paths)
    local function miniface()
        if parsed and parsed.kind == "turbo_json" and type(parsed.miniface) == "string" and parsed.miniface ~= "" then
            local pp = require 'imports/turbo/features/player_presets'
            local src = util.join(parsed.dir, parsed.miniface)
            if util.file_exists(src) then
                local okm = pp.install_miniface(pid, src, false)
                summary = summary .. (okm and ", miniface installed" or ", miniface not installed")
            end
        end
    end

    -- the game's own way (Turbo.dll player_create): the squad screens see him at once
    local create = _G["TurboPlayerCreate"]
    local native_note
    if type(create) == "function" then
        local payload, skipped_cols = M.native_payload(players, links, row, name_row, teamid, user)
        local okc, ok, text, status = pcall(create, 1, payload)
        if not okc then ok, text, status = false, tostring(ok), "failed" end
        if status == "off" then
            native_note = "; " .. tostring(text) .. ": added through the database (the squad screens show him after a save and a load)"
        elseif ok then
            summary = summary:gsub(", shirt %d+", ", shirt chosen by the game")
            summary = summary:gsub(" %(your club: added to your team sheet as a reserve%)", " (your club: the game's own move adds him)")
            summary = summary .. "; created by the game" .. (status == "queued" and " (queued: " or " (") .. tostring(text) .. ")"
            if skipped_cols > 0 then summary = summary .. string.format("; %d non-integer field(s) left to the game's default", skipped_cols) end
            if user > 0 and teamid == user then
                local state = moves.refresh_plan(pid)
                if state == "plan" then summary = summary .. ", development plan set" end
            end
            miniface()
            return true, summary
        else
            return false, "the game did not create the player: " .. tostring(text)
        end
    end

    -- write: players, then the club link, then the names; a later failure removes what was added
    local pres, ierr = insert_row("players", row)
    if not pres then return false, ierr end
    if not db.find(players, "playerid", pid) then return false, "the players row was not added" end
    local lres, lerr = insert_row("teamplayerlinks", link)
    if not lres or not db.find(links, "playerid", pid) then
        delete_row("players", pres)
        return false, lerr or "the teamplayerlinks row was not added"
    end
    if name_row then
        local nres, nerr = insert_row("editedplayernames", name_row)
        if not nres then
            delete_row("teamplayerlinks", lres)
            delete_row("players", pres)
            return false, nerr
        end
    end
    if user > 0 and teamid == user then
        local oks2, sherr = moves.add_to_sheet(teamid, pid)
        if not oks2 then summary = summary .. "; team sheet not changed: " .. tostring(sherr) end
        local state = moves.refresh_plan(pid)   -- his development plan (if the game made one) holds the table's attributes
        if state == "plan" then summary = summary .. ", development plan set" end
    end
    miniface()
    if native_note then summary = summary .. native_note end
    return true, summary
end

return M
