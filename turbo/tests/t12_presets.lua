package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t12 player presets: export (LE CSV + Turbo JSON), import (FC 27 / FC 26 columns, groups), clone / create player")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local preset = require 'imports/turbo/core/preset'
local util = require 'imports/turbo/core/util'

local function run(mod, overrides) return H.turbo().run(mod, overrides) end
local function pval(pid, field)
    local rec = sim:find_row("players", "playerid", pid)
    return rec and sim:value("players", rec, field) or nil
end
local function link_of(pid)
    local rec = sim:find_row("teamplayerlinks", "playerid", pid)
    if not rec then return nil end
    return sim:value("teamplayerlinks", rec, "teamid"), sim:value("teamplayerlinks", rec, "jerseynumber")
end
local function edited(pid)
    local rec = sim:find_row("editedplayernames", "playerid", pid)
    if not rec then return nil end
    return { firstname = sim:value("editedplayernames", rec, "firstname"), surname = sim:value("editedplayernames", rec, "surname"),
             commonname = sim:value("editedplayernames", rec, "commonname") }
end
local function db_bytes()
    local snap = {}
    for _, t in pairs(sim.tables) do
        for a = t.first, t.first + t.rec_size * t.n - 1 do snap[a] = sim.mem[a] end
    end
    return snap
end
local function changed_since(snap)
    local n = 0
    for a, v in pairs(snap) do if sim.mem[a] ~= v then n = n + 1 end end
    return n
end

local PRESETS = H.LE .. "/extensions/player_presets"
local csv_path = PRESETS .. "/Player_1001_1001.csv"
local json_path = H.out("players/Player_1001_1001.json")

H.case("export: Live Editor preset CSV with Live Editor's header, and a Turbo JSON", function()
    local ok, msg = run("player_presets", { mode = "export", playerid = 1001, miniface = false })
    H.eq(ok, true, msg)
    H.has(msg, "exported player 1001")
    local lines = H.csv_lines(csv_path)
    H.eq(#lines, 2, "header + one row")
    H.eq(lines[1], table.concat(preset.LE_HEADER, ","), "Live Editor's column order")
    local cells = preset.parse_csv(H.read(csv_path))
    H.eq(#cells[2], #preset.LE_HEADER, "every column present")
    local row = {}
    for i, h in ipairs(cells[1]) do row[h] = cells[2][i] end
    H.eq(row.playerid, "1001"); H.eq(row.overallrating, "61"); H.eq(row.commonname, "Player 1001")
    H.eq(row.firstname, "", "no edited name: only commonname is filled, like Live Editor")
    H.eq(row.aggression, "", "a field this database does not have stays empty")
    local doc = preset.json().decode(H.read(json_path))
    H.eq(doc.format, "turbo-player-preset"); H.eq(doc.playerid, 1001); H.eq(doc.players.potential, 71)
    H.eq(doc.links[1].teamid, 1); H.eq(doc.links[1].jerseynumber, 1); H.eq(doc.miniface, nil, "no miniface asked")
end)

H.case("export: several players at once, names from editedplayernames, miniface through Live Editor", function()
    sim.legacy_files["data/ui/imgAssets/heads/p1002.dds"] = "DDS " .. string.rep("x", 200)
    local ok, msg = run("player_presets", { mode = "export", playerids = { 1002, 2001, 777777 } })
    H.eq(ok, false, msg)
    H.has(msg, "exported 2 players"); H.has(msg, "777777: player 777777 not found")
    local doc = preset.json().decode(H.read(H.out("players/Player_1002_1002.json")))
    H.eq(doc.miniface, "Player_1002_1002.dds")
    H.ok(H.read(H.out("players/Player_1002_1002.dds")):sub(1, 4) == "DDS ", "miniface copied")
    H.eq(sim:count_calls("LegacyFileExport"), 1)
    local lines = H.csv_lines(PRESETS .. "/Player_2001_2001.csv")
    H.eq(#lines, 2)
end)

H.case("import onto an existing player: round trip of the CSV, groups, names into editedplayernames", function()
    local ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 2001, groups = { "attributes" } })
    H.eq(ok, false, msg)
    H.has(msg, "nothing to import", "the test world has no attribute fields")
    local before_ovr = pval(2001, "overallrating")
    ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 2001, groups = { "profile", "positions" } })
    H.eq(ok, true, msg)
    H.eq(pval(2001, "overallrating"), 61, "overall copied")
    H.eq(pval(2001, "potential"), 71)
    H.eq(pval(2001, "preferredposition1"), 0)
    H.ok(before_ovr ~= 61, "value really changed")
    H.eq(pval(2001, "playerid"), 2001, "identity untouched")
    H.eq(pval(2001, "contractvaliduntil"), 2028, "contract group not chosen: unchanged")
    H.eq(edited(2001), nil, "names group not chosen")
    ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 2001, groups = { "names" } })
    H.eq(ok, true, msg)
    H.eq(edited(2001).commonname, "Player 1001", "editedplayernames row added")
    -- a second names import updates the row instead of adding one
    local n = #sim:rows("editedplayernames")
    ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 2001, groups = { "names" } })
    H.eq(ok, true, msg)
    H.eq(#sim:rows("editedplayernames"), n, "row updated, not added")
end)

H.case("import: Turbo JSON with the miniface installs it under mods\\legacy, backing up the old one", function()
    local root = H.LE
    local dir = root .. "/mods/legacy/data/ui/imgAssets/heads"
    os.execute(string.format("mkdir -p '%s'", dir))
    local f = io.open(dir .. "/p2002.dds", "wb"); f:write("DDS old"); f:close()
    local ok, msg = run("player_presets", { mode = "import", file = H.out("players/Player_1002_1002.json"), playerid = 2002,
        groups = { "miniface" } })
    H.eq(ok, true, msg)
    H.has(msg, "miniface")
    H.eq(H.read(dir .. "/p2002.dds"):sub(1, 5), "DDS x", "new miniface in place")
    local lp = io.popen(string.format("ls '%s/turbo_output/miniface_backups' 2>/dev/null", H.LE))
    local backups = {}
    for line in lp:lines() do if line:match("^p2002_.*%.dds$") then backups[#backups + 1] = line end end
    lp:close()
    H.eq(#backups, 1, "old file backed up")
end)

H.case("import: FC 26 column set (cards.csv shape, renamed and unknown columns, out-of-range values)", function()
    local path = H.out("fc26_cards.csv")
    local f = io.open(path, "wb")
    f:write("uid,name,revision,origin,playerid,overallrating,potential,marking,preferredposition1,trait1,bogusfield\r\n")
    f:write("1,Cha Bum Kun,Debut Icon,N/A,191208,85,86,70,25,639633504,5\r\n")
    f:write("2,\"Cha, Bum Kun\",Champion Icon,N/A,191208,200,88,72,25,0,5\r\n")
    f:close()
    local ok, msg = run("player_presets", { mode = "import", file = path, playerid = 2003, groups = { "profile", "positions", "playstyles" } })
    H.eq(ok, true, msg)
    H.eq(pval(2003, "potential"), 88, "newest row used")
    H.eq(pval(2003, "overallrating"), 58, "out-of-range overall skipped, not clamped")
    H.has(msg, "skipped out-of-range: overallrating=200")
    H.has(msg, "ignored columns: bogusfield, marking", "marking renamed to defensiveawareness, which this database lacks")
    H.eq(pval(2003, "preferredposition1"), 25)
    -- a chosen row
    ok, msg = run("player_presets", { mode = "import", file = path, playerid = 2003, groups = { "profile" }, row = 1 })
    H.eq(ok, true, msg)
    H.eq(pval(2003, "overallrating"), 85, "row 1 used")
    H.has(msg, "row 1 of 2")
end)

H.case("import refusals: missing file, wrong file, unknown player, bad row", function()
    local ok, msg = run("player_presets", { mode = "import", file = H.out("nope.csv"), playerid = 2003 })
    H.eq(ok, false); H.has(msg, "cannot read")
    local path = H.out("notapreset.csv")
    local f = io.open(path, "wb"); f:write("a,b\r\n1,2\r\n"); f:close()
    ok, msg = run("player_presets", { mode = "import", file = path, playerid = 2003 })
    H.eq(ok, false); H.has(msg, "not a Live Editor player preset")
    ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 777777 })
    H.eq(ok, false); H.has(msg, "player 777777 not found")
    ok, msg = run("player_presets", { mode = "import", file = csv_path, playerid = 2003, row = 9 })
    H.eq(ok, false); H.has(msg, "row 9 does not exist")
    ok, msg = run("player_presets", { mode = "bogus" })
    H.eq(ok, false); H.has(msg, "unknown mode")
end)

H.case("clone: a copy of another club's player into a third club, free id and shirt, dry run writes nothing", function()
    local snap = db_bytes()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3 })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg)
    H.has(msg, "[DRY RUN] new player 2057 at Team 3 (3): copy of player 2005")
    H.eq(changed_since(snap), 0, "dry run wrote nothing")

    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3, jersey = 2 })
    H.eq(ok, true, msg)
    H.has(msg, "new player 2057")
    H.eq(pval(2057, "overallrating"), pval(2005, "overallrating"), "fields copied")
    H.eq(pval(2057, "birthdate"), pval(2005, "birthdate"))
    local team, shirt = link_of(2057)
    H.eq(team, 3)
    H.ok(shirt ~= 2 and shirt >= 1 and shirt <= 99, "shirt 2 is taken at team 3: another one chosen (" .. tostring(shirt) .. ")")
    H.eq(sim:count_calls("InsertDBTableRow"), 3, "players + teamplayerlinks + editedplayernames (round-trip test) rows")
    -- the next one gets the next id, and names go to editedplayernames
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 111592, names = { firstname = "Neo", surname = "Clone" } })
    H.eq(ok, true, msg)
    H.has(msg, "new player 2058 Neo Clone at Free Agents (111592)")
    H.eq(edited(2058).surname, "Clone")
    H.eq(link_of(2058), 111592)
end)

H.case("create from a preset file as a new player; blank player gets field minimums", function()
    local ok, msg = run("create_player", { source = { file = csv_path }, teamid = 4 })
    H.eq(ok, true, msg)
    H.has(msg, "new player 2059 Player 1001 at Team 4 (4)")
    H.eq(pval(2059, "overallrating"), 61)
    H.eq(edited(2059).commonname, "Player 1001", "preset names kept")
    ok, msg = run("create_player", { source = { blank = true }, teamid = 4, set = { overallrating = 50, potential = 60 },
        names = { commonname = "Blank Guy" } })
    H.eq(ok, true, msg)
    H.has(msg, "fields at their minimum")
    H.eq(pval(2060, "overallrating"), 50); H.eq(pval(2060, "contractvaliduntil"), 0)
    -- Turbo JSON source with its miniface
    ok, msg = run("create_player", { source = { file = H.out("players/Player_1002_1002.json") }, teamid = 4 })
    H.eq(ok, true, msg)
    H.has(msg, "miniface installed")
    H.ok(util.file_exists(H.LE .. "/mods/legacy/data/ui/imgAssets/heads/p2061.dds"), "miniface for the new id")
end)

H.case("create refusals: your own club, unknown team, bad source, used id, bad set field, id range", function()
    local snap = db_bytes()
    local calls = sim:count_calls("InsertDBTableRow")
    local ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = W.USER_TEAM })
    H.eq(ok, false); H.has(msg, "your own club")
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 99999 })
    H.eq(ok, false); H.has(msg, "team 99999 not found")
    ok, msg = run("create_player", { source = { playerid = 777777 }, teamid = 3 })
    H.eq(ok, false); H.has(msg, "source player 777777 not found")
    ok, msg = run("create_player", { source = {}, teamid = 3 })
    H.eq(ok, false); H.has(msg, "source must be")
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3, playerid = 2005 })
    H.eq(ok, false); H.has(msg, "already used")
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3, set = { nosuchfield = 1 } })
    H.eq(ok, false); H.has(msg, "no field nosuchfield")
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3, set = { potential = 999 } })
    H.eq(ok, false); H.has(msg, "potential=999 is outside")
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3, max_playerid = 2050 })
    H.eq(ok, false); H.has(msg, "no free player id below 2050")
    H.eq(changed_since(snap), 0, "nothing written by a refused run")
    H.eq(sim:count_calls("InsertDBTableRow"), calls, "no row added by a refused run")
    -- allowed into your club when asked for
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = W.USER_TEAM, allow_user_club = true })
    H.eq(ok, true, msg)
    H.has(msg, "your club: not on the team sheet")
    H.eq(link_of(2062), W.USER_TEAM)
end)

H.case("runner scripts: export, import and create show a message box", function()
    H.write_config({ modules = { player_presets = { playerids = { 2001 }, file = csv_path, playerid = 2001, groups = { "profile" } },
                                 create_player = { source = { playerid = 2001 }, teamid = 5 } } })
    for _, s in ipairs({ "turbo_export_player", "turbo_import_player", "turbo_create_player" }) do
        local before = #sim.boxes
        H.script(s)
        H.ok(#sim.boxes > before, s .. " message box")
        H.ok(not tostring(sim.boxes[#sim.boxes].text):find("crashed"), s .. ": " .. tostring(sim.boxes[#sim.boxes].text))
    end
end)

H.case("caps: creating players is a Turbo-made tool in Live Editor v27.1.2", function()
    local caps = require 'imports/turbo/core/caps'
    local made = caps.turbo_made()
    local found = false
    for _, k in ipairs(made) do if k == "create_player" then found = true end end
    H.ok(found, "create_player in turbo_made")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
