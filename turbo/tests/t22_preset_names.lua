package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t22 player names in presets: real names exported, never written back as a common name, repair_names for 1.2.0 rows")

-- The 1.2.0 bug (seen in a live career, 2026-10-05): Export wrote a player without an editedplayernames row as
-- { commonname = his shown name, firstname = "", surname = "", playerjerseyname = "" }, and Import (Names group on by
-- default) wrote that row back: "Jacopo Segre" became a common name and his shirt lost its name.

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { edited_names = false, player_fields = {
    { name = "firstnameid", depth = 17 }, { name = "lastnameid", depth = 17 },
    { name = "commonnameid", depth = 17 }, { name = "playerjerseynameid", depth = 17 } } })
local spare = {}
for i = 1, 24 do spare[i] = { __invalid = true } end
sim:add_table({ name = "editedplayernames", short = "edpn",
    fields = {
        { name = "playerid", short = "pid_", depth = 21 },
        { name = "firstname", short = "fnam", type = "string", depth = 8 * 45 },
        { name = "surname", short = "snam", type = "string", depth = 8 * 45 },
        { name = "commonname", short = "cnam", type = "string", depth = 8 * 45 },
        { name = "playerjerseyname", short = "pjnm", type = "string", depth = 8 * 45 },
    },
    rows = spare })

local preset = require 'imports/turbo/core/preset'
local util = require 'imports/turbo/core/util'

-- the game's names (playernames.name is compressed text: only GetDBTableRows reads it)
local TEXT = { [10] = "Jacopo", [11] = "Segre", [12] = "SEGRE", [20] = "Jo\195\163o", [21] = "Pedro Junqueira",
               [22] = "Jo\195\163o Pedro", [30] = "Daniele", [31] = "Padelli", [40] = "Marco", [41] = "Bianchi" }
local name_rows = {}
for id, t in pairs(TEXT) do name_rows[#name_rows + 1] = { nameid = id, name = t } end
table.sort(name_rows, function(a, b) return a.nameid < b.nameid end)
sim:add_table({ name = "playernames", short = "pnms",
    fields = { { name = "nameid", short = "nid_", depth = 17 }, { name = "name", short = "nam_", type = "compressed", depth = 8 * 32 } },
    rows = name_rows })

local function pset(pid, field, v)
    local rec = sim:find_row("players", "playerid", pid)
    sim:set_field(rec, sim.tables.players[field], v)
end
local function ids(pid, first, last, common, jersey)
    pset(pid, "firstnameid", first); pset(pid, "lastnameid", last)
    pset(pid, "commonnameid", common); pset(pid, "playerjerseynameid", jersey)
end
ids(1002, 10, 11, 0, 12)    -- Jacopo Segre, shirt SEGRE
ids(1003, 20, 21, 22, 22)   -- a common name of his own: Joao Pedro
ids(1004, 30, 31, 0, 0)     -- Daniele Padelli, no shirt-name id
ids(1007, 30, 11, 0, 0)     -- Daniele Segre
ids(1008, 40, 41, 0, 0)     -- Marco Bianchi
ids(2001, 40, 41, 0, 0)
ids(2004, 40, 41, 0, 0)   -- Marco Bianchi, no name row (the clone source)
-- 1005 has no name ids (like a created player)

local function edited(pid)
    local rec = sim:find_row("editedplayernames", "playerid", pid)
    if not rec then return nil end
    local out = {}
    for _, f in ipairs({ "firstname", "surname", "commonname", "playerjerseyname" }) do out[f] = sim:value("editedplayernames", rec, f) end
    return out
end
local function names_eq(got, first, sur, common, jersey, what)
    H.ok(got ~= nil, what .. ": a name row")
    if not got then return end
    H.eq(got.firstname, first, what .. " first name"); H.eq(got.surname, sur, what .. " surname")
    H.eq(got.commonname, common, what .. " common name"); H.eq(got.playerjerseyname, jersey, what .. " shirt name")
end
-- Live Editor's GetPlayerName: the shown name (the name row, else the ids)
GetPlayerName = function(pid)
    local e = edited(pid)
    if e then return e.commonname ~= "" and e.commonname or util.trim(e.firstname .. " " .. e.surname) end
    local rec = sim:find_row("players", "playerid", pid)
    if not rec then return tostring(pid) end
    local function t(f) return TEXT[sim:value("players", rec, f)] or "" end
    if t("commonnameid") ~= "" then return t("commonnameid") end
    local n = util.trim(t("firstnameid") .. " " .. t("lastnameid"))
    return n ~= "" and n or ("Player " .. pid)
end
local function damage(pid, common, jersey)   -- the row 1.2.0's import left
    InsertDBTableRow("editedplayernames", { playerid = tostring(pid), firstname = "", surname = "", commonname = common,
        playerjerseyname = jersey or "" })
end
local function run(mod, cfg) return H.turbo().run(mod, cfg) end
local function rows_n() return #sim:rows("editedplayernames") end

local json = preset.json()
local JSON_1002 = H.out("players/Jacopo_Segre_1002.json")

H.case("export: a player without a name row is exported with his real names (his name ids' texts)", function()
    local ok, msg = run("player_presets", { mode = "export", playerids = { 1002, 1003 }, miniface = false })
    H.eq(ok, true, msg)
    local doc = json.decode(H.read(JSON_1002))
    H.ok(doc ~= nil, "file named after his real names")
    names_eq(doc.names, "Jacopo", "Segre", "", "SEGRE", "1002")
    H.eq(doc.names_from, "name ids"); H.eq(doc.name, "Jacopo Segre")
    local doc3 = json.decode(H.read(H.out("players/Jo_o_Pedro_1003.json")))
    names_eq(doc3.names, "Jo\195\163o", "Pedro Junqueira", "Jo\195\163o Pedro", "Jo\195\163o Pedro", "1003")
    local cells = preset.parse_csv(H.read(H.LE .. "/extensions/player_presets/Jacopo_Segre_1002.csv"))
    local row = {}
    for i, h in ipairs(cells[1]) do row[h] = cells[2][i] end
    H.eq(row.firstname, "Jacopo"); H.eq(row.surname, "Segre"); H.eq(row.commonname, ""); H.eq(row.playerjerseyname, "SEGRE")
    H.eq(sim:count_calls("GetDBTableRows"), 1, "the name texts are read once for the whole export")
end)

H.case("import of his own export, every group: his names are not written, no name row is added", function()
    local n = rows_n()
    local ok, msg = run("player_presets", { mode = "import", file = JSON_1002, playerid = 1002 })
    H.eq(ok, true, msg)
    H.has(msg, "names unchanged")
    H.ok(not msg:find("names,"), "names not counted as written: " .. msg)
    H.eq(edited(1002), nil, "still no name row")
    H.eq(rows_n(), n)
end)

-- a file exactly as 1.2.0 exported it
local OLD = H.out("players/old_Jacopo_Segre_1002.json")
do
    local doc = json.decode(H.read(JSON_1002))
    doc.names = { firstname = "", surname = "", commonname = "Jacopo Segre", playerjerseyname = "" }
    doc.names_from = nil
    util.write_file(OLD, json.encode(doc))
end

H.case("the bug: a 1.2.0 export (shown name as a common name) imported back onto its player changes no name", function()
    local n = rows_n()
    local ok, msg = run("player_presets", { mode = "import", file = OLD, playerid = 1002 })
    H.eq(ok, true, msg)
    H.eq(edited(1002), nil, "NOT turned into a common name")
    H.eq(rows_n(), n)
    ok, msg = run("player_presets", { mode = "import", file = OLD, playerid = 1002, groups = { "names" } })
    H.eq(ok, true, msg); H.has(msg, "nothing written, names unchanged")
    -- Live Editor's own CSV export has the same shape (shown name in commonname) with the name ids
    local csv_path = H.out("old_padelli.csv")
    local f = io.open(csv_path, "wb")
    f:write("playerid,firstname,surname,playerjerseyname,commonname,firstnameid,lastnameid,commonnameid,playerjerseynameid,overallrating\r\n")
    f:write("1004,,,,Daniele Padelli,30,31,0,0,66\r\n")
    f:close()
    ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 1004 })
    H.eq(ok, true, msg)
    H.eq(edited(1004), nil, "Padelli keeps his names")
    -- a player with a real common name (Joao Pedro) and his old-style file
    local doc = json.decode(H.read(H.out("players/Jo_o_Pedro_1003.json")))
    doc.names = { firstname = "", surname = "", commonname = "Jo\195\163o Pedro", playerjerseyname = "" }
    local old3 = H.out("players/old_jp.json")
    util.write_file(old3, json.encode(doc))
    ok, msg = run("player_presets", { mode = "import", file = old3, playerid = 1003, groups = { "names" } })
    H.eq(ok, true, msg); H.eq(edited(1003), nil, "his own common name not written as a row")
end)

H.case("a 1.2.0 export imported onto ANOTHER player: the file's real names (from its ids), not one common name", function()
    local ok, msg = run("player_presets", { mode = "import", file = OLD, playerid = 2001, groups = { "names" } })
    H.eq(ok, true, msg)
    H.has(msg, "names taken from the file's name ids")
    names_eq(edited(2001), "Jacopo", "Segre", "", "SEGRE", "2001")
end)

H.case("a shown-name-only file from another database: written as the common name, the shirt name never empty", function()
    local path = H.out("other_game.csv")
    local f = io.open(path, "wb")
    f:write("playerid,firstname,surname,playerjerseyname,commonname,firstnameid,lastnameid,overallrating\r\n")
    f:write("55555,,,,Mister X,9999,9998,70\r\n")
    f:close()
    local ok, msg = run("player_presets", { mode = "import", file = path, playerid = 2002, groups = { "names" } })
    H.eq(ok, true, msg)
    names_eq(edited(2002), "", "", "Mister X", "Mister X", "2002")
end)

H.case("a real rename (first name and surname in the file): written, the shirt name filled from the surname", function()
    local path = H.out("rename.csv")
    local f = io.open(path, "wb")
    f:write("playerid,firstname,surname,playerjerseyname,commonname,overallrating\r\n")
    f:write("1,Neo,Turbo,,,70\r\n")
    f:close()
    local ok, msg = run("player_presets", { mode = "import", file = path, playerid = 2003, groups = { "names" } })
    H.eq(ok, true, msg)
    names_eq(edited(2003), "Neo", "Turbo", "", "Turbo", "2003")
    -- the same file again: unchanged, nothing written
    ok, msg = run("player_presets", { mode = "import", file = path, playerid = 2003, groups = { "names" } })
    H.eq(ok, true, msg); H.has(msg, "names unchanged")
end)

H.case("repair_names: check first, then the 1.2.0 rows are rewritten in place with his own names; others left alone", function()
    damage(1004, "Daniele Padelli")
    damage(1003, "Jo\195\163o Pedro")
    damage(1005, "Created Guy")             -- no name ids: a created player's own name
    damage(1007, "Danny", "DANNY")          -- a shirt name: a common name somebody chose
    damage(1008, "Someone Else")            -- not his game name (Marco Bianchi)
    local n = rows_n()
    local inserts, deletes = sim:count_calls("InsertDBTableRow"), sim:count_calls("DeleteDBTableRowByAddr")
    local ok, msg = run("player_presets", { mode = "repair_names", check = true })
    H.eq(ok, true, msg)
    H.has(msg, "would repair 2 player names"); H.has(msg, "Daniele Padelli (1004)")
    names_eq(edited(1004), "", "", "Daniele Padelli", "", "check wrote nothing")

    ok, msg = run("player_presets", { mode = "repair_names" })
    H.eq(ok, true, msg)
    H.has(msg, "repaired 2 player names")
    H.has(msg, "Created Guy (1005) no name ids"); H.has(msg, "Someone Else (1008) not his game name Marco Bianchi")
    names_eq(edited(1004), "Daniele", "Padelli", "", "Padelli", "1004 (no shirt-name id: the surname)")
    names_eq(edited(1003), "Jo\195\163o", "Pedro Junqueira", "Jo\195\163o Pedro", "Jo\195\163o Pedro", "1003 (his own common name kept)")
    names_eq(edited(1005), "", "", "Created Guy", "", "1005 untouched")
    names_eq(edited(1007), "", "", "Danny", "DANNY", "1007 untouched")
    names_eq(edited(1008), "", "", "Someone Else", "", "1008 untouched")
    H.eq(rows_n(), n, "rewritten in place: no row added or removed")
    H.eq(sim:count_calls("InsertDBTableRow"), inserts, "no insert")
    H.eq(sim:count_calls("DeleteDBTableRowByAddr"), deletes, "no raw delete (the game's indexes stay valid)")
    H.eq(GetPlayerName(1004), "Daniele Padelli", "shown as before")

    ok, msg = run("player_presets", { mode = "repair_names" })
    H.eq(ok, true, msg); H.has(msg, "no player name repaired")
end)

H.case("repair_names: limited to chosen players; without the game's names nothing is changed; dry run", function()
    damage(1002, "Jacopo Segre")
    local ok, msg = run("player_presets", { mode = "repair_names", playerids = { 1004 } })
    H.eq(ok, true, msg); H.has(msg, "no name of these players needs repairing")
    names_eq(edited(1002), "", "", "Jacopo Segre", "", "1002 not chosen")
    local saved = GetDBTableRows
    GetDBTableRows = nil
    ok, msg = run("player_presets", { mode = "repair_names" })
    GetDBTableRows = saved
    H.eq(ok, false); H.has(msg, "cannot be read"); H.has(msg, "nothing checked or changed")
    names_eq(edited(1002), "", "", "Jacopo Segre", "", "1002 untouched")
    H.write_config({ turbo = { dry_run = true } })
    ok, msg = run("player_presets", { mode = "repair_names" })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]"); H.has(msg, "would repair 1 player name")
    names_eq(edited(1002), "", "", "Jacopo Segre", "", "dry run wrote nothing")
    ok, msg = run("player_presets", { mode = "repair_names", playerids = { 1002 } })
    H.eq(ok, true, msg); H.has(msg, "repaired 1 player name")
    names_eq(edited(1002), "Jacopo", "Segre", "", "SEGRE", "1002 repaired")
    ok, msg = run("player_presets", { mode = "repair_names", playerids = { "x" } })
    H.eq(ok, false); H.has(msg, "playerids must be integers")
end)

H.case("create from a 1.2.0 export: the new player gets the real names and a shirt name", function()
    local ok, msg = run("create_player", { source = { file = OLD }, teamid = 4 })
    H.eq(ok, true, msg)
    local pid = tonumber(msg:match("new player (%d+)"))
    H.ok(pid ~= nil, msg)
    names_eq(edited(pid), "Jacopo", "Segre", "", "SEGRE", "created")
end)

H.case("clone given only a surname: the original's first name is kept, the shirt follows the new surname", function()
    local ok, msg = run("create_player", { source = { playerid = 2004 }, teamid = 4, names = { surname = "Clone" } })
    H.eq(ok, true, msg)
    local pid = tonumber(msg:match("new player (%d+)"))
    names_eq(edited(pid), "Marco", "Clone", "", "Clone", "clone")
    ok, msg = run("create_player", { source = { playerid = 2004 }, teamid = 4, names = { commonname = "Neo" } })
    H.eq(ok, true, msg)
    pid = tonumber(msg:match("new player (%d+)"))
    names_eq(edited(pid), "Marco", "Bianchi", "Neo", "Neo", "clone with a common name")
    ok, msg = run("create_player", { source = { playerid = 2004 }, teamid = 4 })
    H.eq(ok, true, msg)
    pid = tonumber(msg:match("new player (%d+)"))
    H.eq(edited(pid), nil, "no names given: no row (the copied name ids show his names)")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
