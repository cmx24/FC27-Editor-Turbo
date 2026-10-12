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
--     "id_assets": { "head3d": true, "headvariation": 0, "callname": true, "miniface": false },   -- optional, see below
--   }
-- A typed playerid (2.3, the GUI's Create player > Player ID box, core/player_id.h) is checked before anything is written
-- (M.check_typed_id): a whole number from 1 to max_playerid, not the player-career player's id, no row in any of M.ID_TABLES,
-- no entry in your club's squad status table (PlayerStatusManager) when it can be read. Kill switch
-- turbo_output\create_player_typed_id_off.txt: typed ids refused (automatic ids as before). "id_assets" = what the GUI's
-- current asset catalog says the game files ship for that id; with "head3d" the new player gets that 3D head
-- (headassetid = his id, headclasscode 0, hashighqualityhead 1, headvariation) when the asset gate (core/asset_gate.lua)
-- is current and lists it, else nothing is written; his callname recording follows his id by itself. The chosen id is
-- always logged ("create_player: player id ...").
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
-- Your club (after 2.0.3): right after the create (both ways) the match crash guard's repair gives him a squad status entry at once
-- and the summary / log say whether he has one (kill switches create_status_repair_off.txt, squad_status_repair_off.txt).

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local moves = require 'imports/turbo/core/moves'
local preset = require 'imports/turbo/core/preset'
local pnames = require 'imports/turbo/core/names'
local log = require 'imports/turbo/core/log'
local env = require 'imports/turbo/core/env'

local M = {}

M.GENERATED_MIN = 460000   -- the game numbers its own generated players from here
M.PLAYER_CAREER_ID = 31399 -- the player-career player (Turbo.dll's player_create refuses his id too)
M.TYPED_ID_KILL_FILE = "create_player_typed_id_off.txt"
-- Tables a typed id must not be in: a row there means a player of this career has or had the id (the GUI's Player ID box
-- checks the same list, turbogui/src/core/player_id.h owner_tables). Tables this database lacks are skipped.
M.ID_TABLES = { "players", "editedplayernames", "teamplayerlinks", "playerloans", "transfers", "career_youthplayers",
    "career_youthplayerhistory", "career_playergrowthuserseason", "career_playerawards", "career_playermatchratinghistory",
    "career_teamofweek", "career_playercontract" }

-- The first free player id: above every id below `max_id`, at least `min_id`; ids in `skip` (a set: ids that carry a game
-- head, miniface or callname recording, from a current asset gate) are passed over
function M.free_playerid(min_id, max_id, skip)
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
    while skip and skip[pid] and pid < max_id do pid = pid + 1 end
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
        -- a file of the old export holds only the shown name (as a common name): its own name ids give the real names
        names = pnames.from_file(names, row, {})
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

-- A player created at your club gets his squad status entry (PlayerStatusManager) at once (after 2.0.3): the match crash guard's
-- repair (squad_role.repair_entries, called here directly; bridge.lua's guard is unchanged) runs right after the create instead
-- of at the next career event; a club player without one crashed the match load (2026-10-11). Kill switches in turbo_output:
-- create_status_repair_off.txt (this step only) and squad_status_repair_off.txt (the whole guard). Always logged. Returns the
-- summary's note.
M.STATUS_REPAIR_KILL_FILE = "squad_status_repair_off.txt"
M.CREATE_STATUS_REPAIR_KILL_FILE = "create_status_repair_off.txt"

local function out_switch(name)
    local root = env.le_root()
    return root ~= nil and util.file_exists(util.join(util.join(root, "turbo_output"), name))
end

-- A typed player id, before anything is written: nil when the id may be used, else why not. `note` (second result) says
-- what could not be checked (the squad status table outside a career, ...).
function M.check_typed_id(pid, max_id)
    max_id = math.min(max_id or (M.GENERATED_MIN - 1), M.GENERATED_MIN - 1)
    if out_switch(M.TYPED_ID_KILL_FILE) then
        return string.format("typed player ids are switched off (turbo_output\\%s): leave the id empty (0) for an automatic one",
            M.TYPED_ID_KILL_FILE)
    end
    if math.type(pid) ~= "integer" then return string.format("player id %s is not a whole number", tostring(pid)) end
    if pid < 1 or pid > max_id then
        return string.format("player id %d is out of range: 1 to %d (%d and up is the game's own range for the players it generates)",
            pid, max_id, M.GENERATED_MIN)
    end
    if pid == M.PLAYER_CAREER_ID then return string.format("player id %d is the player-career player's id: not used for a new player", pid) end
    local used = {}
    for _, t in ipairs(M.ID_TABLES) do
        local tbl = db.get_table(t)
        if tbl and db.has_field(tbl, "playerid") and db.find(tbl, "playerid", pid) then used[#used + 1] = t end
    end
    if #used > 0 then
        return string.format("player id %d is already used: %s %s for it (a player of this career has or had this id)", pid,
            table.concat(used, ", "), #used == 1 and "has a row" or "have rows")
    end
    -- the game's squad status table of your club: an entry there belongs to a player of your club (the match crash guard
    -- would treat the new player as that one)
    local okr, st, serr = pcall(function() return (require 'imports/turbo/features/squad_role').psm_state(game.user_team_id()) end)
    if okr and type(st) == "table" and type(st.pids) == "table" then
        if st.pids[pid] then
            return string.format("player id %d already has an entry in your club's squad status table (PlayerStatusManager): " ..
                "a player of your club had this id", pid)
        end
        return nil
    end
    return nil, "squad status table not checked: " .. tostring(okr and serr or st)
end

local function status_repair()
    local role = require 'imports/turbo/features/squad_role'
    -- the role by age reads the players table: only built when a player is missing an entry (fallback when the game gives none)
    local by_age = nil
    local function role_of(p)
        if by_age == nil then
            local ok, f = pcall(function()
                return (require 'imports/turbo/features/team_mass').role_by_age(role.club_players(game.user_team_id()))
            end)
            by_age = ok and f or false
        end
        return by_age and by_age(p) or nil
    end
    return role.repair_entries(role_of, false, { prefer_game_role = true })
end

local function status_entry(pid)
    local later = "the match crash guard adds it at the next career load, day or match build-up"
    local note
    if out_switch(M.STATUS_REPAIR_KILL_FILE) then
        note = string.format("squad status entry not checked: the match crash guard is off (kill switch turbo_output\\%s)",
            M.STATUS_REPAIR_KILL_FILE)
        log.info("match crash guard after create_player (player %d): %s", pid, note)
        return note
    end
    if out_switch(M.CREATE_STATUS_REPAIR_KILL_FILE) then
        note = string.format("squad status entry not checked now (kill switch turbo_output\\%s); %s",
            M.CREATE_STATUS_REPAIR_KILL_FILE, later)
        log.info("match crash guard after create_player (player %d): %s", pid, note)
        return note
    end
    local okc, added, text, left, created = pcall(status_repair)
    if not okc then
        note = string.format("squad status entry not added now: the repair failed (%s); %s", tostring(added), later)
        log.warn("match crash guard after create_player (player %d): %s", pid, note)
        return note
    end
    -- read back from the game's table (nil when it cannot be read): the note never claims an entry the table does not show
    local okr, st = pcall(function() return (require 'imports/turbo/features/squad_role').psm_state(game.user_team_id()) end)
    local has = nil
    if okr and type(st) == "table" and type(st.pids) == "table" then has = st.pids[pid] ~= nil end
    local made = type(created) == "table" and created[pid] or nil
    if added == nil then
        note = string.format("squad status entry not added now (%s); %s", tostring(text), later)
    elseif made and has ~= false then
        note = string.format("squad status entry added at once (role %s, %s)", tostring(made.role),
            made.via == "game" and "the game's AddPlayer" or "in memory")
    elseif made then
        note = string.format("squad status entry reported added but not found when the table was read back; %s", later)
    elseif has == true or (has == nil and (left or 0) == 0) then
        note = "squad status entry present"
    elseif (left or 0) == 0 then
        note = string.format("squad status entry missing: he is not among your club's player links yet; %s", later)
    else
        note = string.format("squad status entry not added now (%s); %s", tostring(text), later)
    end
    log.info("match crash guard after create_player (player %d): %s (%s)", pid, note, tostring(text))
    return note
end

local function delete_row(table_name, res)
    local del = _G["DeleteDBTableRowByAddr"]
    local addr = type(res) == "table" and type(res.playerid) == "table" and res.playerid.addr or nil
    if type(del) ~= "function" or not addr then return false end
    local ok, r = pcall(del, table_name, tostring(addr))
    return ok and r ~= false
end

-- One log line per CMTracker real-face decision (asset gate checked or unchecked)
local function log_gate(pid, real, ok, why)
    local log = require 'imports/turbo/core/log'
    log.info("create_player %d: real face %d %s (%s)", pid, real, ok and "given" or "refused", tostring(why))
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
    -- 0 / absent = automatic; anything else is a typed id and must be a whole number (2.3: a typed text, a fraction or a
    -- negative number no longer falls back to an automatic id)
    local typed = cfg.playerid
    local pid = 0
    if typed ~= nil and typed ~= 0 and typed ~= "" then
        pid = util.to_int(typed)
        if not pid then
            return false, string.format("player id %s is not a whole number (0 or none = the first free id)", tostring(typed))
        end
    end
    local id_typed = pid ~= 0
    local id_assets = (id_typed and type(cfg.id_assets) == "table") and cfg.id_assets or nil
    -- asset gate (2.1, core/asset_gate.lua): with a current gate, a new player never gets an id that carries another
    -- player's game head, miniface or callname recording (unless it is his own CMTracker real face id, checked below, or
    -- the id was chosen for that head: id_assets.head3d)
    local agate = require 'imports/turbo/core/asset_gate'
    local gate = agate.load()
    local taken = agate.taken_ids(gate)
    local typed_taken = nil
    local id_how
    if id_typed then
        local why, note = M.check_typed_id(pid, max_id)
        if why then return false, why .. " (nothing was written)" end
        typed_taken = taken[pid]
        id_how = "typed" .. (note and ("; " .. note) or "")
    else
        local free, ferr = M.free_playerid(min_id, max_id, taken)
        if not free then return false, ferr end
        pid = free
        id_how = string.format("automatic: the first free id above every existing one, below %d", max_id + 1)
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
        local given = {}
        for _, f in ipairs(preset.NAME_FIELDS) do
            if type(cfg.names[f]) == "string" and cfg.names[f] ~= "" then given[f] = cfg.names[f] end
        end
        if next(given) then
            -- a source without a name row shows the names of its ids: they are the base, so a new surname alone does
            -- not leave the copy without a first name
            local has = false
            for _, f in ipairs(preset.NAME_FIELDS) do if (names[f] or "") ~= "" then has = true end end
            if not has then
                local base = pnames.names_of_ids(vals, {})
                if base then for f, v in pairs(base) do names[f] = v end end
            end
            -- the shirt follows the names given (their surname, else their common name), unless a shirt name is given
            if not given.playerjerseyname and (given.surname or given.commonname) then
                names.playerjerseyname = given.surname or given.commonname
            end
            for f, v in pairs(given) do names[f] = v end
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
    -- has one AND the game has that head; otherwise a generic head of the new id. The proof (core/asset_gate.lua): a current
    -- asset gate lists the head (the game files ship it), else the DB row of that player has headclasscode 0 and his own
    -- head (2.1: "a player the game knows" was not enough, a generic-head player gave a class-0 head with no 3D model)
    local head_note, head_log
    local cm = parsed and parsed.doc and type(parsed.doc.cmtracker) == "table" and parsed.doc.cmtracker or nil
    local real_id = cm and util.to_int(cm.real_face_id) or nil
    local want_head = id_assets ~= nil and id_assets.head3d == true
    if typed_taken and real_id ~= pid and not want_head then
        return false, string.format("player id %d carries a game %s of another player (asset catalog %s): choose another id " ..
            "or 0 (the Turbo GUI's Create player > Player ID box gives a new player that head)", pid,
            typed_taken == "head3d" and "head" or typed_taken, gate.build or "?")
    end
    if cm and db.has_field(players, "headassetid") and db.has_field(players, "headclasscode") then
        local real = real_id
        local okh, why, hidden = false, nil, false
        if real and real > 0 then okh, why, hidden = agate.check_head(gate, real, nil, true) end
        if okh then
            row.headassetid, row.headclasscode = real, 0
            head_note = string.format(", real face of player %d", real)
            if hidden then head_log = real end
        else
            row.headassetid, row.headclasscode = pid, 1
            head_note = real and string.format(", generic head (the game has no real face for this player: %s)", why) or nil
        end
        if real and real > 0 then log_gate(pid, real, okh, why) end
    end
    -- the id was chosen for what the game files ship for it (the GUI's Player ID box): its 3D head, proven by a current
    -- asset gate (the GUI writes it only while its catalog matches the running game), else nothing is written
    local id_note
    if want_head then
        if not (db.has_field(players, "headassetid") and db.has_field(players, "headclasscode")) then
            return false, "the players table has no headassetid / headclasscode: the head of id " .. pid .. " cannot be given (nothing was written)"
        end
        if not agate.trusted(gate) then
            return false, string.format("the 3D head of player id %d cannot be checked now (%s): nothing was written. Keep the " ..
                "Turbo GUI open with an asset catalog for this game build and try again, or choose another id", pid, tostring(gate.reason))
        end
        local okh, why, hidden = agate.check_head(gate, pid, nil, true)
        if not okh then return false, string.format("player id %d: %s (nothing was written)", pid, tostring(why)) end
        row.headassetid, row.headclasscode = pid, 0
        if db.has_field(players, "hashighqualityhead") then row.hashighqualityhead = 1 end
        local var = util.to_int(id_assets.headvariation)
        if var and db.has_field(players, "headvariation") then
            local okv = db.validate(players, "headvariation", var)
            if okv == nil then return false, string.format("headvariation %s is out of range (nothing was written)", tostring(id_assets.headvariation)) end
            row.headvariation = okv
        end
        head_note = string.format(", the game files' 3D head of id %d", pid)
        head_log = hidden and pid or nil
        id_note = "3D head of the game files (asset catalog " .. tostring(gate.build or "?") .. ")"
    end
    if id_assets and id_assets.callname == true then
        id_note = (id_note and (id_note .. ", ") or "") .. "own callname recording (the commentary finds it by player id)"
    end
    if id_note then id_how = id_how .. "; the game files' assets of this id: " .. id_note end

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
        -- the game prints this row's shirt name: never empty (the surname, else the common name)
        names.playerjerseyname = pnames.jersey(names)
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
    if id_assets and id_assets.callname == true then summary = summary .. ", his own callname recording (found by his id)" end
    if id_typed then summary = summary .. ", player id typed" end
    summary = summary .. string.format(", joined today%s", contract_note and (", contract until " .. contract_note) or "")
    if skipped and #skipped > 0 then summary = summary .. "; out of range, minimum used: " .. table.concat(skipped, ", ") end
    if user > 0 and teamid == user then summary = summary .. " (your club: added to your team sheet as a reserve)" end
    log.info("create_player: player id %d (%s)%s", pid, id_how, ctx.dry and " [dry run: nothing written]" or "")
    if ctx.dry then return true, summary end
    -- a head no row of the save used: logged (before the write) so it can be reverted by hand
    if head_log then agate.log_write(gate, "player " .. pid, "headassetid", "(new player)", head_log) end

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
                if status == "queued" then
                    local qnote = "squad status entry: checked by the match crash guard at the next career event (the game " ..
                        "creates him on its next tick)"
                    log.info("match crash guard after create_player (player %d): %s", pid, qnote)
                    summary = summary .. "; " .. qnote
                else
                    summary = summary .. "; " .. status_entry(pid)
                end
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
        summary = summary .. "; " .. status_entry(pid)
    end
    miniface()
    if native_note then summary = summary .. native_note end
    return true, summary
end

return M
